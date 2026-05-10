#include "bzip2.h"

static void init_symbols(unsigned char symbols[256]) {
    for (int i = 0; i < 256; i++) {
        symbols[i] = (unsigned char)i;
    }
}

void mtf_encode(unsigned char *input, size_t len, unsigned char *output) {
    if (!input || !output) return;

    unsigned char symbols[256];
    init_symbols(symbols);

    for (size_t i = 0; i < len; i++) {
        unsigned char value = input[i];
        int index = 0;
        while (index < 256 && symbols[index] != value) {
            index++;
        }
        output[i] = (unsigned char)index;

        while (index > 0) {
            symbols[index] = symbols[index - 1];
            index--;
        }
        symbols[0] = value;
    }
}

void mtf_decode(unsigned char *input, size_t len, unsigned char *output) {
    if (!input || !output) return;

    unsigned char symbols[256];
    init_symbols(symbols);

    for (size_t i = 0; i < len; i++) {
        int index = input[i];
        unsigned char value = symbols[index];
        output[i] = value;

        while (index > 0) {
            symbols[index] = symbols[index - 1];
            index--;
        }
        symbols[0] = value;
    }
}
