#ifndef MTF_H
#define MTF_H

#include <stddef.h>

void mtf_encode(unsigned char *input, size_t len, unsigned char *output);
void mtf_decode(unsigned char *input, size_t len, unsigned char *output);

#endif
