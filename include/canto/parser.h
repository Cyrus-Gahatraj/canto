#pragma once

#include "ast.h"
#include "source_map.h"
#include "diagnostic.h"
#include "arena.h"

typedef enum {
    PREC_NONE		= 0,
    PREC_OR			= 1,   // or 
    PREC_AND		= 2,   // and 
    PREC_EQ         = 3,   // = !=
    PREC_CMP        = 4,   // < > <= >=
    PREC_TERM       = 5,   // + -
    PREC_FACTOR     = 6,   // * / %
    PREC_UNARY      = 7,   // - not 
    PREC_CALL       = 8,   // ()
} Precedence;

typedef struct Parser Parser;

// Function
// name PrefixParseFn with argument Parser
// name InfixParseFn with argument Parser, left
typedef Node* (*PrefixParseFn)(Parser *parser);
typedef Node* (*InfixParseFn) (Parser *parser, Node *left);

typedef struct {
    PrefixParseFn prefix;    // NULL = this token can't start an expr
    InfixParseFn  infix;     // NULL = this token can't continue expr
    Precedence    prec;      // binding power when used as infix
} ParseRule;

struct Parser {
    SourceMap* map;
    Token*  tokens;
    uint32_t count;
    uint32_t cursor; 
	DiagEngine* diags;

	const ParseRule* rules;
	Arena arena;

    bool in_edit_block; 
    bool had_error;
};

void init_parser(Parser* parser, DiagEngine* diags, 
				Token* tokens, uint32_t count, SourceMap* map);
void free_parser(Parser* parser);

Node* parse_program(Parser* parser);
Node* parse_expression(Parser* parser);

