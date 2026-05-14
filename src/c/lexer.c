#include <stdlib.h>
#include <string.h>
#include "canto/lexer.h"
#include "canto/memory.h"

static void append_token(Lexer* lexer, Token token) {
	if (lexer->tk_capacity < lexer->tk_count + 1){
		lexer->tk_capacity = EXTEND_ARENA_CAPACITY(lexer->tk_capacity);
		lexer->tokens = EXTEND_ARENA(Token, lexer->tokens, lexer->tk_capacity);
	}
	
	lexer->tokens[lexer->tk_count++] = token;
}


static uint32_t append_symbol(Lexer* lexer, Symbol symbol) {
	if (lexer->symbols.capacity < lexer->symbols.count + 1){
		lexer->symbols.capacity = EXTEND_ARENA_CAPACITY(lexer->symbols.capacity);
		lexer->symbols.syms = EXTEND_ARENA(Symbol, lexer->symbols.syms, lexer->symbols.capacity);
	}
	
	uint32_t index = lexer->symbols.count;
	lexer->symbols.syms[index] = symbol;
	lexer->symbols.count++;
	return index;
}

void init_lexer(Lexer* lexer, const char* buffer, const char* file_path) {
    // Initialize SourceMap
    lexer->map.file_path = file_path ? file_path : "<repl>";
    lexer->map.source_buffer = buffer;
    lexer->map.source_length = (uint32_t)strlen(buffer);
    lexer->map.line.count = 0;
    lexer->map.line.capacity = 0;
    lexer->map.line.offsets = NULL;

    // Initialize SymEntry
    lexer->symbols.count = 0;
    lexer->symbols.capacity = 0;
    lexer->symbols.syms = NULL;

    // Setup Token Stream
    lexer->tk_count = 0;
    lexer->tk_capacity = 0;
    lexer->tokens = NULL;

    // Setup Scanning Pointers
    lexer->start = buffer;
    lexer->current = buffer;
    lexer->line = 1;
}

static bool at_end(Lexer* lexer) {
	return *lexer->current == '\0';
}

static char advance(Lexer* lexer) {
	lexer->current++;
	return lexer->current[-1];
}

static bool is_digit(char c) {
	return (c >= '0' && c <= '9');
}

static char peek(Lexer* lexer) {
	return *lexer->current;
}

static char peek_next(Lexer* lexer) {
	if (at_end(lexer)) return '\0';
	return lexer->current[1];
}

static void number(Lexer* lexer) {
	while (is_digit(peek(lexer))) advance(lexer);

	bool is_double = false;
	if (peek(lexer) == '.' && is_digit(peek_next(lexer))) {
		is_double = true;
		advance(lexer);	
		while (is_digit(peek(lexer))) advance(lexer);
	}

	Token tk;

	uint32_t offset = (uint32_t)(lexer->start - lexer->map.source_buffer);
	uint32_t length = (uint32_t) (lexer->current - lexer->start);
	Symbol symbol = { .start = lexer->start, .length = length };

	if (is_double) {
		tk = (Token) {
			.flags = TOKEN_FLAG_NONE,
			.sym = append_symbol(lexer, symbol),
			.kind = TK_DOUBLE_LIT,
			.span = (Span) { .start = offset, .length = length },
			.as = {strtod(lexer->start, NULL)}, 
		};	
	} else{
		tk = (Token) {
			.flags = TOKEN_FLAG_NONE,
			.sym = append_symbol(lexer, symbol),
			.kind = TK_INT_LIT,
			.span = (Span) { .start = offset, .length = length },
			.as = {strtoll(lexer->start, NULL, 10)},
		};
	}

	append_token(lexer, tk);
}

void run_lex(Lexer* lexer){
	
	while (!at_end(lexer)) {

		lexer->start = lexer->current;

		char c = advance(lexer);
		if (is_digit(c)) number(lexer); 
	}
	
}

