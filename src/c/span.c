#include "canto/span.h"

uint32_t get_token_offset(const char* buffer_start, const char* token_start) {
    return (uint32_t)(token_start - buffer_start);
}

uint32_t get_token_length(const char* token_start, const char* current) {
    return (uint32_t)(current - token_start);
}

Span get_span(const char* buffer_start, const char* token_start, const char* current) {
    uint32_t offset = get_token_offset(buffer_start, token_start);
    uint32_t length = get_token_length(token_start, current);
    
    Span span = (Span) { .start = offset, .length = length };
    return span;
}

