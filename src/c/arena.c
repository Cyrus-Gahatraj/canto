#include "canto/arena.h"
#include "canto/common.h"
#include "canto/memory.h"
#include <stdio.h>
#include <string.h>

static ArenaBlock* new_block(uint32_t capacity) {
	ArenaBlock* block = reallocate(NULL, sizeof(ArenaBlock));
	block->data = reallocate(NULL, capacity);
	block->used = 0;
	block->capacity = capacity;
	block->next = NULL;
	return block;
}

void arena_init(Arena *a) {
	a->current = new_block(ARENA_BLOCK_SIZE);
}

void arena_free(Arena *a) {
	ArenaBlock *block = a->current;

	while (block) {
		ArenaBlock* next = block->next;
		reallocate(block->data, 0); 
        reallocate(block, 0);
		block = next;
	}

	a->current = NULL;
}

void *arena_alloc(Arena *a, uint32_t size) {
    // align to 8 bytes
    size = (size + 7u) & ~7u;

    if (a->current->used + size > a->current->capacity) {
        uint32_t cap   = size > ARENA_BLOCK_SIZE ? size : ARENA_BLOCK_SIZE;
        ArenaBlock *next = new_block(cap);
        next->next = a->current;
        a->current = next;
    }

    void *ptr = a->current->data + a->current->used;
    a->current->used += size;
    memset(ptr, 0, size);
    return ptr;
}  

