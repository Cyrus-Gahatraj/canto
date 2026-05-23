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
static Node* parse_block(Parser* parser);
static Node* parse_if_stmt(Parser* parser);
static Node* parse_string(Parser* parser);
static Node* parse_dot_prefix    (Parser* parser);
static Node* parse_dot_infix     (Parser* parser, Node* left);
static Node* parse_dot_dot_infix (Parser* parser, Node* left);
static Node* parse_edit          (Parser* parser, Node* target);

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
	[TK_PIPE]       = { NULL, NULL, PREC_NONE },

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
	[TK_KW_AND]     = { NULL, parse_binary, PREC_AND },
    [TK_KW_OR]      = { NULL, parse_binary, PREC_OR },

	// string
	[TK_STRING_LIT] = { parse_string, NULL, PREC_NONE },


	// Dot
	[TK_DOT]     = { parse_dot_prefix, parse_dot_infix, PREC_CALL },
	[TK_DOT_DOT] = { parse_dot_prefix, parse_dot_dot_infix, PREC_CALL },
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
		// skip only horizontal trivia — newlines terminate the expression
		while (!check(parser, TK_LEX_EOF) &&
		       (check(parser, TK_WHITESPACE)   ||
		        check(parser, TK_LINE_COMMENT) ||
		        check(parser, TK_BLOCK_COMMENT)))
			next(parser);

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
        // skip only horizontal trivia — newlines terminate the write statement
        while (!check(parser, TK_LEX_EOF) &&
               (check(parser, TK_WHITESPACE)   ||
                check(parser, TK_LINE_COMMENT) ||
                check(parser, TK_BLOCK_COMMENT)))
            next(parser);

        TokenKind kind = current(parser)->kind;

        if (kind == TK_LEX_EOF   ||
            kind == TK_SEMICOLON ||
            kind == TK_NEWLINE   ||
            kind == TK_RBRACE)
            break;

        if (kind == TK_COMMA) { next(parser); continue; }

        if (kind == TK_KW_WRITE  ||
            kind == TK_KW_LET    ||
            kind == TK_KW_IF     ||
            kind == TK_KW_LOOP   ||
            kind == TK_KW_RETURN)
            break;

        Node *arg = parse_expression(parser);
        if (arg)
            node->write.exprs[node->write.count++] = arg;
        else
            break;
    }

    return node;
}

static Node* parse_if_stmt(Parser* parser) {

	Node* cond = parse_expression(parser);
	if (!cond) return NULL;

	bool is_loop = false;
	skip_trivia(parser);

	if (check(parser, TK_PIPE)) {
		next(parser);
		skip_trivia(parser);
		expect_token(parser, TK_KW_LOOP, "Expected 'loop' after '|'.");
		is_loop = true;
	}

	expect_token(parser, TK_LBRACE, "Expected '{' after if condition");
	Node* then_block = parse_block(parser);

	Node* else_branch = NULL;
	skip_trivia(parser);

	if (match(parser, TK_KW_OR)) 
		else_branch = parse_if_stmt(parser);

	else if (match(parser, TK_KW_ELSE)) {
		skip_trivia(parser);
		bool else_is_loop = false;

		if (check(parser, TK_PIPE)) {
			next(parser);
			skip_trivia(parser);
			expect_token(parser, TK_KW_LOOP, "Expected 'loop' after '|'.");
			else_is_loop = true;
		}

		skip_trivia(parser);
		expect_token(parser, TK_LBRACE, "Expected '{' after else");
		else_branch = parse_block(parser);

		if (else_is_loop) {
			Node *loop = make_node(parser, NODE_LOOP,
								   (Span){0, parser->cursor});
			loop->loop.cond  = NULL;
			loop->loop.count = NULL;
			loop->loop.body  = else_branch;
			else_branch = loop;
		}
	}

	Node* if_node = make_node(parser, NODE_IF, (Span) {0, parser->cursor});
	if_node->if_.cond = cond;
	if_node->if_.then_ = then_block;
	if_node->if_.else_ = else_branch;
	if_node->if_.is_loop = is_loop;

	return if_node;
}

static Node* parse_edit(Parser* parser, Node* target) {
    Span start = current(parser)->span;
    next(parser);   // consume 'edit'
    skip_trivia(parser);
    expect_token(parser, TK_LBRACE, "expected '{' after edit");
    skip_trivia(parser);

    Node  **pairs = NULL;
    uint32_t count = 0, cap = 0;

    while (!check(parser, TK_RBRACE) && !check(parser, TK_LEX_EOF)) {
        skip_trivia(parser);
        if (check(parser, TK_RBRACE)) break;

        Span pair_span = current(parser)->span;
        Node *pair     = make_node(parser, NODE_EDIT_PAIR, pair_span);

        if (check(parser, TK_DOT_DOT)) {
            // ..field: value  — parent field
            next(parser);   // consume ..
            Token field = expect_token(parser, TK_IDENT,
                              "expected field name after '..'");
            expect_token(parser, TK_COLON, "expected ':' after field name");
            skip_trivia(parser);
            pair->edit_pair.field_sym = field.sym;
            pair->edit_pair.value     = parse_expression(parser);
            pair->edit_pair.is_parent = true;

        } else if (check(parser, TK_DOT)) {
            next(parser);   // consume . 
			skip_trivia(parser);

            if (check(parser, TK_PLUS)  || check(parser, TK_MINUS) ||
                check(parser, TK_STAR)  || check(parser, TK_SLASH)) {
                // . op expr  — relative operation on self 
                Token op = *current(parser);
                next(parser);
                skip_trivia(parser);
                Node *val = parse_expression(parser);

                Node *rel = make_node(parser, NODE_RELATIVE, op.span);
                rel->relative.op        = op.kind;
                rel->relative.expr      = val;
                rel->relative.is_parent = false;

                pair->edit_pair.field_sym = 0;
                pair->edit_pair.value     = rel;
                pair->edit_pair.is_parent = false;

            } else {
                // .field: value  — current design field
                Token field = expect_token(parser, TK_IDENT,
                                  "expected field name after '.'");
                expect_token(parser, TK_COLON, "expected ':' after field name");
                skip_trivia(parser);
                pair->edit_pair.field_sym = field.sym;
                pair->edit_pair.value     = parse_expression(parser);
                pair->edit_pair.is_parent = false;
            }

        } else {
            // plain value — absolute set on scalar variable 
            pair->edit_pair.field_sym = 0;
            pair->edit_pair.value     = parse_expression(parser);
            pair->edit_pair.is_parent = false;
        }

        if (count >= cap) {
            cap   = cap ? cap * 2 : 4;
            pairs = realloc(pairs, cap * sizeof(Node*));
        }
        pairs[count++] = pair;

        skip_trivia(parser);
        match(parser, TK_COMMA);
        skip_trivia(parser);
    }

    expect_token(parser, TK_RBRACE, "expected '}' to close edit block");

    Node* node = make_node(parser, NODE_EDIT, start);
    node->edit.target     = target;
    node->edit.pairs      = pairs;
    node->edit.pair_count = count;
    return node;
}

static Node* parse_dot_prefix(Parser* parser) {
    Token tk = *current(parser);
    next(parser);   // consume . 
    Node* node = make_node(parser, NODE_DOT, tk.span);
    node->dot.left      = NULL;   // NULL = bare dot
    node->dot.field_sym = 0;
    return node;
}

static Node* parse_dot_infix(Parser* parser, Node* left) {
    next(parser);   // consume .
    skip_trivia(parser);

    if (check(parser, TK_KW_EDIT)) {
        return parse_edit(parser, left);   // hand off to edit parser
    }

    // normal field access
    Token field = expect_token(parser, TK_IDENT,
                      "expected field name after '.'");
    Node* node = make_node(parser, NODE_DOT, field.span);
    node->dot.left      = left;
    node->dot.field_sym = field.sym;
    return node;
}

static Node* parse_dot_dot_infix(Parser* parser, Node* left) {
    Token tk = *current(parser);
    next(parser);   // consume ..
    skip_trivia(parser);
    Token field = expect_token(parser, TK_IDENT,
                      "expected field name after '..'");
    Node* node = make_node(parser, NODE_DOT_DOT, tk.span);
    node->dot.left      = left;
    node->dot.field_sym = field.sym;
    return node;
}

static Node* parse_continue(Parser* parser) {
	Span start = current(parser)->span;
	next(parser);
	return make_node(parser, NODE_CONTINUE, start);
}

static Node* parse_break(Parser* parser) {
	Span start = current(parser)->span;
	next(parser);
	return make_node(parser, NODE_BREAK, start);
}

static Node* parse_loop_stmt(Parser* parser) {
    Node* loop_node = make_node(parser, NODE_LOOP, (Span) {0, parser->cursor});
    loop_node->loop.cond       = NULL;
    loop_node->loop.count      = NULL;

    skip_trivia(parser);

    // "loop 10 { .... }"
    if (!check(parser, TK_LBRACE) && !check(parser, TK_LPAREN)) {
        loop_node->loop.count = parse_expression(parser);
        skip_trivia(parser);
    }

    expect_token(parser, TK_LBRACE, "Expected '{' to start loop body");
    loop_node->loop.body = parse_block(parser);

    return loop_node;
}

static Node* parse_stmt(Parser* parser) {
    skip_trivia(parser);
    switch (current(parser)->kind) {
        case TK_KW_LET: return parse_let_declaration(parser);
        case TK_KW_WRITE: return parse_write(parser);
		case TK_KW_IF: next(parser); return parse_if_stmt(parser);
	    case TK_KW_LOOP: next(parser); return parse_loop_stmt(parser);
		case TK_KW_CONTINUE: return parse_continue(parser);
		case TK_KW_BREAK: return parse_break(parser);
        case TK_LEX_EOF: return NULL;
        default:         return parse_expression(parser);
    }
}

static Node* parse_block(Parser* parser) {
	Node **stmts = NULL; 
	uint32_t count = 0, cap = 0;

	while(!check(parser, TK_RBRACE) && !check(parser, TK_LEX_EOF)) {
		skip_trivia(parser);
		if (check(parser, TK_RBRACE) || check(parser, TK_LEX_EOF)) break;

		Node *s = parse_stmt(parser);
		if (!s) break;

		if (count >= cap) {
			cap = cap ? cap * 2 : 8;
			stmts = realloc(stmts, cap * sizeof(Node*));
		}
		stmts[count++] = s;

		skip_trivia(parser);
        match(parser, TK_SEMICOLON);
	}
	expect_token(parser, TK_RBRACE, "Expected '}' to close block.");

	Node* block = make_node(parser, NODE_BLOCK,
							(Span){0, parser->cursor});
	block->block.stmts = stmts;
	block->block.count = count;
	return block;
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

