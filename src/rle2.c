#include "bzip2.h"

/*
 * Stage-2 RLE for MTF output.
 * MTF data commonly contains long zero runs. This format stores every zero run
 * as two bytes: 0x00, run_length. Non-zero values are copied literally.
 */
void rle2_encode(unsigned char *input, size_t len,
                 unsigned char *output, size_t *out_len) {
    if (!input || !output || !out_len) return;

    size_t out = 0;
    size_t i = 0;
    while (i < len) {
        if (input[i] == 0) {
            size_t run = 1;
            while (i + run < len && input[i + run] == 0 && run < 255) {
                run++;
            }
            output[out++] = 0;
            output[out++] = (unsigned char)run;
            i += run;
        } else {
            output[out++] = input[i++];
        }
    }
    *out_len = out;
}

void rle2_decode(unsigned char *input, size_t len,
                 unsigned char *output, size_t *out_len) {
    if (!input || !output || !out_len) return;

    size_t out = 0;
    size_t i = 0;
    while (i < len) {
        if (input[i] == 0) {
            if (i + 1 >= len) {
                break;
            }
            unsigned char run = input[i + 1];
            for (unsigned int j = 0; j < run; j++) {
                output[out++] = 0;
            }
            i += 2;
        } else {
            output[out++] = input[i++];
        }
    }
    *out_len = out;
}
