#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "canto/parser.h"
#include "canto/arena.h"
#include "canto/ast.h"
#include "canto/diagnostic.h"
#include "canto/token.h"

#define INIT_NODE_CAPACITY 64

static Node* parse_ident(Parser *parser);
static Node* parse_number(Parser* parser);
static Node* parse_unary(Parser* parser);
static Node* parse_bool(Parser* parser);
static Node* parse_binary(Parser* parser, Node* left);
static Node* parse_grouping(Parser* parser);
static Node* parse_let_declaration(Parser* parser);
static Node* parse_string(Parser* parser);

static const ParseRule global_rules[TK_COUNT] = {
	// identifier
	[TK_IDENT]      = { parse_ident, NULL, PREC_NONE   },

	// Number
	[TK_INT_LIT]	= { parse_number, NULL, PREC_NONE },
	[TK_DOUBLE_LIT]	= { parse_number, NULL, PREC_NONE },

	// Operator
	[TK_PLUS]       = { NULL, parse_binary, PREC_TERM },
    [TK_MINUS]      = { parse_unary, parse_binary, PREC_TERM },
    [TK_STAR]       = { NULL, parse_binary, PREC_FACTOR },
    [TK_SLASH]      = { NULL, parse_binary, PREC_FACTOR },
    [TK_PERCENTAGE] = { NULL, parse_binary, PREC_FACTOR },

	// Grouping
	[TK_LPAREN]     = { parse_grouping, NULL, PREC_CALL },
	[TK_RPAREN]     = { NULL, NULL, PREC_NONE },	 // RPAREN don't need grouping

	// Comparision
	[TK_GT]         = { NULL, parse_binary, PREC_CMP },
    [TK_LT]         = { NULL, parse_binary, PREC_CMP },
    [TK_GEQ]        = { NULL, parse_binary, PREC_CMP },
    [TK_LEQ]        = { NULL, parse_binary, PREC_CMP },
	[TK_EQUAL]      = { NULL, parse_binary, PREC_EQ },
    [TK_NOT_EQUAL]  = { NULL, parse_binary, PREC_EQ },

	// Boolean
	[TK_KW_TRUE]    = { parse_bool, NULL, PREC_NONE },
	[TK_KW_FALSE]   = { parse_bool, NULL, PREC_NONE },
	[TK_BANG]       = { parse_unary, NULL, PREC_NONE },
	[TK_BOOL_AND]   = { NULL, parse_binary, PREC_AND },
    [TK_BOOL_OR]    = { NULL, parse_binary, PREC_OR },

	// string
	[TK_STRING_LIT] = { parse_string, NULL, PREC_NONE },
};

inline static Token* current(Parser* parser) {
	return &parser->tokens[parser->cursor];
}

inline static Token* peek_at(Parser* parser, uint32_t offset) {
	uint32_t index = parser->cursor + offset;
	if (index > parser->count) return &parser->tokens[parser->count - 1];
	return &parser->tokens[index];
}

inline static bool check(Parser* parser, TokenKind kind) {
	return current(parser)->kind == kind;
}

inline static Token next(Parser* parser) {
	Token tk = *current(parser);
	if (parser->cursor < parser->count) parser->cursor++;
	return tk;
}

inline static bool match(Parser* parser, TokenKind kind) {
	if (!check(parser, kind)) return false;
	next(parser);
	return true;
}

static void skip_trivia(Parser *p) {
    while (!check(p, TK_LEX_EOF) && 
           (check(p, TK_WHITESPACE)  ||
            check(p, TK_NEWLINE)     ||
            check(p, TK_LINE_COMMENT)||
            check(p, TK_BLOCK_COMMENT)))
        next(p);
}

static Token expect_token(Parser* parser, TokenKind kind, const char* message) {
	skip_trivia(parser);

	if (check(parser, kind)) return next(parser);
	append_diag(parser->diags, message, current(parser)->span, DIAG_PHASE_PARSE, DIAG_ERROR);
	parser->had_error = true;

	return *current(parser);
}

static Node* make_node(Parser* parser, NodeKind kind, Span span) {
	Node* node = ARENA_ALLOC(&parser->arena, Node);
	node->kind = kind;
	node->span = span;
	return node;
}

static Node* parse_expr(Parser* parser, Precedence prec) {
	skip_trivia(parser);
	
	Token tk = *current(parser);
	TokenKind prefix_kind = tk.kind;
	ParseRule rule = parser->rules[prefix_kind];
	
	// if don't have the prefix rule
	if (!rule.prefix) {
		append_diag(parser->diags, "expected expression.", tk.span, DIAG_PHASE_PARSE, DIAG_ERROR);
		parser->had_error = true;
		next(parser);
		return NULL;
	}

	Node* left = rule.prefix(parser);
	for(;;) {
		skip_trivia(parser);

		TokenKind infix_kind = current(parser)->kind;
		ParseRule infix_rule = parser->rules[infix_kind];

		// break if there is no infix rule or precedance is low
		if (!infix_rule.infix || infix_rule.prec <= prec) break;

		left = infix_rule.infix(parser, left);
	}

	return left;
}

static Node* parse_number(Parser* parser) {
	Token tk = next(parser);	
	NodeKind node_kind = tk.kind == TK_INT_LIT? NODE_INT_LIT : NODE_DOUBLE_LIT;
	Node* node = make_node(parser, node_kind, tk.span);

	if (tk.kind == TK_INT_LIT) node->int_lit.value = tk.as.i64;
	else node->double_lit.value = tk.as.f64;

	return node;
}

static Node* parse_string(Parser* parser) {
	Token tk = next(parser);
	Node* node = make_node(parser, NODE_STRING_LIT, tk.span);

	node->string_lit.sym = tk.sym;
	node->string_lit.interpolated = !!(tk.flags & TOKEN_FLAG_INTERPOLATE);
	return node;
}

static Node* parse_bool(Parser* parser) {
	Token tk = next(parser);
	Node* node = make_node(parser, NODE_BOOL_LIT, tk.span);

	node->bool_lit.value = (tk.kind == TK_KW_TRUE);
	return node;
}

static Node* parse_ident(Parser* parser) {
	Token tk = next(parser);
	Node* node = make_node(parser, NODE_IDENT, tk.span);

	node->ident.sym = tk.sym;
	return node;
}

static Node* parse_unary(Parser* parser) {
	Token op = next(parser);
	Node* expr = parse_expr(parser, PREC_UNARY);
	Node* node = make_node(parser, NODE_UNARY, op.span);

	node->unary.op = op.kind;
	node->unary.expr = expr;
	return node;
}

static Node* parse_grouping(Parser* parser) {
	Span start = current(parser)->span;
	next(parser);
	skip_trivia(parser);

	Node* inner = parse_expr(parser, PREC_NONE);
	expect_token(parser, TK_RPAREN, "expected ')' to close the expression.");

	Node* node = make_node(parser, NODE_GROUP, start);
	node->group.expr = inner;

	return node;
}

static Node* parse_binary(Parser* parser, Node* left) {
	Token op = next(parser);	// consume operator
	Precedence prec = parser->rules[op.kind].prec;
	Node* right = parse_expr(parser, prec);

	Node* node = make_node(parser, NODE_BINARY, op.span);
	node->binary.left = left;
	node->binary.op = op.kind;
	node->binary.right = right;
	return node;
}

static Node* parse_let_declaration(Parser* parser) {
	Span start = current(parser)->span;
	next(parser);
	skip_trivia(parser);

	Token ident_tk = expect_token(parser, TK_IDENT, "expected variable name after 'let'.");
	Node* type_ann = NULL;

	if (match(parser, TK_COLON)) {
		Token type_tk = expect_token(parser, TK_IDENT, "expected type name after ':'.");
		type_ann = make_node(parser, NODE_IDENT, type_tk.span);
		type_ann->ident.sym = type_tk.sym;
	}
	skip_trivia(parser);
	Node* value = parse_expression(parser);
	
	Node* node = make_node(parser, NODE_LET, start);
	node->let.is_fn = false;
	node->let.name_sym = ident_tk.sym;
	node->let.type_ann = type_ann;
	node->let.value = value;
	
	return node;
}


static Node* parse_write(Parser* parser) {
    Span start = current(parser)->span;
    next(parser);

    Node *node = make_node(parser, NODE_WRITE, start);
    node->write.exprs = malloc(sizeof(Node*) * 64);
    node->write.count = 0;

	while (true) {
        skip_trivia(parser);
        TokenKind kind = current(parser)->kind;

        if (kind == TK_LEX_EOF || kind == TK_SEMICOLON || kind == TK_NEWLINE || kind == TK_RBRACE)
            break;

        if (kind == TK_COMMA) {
			next(parser); 
			continue; 
		}

        Node *arg = parse_expression(parser);
        if (arg)
            node->write.exprs[node->write.count++] = arg;
    }

    return node;
}

static Node* parse_stmt(Parser* parser) {
    skip_trivia(parser);
    switch (current(parser)->kind) {
        case TK_KW_LET: return parse_let_declaration(parser);
        case TK_KW_WRITE: return parse_write(parser);
        case TK_LEX_EOF: return NULL;
        default:         return parse_expression(parser);
    }
}

Node* parse_program(Parser* parser) {
    Node  **stmts = NULL;
    uint32_t count = 0, cap = 0;

    while (!check(parser, TK_LEX_EOF)) {
        skip_trivia(parser);
        if (check(parser, TK_LEX_EOF)) break;

        Node *s = parse_stmt(parser);
        if (!s) break;

        if (count >= cap) {
            cap   = cap ? cap * 2 : 8;
            stmts = realloc(stmts, cap * sizeof(Node*));
        }
        stmts[count++] = s;

        // consume statement terminator
        skip_trivia(parser);
        match(parser, TK_SEMICOLON);
    }

    Node *root = make_node(parser, NODE_PROGRAM,
                           (Span){0, parser->map->source_length});
    root->block.stmts = stmts;
    root->block.count = count;
    return root;
}

Node* parse_expression(Parser* parser) {
    return parse_expr(parser, PREC_NONE);
}

void init_parser(Parser *parser, DiagEngine* diags, 
				Token *tokens, uint32_t count, SourceMap* map) {
	parser->map = map;
	parser->tokens = tokens;
	parser->count = count;
	parser->cursor = 0;
	parser->rules = global_rules;
	parser->diags = diags;
	parser->in_edit_block = false;
	parser->had_error = false;
	arena_init(&parser->arena);
}

void free_parser(Parser* parser) {
	arena_free(&parser->arena);
}

