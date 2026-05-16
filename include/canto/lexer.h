#pragma once

#include "diagnostic.h"
#include "source_map.h"
#include "common.h"
#include "token.h"
#include "symtable.h"

typedef struct {
	const SourceMap* map;
	SymTable symbols;
	Token* tokens;
	uint32_t tk_count;
	uint32_t tk_capacity;

	const char* start;
	const char* current;
	uint32_t line;
} Lexer;

void init_lexer(Lexer* lexer, const SourceMap* map);
void run_lex(Lexer* lexer, DiagEngine* engine);

