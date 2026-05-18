#pragma once

#include "ast.h"

#ifdef __cplusplus
extern "C" {
#endif

	void codegen_init(void);
	int codegen_eval_expr(Node* node);
	void codegen_free(void);

	// testing
	void codegen_dump(void);

#ifdef __cplusplus
}
#endif

