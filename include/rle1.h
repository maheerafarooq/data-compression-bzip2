#ifndef RLE1_H
#define RLE1_H

#include <stddef.h>

void rle1_encode(unsigned char *input, size_t len,
                 unsigned char *output, size_t *out_len);
void rle1_decode(unsigned char *input, size_t len,
                 unsigned char *output, size_t *out_len);

void rle1_encode_threshold(unsigned char *input, size_t len,
                           unsigned char *output, size_t *out_len,
                           unsigned char threshold);
void rle1_encode_adaptive(unsigned char *input, size_t len,
                          unsigned char *output, size_t *out_len,
                          unsigned char *chosen_threshold);
void rle1_entropy_pipeline_encode(unsigned char *input, size_t len,
                                  unsigned char *output, size_t *out_len,
                                  unsigned char threshold);
void rle1_entropy_pipeline_decode(unsigned char *input, size_t len,
                                  unsigned char *output, size_t *out_len);

#endif
