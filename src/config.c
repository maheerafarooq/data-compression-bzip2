#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_set_defaults(AppConfig *cfg) {
    if (!cfg) return;
    cfg->block_size         = 500000u;
    cfg->rle1_enabled       = 1;
    cfg->bwt_use_matrix     = 1;
    cfg->mtf_enabled        = 1;
    cfg->rle2_enabled       = 1;
    cfg->huffman_enabled    = 1;
    cfg->benchmark_mode     = 0;
    cfg->output_metrics     = 1;
    memcpy(cfg->input_directory,  "./benchmarks/", sizeof("./benchmarks/"));
    memcpy(cfg->output_directory, "./results/",     sizeof("./results/"));
}

void config_str_trim(char *s) {
    if (!s) return;
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[--n] = '\0';
    }
}

static int streq_ic(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static int str_to_bool(const char *v) {
    return streq_ic(v, "1") || streq_ic(v, "true") || streq_ic(v, "yes")
        || streq_ic(v, "on");
}

static int parse_size_value(const char *s, size_t *out) {
    char *end = NULL;
    unsigned long u = strtoul(s, &end, 10);
    if (end == s) return -1;
    *out = (size_t)u;
    return 0;
}

int config_validate_block_size(size_t block_size, char *errbuf, size_t errbuf_size) {
    if (block_size < CONFIG_BLOCK_SIZE_MIN || block_size > CONFIG_BLOCK_SIZE_MAX) {
        if (errbuf && errbuf_size) {
            snprintf(errbuf, errbuf_size,
                     "block_size must be in [%u, %u] bytes (per course spec: 100–900 KB)\n",
                     (unsigned)CONFIG_BLOCK_SIZE_MIN,
                     (unsigned)CONFIG_BLOCK_SIZE_MAX);
        }
        return -1;
    }
    return 0;
}

int config_load(const char *filepath, AppConfig *cfg) {
    if (!cfg) return -1;
    config_set_defaults(cfg);
    if (!filepath) return 0;

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        config_set_defaults(cfg);
        return 0;
    }

    char line[1024];
    int line_no = 0;
    int ret = 0;

    while (fgets(line, (int)sizeof line, f)) {
        line_no++;
        char *c = line;
        /* strip comments (including rest of line after #) */
        char *hash = strchr(c, '#');
        if (hash) *hash = '\0';
        /* trim */
        size_t n = strcspn(c, "\r\n");
        c[n] = '\0';
        config_str_trim(c);
        if (c[0] == '\0') continue;

        if (c[0] == '[') continue;  /* [ section ] — ignored, keys are unique for our use */

        char *eq = strchr(c, '=');
        if (!eq) {
            fprintf(stderr, "config: %s line %d: expected key=value\n", filepath, line_no);
            ret = -1;
            break;
        }
        *eq++ = '\0';
        config_str_trim(c);
        config_str_trim(eq);

        if (streq_ic(c, "block_size")) {
            if (parse_size_value(eq, &cfg->block_size) != 0) {
                fprintf(stderr, "config: invalid block_size at line %d\n", line_no);
                ret = -1;
                break;
            }
        } else if (streq_ic(c, "rle1_enabled")) {
            cfg->rle1_enabled = str_to_bool(eq);
        } else if (streq_ic(c, "bwt_type")) {
            if (strstr(eq, "suffix") != NULL) {
                cfg->bwt_use_matrix = 0;
            } else {
                cfg->bwt_use_matrix = 1;
            }
        } else if (streq_ic(c, "mtf_enabled")) {
            cfg->mtf_enabled = str_to_bool(eq);
        } else if (streq_ic(c, "rle2_enabled")) {
            cfg->rle2_enabled = str_to_bool(eq);
        } else if (streq_ic(c, "huffman_enabled")) {
            cfg->huffman_enabled = str_to_bool(eq);
        } else if (streq_ic(c, "benchmark_mode")) {
            cfg->benchmark_mode = str_to_bool(eq);
        } else if (streq_ic(c, "output_metrics")) {
            cfg->output_metrics = str_to_bool(eq);
        } else if (streq_ic(c, "input_directory")) {
            snprintf(cfg->input_directory, sizeof cfg->input_directory, "%s", eq);
        } else if (streq_ic(c, "output_directory")) {
            snprintf(cfg->output_directory, sizeof cfg->output_directory, "%s", eq);
        }
    }
    fclose(f);

    {
        char err[256];
        if (config_validate_block_size(cfg->block_size, err, sizeof err) != 0) {
            fprintf(stderr, "config: %s", err);
            return -1;
        }
    }

    return ret;
}
