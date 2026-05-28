#include "canto/keyword_modifier.h"

typedef struct Type {
    const char *name;
} Type;

static KeywordAttr type_attrs[] = {
    { "name",    "type",  "Display name for the type" },
    { "is_many", "false", "Whether the type represents a many_items/array" },
};

const KeywordMeta type_meta = {
    .tk_type     = TK_KW_TYPE,
    .name        = "type",
    .description = "Type descriptor.",
    .attributes  = type_attrs,
    .attr_count  = sizeof(type_attrs) / sizeof(type_attrs[0]),
};
