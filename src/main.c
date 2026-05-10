#include "bzip2.h"
#include "config.h"
#include "pipeline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif

#ifdef _WIN32
#define PATH_SEP_CH '\\'
#else
#define PATH_SEP_CH '/'
#endif

enum { MAIN_PATH_MAX = 4096 };

enum { RLE_CAP_NUM = 3, RLE_CAP_DEN = 4 };

static int prompt_line(const char *prompt, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return -1;
    printf("%s", prompt);
    if (!fgets(out, (int)out_sz, stdin)) return 1;
    out[strcspn(out, "\r\n")] = '\0';
    return 0;
}

static int prompt_int(const char *prompt, int *value) {
    char line[64];
    char *end = NULL;
    long v;
    int rc = prompt_line(prompt, line, sizeof line);
    if (rc != 0) return rc;
    v = strtol(line, &end, 10);
    while (*end == ' ' || *end == '\t') end++;
    if (end == line || *end != '\0') return -1;
    *value = (int)v;
    return 0;
}

static int read_file(const char *path, unsigned char **data, size_t *len) {
    FILE *f = fopen(path, "rb");
    long size;
    if (!f) {
        printf("Could not open input file: %s\n", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    size = ftell(f);
    if (size < 0) { fclose(f); return -1; }
    rewind(f);
    *data = (unsigned char *)malloc((size_t)size + 1);
    if (!*data) { fclose(f); return -1; }
    *len = fread(*data, 1, (size_t)size, f);
    fclose(f);
    return 0;
}

static void trim_trailing_seps(char *s) {
    if (!s) {
        return;
    }
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '/' || s[n - 1] == '\\')) {
        s[--n] = '\0';
    }
}

static void ensure_output_dir(const char *output_directory) {
    char path[CONFIG_PATH_MAX];
    snprintf(path, sizeof path, "%s", output_directory);
    trim_trailing_seps(path);
    if (path[0] == '\0') {
        return;
    }
#ifdef _WIN32
    if (_mkdir(path) != 0 && errno != EEXIST) {
#else
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
#endif
        fprintf(stderr, "Could not create directory %s\n", path);
    }
}

static const char *path_basename(const char *path) {
    if (!path) {
        return "";
    }
    const char *slash = strrchr(path, '/');
    const char *back  = strrchr(path, '\\');
    const char *p     = slash > back ? slash : back;
    return p ? p + 1 : path;
}

/* One row to csv if csv != NULL. Returns 0 on success.
 * CSV columns (per project description, §7.3):
 *   File           = basename of input
 *   Size           = original byte size
 *   BlockSize      = block_size used (config.ini, 100K–900K)
 *   CompressionRatio = 100 × (1 − compressed/original)  [percent saved]
 *   Time           = pipeline encode+decode, seconds, 3 decimals
 *   Memory         = peak working-set estimate in KB
 */
static int benchmark_one_file(const char *path, AppConfig *cfg, FILE *csv) {
    unsigned char *data = NULL;
    unsigned char *decoded = NULL;
    size_t         len = 0;
    size_t         dec_len = 0;
    size_t         final_size = 0;
    size_t         peak_bytes = 0;

    if (read_file(path, &data, &len) != 0) {
        return -1;
    }
    char err[256];
    if (config_validate_block_size(cfg->block_size, err, sizeof err) != 0) {
        fprintf(stderr, "%s", err);
        free(data);
        return -1;
    }

    clock_t start = clock();
    int     pr = pipeline_run(data, len, cfg, 0, &final_size, &decoded, &dec_len, &peak_bytes);
    clock_t end = clock();
    free(data);
    free(decoded);
    if (pr != 0) {
        fprintf(stderr, "Pipeline failed: %s\n", path);
        return -1;
    }

    double seconds = (double)(end - start) / (double)CLOCKS_PER_SEC;
    if (seconds < 0) {
        seconds = 0.0;
    }
    double savings_pct = 0.0;
    if (len > 0) {
        savings_pct = (1.0 - (double)final_size / (double)len) * 100.0;
    }
    size_t memory_kb = (peak_bytes + 1023u) / 1024u;

    const char *base = path_basename(path);
    if (csv) {
        fprintf(csv, "%s,%zu,%zu,%.2f,%.3f,%zu\n",
                base, len, cfg->block_size, savings_pct, seconds, memory_kb);
    }
    printf("  %-22s  size=%zu  saved=%.2f%%  time=%.3fs  mem=%zuKB\n",
           base, len, savings_pct, seconds, memory_kb);
    return 0;
}

static void walk_benchmark_files(const char *dir, AppConfig *cfg, FILE *csv, unsigned *n_ok) {
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "Cannot open %s\n", dir);
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' && (e->d_name[1] == '\0'
                                    || (e->d_name[1] == '.' && e->d_name[2] == '\0'))) {
            continue;
        }
        char full[MAIN_PATH_MAX];
        if (snprintf(full, sizeof full, "%s%c%s", dir, PATH_SEP_CH, e->d_name) >= (int)sizeof full) {
            fprintf(stderr, "Path too long, skip: %s/%s\n", dir, e->d_name);
            continue;
        }
        struct stat st;
        if (stat(full, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            walk_benchmark_files(full, cfg, csv, n_ok);
        } else if (S_ISREG(st.st_mode)) {
            if (benchmark_one_file(full, cfg, csv) == 0) {
                (*n_ok)++;
            }
        }
    }
    closedir(d);
}

static int run_default_benchmark_suite(void) {
    AppConfig cfg;
    config_set_defaults(&cfg);
    if (config_load(CONFIG_DEFAULT_PATH, &cfg) != 0) {
        fprintf(stderr, "Failed to load config.ini\n");
        return 1;
    }
    char err[256];
    if (config_validate_block_size(cfg.block_size, err, sizeof err) != 0) {
        fprintf(stderr, "%s", err);
        return 1;
    }

    ensure_output_dir(cfg.output_directory);

    char csv_path[MAIN_PATH_MAX];
    snprintf(csv_path, sizeof csv_path, "%sresults.csv", cfg.output_directory);

    FILE *csv = fopen(csv_path, "w");
    if (!csv) {
        perror(csv_path);
        return 1;
    }
    fprintf(csv, "File,Size,BlockSize,CompressionRatio,Time,Memory\n");

    char root[CONFIG_PATH_MAX];
    snprintf(root, sizeof root, "%s", cfg.input_directory);
    trim_trailing_seps(root);

    printf("Full pipeline on all files under %s\nWriting %s …\n\n", root, csv_path);

    unsigned n_ok = 0;
    walk_benchmark_files(root, &cfg, csv, &n_ok);
    fclose(csv);

    printf("\nDone: %u file(s), output %s\n", n_ok, csv_path);
    return 0;
}

static int write_file(const char *path, const unsigned char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        printf("Could not write output file: %s\n", path);
        return -1;
    }
    if (len && fwrite(data, 1, len, f) != len) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static int files_are_identical(const char *a_path, const char *b_path) {
    FILE *a = fopen(a_path, "rb");
    FILE *b = fopen(b_path, "rb");
    int ca, cb;
    if (!a || !b) { if (a) fclose(a); if (b) fclose(b); return 0; }
    do {
        ca = fgetc(a); cb = fgetc(b);
        if (ca != cb) { fclose(a); fclose(b); return 0; }
    } while (ca != EOF);
    fclose(a); fclose(b); return 1;
}

static void print_bytes_preview(const char *label, const unsigned char *buf, size_t len) {
    printf("%s (%zu bytes): ", label, len);
    size_t limit = len < 96 ? len : 96;
    for (size_t i = 0; i < limit; i++) {
        if (buf[i] >= 32 && buf[i] < 127) printf("%c", buf[i]);
        else printf("[%02X]", buf[i]);
    }
    if (len > limit) printf("...");
    printf("\n");
}

static void put_u32_le(unsigned char *out, size_t *p, unsigned int v) {
    out[(*p)++] = (unsigned char)(v & 0xffu);
    out[(*p)++] = (unsigned char)((v >> 8) & 0xffu);
    out[(*p)++] = (unsigned char)((v >> 16) & 0xffu);
    out[(*p)++] = (unsigned char)((v >> 24) & 0xffu);
}
static unsigned int get_u32_le(const unsigned char *in, size_t *p) {
    unsigned int v = ((unsigned int)in[*p]) | ((unsigned int)in[*p+1] << 8) | ((unsigned int)in[*p+2] << 16) | ((unsigned int)in[*p+3] << 24);
    *p += 4; return v;
}
static void put_u64_le(unsigned char *out, size_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) out[(*p)++] = (unsigned char)((v >> (8*i)) & 0xffu);
}
static uint64_t get_u64_le(const unsigned char *in, size_t *p) {
    uint64_t v = 0; for (int i = 0; i < 8; i++) v |= ((uint64_t)in[(*p)++]) << (8*i); return v;
}

static void default_out_path(const char *in, const char *suffix, char *out, size_t out_sz) {
    if (!out || out_sz == 0) {
        return;
    }
    size_t in_len  = in ? strlen(in) : 0;
    size_t sfx_len = suffix ? strlen(suffix) : 0;
    if (in_len + sfx_len + 1 > out_sz) {
        size_t keep = out_sz > sfx_len + 1 ? out_sz - sfx_len - 1 : 0;
        if (keep > 0 && in) {
            memcpy(out, in, keep);
        }
        if (sfx_len > 0 && keep + sfx_len + 1 <= out_sz) {
            memcpy(out + keep, suffix, sfx_len);
        }
        out[keep + (keep + sfx_len + 1 <= out_sz ? sfx_len : 0)] = '\0';
        return;
    }
    if (in_len > 0) {
        memcpy(out, in, in_len);
    }
    if (sfx_len > 0) {
        memcpy(out + in_len, suffix, sfx_len);
    }
    out[in_len + sfx_len] = '\0';
}

static int ask_paths(char *in, size_t in_sz, char *out, size_t out_sz, const char *default_suffix) {
    if (prompt_line("Input file path: ", in, in_sz) != 0 || in[0] == '\0') return -1;
    if (prompt_line("Output file path (Enter = default): ", out, out_sz) != 0) return -1;
    if (out[0] == '\0') default_out_path(in, default_suffix, out, out_sz);
    return 0;
}

static int choose_mode(void) {
    int mode = 0;
    printf("1) Encode only\n2) Decode only\n3) Encode + decode + verify\n");
    if (prompt_int("Select mode: ", &mode) != 0) return 0;
    return mode;
}

static void run_rle1_file(void) {
    int mode = choose_mode();
    char in[512], out[512], dec[512];
    unsigned char *data = NULL, *encoded = NULL, *decoded = NULL;
    size_t len = 0, enc_len = 0, dec_len = 0;

    if (mode == 1) {
        if (ask_paths(in, sizeof in, out, sizeof out, ".rle1") != 0) return;
        if (read_file(in, &data, &len) != 0) return;
        encoded = malloc(len * 3 + 16);
        rle1_encode(data, len, encoded, &enc_len);
        print_bytes_preview("Input", data, len);
        print_bytes_preview("RLE-1 encoded", encoded, enc_len);
        write_file(out, encoded, enc_len);
        printf("Wrote encoded file: %s\n", out);
    } else if (mode == 2) {
        if (ask_paths(in, sizeof in, out, sizeof out, ".decoded") != 0) return;
        if (read_file(in, &data, &len) != 0) return;
        decoded = malloc(len * 255 + 16);
        rle1_decode(data, len, decoded, &dec_len);
        print_bytes_preview("Encoded input", data, len);
        print_bytes_preview("RLE-1 decoded", decoded, dec_len);
        write_file(out, decoded, dec_len);
        printf("Wrote decoded file: %s\n", out);
    } else if (mode == 3) {
        if (ask_paths(in, sizeof in, out, sizeof out, ".rle1") != 0) return;
        default_out_path(in, ".rle1.decoded", dec, sizeof dec);
        if (read_file(in, &data, &len) != 0) return;
        encoded = malloc(len * 3 + 16); decoded = malloc(len * 4 + 16);
        rle1_encode(data, len, encoded, &enc_len);
        rle1_decode(encoded, enc_len, decoded, &dec_len);
        print_bytes_preview("Input", data, len);
        print_bytes_preview("RLE-1 encoded", encoded, enc_len);
        print_bytes_preview("Decoded", decoded, dec_len);
        write_file(out, encoded, enc_len); write_file(dec, decoded, dec_len);
        printf("Verify: %s\n", (dec_len == len && memcmp(data, decoded, len) == 0) ? "RLE-1 OK" : "RLE-1 FAIL");
    }
    free(data); free(encoded); free(decoded);
}

static int bwt_write_encoded(const char *out_path, const unsigned char *bwt, size_t len, int primary) {
    unsigned char *file = malloc(len + 16);
    size_t p = 0;
    if (!file) return -1;
    file[p++]='B'; file[p++]='W'; file[p++]='T'; file[p++]='1';
    put_u64_le(file, &p, (uint64_t)len);
    put_u32_le(file, &p, (unsigned int)primary);
    memcpy(file + p, bwt, len); p += len;
    int rc = write_file(out_path, file, p);
    free(file); return rc;
}
static int bwt_read_encoded(const unsigned char *file, size_t file_len, unsigned char **bwt, size_t *len, int *primary) {
    if (file_len < 16 || file[0] != 'B' || file[1] != 'W' || file[2] != 'T' || file[3] != '1') return -1;
    size_t p = 4;
    *len = (size_t)get_u64_le(file, &p);
    *primary = (int)get_u32_le(file, &p);
    if (p + *len > file_len) return -1;
    *bwt = malloc(*len + 1);
    if (!*bwt) return -1;
    memcpy(*bwt, file + p, *len);
    return 0;
}

static void run_bwt_file(int suffix_array_mode) {
    int mode = choose_mode();
    char in[512], out[512], dec[512];
    unsigned char *data = NULL, *encoded = NULL, *decoded = NULL, *raw = NULL;
    size_t len = 0, raw_len = 0;
    int primary = -1;
    const char *name = suffix_array_mode ? "BWT suffix-array" : "BWT matrix";
    if (mode == 1) {
        if (ask_paths(in, sizeof in, out, sizeof out, suffix_array_mode ? ".bwt_sa" : ".bwt") != 0) return;
        if (read_file(in, &data, &len) != 0) return;
        encoded = malloc(len + 1);
        if (suffix_array_mode) bwt_encode_suffix_array(data, len, encoded, &primary); else bwt_encode(data, len, encoded, &primary);
        print_bytes_preview("Input", data, len); print_bytes_preview(name, encoded, len);
        printf("Primary index: %d\n", primary);
        bwt_write_encoded(out, encoded, len, primary); printf("Wrote: %s\n", out);
    } else if (mode == 2) {
        if (ask_paths(in, sizeof in, out, sizeof out, ".decoded") != 0) return;
        if (read_file(in, &raw, &raw_len) != 0) return;
        if (bwt_read_encoded(raw, raw_len, &encoded, &len, &primary) != 0) { printf("Invalid BWT encoded file.\n"); goto done; }
        decoded = malloc(len + 1); bwt_decode(encoded, len, primary, decoded);
        print_bytes_preview("BWT encoded", encoded, len); print_bytes_preview("Decoded", decoded, len);
        write_file(out, decoded, len); printf("Wrote decoded file: %s\n", out);
    } else if (mode == 3) {
        if (ask_paths(in, sizeof in, out, sizeof out, suffix_array_mode ? ".bwt_sa" : ".bwt") != 0) return;
        default_out_path(in, suffix_array_mode ? ".bwt_sa.decoded" : ".bwt.decoded", dec, sizeof dec);
        if (read_file(in, &data, &len) != 0) return;
        encoded = malloc(len + 1); decoded = malloc(len + 1);
        if (suffix_array_mode) bwt_encode_suffix_array(data, len, encoded, &primary); else bwt_encode(data, len, encoded, &primary);
        bwt_decode(encoded, len, primary, decoded);
        print_bytes_preview("Input", data, len); print_bytes_preview(name, encoded, len); printf("Primary index: %d\n", primary); print_bytes_preview("Decoded", decoded, len);
        bwt_write_encoded(out, encoded, len, primary); write_file(dec, decoded, len);
        printf("Verify: %s\n", memcmp(data, decoded, len) == 0 ? "BWT OK" : "BWT FAIL");
    }
done:
    free(data); free(encoded); free(decoded); free(raw);
}

static void run_block_file(void) {
    char in[512], out[512]; int bs = 0;
    if (prompt_line("Input file path: ", in, sizeof in) != 0 || in[0] == '\0') return;
    if (prompt_line("Output reassembled file path (Enter = reassembled_output.bin): ", out, sizeof out) != 0) return;
    if (out[0] == '\0') snprintf(out, sizeof out, "reassembled_output.bin");
    if (prompt_int("Block size in bytes (100-900000): ", &bs) != 0 || bs < 100 || bs > 900000) { printf("Invalid block size.\n"); return; }
    BlockManager *m = divide_into_blocks(in, (size_t)bs);
    if (!m) return;
    printf("Split into %d block(s) of up to %d bytes each.\n", m->num_blocks, bs);
    if (reassemble_blocks(m, out) == 0 && files_are_identical(in, out)) printf("Block I/O OK. Reassembled file matches original.\n");
    else printf("Block I/O FAIL.\n");
    printf("Output file: %s\n", out);
    free_block_manager(m);
}

static void run_config_file(void) {
    char path[512]; AppConfig cfg;
    if (prompt_line("Config file path (Enter = config.ini): ", path, sizeof path) != 0) return;
    if (path[0] == '\0') snprintf(path, sizeof path, "config.ini");
    if (config_load(path, &cfg) != 0) { printf("Config parsing FAIL.\n"); return; }
    printf("Config parsing OK\n");
    printf(" block_size=%zu\n rle1_enabled=%s\n bwt_type=%s\n mtf_enabled=%s\n rle2_enabled=%s\n huffman_enabled=%s\n benchmark_mode=%s\n output_metrics=%s\n input_directory=%s\n output_directory=%s\n",
           cfg.block_size, cfg.rle1_enabled ? "true" : "false", cfg.bwt_use_matrix ? "matrix" : "suffix_array",
           cfg.mtf_enabled ? "true" : "false", cfg.rle2_enabled ? "true" : "false", cfg.huffman_enabled ? "true" : "false",
           cfg.benchmark_mode ? "true" : "false", cfg.output_metrics ? "true" : "false", cfg.input_directory, cfg.output_directory);
}

static void run_mtf_file(void) {
    int mode = choose_mode(); char in[512], out[512], dec[512]; unsigned char *data=NULL,*enc=NULL,*decoded=NULL; size_t len=0;
    if (mode == 1) { if (ask_paths(in,sizeof in,out,sizeof out,".mtf")!=0) return; if(read_file(in,&data,&len)!=0)return; enc=malloc(len+1); mtf_encode(data,len,enc); print_bytes_preview("Input",data,len); print_bytes_preview("MTF encoded",enc,len); write_file(out,enc,len); printf("Wrote: %s\n",out); }
    else if (mode == 2) { if(ask_paths(in,sizeof in,out,sizeof out,".decoded")!=0)return; if(read_file(in,&data,&len)!=0)return; decoded=malloc(len+1); mtf_decode(data,len,decoded); print_bytes_preview("Encoded input",data,len); print_bytes_preview("MTF decoded",decoded,len); write_file(out,decoded,len); printf("Wrote: %s\n",out); }
    else if (mode == 3) { if(ask_paths(in,sizeof in,out,sizeof out,".mtf")!=0)return; default_out_path(in,".mtf.decoded",dec,sizeof dec); if(read_file(in,&data,&len)!=0)return; enc=malloc(len+1); decoded=malloc(len+1); mtf_encode(data,len,enc); mtf_decode(enc,len,decoded); print_bytes_preview("Input",data,len); print_bytes_preview("MTF encoded",enc,len); print_bytes_preview("Decoded",decoded,len); write_file(out,enc,len); write_file(dec,decoded,len); printf("Verify: %s\n",memcmp(data,decoded,len)==0?"MTF OK":"MTF FAIL"); }
    free(data); free(enc); free(decoded);
}

static void run_rle2_file(void) {
    int mode = choose_mode(); char in[512], out[512], dec[512]; unsigned char *data=NULL,*enc=NULL,*decoded=NULL; size_t len=0,enc_len=0,dec_len=0;
    if (mode == 1) { if(ask_paths(in,sizeof in,out,sizeof out,".rle2")!=0)return; if(read_file(in,&data,&len)!=0)return; enc=malloc(len*2+16); rle2_encode(data,len,enc,&enc_len); print_bytes_preview("Input",data,len); print_bytes_preview("RLE-2 encoded",enc,enc_len); write_file(out,enc,enc_len); printf("Wrote: %s\n",out); }
    else if (mode == 2) { if(ask_paths(in,sizeof in,out,sizeof out,".decoded")!=0)return; if(read_file(in,&data,&len)!=0)return; decoded=malloc(len*255+16); rle2_decode(data,len,decoded,&dec_len); print_bytes_preview("Encoded input",data,len); print_bytes_preview("RLE-2 decoded",decoded,dec_len); write_file(out,decoded,dec_len); printf("Wrote: %s\n",out); }
    else if (mode == 3) { if(ask_paths(in,sizeof in,out,sizeof out,".rle2")!=0)return; default_out_path(in,".rle2.decoded",dec,sizeof dec); if(read_file(in,&data,&len)!=0)return; enc=malloc(len*2+16); decoded=malloc(len*255+16); rle2_encode(data,len,enc,&enc_len); rle2_decode(enc,enc_len,decoded,&dec_len); print_bytes_preview("Input",data,len); print_bytes_preview("RLE-2 encoded",enc,enc_len); print_bytes_preview("Decoded",decoded,dec_len); write_file(out,enc,enc_len); write_file(dec,decoded,dec_len); printf("Verify: %s\n",dec_len==len&&memcmp(data,decoded,len)==0?"RLE-2 OK":"RLE-2 FAIL"); }
    free(data); free(enc); free(decoded);
}

static void run_huffman_file(void) {
    int mode = choose_mode(); char in[512], out[512], dec[512]; unsigned char *data=NULL,*enc=NULL,*decoded=NULL; size_t len=0,enc_len=0,dec_len=0;
    if (mode == 1) { if(ask_paths(in,sizeof in,out,sizeof out,".huf")!=0)return; if(read_file(in,&data,&len)!=0)return; enc=malloc(4+8+256*4+len*4+64); huffman_encode(data,len,enc,&enc_len); print_bytes_preview("Input",data,len); printf("Huffman encoded (%zu bytes, includes header)\n",enc_len); write_file(out,enc,enc_len); printf("Wrote: %s\n",out); }
    else if (mode == 2) { if(ask_paths(in,sizeof in,out,sizeof out,".decoded")!=0)return; if(read_file(in,&data,&len)!=0)return; decoded=malloc(len*255+4096); huffman_decode(data,len,decoded,&dec_len); print_bytes_preview("Huffman decoded",decoded,dec_len); write_file(out,decoded,dec_len); printf("Wrote: %s\n",out); }
    else if (mode == 3) { if(ask_paths(in,sizeof in,out,sizeof out,".huf")!=0)return; default_out_path(in,".huf.decoded",dec,sizeof dec); if(read_file(in,&data,&len)!=0)return; enc=malloc(4+8+256*4+len*4+64); decoded=malloc(len+16); huffman_encode(data,len,enc,&enc_len); huffman_decode(enc,enc_len,decoded,&dec_len); print_bytes_preview("Input",data,len); printf("Huffman encoded (%zu bytes, includes header)\n",enc_len); print_bytes_preview("Decoded",decoded,dec_len); write_file(out,enc,enc_len); write_file(dec,decoded,dec_len); printf("Verify: %s\n",dec_len==len&&memcmp(data,decoded,len)==0?"Huffman OK":"Huffman FAIL"); }
    free(data); free(enc); free(decoded);
}

/* Full pipeline: config.ini (block size, BWT type, stage toggles) + course order per block:
 * RLE-1 → BWT → MTF → RLE-2 → Huffman; blocks wrapped in BZP1 container (see pipeline.c). */
static int full_pipeline_process(unsigned char *input, size_t n, int show, size_t *final_size,
                                 unsigned char **decoded_out, size_t *decoded_len_out) {
    AppConfig cfg;
    config_set_defaults(&cfg);
    config_load(CONFIG_DEFAULT_PATH, &cfg);
    return pipeline_run(input, n, &cfg, show, final_size, decoded_out, decoded_len_out, NULL);
}

static void run_stage1_pipeline_file(void) {
    char in[512], outp[512]; unsigned char *data=NULL,*decoded=NULL; size_t len=0;
    if(ask_paths(in,sizeof in,outp,sizeof outp,".stage1.decoded")!=0)return;
    if(read_file(in,&data,&len)!=0)return;
    size_t cap=len*3+64; unsigned char *rle=malloc(cap),*bwt=malloc(cap),*tmp=malloc(cap),*out=malloc(len*4+64); size_t rle_len=0,out_len=0; int primary=-1;
    rle1_encode(data,len,rle,&rle_len); bwt_encode(rle,rle_len,bwt,&primary); bwt_decode(bwt,rle_len,primary,tmp); rle1_decode(tmp,rle_len,out,&out_len);
    print_bytes_preview("Original input",data,len); print_bytes_preview("After RLE-1",rle,rle_len); print_bytes_preview("After BWT",bwt,rle_len); printf("BWT primary index: %d\n",primary); print_bytes_preview("After inverse BWT",tmp,rle_len); print_bytes_preview("Final recovered",out,out_len);
    write_file(outp,out,out_len); printf("Verify: %s\n",out_len==len&&memcmp(data,out,len)==0?"Stage-1 pipeline OK":"Stage-1 pipeline FAIL");
    free(data); free(decoded); free(rle); free(bwt); free(tmp); free(out);
}

static void run_full_pipeline_file(void) {
    char in[512], outp[512]; unsigned char *data=NULL,*decoded=NULL; size_t len=0, dec_len=0, final_size=0;
    if(ask_paths(in,sizeof in,outp,sizeof outp,".full.decoded")!=0)return;
    if(read_file(in,&data,&len)!=0)return;
    if(full_pipeline_process(data,len,1,&final_size,&decoded,&dec_len)==0){ write_file(outp,decoded,dec_len); printf("Verify: %s\n",dec_len==len&&memcmp(data,decoded,len)==0?"Full pipeline OK":"Full pipeline FAIL"); printf("Recovered file: %s\n",outp); }
    free(data); free(decoded);
}

static void run_enhanced_rle_file(void) {
    int mode=choose_mode(); char in[512],out[512],dec[512]; unsigned char *data=NULL,*enc=NULL,*decoded=NULL; size_t len=0,enc_len=0,dec_len=0; int choice=0,thr=3; unsigned char chosen=0;
    printf("Enhanced RLE options:\n1) Threshold RLE\n2) Adaptive RLE\n3) RLE + Huffman entropy pipeline\n"); if(prompt_int("Select enhanced RLE option: ",&choice)!=0)return;
    if(choice==1||choice==3){ if(prompt_int("Threshold value (2-255): ",&thr)!=0||thr<2||thr>255)thr=3; }
    if(mode==1){ if(ask_paths(in,sizeof in,out,sizeof out,choice==3?".rle_entropy":".rle_enh")!=0)return; if(read_file(in,&data,&len)!=0)return; enc=malloc(len*8+4096); if(choice==2) rle1_encode_adaptive(data,len,enc,&enc_len,&chosen); else if(choice==3) rle1_entropy_pipeline_encode(data,len,enc,&enc_len,(unsigned char)thr); else rle1_encode_threshold(data,len,enc,&enc_len,(unsigned char)thr); print_bytes_preview("Input",data,len); print_bytes_preview("Enhanced encoded",enc,enc_len); if(choice==2)printf("Adaptive chosen threshold: %u\n",chosen); write_file(out,enc,enc_len); printf("Wrote: %s\n",out); }
    else if(mode==2){ if(ask_paths(in,sizeof in,out,sizeof out,".decoded")!=0)return; if(read_file(in,&data,&len)!=0)return; decoded=malloc(len*255+4096); if(choice==3) rle1_entropy_pipeline_decode(data,len,decoded,&dec_len); else rle1_decode(data,len,decoded,&dec_len); print_bytes_preview("Decoded",decoded,dec_len); write_file(out,decoded,dec_len); }
    else if(mode==3){ if(ask_paths(in,sizeof in,out,sizeof out,choice==3?".rle_entropy":".rle_enh")!=0)return; default_out_path(in,".enhanced.decoded",dec,sizeof dec); if(read_file(in,&data,&len)!=0)return; enc=malloc(len*8+4096); decoded=malloc(len*255+4096); if(choice==2) rle1_encode_adaptive(data,len,enc,&enc_len,&chosen); else if(choice==3) rle1_entropy_pipeline_encode(data,len,enc,&enc_len,(unsigned char)thr); else rle1_encode_threshold(data,len,enc,&enc_len,(unsigned char)thr); if(choice==3) rle1_entropy_pipeline_decode(enc,enc_len,decoded,&dec_len); else rle1_decode(enc,enc_len,decoded,&dec_len); print_bytes_preview("Input",data,len); print_bytes_preview("Enhanced encoded",enc,enc_len); if(choice==2)printf("Adaptive chosen threshold: %u\n",chosen); print_bytes_preview("Decoded",decoded,dec_len); write_file(out,enc,enc_len); write_file(dec,decoded,dec_len); printf("Verify: %s\n",dec_len==len&&memcmp(data,decoded,len)==0?"Enhanced RLE OK":"Enhanced RLE FAIL"); }
    free(data);free(enc);free(decoded);
}

static void run_range_file(void) {
    int mode=choose_mode(); char in[512],out[512],dec[512]; unsigned char *data=NULL,*enc=NULL,*decoded=NULL; size_t len=0,enc_len=0,dec_len=0;
    if(mode==1){ if(ask_paths(in,sizeof in,out,sizeof out,".rng")!=0)return; if(read_file(in,&data,&len)!=0)return; enc=malloc(len+2048); range_encode(data,len,enc,&enc_len); print_bytes_preview("Input",data,len); printf("Range experimental encoded size: %zu bytes\n",enc_len); write_file(out,enc,enc_len); }
    else if(mode==2){ if(ask_paths(in,sizeof in,out,sizeof out,".decoded")!=0)return; if(read_file(in,&data,&len)!=0)return; decoded=malloc(len+4096); range_decode(data,len,decoded,&dec_len); print_bytes_preview("Range decoded",decoded,dec_len); write_file(out,decoded,dec_len); }
    else if(mode==3){ if(ask_paths(in,sizeof in,out,sizeof out,".rng")!=0)return; default_out_path(in,".rng.decoded",dec,sizeof dec); if(read_file(in,&data,&len)!=0)return; enc=malloc(len+2048); decoded=malloc(len+4096); range_encode(data,len,enc,&enc_len); range_decode(enc,enc_len,decoded,&dec_len); printf("Range experimental encoded size: %zu bytes\n",enc_len); print_bytes_preview("Decoded",decoded,dec_len); write_file(out,enc,enc_len); write_file(dec,decoded,dec_len); printf("Verify: %s\n",dec_len==len&&memcmp(data,decoded,len)==0?"Range experiment OK":"Range experiment FAIL"); }
    free(data);free(enc);free(decoded);
}

static void run_benchmark_file(const char *arg_path, const char *arg_block, const char *arg_csv) {
    char   path[512], csv[512];
    int    bs_prompt = 0;
    AppConfig cfg;
    config_set_defaults(&cfg);
    config_load(CONFIG_DEFAULT_PATH, &cfg);

    if (arg_path) {
        snprintf(path, sizeof path, "%s", arg_path);
        snprintf(csv, sizeof csv, "%s", arg_csv ? arg_csv : "results/results.csv");
        if (arg_block && arg_block[0] != '\0') {
            long bs_long = strtol(arg_block, NULL, 10);
            if (bs_long >= (long)CONFIG_BLOCK_SIZE_MIN && bs_long <= (long)CONFIG_BLOCK_SIZE_MAX) {
                cfg.block_size = (size_t)bs_long;
            } else {
                fprintf(stderr, "Benchmark block_size must be between %u and %u bytes.\n",
                        (unsigned)CONFIG_BLOCK_SIZE_MIN, (unsigned)CONFIG_BLOCK_SIZE_MAX);
                return;
            }
        }
    } else {
        if (prompt_line("Benchmark input file path: ", path, sizeof path) != 0) return;
        if (prompt_int("Block size: ", &bs_prompt) != 0) bs_prompt = 500000;
        if (bs_prompt >= (int)CONFIG_BLOCK_SIZE_MIN && bs_prompt <= (int)CONFIG_BLOCK_SIZE_MAX) {
            cfg.block_size = (size_t)bs_prompt;
        }
        if (prompt_line("CSV output path (Enter = results/results.csv): ", csv, sizeof csv) != 0) return;
        if (csv[0] == '\0') snprintf(csv, sizeof csv, "results/results.csv");
    }

    char err[256];
    if (config_validate_block_size(cfg.block_size, err, sizeof err) != 0) {
        fprintf(stderr, "%s", err);
        return;
    }

    FILE *f = fopen(csv, "w");
    if (!f) {
        printf("Could not write CSV: %s\n", csv);
        return;
    }
    fprintf(f, "File,Size,BlockSize,CompressionRatio,Time,Memory\n");
    if (benchmark_one_file(path, &cfg, f) != 0) {
        fclose(f);
        return;
    }
    fclose(f);
    printf("Benchmark complete. CSV: %s\n", csv);
}

static void menu(void) {
    for (;;) {
        int c = -1;
        printf("\n=== BZip2 File-Based Evaluation Menu ===\n");
        printf("1) RLE-1 encode/decode using files\n"
               "2) BWT matrix encode/decode using files\n"
               "3) Block divide/reassemble using files\n"
               "4) Config parsing using file\n"
               "5) Stage-1 pipeline with intermediate outputs\n"
               "6) MTF encode/decode using files\n"
               "7) RLE-2 encode/decode using files\n"
               "8) Huffman encode/decode using files\n"
               "9) Full pipeline with all intermediate outputs\n"
               "10) EXTRA 8.1 Enhanced RLE variants\n"
               "11) EXTRA 8.2 Suffix-array BWT\n"
               "12) EXTRA 8.3 Alternative entropy coding experiment\n"
               "13) Batch benchmark all files in benchmarks/ -> results/results.csv\n"
               "14) Single-file benchmark (interactive prompts)\n"
               "0) Exit\n");
        if (prompt_int("Select option: ", &c) != 0) {
            continue;
        }
        if (c == 0) {
            return;
        }
        if (c == 1)       run_rle1_file();
        else if (c == 2)  run_bwt_file(0);
        else if (c == 3)  run_block_file();
        else if (c == 4)  run_config_file();
        else if (c == 5)  run_stage1_pipeline_file();
        else if (c == 6)  run_mtf_file();
        else if (c == 7)  run_rle2_file();
        else if (c == 8)  run_huffman_file();
        else if (c == 9)  run_full_pipeline_file();
        else if (c == 10) run_enhanced_rle_file();
        else if (c == 11) run_bwt_file(1);
        else if (c == 12) run_range_file();
        else if (c == 13) (void)run_default_benchmark_suite();
        else if (c == 14) run_benchmark_file(NULL, NULL, NULL);
        else              printf("Unknown option.\n");
    }
}

static void usage(const char *argv0) {
    printf(
        "Usage:\n"
        "  %s\n"
        "      Full pipeline on every file under input_directory → results/results.csv (see config.ini).\n"
        "  %s --menu\n"
        "      Interactive menu (per-stage tests, single-file benchmark, …).\n"
        "  %s --benchmark <file> <block-size> [csv-path]\n"
        "      Benchmark one file only.\n"
        "  %s --help\n",
        argv0, argv0, argv0, argv0);
}

int main(int argc, char **argv) {
    if (argc >= 4 && strcmp(argv[1], "--benchmark") == 0) {
        run_benchmark_file(argv[2], argv[3], argc >= 5 ? argv[4] : "results/results.csv");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(argv[0]);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--menu") == 0) {
        menu();
        return 0;
    }
    if (argc == 1) {
        return run_default_benchmark_suite();
    }
    usage(argv[0]);
    return 1;
}
