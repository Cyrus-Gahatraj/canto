#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void jit_init(void);
int jit_run(void);

void jit_free(void);

#ifdef __cplusplus
}
#endif
