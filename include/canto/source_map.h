#pragma once

#include "common.h"

typedef struct {
	uint32_t* offsets;
	uint32_t count;
	uint32_t capacity;
} LineMap;

typedef struct {
	const char* file_path;
	const char* source_buffer;
	uint32_t source_length;
	LineMap line;
} SourceMap;

void init_source_map(SourceMap* map, const char* file_path, const char* buffer);
void source_map_add_line(SourceMap* map, uint32_t offset);
void free_source_map(SourceMap* map);

