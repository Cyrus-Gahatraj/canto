#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "canto/symtable.h"
#include "canto/memory.h"

#define INITIAL_SYM_CAP  16
#define INITIAL_SLOT_CAP 32

static inline bool syms_match(Symbol *symbol,
                               const char *str, uint32_t length, Hash hash) {
    return symbol->hash   == hash
        && symbol->length == length
        && memcmp(symbol->start, str, length) == 0;
}

void symtable_init(SymTable *table) {
    table->capacity   = INITIAL_SYM_CAP;
    table->syms       = calloc(table->capacity, sizeof(Symbol));
    table->count      = 0;
    table->slot_count = INITIAL_SLOT_CAP;
    table->slots      = malloc(table->slot_count * sizeof(uint32_t));

    if (!table->slots || !table->syms) {
        fprintf(stderr, "symtable: out of memory\n");
        exit(1);
    }
    for (uint32_t i = 0; i < table->slot_count; i++)
        table->slots[i] = EMPTY_SLOT;
}

void symtable_free(SymTable *table) {
    for (uint32_t i = 1; i <= table->count; i++)
        free((void*)table->syms[i].start);
    free(table->syms);
    free(table->slots);
    memset(table, 0, sizeof(*table)); 
}

void symtable_hash(Symbol *symbol) {
    symbol->hash = FNV_OFFSET;
    for (uint32_t i = 0; i < symbol->length; i++) {
        symbol->hash ^= (Hash)(unsigned char)(symbol->start[i]);
        symbol->hash *= FNV_PRIME;
    }
}

static uint32_t find_slot(const SymTable *table, Symbol *symbol) {
    uint32_t mask = table->slot_count - 1;
    uint32_t slot = symbol->hash & mask;
    for (;;) {
        uint32_t id = table->slots[slot];
        if (id == EMPTY_SLOT) return slot;
        if (syms_match(&table->syms[id],
                        symbol->start, symbol->length, symbol->hash))
            return slot;
        slot = (slot + 1) & mask;
    }
}

static void rehash(SymTable *table) {
    for (uint32_t i = 0; i < table->slot_count; i++) table->slots[i] = EMPTY_SLOT;

    for (uint32_t id = 1; id <= table->count; id++) { 
        uint32_t slot = find_slot(table, &table->syms[id]);
        table->slots[slot] = id;
    }
}

SymId intern_symbol(SymTable *table, Symbol *symbol) {
    symtable_hash(symbol);
    uint32_t slot = find_slot(table, symbol);

    if (table->slots[slot] != EMPTY_SLOT)
        return table->slots[slot];

    if (table->count + 2 >= table->capacity) {
        table->capacity = EXTEND_ARENA_CAPACITY(table->capacity);
        table->syms     = EXTEND_ARENA(Symbol, table->syms, table->capacity);
    }

    char *copy = malloc(symbol->length + 1);
    if (!copy) {
        fprintf(stderr, "symtable: out of memory for string copy\n");
        exit(1);
    }
    memcpy(copy, symbol->start, symbol->length);
    copy[symbol->length] = '\0';

    uint32_t new_id     = table->count + 1;
    table->syms[new_id] = (Symbol){
        .start  = copy,
        .length = symbol->length,
        .hash   = symbol->hash,
        .kind   = symbol->kind,
    };
    table->count++;
    table->slots[slot] = new_id;

    if (table->count * 2 >= table->slot_count) {
        table->slot_count *= 2;
        table->slots = realloc(table->slots,
                               table->slot_count * sizeof(uint32_t));
        if (!table->slots) {
            fprintf(stderr, "symtable: out of memory\n");
            exit(1);
        }
        rehash(table);
    }

    return new_id;
}
