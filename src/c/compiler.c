#include "canto/compiler.h"
#include "canto/diagnostic.h"
#include "canto/lexer.h"
#include "canto/source_map.h"
#include<stdio.h>
#include<stdlib.h>

void compile(const char* source, const char* file_path){
	Lexer lexer;
	DiagEngine diags;
	SourceMap map;
	
	init_source_map(&map, file_path, source);
	init_lexer(&lexer, &map);
	diag_init(&diags);
	run_lex(&lexer, &diags);
	diag_render_report(&diags, &map);
	
	for (uint32_t i = 0; i < lexer.tk_count; ++i) {
		Token* tk = &lexer.tokens[i];
		printf("Token %d: Kind %d, Span [%d, %d]\n", 
                i, tk->kind, tk->span.start, tk->span.length);
	}

	symtable_free(&lexer.symbols);
    free(lexer.tokens);
    diag_free(&diags);
}

