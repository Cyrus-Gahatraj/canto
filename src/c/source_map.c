#include "canto/source_map.h"
#include "canto/memory.h"
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

void source_map_add_line(SourceMap* map, uint32_t offset) {
    if (map->line.count >= map->line.capacity) {
        map->line.capacity = EXTEND_ARENA_CAPACITY(map->line.capacity);
        map->line.offsets = EXTEND_ARENA(uint32_t, map->line.offsets, map->line.capacity);
    }
    map->line.offsets[map->line.count++] = offset;
}

void free_source_map(SourceMap* map) {
	if (map->line.offsets != NULL) free(map->line.offsets);
	map->line.offsets = NULL;
}

