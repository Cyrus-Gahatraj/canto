#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "canto/lexer.h"
#include "canto/memory.h"
#include "canto/symtable.h"
#include "canto/token.h"
#include "internal/keyword_lookup.c"

typedef const struct keyword* keyword;

static void append_token(Lexer* lexer, Token token) {
	if (lexer->tk_capacity < lexer->tk_count + 1){
		lexer->tk_capacity = EXTEND_ARENA_CAPACITY(lexer->tk_capacity);
		lexer->tokens = EXTEND_ARENA(Token, lexer->tokens, lexer->tk_capacity);
	}
	
	lexer->tokens[lexer->tk_count++] = token;
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
	symtable_init(&lexer->symbols);

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

static bool tk_is_kw(TokenKind kind) {
	return (kind > TK_KW_BEGINNING && kind < TK_KW_ENDING);
}

static Token create_token(Lexer* lexer, TokenKind kind, TokenFlags flags) {
	Token tk;
	tk.kind = kind;
	tk.flags = flags;
	tk.span = get_span(lexer);
	tk.sym = 0;

	// Only push if identifiers, keyword or literal
	if (kind == TK_IDENT || tk_is_kw(kind) || kind == TK_STRING_LIT) {
		Symbol symbol = (Symbol) { .start = lexer->start, .length = get_token_length(lexer)};
		tk.sym = intern_symbol(&lexer->symbols, &symbol);
	}

	return tk;
}

static Token error_token(Lexer* lexer, const char* message) {
	Token tk;
	tk.kind = TK_LEX_ERROR;
	tk.flags = TOKEN_FLAG_NONE;
	tk.span = get_span(lexer);
	Symbol symbol = (Symbol){ .start = message, .length = strlen(message)};
	intern_symbol(&lexer->symbols, &symbol);
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
	lexer->start = lexer->current;
	advance(lexer);
	Token tk = create_token(lexer, TK_NEWLINE, TOKEN_FLAG_NONE);
	append_token(lexer, tk);
	lexer->line++;
}

static bool is_alpha(char c) {
	return (c >= 'a' && c <= 'z') ||
		   (c >= 'A' && c <= 'Z') ||
		   (c == '_');
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

static bool match(Lexer* lexer, char expected) {
    if (at_end(lexer)) return false;
    if (*lexer->current != expected) return false;
    lexer->current++; 
    return true;
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
				break;
			default: return;
		}
	}
}

static void identifier(Lexer* lexer) {
	while (is_alpha(peek(lexer)) || is_digit(peek(lexer))) {
		advance(lexer);
	}

	uint32_t length = get_token_length(lexer);
	keyword kw = lookup_keyword(lexer->start, length);

	TokenKind kind;
	if (kw != NULL) kind = (TokenKind) kw->token;
	else kind = TK_IDENT;

	append_token(lexer, create_token(lexer, kind, TOKEN_FLAG_NONE));
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
	TokenFlags flags = TOKEN_FLAG_NONE;

    while (!at_end(lexer)) {
        char c = peek(lexer);
    
		if (c == '{') flags |= TOKEN_FLAG_INTERPOLATE;
        if (c == '"') {
            advance(lexer);
			Token tk = create_token(lexer, TK_STRING_LIT, TOKEN_FLAG_NONE);
			Symbol symbol = { .start = lexer->start, .length = get_token_length(lexer)};
			tk.sym = intern_symbol(&lexer->symbols, &symbol);
            append_token(lexer, tk);
            return;
        }

        if (c == '\n') {
            lexer->line++;
        }
        advance(lexer);
    }

    append_token(lexer, error_token(lexer, "Unterminated string."));
}

void run_lex(Lexer* lexer) {
    while (!at_end(lexer)) {
        lexer->start = lexer->current;
        char c = peek(lexer);

        // Handle identifiers/keywords
        if (is_alpha(c)) {
            identifier(lexer);
            continue;
        }

        // Handle numbers
        if (is_digit(c)) {
			advance(lexer);
            number(lexer);
            continue;
        }

        // Handle Trivia (Whitespace/Comments)
        if (is_trivia(c)) {
			advance(lexer);
            trivia(lexer);
            continue;
        }

        switch (c) {
            // Single characters
            case '=': advance(lexer); append_token(lexer, create_token(lexer, TK_EQUAL, TOKEN_FLAG_NONE)); break;
            case '(': advance(lexer); append_token(lexer, create_token(lexer, TK_LPAREN, TOKEN_FLAG_NONE)); break;
            case ')': advance(lexer); append_token(lexer, create_token(lexer, TK_RPAREN, TOKEN_FLAG_NONE)); break;
            case '{': advance(lexer); append_token(lexer, create_token(lexer, TK_LBRACE, TOKEN_FLAG_NONE)); break;
            case '}': advance(lexer); append_token(lexer, create_token(lexer, TK_RBRACE, TOKEN_FLAG_NONE)); break;
            case '[': advance(lexer); append_token(lexer, create_token(lexer, TK_LBRACKET, TOKEN_FLAG_NONE)); break;
            case ']': advance(lexer); append_token(lexer, create_token(lexer, TK_RBRACKET, TOKEN_FLAG_NONE)); break;
            case ';': advance(lexer); append_token(lexer, create_token(lexer, TK_SEMICOLON, TOKEN_FLAG_NONE)); break;
            case ',': advance(lexer); append_token(lexer, create_token(lexer, TK_COMMA, TOKEN_FLAG_NONE)); break;
            case '.': advance(lexer); append_token(lexer, create_token(lexer, TK_DOT, TOKEN_FLAG_NONE)); break;
            case ':': advance(lexer); append_token(lexer, create_token(lexer, TK_COLON, TOKEN_FLAG_NONE)); break;
            case '+': advance(lexer); append_token(lexer, create_token(lexer, TK_PLUS, TOKEN_FLAG_NONE)); break;
            case '-': advance(lexer); append_token(lexer, create_token(lexer, TK_MINUS, TOKEN_FLAG_NONE)); break;
            case '*': advance(lexer); append_token(lexer, create_token(lexer, TK_STAR, TOKEN_FLAG_NONE)); break;
            case '/': advance(lexer); append_token(lexer, create_token(lexer, TK_SLASH, TOKEN_FLAG_NONE)); break;
            case '%': advance(lexer); append_token(lexer, create_token(lexer, TK_PERCENTAGE, TOKEN_FLAG_NONE)); break;
            case '"': string(lexer); break;

            // Two-character operators
            case '!':
                append_token(lexer, create_token(lexer, match(lexer, '=') ? TK_NOT_EQUAL : TK_BANG, TOKEN_FLAG_JOINT));
                break;
            case '<':
                append_token(lexer, create_token(lexer, match(lexer, '=') ? TK_LEQ : TK_LT, TOKEN_FLAG_JOINT));
                break;
            case '>':
                append_token(lexer, create_token(lexer, match(lexer, '=') ? TK_GEQ : TK_GT, TOKEN_FLAG_JOINT));
                break;

            // Logical And/Or
            case '&':
                if (match(lexer, '&')) {
                    append_token(lexer, create_token(lexer, TK_BOOL_AND, TOKEN_FLAG_JOINT));
                }
                break;
            case '|':
                if (match(lexer, '|')) {
                    append_token(lexer, create_token(lexer, TK_BOOL_OR, TOKEN_FLAG_JOINT));
                }
                break;

            default:
                break;
        }
    }
	
    lexer->start = lexer->current;
    append_token(lexer, create_token(lexer, TK_LEX_EOF, TOKEN_FLAG_NONE));
}
