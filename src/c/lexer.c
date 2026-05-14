#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "canto/lexer.h"
#include "canto/memory.h"
#include "canto/token.h"

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

inline static uint32_t get_token_offset(Lexer* lexer) {
	return (uint32_t)(lexer->start - lexer->map.source_buffer);
}

inline static uint32_t get_token_length(Lexer* lexer) {
	return (uint32_t)(lexer->current - lexer->start);
}

static Span get_span(Lexer* lexer) {
	uint32_t offset = get_token_offset(lexer);
	uint32_t length = get_token_length(lexer);
	Span span = (Span) { .start = offset, .length = length };
	return span;
}

static Token create_token(Lexer* lexer, TokenKind kind, TokenFlags flags) {
	Token tk;
	tk.kind = kind;
	tk.flags = flags;
	tk.span = get_span(lexer);
	uint32_t length = get_token_length(lexer);
	Symbol symbol = { .start = lexer->start, .length = length };
	tk.sym = append_symbol(lexer, symbol);

	return tk;
}

static Token error_token(Lexer* lexer, const char* message) {
	Token tk;
	tk.kind = TK_LEX_ERROR;
	tk.flags = TOKEN_FLAG_NONE;
	tk.span = get_span(lexer);
	Symbol sym = (Symbol){ .start = message, .length = strlen(message)};
	append_symbol(lexer, sym);
	return tk;
}

static bool at_end(Lexer* lexer) {
	return *lexer->current == '\0';
}

static char advance(Lexer* lexer) {
	lexer->current++;
	return lexer->current[-1];
}

static void advance_newline(Lexer* lexer) {
	Token tk;
	tk = create_token(lexer, TK_NEWLINE, TOKEN_FLAG_NONE);
	append_token(lexer, tk);
	advance(lexer);
	lexer->line++;
}

static bool is_digit(char c) {
	return (c >= '0' && c <= '9');
}

static bool is_trivia(char c) {
	return (c == ' ' || c == '\n' || c == '\t' || c == '~');
}

static char peek(Lexer* lexer) {
	return *lexer->current;
}

static char peek_next(Lexer* lexer) {
	if (at_end(lexer)) return '\0';
	return lexer->current[1];
}

static void trivia(Lexer* lexer) {
	for(;;){
		char c = peek(lexer);
		Token tk;
		switch (c) {
			case ' ':
			case '\r':
			case '\t':
				tk = create_token(lexer, TK_WHITESPACE, TOKEN_FLAG_NONE);
				append_token(lexer, tk);
				advance(lexer);
				break;
			case '\n': advance_newline(lexer); break;
			case '~':
				if (peek_next(lexer) == '~') {
					//multiline comment
					advance(lexer); advance(lexer);

					while (!at_end(lexer)) {
						if (peek(lexer) == '~' && peek_next(lexer) == '~'){
							advance(lexer); advance(lexer);
							break;
						}
						if (peek(lexer) == '\n') advance_newline(lexer);
					}
				}
				else {
					while(!at_end(lexer) && peek(lexer) != '\n') {
						advance(lexer);	
					}
					if (peek(lexer) == '\n') advance_newline(lexer);
				}
			default: return;
		}
	}
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
	if (is_double) {
		tk = create_token(lexer, TK_DOUBLE_LIT, TOKEN_FLAG_NONE);
		tk.as.f64 =  strtod(lexer->start, NULL);
	} else{
		tk = create_token(lexer, TK_INT_LIT, TOKEN_FLAG_NONE);
		tk.as.i64 = strtoll(lexer->start, NULL, 10);
	}

	append_token(lexer, tk);
}

static void string(Lexer* lexer) {
	advance(lexer);
    while (!at_end(lexer)) {
        char c = peek(lexer);
        
        if (c == '"') {
            advance(lexer);
            append_token(lexer, create_token(lexer, TK_STRING_LIT, TOKEN_FLAG_NONE));
            return;
        }

        if (c == '\n') {
            lexer->line++;
        }
        advance(lexer);
    }

    append_token(lexer, error_token(lexer, "Unterminated string."));
}

void run_lex(Lexer* lexer){
	
	while (!at_end(lexer)) {

		lexer->start = lexer->current;
		char c = peek(lexer);
		if (is_trivia(c)) trivia(lexer); 
		if (is_digit(c)) number(lexer); 

		switch (c) {
			case '"': string(lexer);
		}
	}

	Token eof = create_token(lexer, TK_LEX_EOF, TOKEN_FLAG_NONE); 
	append_token(lexer, eof);
}

