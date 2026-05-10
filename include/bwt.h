#ifndef BWT_H
#define BWT_H

#include <stddef.h>

typedef struct {
    char *rotation;
    int   index;
} Rotation;

int  compare_rotations(const void *a, const void *b);
void bwt_encode(unsigned char *input, size_t len,
                unsigned char *output, int *primary_index);
void bwt_decode(unsigned char *input, size_t len,
                int primary_index, unsigned char *output);

int *build_suffix_array(unsigned char *text, int n);
void bwt_encode_suffix_array(unsigned char *input, size_t len,
                             unsigned char *output, int *primary_index);

#endif
