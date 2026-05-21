#include<stdio.h>
#include<stdlib.h>

#include "canto/compiler.h"
#include "canto/ast.h"
#include "canto/diagnostic.h"
#include "canto/lexer.h"
#include "canto/parser.h"
#include "canto/source_map.h"
#include "canto/codegen.h"

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
            codegen_free();
            codegen_init();
			codegen_set_symtable(&lexer.symbols);

			bool ok = true;
           // walk the program and codegen each statement
           for (uint32_t i = 0; i < tree->block.count; i++) {
                if (codegen_eval_expr(tree->block.stmts[i]) != 0){
					ok = false;
					break;
				} 
           }

		   if (ok) codegen_finalize(0);
		   else codegen_finalize(1);

		   printf("\n");
		   codegen_print_ir();
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

	exit(0); // repl will only run once for now
}
