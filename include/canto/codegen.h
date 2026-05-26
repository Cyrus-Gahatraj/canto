#pragma once

#include "ast.h"
#include "symtable.h"

#ifdef __cplusplus
extern "C" {
#endif

	void codegen_init(void);
	int codegen_eval_expr(Node* node);
	void codegen_free(void);

	void codegen_dump(const char* output_path);
	void codegen_set_symtable(SymTable *table);
	void codegen_set_repl_mode(bool repl);
	void codegen_finalize(int ret);
	void codegen_finalize_repl(void);
	void codegen_print_ir();

#ifdef __cplusplus
}
#endif

