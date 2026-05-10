#ifndef RANGE_H
#define RANGE_H

#include <stddef.h>

void range_encode(unsigned char *input, size_t len,
                  unsigned char *output, size_t *out_len);
void range_decode(unsigned char *input, size_t len,
                  unsigned char *output, size_t *out_len);

#endif
