#include "bzip2.h"

#include <stdio.h>

BlockManager *divide_into_blocks(const char *filename, size_t block_size) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    if (file_size <= 0) {
        fprintf(stderr, "Error: File is empty or unreadable\n");
        fclose(file);
        return NULL;
    }

    int num_blocks = (int)((file_size + (long)block_size - 1) / (long)block_size);

    BlockManager *manager = (BlockManager *)malloc(sizeof(BlockManager));
    if (!manager) {
        fprintf(stderr, "Error: Memory allocation failed for BlockManager\n");
        fclose(file);
        return NULL;
    }

    manager->blocks = (Block *)malloc((size_t)num_blocks * sizeof(Block));
    if (!manager->blocks) {
        fprintf(stderr, "Error: Memory allocation failed for blocks\n");
        free(manager);
        fclose(file);
        return NULL;
    }

    manager->num_blocks = num_blocks;
    manager->block_size  = block_size;

    for (int i = 0; i < num_blocks; i++) {
        size_t bytes_to_read = block_size;
        long   remaining     = file_size - (long)i * (long)block_size;
        if (remaining < (long)bytes_to_read) {
            bytes_to_read = (size_t)(remaining > 0 ? remaining : 0);
        }

        manager->blocks[i].data = (unsigned char *)malloc(bytes_to_read);
        if (!manager->blocks[i].data) {
            fprintf(stderr, "Error: Memory allocation failed for block %d\n", i);
            for (int j = 0; j < i; j++) {
                free(manager->blocks[j].data);
            }
            free(manager->blocks);
            free(manager);
            fclose(file);
            return NULL;
        }

        size_t bytes_read = fread(manager->blocks[i].data, 1, bytes_to_read, file);
        manager->blocks[i].size = bytes_read;
        manager->blocks[i].original_size = bytes_read;
    }

    fclose(file);
    return manager;
}

int reassemble_blocks(BlockManager *manager, const char *output_filename) {
    if (!manager || !output_filename) {
        fprintf(stderr, "Error: NULL manager or filename in reassemble_blocks\n");
        return -1;
    }

    FILE *out = fopen(output_filename, "wb");
    if (!out) {
        fprintf(stderr, "Error: Cannot open output file '%s'\n", output_filename);
        return -1;
    }

    for (int i = 0; i < manager->num_blocks; i++) {
        if (!manager->blocks[i].data || manager->blocks[i].size == 0) {
            continue;
        }
        size_t written = fwrite(manager->blocks[i].data, 1,
                                manager->blocks[i].size, out);
        if (written != manager->blocks[i].size) {
            fprintf(stderr, "Error: Failed to write block %d\n", i);
            fclose(out);
            return -1;
        }
    }

    fclose(out);
    return 0;
}

void free_block_manager(BlockManager *manager) {
    if (!manager) return;

    if (manager->blocks) {
        for (int i = 0; i < manager->num_blocks; i++) {
            if (manager->blocks[i].data) {
                free(manager->blocks[i].data);
                manager->blocks[i].data = NULL;
            }
        }
        free(manager->blocks);
        manager->blocks = NULL;
    }
    free(manager);
}
