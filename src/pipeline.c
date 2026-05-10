#include "pipeline.h"
#include "bzip2.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define PKG_MAGIC0 'B'
#define PKG_MAGIC1 'Z'
#define PKG_MAGIC2 'P'
#define PKG_MAGIC3 '1'
#define PKG_VERSION 1u

#define RAW_HUFF_TAG0 'N'
#define RAW_HUFF_TAG1 'O'
#define RAW_HUFF_TAG2 'H'
#define RAW_HUFF_TAG3 '1'

static void put_u32_le(unsigned char *out, size_t *p, unsigned int v) {
    out[(*p)++] = (unsigned char)(v & 0xffu);
    out[(*p)++] = (unsigned char)((v >> 8) & 0xffu);
    out[(*p)++] = (unsigned char)((v >> 16) & 0xffu);
    out[(*p)++] = (unsigned char)((v >> 24) & 0xffu);
}

static void put_u64_le(unsigned char *out, size_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        out[(*p)++] = (unsigned char)((v >> (8 * i)) & 0xffu);
    }
}

static uint64_t get_u64_le(const unsigned char *in, size_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v |= (uint64_t)in[(*p)++] << (8 * i);
    }
    return v;
}

/*
 * One block: encode to a self-contained entropy chunk (Huffman or raw-RLE2 wrap),
 * then decode back to plain block for verification.
 */
static int process_one_block(const unsigned char *in, size_t n, const AppConfig *cfg,
                             int verbose,
                             unsigned char **entropy_chunk_out, size_t *entropy_len_out,
                             unsigned char *plain_out, size_t *plain_len_out) {
    if (!in || !cfg || !entropy_chunk_out || !entropy_len_out || !plain_out || !plain_len_out) {
        return -1;
    }
    *entropy_len_out = 0;
    *plain_len_out   = 0;
    if (n == 0) {
        return 0;
    }

    size_t cap1 = n * 3 + 128;
    size_t cap2 = cap1 * 2 + 128;
    size_t hcap = 4 + 8 + 256 * 4 + cap2 * 4 + 256;

    unsigned char *rle1  = (unsigned char *)malloc(cap1);
    unsigned char *bwt   = (unsigned char *)malloc(cap1);
    unsigned char *mtf   = (unsigned char *)malloc(cap1);
    unsigned char *rle2  = (unsigned char *)malloc(cap2);
    unsigned char *entr  = (unsigned char *)malloc(hcap);
    unsigned char *dentr = (unsigned char *)malloc(cap2 * 2);
    unsigned char *drle2 = (unsigned char *)malloc(cap2 * 2);
    unsigned char *dmtf  = (unsigned char *)malloc(cap1 * 2);
    unsigned char *dbwt  = (unsigned char *)malloc(cap1 * 2);
    unsigned char *drle1 = (unsigned char *)malloc(cap1 * 2);

    if (!rle1 || !bwt || !mtf || !rle2 || !entr || !dentr || !drle2 || !dmtf || !dbwt || !drle1) {
        free(rle1);
        free(bwt);
        free(mtf);
        free(rle2);
        free(entr);
        free(dentr);
        free(drle2);
        free(dmtf);
        free(dbwt);
        free(drle1);
        return -1;
    }

    size_t rle1_len = 0;
    if (cfg->rle1_enabled) {
        rle1_encode((unsigned char *)in, n, rle1, &rle1_len);
    } else {
        if (n > cap1) {
            free(rle1);
            free(bwt);
            free(mtf);
            free(rle2);
            free(entr);
            free(dentr);
            free(drle2);
            free(dmtf);
            free(dbwt);
            free(drle1);
            return -1;
        }
        memcpy(rle1, in, n);
        rle1_len = n;
    }

    int primary = -1;
    if (cfg->bwt_use_matrix) {
        bwt_encode(rle1, rle1_len, bwt, &primary);
    } else {
        bwt_encode_suffix_array(rle1, rle1_len, bwt, &primary);
    }

    if (cfg->mtf_enabled) {
        mtf_encode(bwt, rle1_len, mtf);
    } else {
        memcpy(mtf, bwt, rle1_len);
    }

    size_t rle2_len = 0;
    if (cfg->rle2_enabled) {
        rle2_encode(mtf, rle1_len, rle2, &rle2_len);
    } else {
        memcpy(rle2, mtf, rle1_len);
        rle2_len = rle1_len;
    }

    size_t ent_len = 0;
    if (cfg->huffman_enabled) {
        huffman_encode(rle2, rle2_len, entr, &ent_len);
    } else {
        if (12 + rle2_len > hcap) {
            free(rle1);
            free(bwt);
            free(mtf);
            free(rle2);
            free(entr);
            free(dentr);
            free(drle2);
            free(dmtf);
            free(dbwt);
            free(drle1);
            return -1;
        }
        size_t p = 0;
        entr[p++] = RAW_HUFF_TAG0;
        entr[p++] = RAW_HUFF_TAG1;
        entr[p++] = RAW_HUFF_TAG2;
        entr[p++] = RAW_HUFF_TAG3;
        put_u64_le(entr, &p, (uint64_t)rle2_len);
        memcpy(entr + p, rle2, rle2_len);
        ent_len = p + rle2_len;
    }

    /* ---------- decode (verify round-trip for this block) ---------- */
    size_t dentr_len = 0;
    if (cfg->huffman_enabled) {
        huffman_decode(entr, ent_len, dentr, &dentr_len);
    } else {
        if (ent_len < 12 || entr[0] != RAW_HUFF_TAG0 || entr[1] != RAW_HUFF_TAG1
            || entr[2] != RAW_HUFF_TAG2 || entr[3] != RAW_HUFF_TAG3) {
            free(rle1);
            free(bwt);
            free(mtf);
            free(rle2);
            free(entr);
            free(dentr);
            free(drle2);
            free(dmtf);
            free(dbwt);
            free(drle1);
            return -1;
        }
        size_t pp = 4;
        uint64_t rl = get_u64_le(entr, &pp);
        if (pp + rl > ent_len || rl > (uint64_t)(cap2 * 2)) {
            free(rle1);
            free(bwt);
            free(mtf);
            free(rle2);
            free(entr);
            free(dentr);
            free(drle2);
            free(dmtf);
            free(dbwt);
            free(drle1);
            return -1;
        }
        memcpy(dentr, entr + pp, (size_t)rl);
        dentr_len = (size_t)rl;
    }

    size_t drle2_len = 0;
    if (cfg->rle2_enabled) {
        rle2_decode(dentr, dentr_len, drle2, &drle2_len);
    } else {
        memcpy(drle2, dentr, dentr_len);
        drle2_len = dentr_len;
    }

    if (drle2_len != rle1_len) {
        free(rle1);
        free(bwt);
        free(mtf);
        free(rle2);
        free(entr);
        free(dentr);
        free(drle2);
        free(dmtf);
        free(dbwt);
        free(drle1);
        return -1;
    }

    if (cfg->mtf_enabled) {
        mtf_decode(drle2, drle2_len, dmtf);
    } else {
        memcpy(dmtf, drle2, drle2_len);
    }

    bwt_decode(dmtf, rle1_len, primary, dbwt);

    size_t drle1_out = 0;
    if (cfg->rle1_enabled) {
        rle1_decode(dbwt, rle1_len, drle1, &drle1_out);
    } else {
        memcpy(drle1, dbwt, rle1_len);
        drle1_out = rle1_len;
    }

    if (drle1_out != n || memcmp(drle1, in, n) != 0) {
        free(rle1);
        free(bwt);
        free(mtf);
        free(rle2);
        free(entr);
        free(dentr);
        free(drle2);
        free(dmtf);
        free(dbwt);
        free(drle1);
        return -1;
    }

    if (verbose) {
        /* Caller prints block context */
        printf("  [block] plain=%zu rle1=%zu rle2=%zu entropy=%zu\n", n, rle1_len, rle2_len,
               ent_len);
    }

    *entropy_chunk_out = entr;
    *entropy_len_out   = ent_len;
    memcpy(plain_out, in, n);
    *plain_len_out = n;

    free(rle1);
    free(bwt);
    free(mtf);
    free(rle2);
    free(dentr);
    free(drle2);
    free(dmtf);
    free(dbwt);
    free(drle1);
    /* entr handed to caller */
    return 0;
}

static void print_preview_line(const char *label, const unsigned char *buf, size_t len) {
    printf("%s (%zu bytes): ", label, len);
    size_t limit = len < 64 ? len : 64;
    for (size_t i = 0; i < limit; i++) {
        if (buf[i] >= 32 && buf[i] < 127) {
            printf("%c", buf[i]);
        } else {
            printf("[%02X]", buf[i]);
        }
    }
    if (len > limit) {
        printf("...");
    }
    printf("\n");
}

int pipeline_run(const unsigned char *input, size_t n, const AppConfig *cfg,
                 int show_intermediate, size_t *total_compressed_bytes,
                 unsigned char **decoded_out, size_t *decoded_len,
                 size_t *peak_memory_bytes) {
    if (!cfg || !total_compressed_bytes || !decoded_out || !decoded_len) {
        return -1;
    }
    *total_compressed_bytes = 0;
    *decoded_out            = NULL;
    *decoded_len            = 0;
    if (peak_memory_bytes) {
        *peak_memory_bytes = 0;
    }

    AppConfig local = *cfg;
    char err[256];
    if (config_validate_block_size(local.block_size, err, sizeof err) != 0) {
        fprintf(stderr, "pipeline: %s", err);
        return -1;
    }

    size_t bs = local.block_size;
    if (bs == 0) {
        bs = 500000u;
    }

    unsigned char *decoded = (unsigned char *)malloc(n ? n : 1);
    if (!decoded) {
        return -1;
    }

    if (n == 0) {
        size_t hdr = 12;
        unsigned char *pkg = (unsigned char *)malloc(hdr);
        if (!pkg) {
            free(decoded);
            return -1;
        }
        size_t p = 0;
        pkg[p++] = PKG_MAGIC0;
        pkg[p++] = PKG_MAGIC1;
        pkg[p++] = PKG_MAGIC2;
        pkg[p++] = PKG_MAGIC3;
        put_u32_le(pkg, &p, PKG_VERSION);
        put_u32_le(pkg, &p, 0u);
        *total_compressed_bytes = hdr;
        free(pkg);
        *decoded_out = decoded;
        *decoded_len = 0;
        if (show_intermediate) {
            printf("Empty input: 0 blocks, package header only (%zu bytes).\n", hdr);
        }
        return 0;
    }

    uint32_t num_blocks = (uint32_t)((n + bs - 1) / bs);
    if (show_intermediate) {
        printf("Pipeline: %zu byte input, block_size=%zu → %u block(s)\n"
               "Stages per block: RLE-1(%s) → BWT(%s) → MTF(%s) → RLE-2(%s) → %s\n",
               n, bs, num_blocks,
               local.rle1_enabled ? "on" : "pass",
               local.bwt_use_matrix ? "matrix" : "suffix_array",
               local.mtf_enabled ? "on" : "pass",
               local.rle2_enabled ? "on" : "pass",
               local.huffman_enabled ? "Huffman" : "raw+RLE2 package");
    }

    unsigned char *package = (unsigned char *)malloc(12);
    if (!package) {
        free(decoded);
        return -1;
    }
    size_t pp = 0;
    package[pp++] = PKG_MAGIC0;
    package[pp++] = PKG_MAGIC1;
    package[pp++] = PKG_MAGIC2;
    package[pp++] = PKG_MAGIC3;
    put_u32_le(package, &pp, PKG_VERSION);
    put_u32_le(package, &pp, num_blocks);

    size_t dec_off = 0;
    for (uint32_t bi = 0; bi < num_blocks; bi++) {
        size_t offset = (size_t)bi * bs;
        size_t bl     = n - offset;
        if (bl > bs) {
            bl = bs;
        }
        unsigned char *chunk = NULL;
        size_t         ch_len = 0;
        size_t         plain_chk = 0;
        if (process_one_block(input + offset, bl, &local,
                               show_intermediate && bi == 0,
                               &chunk, &ch_len, decoded + dec_off, &plain_chk) != 0) {
            free(package);
            free(decoded);
            return -1;
        }
        if (plain_chk != bl) {
            free(chunk);
            free(package);
            free(decoded);
            return -1;
        }
        size_t need = pp + 16 + ch_len;
        unsigned char *np = (unsigned char *)realloc(package, need);
        if (!np) {
            free(chunk);
            free(package);
            free(decoded);
            return -1;
        }
        package = np;
        put_u64_le(package, &pp, (uint64_t)bl);
        put_u64_le(package, &pp, (uint64_t)ch_len);
        memcpy(package + pp, chunk, ch_len);
        pp += ch_len;
        dec_off += bl;
        if (show_intermediate && bi == 0 && chunk && ch_len > 0) {
            print_preview_line("  First block entropy chunk (prefix)", chunk, ch_len < 48 ? ch_len : 48);
        }
        free(chunk);
    }

    *total_compressed_bytes = pp;
    *decoded_out            = decoded;
    *decoded_len            = n;
    if (peak_memory_bytes) {
        /* Peak heap held by pipeline buffers during one block:
         * forward+reverse buffers for RLE-1, BWT, MTF, RLE-2 and Huffman
         * sum to roughly 30× the largest block actually processed. For
         * files smaller than block_size only n bytes worth of buffers
         * are allocated. */
        size_t worst = (n < bs) ? n : bs;
        *peak_memory_bytes = worst * 30u + pp + 4096u;
    }

    if (show_intermediate) {
        printf("Compressed package size (BZP1 container + entropy chunks): %zu bytes\n", pp);
        print_preview_line("Recovered (full)", decoded, n < 96 ? n : 96);
        printf("Verify full file: %s\n", memcmp(input, decoded, n) == 0 ? "OK" : "FAIL");
    }

    free(package);
    return 0;
}
