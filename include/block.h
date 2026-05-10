#ifndef BLOCK_H
#define BLOCK_H

#include <stddef.h>

typedef struct {
    unsigned char *data;
    size_t         size;
    size_t         original_size;
} Block;

typedef struct {
    Block  *blocks;
    int     num_blocks;
    size_t  block_size;
} BlockManager;

BlockManager *divide_into_blocks(const char *filename, size_t block_size);
int           reassemble_blocks(BlockManager *manager, const char *output_filename);
void          free_block_manager(BlockManager *manager);

#endif
