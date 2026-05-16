#include "canto/memory.h"
#include <stdlib.h>
#include <stdio.h>

void* reallocate(void* pointer, size_t new_size) {
	if (new_size == 0) {
		free(pointer);
		return NULL;
	}

	void* result = realloc(pointer, new_size);
	if (result == NULL) {
		fprintf(stderr, ANSI_COLOR_RED "Out of memory\n" ANSI_RESET);
		fprintf(stderr, ANSI_COLOR_RED "Failed to allocate %zu byte. The process has been terminated\n" ANSI_RESET, new_size);
		exit(1);
	}

	return result;
}

