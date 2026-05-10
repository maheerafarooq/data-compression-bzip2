#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

/*
 * Course spec: 100 KB – 900 KB (see config.ini comment block).
 */
#define CONFIG_BLOCK_SIZE_MIN  (100u * 1024u)
#define CONFIG_BLOCK_SIZE_MAX  (900u * 1024u)
#define CONFIG_DEFAULT_PATH    "config.ini"
#define CONFIG_PATH_MAX        512

typedef struct {
    size_t  block_size;
    int     rle1_enabled;
    int     bwt_use_matrix;  /* 1 = matrix BWT, 0 = suffix-array BWT (§8.2) */
    int     mtf_enabled;
    int     rle2_enabled;
    int     huffman_enabled;
    int     benchmark_mode;
    int     output_metrics;
    char    input_directory[CONFIG_PATH_MAX];
    char    output_directory[CONFIG_PATH_MAX];
} AppConfig;

/* Fill defaults (course doc: block_size 500000, paths ./benchmarks/ ./results/) */
void config_set_defaults(AppConfig *cfg);

/* Load config.ini; on missing file, keeps defaults. Returns 0, or -1 on parse/IO error. */
int config_load(const char *filepath, AppConfig *cfg);

/* 0 = valid; -1 = invalid (prints reason to stderr if msg buffer given). */
int config_validate_block_size(size_t block_size, char *errbuf, size_t errbuf_size);

/* Strip surrounding whitespace in-place. */
void config_str_trim(char *s);

#endif
