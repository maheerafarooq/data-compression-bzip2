#ifndef HUFFMAN_H
#define HUFFMAN_H

#include <stddef.h>

/* Course spec, §5.1.2 — exact data structures */
typedef struct HuffmanNode {
    unsigned char symbol;        /* byte value (0–255) */
    int           freq;          /* frequency count */
    struct HuffmanNode *left;
    struct HuffmanNode *right;
} HuffmanNode;

/* Course spec shows `unsigned short code` (Listing 9), but for natural-language
 * text after BWT+MTF+RLE-2 the Huffman tree depth can exceed 16 levels, which
 * would silently truncate codes. We widen to `unsigned int` (32 bits) — the
 * spec calls these "guidance" types (§10) and a length-limited Huffman would
 * be the only other way to keep `unsigned short` safely. */
typedef struct {
    unsigned int  code;          /* canonical Huffman code */
    unsigned char length;        /* code length in bits */
} HuffmanCode;

/* Course spec, §5.1.3 — exact prototypes */
void build_huffman_tree(int *frequencies, HuffmanNode **root);
void generate_canonical_codes(HuffmanNode *root, HuffmanCode *codes);
void huffman_encode(unsigned char *input, size_t len,
                    unsigned char *output, size_t *out_len);
void huffman_decode(unsigned char *input, size_t len,
                    unsigned char *output, size_t *out_len);
void write_header(HuffmanCode *codes, unsigned char *output, size_t *out_len);
void encode_data(unsigned char *input, size_t len, HuffmanCode *codes,
                 unsigned char *output, size_t *out_len);
void free_huffman_tree(HuffmanNode *root);

#endif
