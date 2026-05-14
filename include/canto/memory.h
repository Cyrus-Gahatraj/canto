#pragma once

#include "common.h"

#define EXTEND_ARENA_CAPACITY(capacity) ] \
	(((capacity) < 8) ? 8 : (capacity) * 2)

#define EXTEND_ARENA(type, pointer, new_size) \
	(type*) reallocate(pointer, sizeof(type) * new_size)

#define FREE_ARENA(type, pointer) \
	(type*) reallocate(pointer, 0)

void* reallocate(void* pointer, size_t new_size);

