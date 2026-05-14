#include "canto/compiler.h"
#include "canto/lexer.h"
#include<stdio.h>

void compile(const char* source, const char* file_path){
	if (file_path != NULL) {
		SourceMap map;
		initSourceMap(&map, file_path, source);
	}
}

