#include<stdio.h>
#include<stdlib.h>

#include "canto/compiler.h"
#include "canto/ast.h"
#include "canto/diagnostic.h"
#include "canto/lexer.h"
#include "canto/parser.h"
#include "canto/source_map.h"
#include "canto/codegen.h"

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

		case NODE_LET:
			printf("Let (sym=%u, has_type=%s):\n",
				   node->let.name_sym,
				   node->let.type_ann ? "yes" : "no");
			if (node->let.type_ann) print_ast(node->let.type_ann, indent + 1);
			print_ast(node->let.value, indent + 1);
			break;

		case NODE_PROGRAM:
			printf("Program (%u stmts):\n", node->block.count);
			for (uint32_t i = 0; i < node->block.count; i++)
				print_ast(node->block.stmts[i], indent + 1);
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
        goto cleanup;
    }

    {
        init_parser(&parser, &diags, lexer.tokens, lexer.tk_count, &map);
        Node *tree = parse_program(&parser);

        if (!diag_has_errors(&diags) && tree) {
            print_ast(tree, 0);

            codegen_free();
            codegen_init();

           // walk the program and codegen each statement
            for (uint32_t i = 0; i < tree->block.count; i++) {
                if (codegen_eval_expr(tree->block.stmts[i]) != 0) break;
            }

            codegen_dump();
        }

        if (diag_has_errors(&diags))
            diag_render_report(&diags, &map);

        free_parser(&parser);
    }

cleanup:
    symtable_free(&lexer.symbols);
    free(lexer.tokens);
    diag_free(&diags);
}
