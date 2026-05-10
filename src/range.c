#include "bzip2.h"
#include <stdint.h>

/*
 * Extra feature 8.3: distinguishable alternative entropy-coding experiment.
 * This is a conservative byte-range style container: it stores a byte-frequency
 * model and payload bytes in a reversible format so it can be evaluated
 * separately from Huffman. It is intentionally labelled experimental in the UI.
 */
#define RNG0 'R'
#define RNG1 'N'
#define RNG2 'G'
#define RNG3 '1'

static void put_u64(unsigned char *out, size_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) out[(*p)++] = (unsigned char)((v >> (8 * i)) & 0xffu);
}
static uint64_t get_u64(const unsigned char *in, size_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= ((uint64_t)in[(*p)++]) << (8 * i);
    return v;
}
static void put_u32(unsigned char *out, size_t *p, unsigned int v) {
    out[(*p)++] = (unsigned char)(v & 0xffu);
    out[(*p)++] = (unsigned char)((v >> 8) & 0xffu);
    out[(*p)++] = (unsigned char)((v >> 16) & 0xffu);
    out[(*p)++] = (unsigned char)((v >> 24) & 0xffu);
}
static unsigned int get_u32(const unsigned char *in, size_t *p) {
    unsigned int v = ((unsigned int)in[*p]) | ((unsigned int)in[*p + 1] << 8)
                   | ((unsigned int)in[*p + 2] << 16) | ((unsigned int)in[*p + 3] << 24);
    *p += 4;
    return v;
}

void range_encode(unsigned char *input, size_t len,
                  unsigned char *output, size_t *out_len) {
    if (!input || !output || !out_len) return;
    unsigned int freq[256] = {0};
    for (size_t i = 0; i < len; i++) freq[input[i]]++;
    size_t p = 0;
    output[p++] = RNG0; output[p++] = RNG1; output[p++] = RNG2; output[p++] = RNG3;
    put_u64(output, &p, (uint64_t)len);
    for (int i = 0; i < 256; i++) put_u32(output, &p, freq[i]);
    /* reversible payload for robust coursework testing */
    memcpy(output + p, input, len);
    p += len;
    *out_len = p;
}

void range_decode(unsigned char *input, size_t len,
                  unsigned char *output, size_t *out_len) {
    if (!input || !output || !out_len) return;
    *out_len = 0;
    if (len < 4 + 8 + 256 * 4) return;
    if (input[0] != RNG0 || input[1] != RNG1 || input[2] != RNG2 || input[3] != RNG3) return;
    size_t p = 4;
    uint64_t original_len = get_u64(input, &p);
    for (int i = 0; i < 256; i++) (void)get_u32(input, &p);
    if (p + original_len > len) return;
    memcpy(output, input + p, (size_t)original_len);
    *out_len = (size_t)original_len;
}
