#include "bzip2.h"

/*
 * RLE-1: binary-safe run-length encoding.
 * Runs of length >= 2: 0xFF, byte, count.
 * Single 0xFF: 0xFF, 0xFF, 1
 */

#define RLE1_ESCAPE   0xFF
#define RLE1_MIN_RUN  2
#define RLE1_MAX_RUN  255

void rle1_encode(unsigned char *input, size_t len,
                 unsigned char *output, size_t *out_len) {
    if (!input || !output || !out_len) return;

    size_t in_pos  = 0;
    size_t out_pos = 0;

    while (in_pos < len) {
        unsigned char current = input[in_pos];
        size_t        run_len = 1;

        while (in_pos + run_len < len
               && input[in_pos + run_len] == current
               && run_len < RLE1_MAX_RUN) {
            run_len++;
        }

        if (run_len >= RLE1_MIN_RUN) {
            output[out_pos++] = RLE1_ESCAPE;
            output[out_pos++] = current;
            output[out_pos++] = (unsigned char)run_len;
        } else {
            if (current == RLE1_ESCAPE) {
                output[out_pos++] = RLE1_ESCAPE;
                output[out_pos++] = RLE1_ESCAPE;
                output[out_pos++] = 1;
            } else {
                output[out_pos++] = current;
            }
        }

        in_pos += run_len;
    }

    *out_len = out_pos;
}

void rle1_decode(unsigned char *input, size_t len,
                 unsigned char *output, size_t *out_len) {
    if (!input || !output || !out_len) return;

    size_t in_pos  = 0;
    size_t out_pos = 0;

    while (in_pos < len) {
        if (input[in_pos] == RLE1_ESCAPE) {
            if (in_pos + 2 >= len) {
                break;
            }
            unsigned char byte  = input[in_pos + 1];
            unsigned char count = input[in_pos + 2];
            for (unsigned char i = 0; i < count; i++) {
                output[out_pos++] = byte;
            }
            in_pos += 3;
        } else {
            output[out_pos++] = input[in_pos++];
        }
    }

    *out_len = out_pos;
}

/* =========================================================
 * Extra feature 8.1: Enhanced RLE variants
 * ========================================================= */

void rle1_encode_threshold(unsigned char *input, size_t len,
                           unsigned char *output, size_t *out_len,
                           unsigned char threshold) {
    if (!input || !output || !out_len) return;
    if (threshold < 2) threshold = 2;
    size_t in_pos = 0, out_pos = 0;
    while (in_pos < len) {
        unsigned char current = input[in_pos];
        size_t run_len = 1;
        while (in_pos + run_len < len && input[in_pos + run_len] == current && run_len < RLE1_MAX_RUN) {
            run_len++;
        }
        if (run_len >= threshold || current == RLE1_ESCAPE) {
            output[out_pos++] = RLE1_ESCAPE;
            output[out_pos++] = current;
            output[out_pos++] = (unsigned char)run_len;
        } else {
            for (size_t i = 0; i < run_len; i++) output[out_pos++] = current;
        }
        in_pos += run_len;
    }
    *out_len = out_pos;
}

void rle1_encode_adaptive(unsigned char *input, size_t len,
                          unsigned char *output, size_t *out_len,
                          unsigned char *chosen_threshold) {
    if (!input || !output || !out_len) return;
    size_t repeated_bytes = 0;
    size_t runs = 0;
    for (size_t i = 0; i < len;) {
        size_t run = 1;
        while (i + run < len && input[i + run] == input[i] && run < RLE1_MAX_RUN) run++;
        if (run >= 2) {
            repeated_bytes += run;
            runs++;
        }
        i += run;
    }
    unsigned char threshold = 3;
    if (len > 0) {
        double repeat_ratio = (double)repeated_bytes / (double)len;
        if (repeat_ratio > 0.45) threshold = 2;       /* data is run-heavy */
        else if (repeat_ratio < 0.10 || runs < 2) threshold = 4; /* avoid overhead */
        else threshold = 3;
    }
    if (chosen_threshold) *chosen_threshold = threshold;
    rle1_encode_threshold(input, len, output, out_len, threshold);
}

void rle1_entropy_pipeline_encode(unsigned char *input, size_t len,
                                  unsigned char *output, size_t *out_len,
                                  unsigned char threshold) {
    if (!input || !output || !out_len) return;
    unsigned char *rle = (unsigned char *)malloc(len * 3 + 16);
    if (!rle) { *out_len = 0; return; }
    size_t rle_len = 0;
    rle1_encode_threshold(input, len, rle, &rle_len, threshold);
    huffman_encode(rle, rle_len, output, out_len);
    free(rle);
}

void rle1_entropy_pipeline_decode(unsigned char *input, size_t len,
                                  unsigned char *output, size_t *out_len) {
    if (!input || !output || !out_len) return;
    unsigned char *rle = (unsigned char *)malloc(len * 8 + 4096);
    if (!rle) { *out_len = 0; return; }
    size_t rle_len = 0;
    huffman_decode(input, len, rle, &rle_len);
    rle1_decode(rle, rle_len, output, out_len);
    free(rle);
}
