#pragma once

#include "common.h"
#include "memory.h"

// 64 kiB block
#define ARENA_BLOCK_SIZE (1024 * 64)

typedef struct ArenaBlock ArenaBlock;

struct ArenaBlock{
	uint8_t* data;
	uint8_t  used;
	uint8_t  capacity;
	ArenaBlock* next;	// link-list of blocks
};

typedef struct {
	ArenaBlock* current;
} Arena;

void arena_init(Arena *a);
void arena_free(Arena *a);
void* arena_alloc(Arena *a, uint32_t size);

#define ARENA_ALLOC(arena, T) ((T*)arena_alloc((arena), sizeof(T)))

