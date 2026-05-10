#ifndef RLE2_H
#define RLE2_H

#include <stddef.h>

void rle2_encode(unsigned char *input, size_t len,
                 unsigned char *output, size_t *out_len);
void rle2_decode(unsigned char *input, size_t len,
                 unsigned char *output, size_t *out_len);

#endif
