#include "canto/source_map.h"
#include<stdlib.h>
#include<string.h>

void init_source_map(SourceMap* map, const char* file_path, const char* buffer) {
    map->file_path = file_path ? file_path : "<repl>";
    map->source_buffer = buffer;
    map->source_length = (uint32_t) strlen(buffer);
    map->line.count = 0;
    map->line.capacity = 0;
    map->line.offsets = NULL;
}

void free_source_map(SourceMap* map) {
	if (map->line.offsets != NULL) free(map->line.offsets);
	map->line.offsets = NULL;
}

