#include<string.h>
#include "canto/lexer.h"

static void initLineMap(LineMap* map) {
	map->count = 0;
	map->capacity = 1;
	map->offsets = NULL;
}

void initSourceMap(SourceMap* map, const char* file_path, const char* buffer) {
	map->file_path = file_path;
	map->source_buffer = buffer;
	map->source_length = strlen(map->source_buffer);
	initLineMap(&map->line);
}

