#ifndef CANTO_REPL_H
#define CANTO_REPL_H

#include <stdbool.h>
#include <stdint.h>
#define REPL_RESULT_SLOT 0

#ifdef __cplusplus
extern "C" {
#endif

void repl_init(void);
void repl_free(void);

void repl_setup_globals(void);
void repl_register_storage(void);

void repl_set_type(uint32_t sym_id, uint8_t type_tag);
void repl_print(uint32_t sym_id);

#ifdef __cplusplus
}
#endif

#endif // CANTO_REPL_H
