#pragma once
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FNV_OFFSET  0x811c9dc5u
#define FNV_PRIME   0x01000193u
#define EMPTY_SLOT  0xFFFFFFFFu

typedef uint32_t Hash;

typedef enum {
    SYM_VARIABLE,
    SYM_FUNCTION,
    SYM_TYPE,
    SYM_KEYWORD,
} SymbolKind;

typedef struct {
    const char *start;    // pointer into source buffer or string pool
    uint32_t    length;
    Hash        hash;
    SymbolKind  kind;
} Symbol;

typedef struct {
    Symbol   *syms;       // flat array index by SymId
    uint32_t *slots;      // hash table: slot → index into syms
    uint32_t  count;      
    uint32_t  capacity;  
    uint32_t  slot_count;
} SymTable;

typedef uint32_t SymId;

void symtable_init(SymTable* table);
void symtable_free(SymTable* table);
void symtable_hash(Symbol* symbol);
SymId intern_symbol(SymTable* table, Symbol* symbol);

#ifdef __cplusplus
}
#endif

