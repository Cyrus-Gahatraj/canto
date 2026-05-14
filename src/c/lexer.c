#include<string.h>
#include "canto/lexer.h"

void init_lexer(Lexer* lexer, const char* file_path, const char* buffer) {
    // Initialize SourceMap
    lexer->map.file_path = file_path;
    lexer->map.source_buffer = buffer;
    lexer->map.source_length = (uint32_t)strlen(buffer);
    lexer->map.line.count = 0;
    lexer->map.line.capacity = 0;
    lexer->map.line.offsets = NULL;

    // Initialize SymEntry
    lexer->sym_entry.count = 0;
    lexer->sym_entry.capacity = 0;
    lexer->sym_entry.syms = NULL;

    // Setup Token Stream
    lexer->tk_count = 0;
    lexer->tk_capacity = 0;
    lexer->tokens = NULL;

    // Setup Scanning Pointers
    lexer->start = buffer;
    lexer->current = buffer;
    lexer->line = 1;
}

void run_lex(Lexer* lexer){
	// TODO
}

