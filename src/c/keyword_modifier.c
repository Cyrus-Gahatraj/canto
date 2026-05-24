#include "canto/keyword_modifier.h"
#include "keywords/keyword_list.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Remaining keyword attributes (not yet moved to own files) ──── */

static KeywordAttr let_attrs[] = {
    { "immutable", "true", "Whether binding is immutable" },
};

static KeywordAttr if_attrs[] = {
    { "style", "standard", "Style: standard, expression, or postfix" },
};

static KeywordAttr loop_attrs[] = {
    { "style", "standard", "Style: standard or inline" },
};

/* ── Composite registry (per-keyword files + local) ──────────────── */

static KeywordMeta local_registry[] = {
    { TK_KW_LET,  "let",  "Variable/function binding.", let_attrs, sizeof(let_attrs)/sizeof(let_attrs[0]) },
    { TK_KW_IF,   "if",   "Conditional expression.",    if_attrs,  sizeof(if_attrs)/sizeof(if_attrs[0])   },
    { TK_KW_LOOP, "loop", "Loop construct.",            loop_attrs,sizeof(loop_attrs)/sizeof(loop_attrs[0]) },
};

/* ── Lookup ──────────────────────────────────────────────────────── */

KeywordMeta* get_keyword_meta(uint32_t tk_type) {
    for (uint32_t i = 0; i < keyword_registry_count; i++) {
        if (keyword_registry[i]->tk_type == tk_type)
            return (KeywordMeta*)keyword_registry[i];
    }
    for (uint32_t i = 0; i < sizeof(local_registry)/sizeof(local_registry[0]); i++) {
        if (local_registry[i].tk_type == tk_type)
            return &local_registry[i];
    }
    return NULL;
}

/* ── Instance management ─────────────────────────────────────────── */

KeywordInstance* create_keyword_instance(uint32_t tk_type) {
    KeywordMeta *meta = get_keyword_meta(tk_type);
    if (!meta) return NULL;

    KeywordInstance *inst = calloc(1, sizeof(KeywordInstance));
    if (!inst) return NULL;

    inst->tk_type    = tk_type;
    inst->attr_count = meta->attr_count;
    inst->is_modified = false;

    inst->attr_names  = calloc(inst->attr_count, sizeof(char*));
    inst->attr_values = calloc(inst->attr_count, sizeof(char*));
    if (!inst->attr_names || !inst->attr_values) {
        free(inst->attr_names);
        free(inst->attr_values);
        free(inst);
        return NULL;
    }

    for (uint32_t i = 0; i < inst->attr_count; i++) {
        inst->attr_names[i] = strdup(meta->attributes[i].name);
        if (meta->attributes[i].default_value)
            inst->attr_values[i] = strdup(meta->attributes[i].default_value);
        else
            inst->attr_values[i] = NULL;
    }

    return inst;
}

KeywordInstance* copy_keyword_instance(const KeywordInstance *src) {
    if (!src) return NULL;

    KeywordInstance *inst = calloc(1, sizeof(KeywordInstance));
    if (!inst) return NULL;

    inst->tk_type     = src->tk_type;
    inst->attr_count  = src->attr_count;
    inst->is_modified = src->is_modified;

    inst->attr_names  = calloc(inst->attr_count, sizeof(char*));
    inst->attr_values = calloc(inst->attr_count, sizeof(char*));
    if (!inst->attr_names || !inst->attr_values) {
        free(inst->attr_names);
        free(inst->attr_values);
        free(inst);
        return NULL;
    }

    for (uint32_t i = 0; i < inst->attr_count; i++) {
        inst->attr_names[i] = src->attr_names[i] ? strdup(src->attr_names[i]) : NULL;
        inst->attr_values[i] = src->attr_values[i] ? strdup(src->attr_values[i]) : NULL;
    }

    return inst;
}

bool apply_keyword_edit(KeywordInstance *inst, const char *attr, const char *value) {
    if (!inst || !attr || !value) return false;

    for (uint32_t i = 0; i < inst->attr_count; i++) {
        if (strcmp(inst->attr_names[i], attr) == 0) {
            free(inst->attr_values[i]);
            inst->attr_values[i] = strdup(value);
            inst->is_modified = true;
            return true;
        }
    }
    return false;
}

const char* get_keyword_attr(const KeywordInstance *inst, const char *attr) {
    if (!inst || !attr) return NULL;

    for (uint32_t i = 0; i < inst->attr_count; i++) {
        if (strcmp(inst->attr_names[i], attr) == 0)
            return inst->attr_values[i];
    }
    return NULL;
}

void free_keyword_instance(KeywordInstance *inst) {
    if (!inst) return;

    for (uint32_t i = 0; i < inst->attr_count; i++) {
        free(inst->attr_names[i]);
        free(inst->attr_values[i]);
    }
    free(inst->attr_names);
    free(inst->attr_values);
    free(inst);
}
