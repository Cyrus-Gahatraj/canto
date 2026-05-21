#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "canto/lexer.h"
#include "canto/memory.h"
#include "internal/keyword_lookup.c"

typedef const struct keyword* keyword;

static void append_token(Lexer* lexer, Token token) {
	if (lexer->tk_capacity < lexer->tk_count + 1){
		lexer->tk_capacity = EXTEND_ARENA_CAPACITY(lexer->tk_capacity);
		lexer->tokens = EXTEND_ARENA(Token, lexer->tokens, lexer->tk_capacity);
	}
	
	lexer->tokens[lexer->tk_count++] = token;
}

void init_lexer(Lexer* lexer, SourceMap* map) {
    // Initialize SourceMap
	lexer->map = map;
	source_map_add_line(lexer->map, 0);
	
    // Initialize SymEntry
	symtable_init(&lexer->symbols);

    // Setup Token Stream
    lexer->tk_count = 0;
    lexer->tk_capacity = 0;
    lexer->tokens = NULL;

    // Setup Scanning Pointers
    lexer->start = map->source_buffer;
    lexer->current = map->source_buffer;
    lexer->line = 1;
}

static bool tk_is_kw(TokenKind kind) {
	return (kind > TK_KW_BEGINNING && kind < TK_KW_ENDING);
}

static Token create_token(Lexer* lexer, TokenKind kind, TokenFlags flags) {
	Token tk;
	tk.kind = kind;
	tk.flags = flags;
	tk.sym = 0;
	tk.span = get_span(lexer->map->source_buffer, lexer->start, lexer->current);

	// Only push if identifiers, keyword or literal
	if (kind == TK_IDENT || tk_is_kw(kind) || kind == TK_STRING_LIT) {
		uint32_t length = get_token_length(lexer->start, lexer->current);
		Symbol symbol = (Symbol) { .start = lexer->start, .length = length};
		tk.sym = intern_symbol(&lexer->symbols, &symbol);
	}

	return tk;
}

static Token error_token(Lexer* lexer, DiagEngine* diags, const char* message) {
	Token tk;
	tk.kind = TK_LEX_ERROR;
	tk.flags = TOKEN_FLAG_NONE;
	tk.span = get_span(lexer->map->source_buffer, lexer->start, lexer->current);

	append_diag(diags, message, tk.span, DIAG_PHASE_LEX, DIAG_ERROR);

	Symbol symbol = (Symbol){ .start = message, .length = strlen(message), .kind = SYM_VARIABLE};
	tk.sym = intern_symbol(&lexer->symbols, &symbol);
	return tk;
}

static bool at_end(Lexer* lexer) {
	return *lexer->current == '\0';
}

static char next(Lexer* lexer) {
	lexer->current++;
	return lexer->current[-1];
}

static void add_newline_offset(Lexer* lexer) {
	uint32_t next_line_offset = (uint32_t) (lexer->current - lexer->map->source_buffer);
	source_map_add_line(lexer->map, next_line_offset);
}

static void advance_newline(Lexer* lexer) {
	lexer->start = lexer->current;
	next(lexer);
	add_newline_offset(lexer);
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
	return (c == ' ' || c == '\n' || c == '\t');
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
				next(lexer);
				break;
			case '\n': advance_newline(lexer); break;
			case '~':
				is_trivia(c);
				if (peek_next(lexer) == '~') {
					//multiline comment
					next(lexer); next(lexer);

					while (!at_end(lexer)) {
						if (peek(lexer) == '~' && peek_next(lexer) == '~'){
							next(lexer); next(lexer);
							break;
						}
						if (peek(lexer) == '\n') advance_newline(lexer);
						else next(lexer);
					}
				}
				else {
					next(lexer);
					while(!at_end(lexer) && peek(lexer) != '\n') {
						next(lexer);	
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
		next(lexer);
	}

	uint32_t length = get_token_length(lexer->start, lexer->current);
	keyword kw = lookup_keyword(lexer->start, length);

	TokenKind kind;
	if (kw != NULL) kind = (TokenKind) kw->token;
	else kind = TK_IDENT;

	append_token(lexer, create_token(lexer, kind, TOKEN_FLAG_NONE));
}

static void number(Lexer* lexer) {
	while (is_digit(peek(lexer))) next(lexer);

	bool is_double = false;
	if (peek(lexer) == '.' && is_digit(peek_next(lexer))) {
		is_double = true;
		next(lexer);	
		while (is_digit(peek(lexer))) next(lexer);
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

static void string(Lexer* lexer, DiagEngine* diags) {
    next(lexer);
    lexer->start = lexer->current;

    while (!at_end(lexer)) {
        char c = peek(lexer);

        if (c == '`') {

            if (lexer->current > lexer->start) {
                uint32_t len = (uint32_t)(lexer->current - lexer->start);
                Symbol sym   = { .start = lexer->start, .length = len };
                Token  str   = create_token(lexer, TK_STRING_LIT, TOKEN_FLAG_NONE);
                str.sym      = intern_symbol(&lexer->symbols, &sym);
                append_token(lexer, str);
            }

            next(lexer);
            lexer->start = lexer->current;

            // scan until closing ` or end of string
            while (!at_end(lexer) && peek(lexer) != '`' && peek(lexer) != '"') {
                if (peek(lexer) == '\n') {
                    lexer->line++;
                    add_newline_offset(lexer);
                }
                next(lexer);
            }

            if (peek(lexer) != '`') {
                append_token(lexer,
                    error_token(lexer, diags,
                        "expected closing '`' for interpolation."));
                return;
            }

            // classify the content between the backticks
			// ident, integer, double or bool
            uint32_t    len  = (uint32_t)(lexer->current - lexer->start);
            const char *text = lexer->start;

            if (len == 0) {
                append_token(lexer,
                    error_token(lexer, diags, "empty interpolation '``'."));
                next(lexer);
                lexer->start = lexer->current;
                continue;
            }

            bool is_int   = true;
            bool is_float = false;
            for (uint32_t i = 0; i < len; i++) {
                char ch = text[i];
                if (ch == '.' && !is_float && i > 0) {
                    is_float = true;
                    continue;
                }
                if (ch < '0' || ch > '9') { is_int = false; break; }
            }
            if (is_float && !is_int) is_int = false;

            Token interp_tk;
            if (is_int && !is_float) {
                interp_tk          = create_token(lexer, TK_INT_LIT, TOKEN_FLAG_NONE);
                interp_tk.as.i64   = strtoll(text, NULL, 10);
            } else if (is_int && is_float) {
                interp_tk          = create_token(lexer, TK_DOUBLE_LIT, TOKEN_FLAG_NONE);
                interp_tk.as.f64   = strtod(text, NULL);
            } else {
                Symbol sym         = { .start = text, .length = len };
                interp_tk          = create_token(lexer, TK_IDENT, TOKEN_FLAG_NONE);
                interp_tk.sym      = intern_symbol(&lexer->symbols, &sym);
            }
            append_token(lexer, interp_tk);

            next(lexer);
            lexer->start = lexer->current;
            continue;
        }

        if (c == '"') {
            if (lexer->current > lexer->start) {
                uint32_t len = (uint32_t)(lexer->current - lexer->start);
                Symbol sym   = { .start = lexer->start, .length = len };
                Token  str   = create_token(lexer, TK_STRING_LIT, TOKEN_FLAG_NONE);
                str.sym      = intern_symbol(&lexer->symbols, &str.sym == 0
                                             ? &(Symbol){.start=lexer->start,.length=len}
                                             : &sym);
                str.sym      = intern_symbol(&lexer->symbols, &sym);
                append_token(lexer, str);
            }
            next(lexer);
            return;
        }

        if (c == '\n') {
            lexer->line++;
            add_newline_offset(lexer);
        }
        next(lexer);
    }

    append_token(lexer, error_token(lexer, diags, "Unterminated string literal."));
}

void run_lex(Lexer *lexer, DiagEngine *diags) {
    while (!at_end(lexer)) {
        lexer->start = lexer->current;
        char c = peek(lexer);

        if (is_alpha(c)) { identifier(lexer); continue; }
        if (is_digit(c)) { next(lexer); number(lexer); continue; }
        if (is_trivia(c)) { trivia(lexer); continue; }

        switch (c) {
			case '~': trivia(lexer); continue;
            case '=': next(lexer); append_token(lexer, create_token(lexer, TK_EQUAL,     TOKEN_FLAG_NONE)); break;
            case '(': next(lexer); append_token(lexer, create_token(lexer, TK_LPAREN,    TOKEN_FLAG_NONE)); break;
            case ')': next(lexer); append_token(lexer, create_token(lexer, TK_RPAREN,    TOKEN_FLAG_NONE)); break;
            case '{': next(lexer); append_token(lexer, create_token(lexer, TK_LBRACE,    TOKEN_FLAG_NONE)); break;
            case '}': next(lexer); append_token(lexer, create_token(lexer, TK_RBRACE,    TOKEN_FLAG_NONE)); break;
            case '[': next(lexer); append_token(lexer, create_token(lexer, TK_LBRACKET,  TOKEN_FLAG_NONE)); break;
            case ']': next(lexer); append_token(lexer, create_token(lexer, TK_RBRACKET,  TOKEN_FLAG_NONE)); break;
            case ';': next(lexer); append_token(lexer, create_token(lexer, TK_SEMICOLON, TOKEN_FLAG_NONE)); break;
            case ',': next(lexer); append_token(lexer, create_token(lexer, TK_COMMA,     TOKEN_FLAG_NONE)); break;
            case ':': next(lexer); append_token(lexer, create_token(lexer, TK_COLON,     TOKEN_FLAG_NONE)); break;
            case '+': next(lexer); append_token(lexer, create_token(lexer, TK_PLUS,      TOKEN_FLAG_NONE)); break;
            case '-': next(lexer); append_token(lexer, create_token(lexer, TK_MINUS,     TOKEN_FLAG_NONE)); break;
            case '*': next(lexer); append_token(lexer, create_token(lexer, TK_STAR,      TOKEN_FLAG_NONE)); break;
            case '/': next(lexer); append_token(lexer, create_token(lexer, TK_SLASH,     TOKEN_FLAG_NONE)); break;
            case '%': next(lexer); append_token(lexer, create_token(lexer, TK_PERCENTAGE,TOKEN_FLAG_NONE)); break;
            case '"': string(lexer, diags); break;

            // two-character operators
            case '!':
                next(lexer);
                append_token(lexer, create_token(lexer,
                    match(lexer, '=') ? TK_NOT_EQUAL : TK_BANG,
                    TOKEN_FLAG_NONE));
                break;

            case '<':
                next(lexer);
                append_token(lexer, create_token(lexer,
                    match(lexer, '=') ? TK_LEQ : TK_LT,
                    TOKEN_FLAG_NONE));
                break;

            case '>':
                next(lexer);
                append_token(lexer, create_token(lexer,
                    match(lexer, '=') ? TK_GEQ : TK_GT,
                    TOKEN_FLAG_NONE));
                break;

            case '&':
                next(lexer);
                if (match(lexer, '&'))
                    append_token(lexer, create_token(lexer, TK_BOOL_AND, TOKEN_FLAG_NONE));
                else {
                    Token err = error_token(lexer, diags, "unexpected '&', did you mean '&&'?");
                    append_token(lexer, err);
                }
                break;

            case '|':
                next(lexer);
                if (match(lexer, '|'))
                    append_token(lexer, create_token(lexer, TK_BOOL_OR, TOKEN_FLAG_NONE));
                else {
                    Token err = error_token(lexer, diags, "unexpected '|', did you mean '||'?");
                    append_token(lexer, err);
                }
                break;

            case '.':
                next(lexer);
                if (match(lexer, '.'))
                    append_token(lexer, create_token(lexer, TK_DOT_DOT, TOKEN_FLAG_NONE));
                else
                    append_token(lexer, create_token(lexer, TK_DOT, TOKEN_FLAG_NONE));
                break;

            default:
                next(lexer);
                append_token(lexer, error_token(lexer, diags, "unexpected character."));
                break;
        }
    }

	for (uint32_t i = 0; i < lexer->tk_count; i++) {
    fprintf(stderr, "  [%u] kind=%d span=[%u,%u]\n",
            i, lexer->tokens[i].kind,
            lexer->tokens[i].span.start,
            lexer->tokens[i].span.length);
}

    lexer->start = lexer->current;
    append_token(lexer, create_token(lexer, TK_LEX_EOF, TOKEN_FLAG_NONE));
}
