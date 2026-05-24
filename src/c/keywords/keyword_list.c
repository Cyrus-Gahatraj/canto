#include "keyword_list.h"

const KeywordMeta* const keyword_registry[] = {
    &write_meta,
    &type_meta,
};

const uint32_t keyword_registry_count =
    sizeof(keyword_registry) / sizeof(keyword_registry[0]);
