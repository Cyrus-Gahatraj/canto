#pragma once

#include "common.h"

typedef struct {
	uint32_t start;
	uint32_t length;
} Span;

uint32_t get_token_offset(const char* buffer_start, const char* token_start);
uint32_t get_token_length(const char* token_start, const char* current);
Span get_span(const char* buffer_start, const char* start, const char* current);

