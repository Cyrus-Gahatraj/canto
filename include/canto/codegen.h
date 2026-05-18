#pragma once

#include "ast.h"

#ifdef __cplusplus
extern "C" {
#endif

	void codegen_init(void);
	int codegen_eval_expr(Node* node);
	void codegen_free(void);

#ifdef __cplusplus
}
#endif

