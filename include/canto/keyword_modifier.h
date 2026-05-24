#pragma once

#include "canto/token.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KW_MAX_ATTRS 16

typedef struct {
    const char *name;
    const char *default_value;
    const char *description;
} KeywordAttr;

typedef struct {
    uint32_t     tk_type;
    const char  *name;
    const char  *description;
    KeywordAttr *attributes;
    uint32_t     attr_count;
} KeywordMeta;

typedef struct {
    uint32_t    tk_type;
    char      **attr_names;
    char      **attr_values;
    uint32_t    attr_count;
    bool        is_modified;
} KeywordInstance;

KeywordMeta*        get_keyword_meta(uint32_t tk_type);
KeywordInstance*    create_keyword_instance(uint32_t tk_type);
KeywordInstance*    copy_keyword_instance(const KeywordInstance *src);
bool                apply_keyword_edit(KeywordInstance *inst, const char *attr, const char *value);
const char*         get_keyword_attr(const KeywordInstance *inst, const char *attr);
void                free_keyword_instance(KeywordInstance *inst);

#ifdef __cplusplus
}
#endif
