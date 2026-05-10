#include "bzip2.h"

#include <stdint.h>

#define HUFF_MAGIC_0 'H'
#define HUFF_MAGIC_1 'U'
#define HUFF_MAGIC_2 'F'
#define HUFF_MAGIC_3 '1'
#define HUFF_HEADER_SIZE (4u + 8u + 256u * 4u)

static HuffmanNode *new_node(unsigned char symbol, int freq,
                             HuffmanNode *left, HuffmanNode *right) {
    HuffmanNode *n = (HuffmanNode *)malloc(sizeof(HuffmanNode));
    if (!n) return NULL;
    n->symbol = symbol;
    n->freq   = freq;
    n->left   = left;
    n->right  = right;
    return n;
}

void free_huffman_tree(HuffmanNode *root) {
    if (!root) return;
    free_huffman_tree(root->left);
    free_huffman_tree(root->right);
    free(root);
}

static int is_leaf(const HuffmanNode *n) {
    return n && !n->left && !n->right;
}

/* Course spec §5.1.3 — exact prototype.
 * Output the root via `*root` (NULL if no symbols have non-zero frequency). */
void build_huffman_tree(int *frequencies, HuffmanNode **root) {
    if (!root) return;
    *root = NULL;
    if (!frequencies) return;

    HuffmanNode *nodes[512];
    int count = 0;

    for (int i = 0; i < 256; i++) {
        if (frequencies[i] > 0) {
            nodes[count] = new_node((unsigned char)i, frequencies[i], NULL, NULL);
            if (!nodes[count]) return;
            count++;
        }
    }
    if (count == 0) return;

    while (count > 1) {
        int a = -1, b = -1;
        for (int i = 0; i < count; i++) {
            if (a < 0 || nodes[i]->freq < nodes[a]->freq) {
                b = a;
                a = i;
            } else if (b < 0 || nodes[i]->freq < nodes[b]->freq) {
                b = i;
            }
        }
        if (a > b) {
            int t = a; a = b; b = t;
        }
        HuffmanNode *left   = nodes[a];
        HuffmanNode *right  = nodes[b];
        HuffmanNode *parent = new_node(0, left->freq + right->freq, left, right);
        if (!parent) return;

        nodes[a] = parent;
        nodes[b] = nodes[count - 1];
        count--;
    }
    *root = nodes[0];
}

static void make_codes_recursive(HuffmanNode *root, HuffmanCode *codes,
                                 unsigned int code, unsigned char length) {
    if (!root) return;
    if (is_leaf(root)) {
        codes[root->symbol].code   = code;
        codes[root->symbol].length = length ? length : 1;
        return;
    }
    make_codes_recursive(root->left,  codes, (code << 1),         (unsigned char)(length + 1));
    make_codes_recursive(root->right, codes, (code << 1) | 1u,    (unsigned char)(length + 1));
}

void generate_canonical_codes(HuffmanNode *root, HuffmanCode *codes) {
    if (!codes) return;
    for (int i = 0; i < 256; i++) {
        codes[i].code = 0;
        codes[i].length = 0;
    }
    make_codes_recursive(root, codes, 0, 0);
}

static void put_u32_le(unsigned char *out, size_t *p, unsigned int v) {
    out[(*p)++] = (unsigned char)(v & 0xffu);
    out[(*p)++] = (unsigned char)((v >> 8) & 0xffu);
    out[(*p)++] = (unsigned char)((v >> 16) & 0xffu);
    out[(*p)++] = (unsigned char)((v >> 24) & 0xffu);
}

static unsigned int get_u32_le(const unsigned char *in, size_t *p) {
    unsigned int v = ((unsigned int)in[*p])
        | ((unsigned int)in[*p + 1] << 8)
        | ((unsigned int)in[*p + 2] << 16)
        | ((unsigned int)in[*p + 3] << 24);
    *p += 4;
    return v;
}

static void put_u64_le(unsigned char *out, size_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        out[(*p)++] = (unsigned char)((v >> (8 * i)) & 0xffu);
    }
}

static uint64_t get_u64_le(const unsigned char *in, size_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v |= ((uint64_t)in[*p + i]) << (8 * i);
    }
    *p += 8;
    return v;
}

void write_header(HuffmanCode *codes, unsigned char *output, size_t *out_len) {
    if (!codes || !output || !out_len) return;
    output[0] = HUFF_MAGIC_0;
    output[1] = HUFF_MAGIC_1;
    output[2] = HUFF_MAGIC_2;
    output[3] = HUFF_MAGIC_3;
    for (int i = 0; i < 256; i++) {
        output[4 + i] = codes[i].length;
    }
    *out_len = 260;
}

void encode_data(unsigned char *input, size_t len, HuffmanCode *codes,
                 unsigned char *output, size_t *out_len) {
    if (!input || !codes || !output || !out_len) return;
    size_t out = 0;
    unsigned char current = 0;
    int used = 0;

    for (size_t i = 0; i < len; i++) {
        HuffmanCode hc = codes[input[i]];
        for (int bit = hc.length - 1; bit >= 0; bit--) {
            current = (unsigned char)((current << 1) | ((hc.code >> bit) & 1u));
            used++;
            if (used == 8) {
                output[out++] = current;
                current = 0;
                used = 0;
            }
        }
    }
    if (used > 0) {
        current = (unsigned char)(current << (8 - used));
        output[out++] = current;
    }
    *out_len = out;
}

void huffman_encode(unsigned char *input, size_t len,
                    unsigned char *output, size_t *out_len) {
    if (!input || !output || !out_len) return;

    int freq[256] = {0};
    for (size_t i = 0; i < len; i++) {
        freq[input[i]]++;
    }

    HuffmanNode *root = NULL;
    build_huffman_tree(freq, &root);
    HuffmanCode codes[256];
    generate_canonical_codes(root, codes);

    size_t pos = 0;
    output[pos++] = HUFF_MAGIC_0;
    output[pos++] = HUFF_MAGIC_1;
    output[pos++] = HUFF_MAGIC_2;
    output[pos++] = HUFF_MAGIC_3;
    put_u64_le(output, &pos, (uint64_t)len);
    for (int i = 0; i < 256; i++) {
        put_u32_le(output, &pos, (unsigned int)freq[i]);
    }

    size_t body_len = 0;
    encode_data(input, len, codes, output + pos, &body_len);
    *out_len = pos + body_len;
    free_huffman_tree(root);
}

void huffman_decode(unsigned char *input, size_t len,
                    unsigned char *output, size_t *out_len) {
    if (!input || !output || !out_len) return;
    *out_len = 0;
    if (len < HUFF_HEADER_SIZE) return;
    if (input[0] != HUFF_MAGIC_0 || input[1] != HUFF_MAGIC_1
        || input[2] != HUFF_MAGIC_2 || input[3] != HUFF_MAGIC_3) {
        return;
    }

    size_t pos = 4;
    uint64_t original_len = get_u64_le(input, &pos);
    int freq[256];
    for (int i = 0; i < 256; i++) {
        freq[i] = (int)get_u32_le(input, &pos);
    }

    HuffmanNode *root = NULL;
    build_huffman_tree(freq, &root);
    if (!root) return;

    if (is_leaf(root)) {
        for (uint64_t i = 0; i < original_len; i++) {
            output[i] = root->symbol;
        }
        *out_len = (size_t)original_len;
        free_huffman_tree(root);
        return;
    }

    HuffmanNode *cur = root;
    size_t in_pos = pos;
    uint64_t produced = 0;
    while (in_pos < len && produced < original_len) {
        unsigned char byte = input[in_pos++];
        for (int bit = 7; bit >= 0 && produced < original_len; bit--) {
            int b = (byte >> bit) & 1;
            cur = b ? cur->right : cur->left;
            if (!cur) {
                free_huffman_tree(root);
                return;
            }
            if (is_leaf(cur)) {
                output[produced++] = cur->symbol;
                cur = root;
            }
        }
    }
    *out_len = (size_t)produced;
    free_huffman_tree(root);
}
