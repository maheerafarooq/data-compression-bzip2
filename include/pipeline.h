#ifndef PIPELINE_H
#define PIPELINE_H

#include <stddef.h>

#include "config.h"

/*
 * Course pipeline (per block): Block slice → RLE-1 → BWT → MTF → RLE-2 → Huffman.
 * Multiple blocks are wrapped in a small container (BZP1) so large files use
 * configurable block_size from AppConfig.
 *
 * Loads defaults from cfg; caller may override block_size (e.g. benchmark CLI).
 * Returns total compressed package size in *total_compressed_bytes.
 * peak_memory_bytes (optional, may be NULL) returns a high-water estimate of
 * the heap memory held by pipeline buffers during a single block, in bytes.
 */
int pipeline_run(const unsigned char *input, size_t n, const AppConfig *cfg,
                 int show_intermediate, size_t *total_compressed_bytes,
                 unsigned char **decoded_out, size_t *decoded_len,
                 size_t *peak_memory_bytes);

#endif
