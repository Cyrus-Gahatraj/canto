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

void initSourceMap(SourceMap* map, const char* file_path, const char* buffer);

