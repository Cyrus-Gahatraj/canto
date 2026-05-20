#pragma once

#include "ast.h"
#include "symtable.h"

#ifdef __cplusplus
extern "C" {
#endif

	void codegen_init(void);
	int codegen_eval_expr(Node* node);
	void codegen_free(void);

	void codegen_dump(void);
	void codegen_set_symtable(SymTable *table);

#ifdef __cplusplus
}
#endif

