#include "common.h"
#include "token.h"

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

typedef struct {
    const char* start;
    uint32_t length;
} Symbol;

typedef struct {
    Symbol* syms;
    uint32_t count;
    uint32_t capacity;
} SymTable;

typedef struct {
	SourceMap map;
	SymTable symbols;
	Token* tokens;
	uint32_t tk_count;
	uint32_t tk_capacity;

	const char* start;
	const char* current;
	uint32_t line;
} Lexer;

void init_lexer(Lexer* lexer, const char* file_path, const char* buffer);
void run_lex(Lexer* lexer);

