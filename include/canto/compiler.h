#pragma once
#include "common.h"
#include "symtable.h"

typedef struct {
    bool is_repl;
    SymTable global_symbols;
} CantoContext;

CantoContext* canto_ctx_create(bool is_repl);
void canto_ctx_free(CantoContext* ctx);
bool compile(CantoContext* ctx, const char* source, const char* file_path, const char* output_path);

