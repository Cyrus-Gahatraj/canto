#include "canto/keyword_modifier.h"

typedef struct Write {
    const char *end;
    bool        flush;
} Write;

static KeywordAttr write_attrs[] = {
    { "end",   "\n",   "String appended after each write" },
    { "flush", "true", "Auto-flush after write (true/false)" },
};

const KeywordMeta write_meta = {
    .tk_type     = TK_KW_WRITE,
    .name        = "write",
    .description = "Output data to standard stream.",
    .attributes  = write_attrs,
    .attr_count  = sizeof(write_attrs) / sizeof(write_attrs[0]),
};
