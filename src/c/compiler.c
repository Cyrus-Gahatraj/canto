#include<stdio.h>
#include<stdlib.h>

#include "canto/compiler.h"
#include "canto/ast.h"
#include "canto/diagnostic.h"
#include "canto/lexer.h"
#include "canto/parser.h"
#include "canto/source_map.h"
#include "canto/codegen.h"

static void clean_up(Lexer* lexer, DiagEngine* diags) {
	symtable_free(&lexer->symbols);
    free(lexer->tokens);
    diag_free(diags);
}

static void print_lex_info(Lexer* lexer) {
	for (uint32_t i = 0; i < lexer->tk_count; ++i) {
		Token* tk = &lexer->tokens[i];
		printf("Token %d: Kind %d, Span [%d, %d]\n", 
                i, tk->kind, tk->span.start, tk->span.length);
	}
}

void print_ast(Node* node, int indent) {
    if (!node) {
        printf("%*s(NULL)\n", indent * 2, "");
        return;
    }

    printf("%*s", indent * 2, "");

    switch (node->kind) {
		case NODE_IDENT:
			printf("Ident: sym=%u\n", node->ident.sym);
			break;

        case NODE_INT_LIT:
            printf("IntLit: %lld\n", node->int_lit.value);
            break;

        case NODE_DOUBLE_LIT:
            printf("FloatLit: %f\n", node->double_lit.value);
            break;

        case NODE_BINARY:
            printf("BinaryOp (%d):\n", node->binary.op);
            print_ast(node->binary.left, indent + 1);
            print_ast(node->binary.right, indent + 1);
            break;

		case NODE_UNARY:
			printf("Unary (%d):\n", node->unary.op);
			print_ast(node->unary.expr, indent + 1);
			break;

        case NODE_GROUP:
            printf("GroupedExpression ():\n");
            print_ast(node->group.expr, indent + 1);
            break;

		case NODE_BOOL_LIT:
			printf("BoolLit: %s\n", node->bool_lit.value ? "true" : "false");
			break;

        default:
            printf("Unknown Node Kind: %d\n", node->kind);
            break;
    }
}

void compile(const char* source, const char* file_path){
	Lexer lexer;
	DiagEngine diags;
	SourceMap map;
	Parser parser;
	
	diag_init(&diags);
	init_source_map(&map, file_path, source);
	init_lexer(&lexer, &map);
	run_lex(&lexer, &diags);	

	if (diag_has_errors(&diags)) {
		diag_render_report(&diags, &map);
		clean_up(&lexer, &diags);
		return;
	}

	init_parser(&parser, &diags, lexer.tokens, lexer.tk_count, &map);
	Node* tree = parse_expression(&parser);
    print_ast(tree, 2);

    codegen_init();

    if (!diag_has_errors(&diags) && tree != NULL) {
        int llvm_val = codegen_eval_expr(tree);
        
        if (llvm_val == 0) {
            printf("Codegen Success: Emitted Value %d\n", llvm_val);
            printf("made a file in build/canto.ll\n");
        } else 
            printf("Codegen Warning: codegen_eval_expr returned NULL\n");
    }
    codegen_dump();

    if (diag_has_errors(&diags))
        diag_render_report(&diags, &map);

    free_parser(&parser);
    clean_up(&lexer, &diags);
	exit(0);
}

