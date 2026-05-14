#include "canto/compiler.h"
#include "canto/lexer.h"
#include<stdio.h>

void compile(const char* source, const char* file_path){
	Lexer lexer;
	init_lexer(&lexer, source, file_path);
	run_lex(&lexer);
	
	for (uint32_t i = 0; i < lexer.tk_count; ++i) {
		Token* tk = &lexer.tokens[i];
		printf("Token %d: Kind %d, Span [%d, %d]\n", 
                i, tk->kind, tk->span.start, tk->span.length);
	}
}

