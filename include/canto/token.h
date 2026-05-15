#pragma once
#include "common.h"

typedef enum {
#define TK(name, text)			TK_##name,
#define TK_KW(name, text)		TK_KW_##name,
#define TK_LIT(name, text)		TK_##name,
#define TK_TRIVIA(name, text)	TK_##name,
#include "../private/token_kinds.def" 
#undef TK
#undef TK_KW
#undef TK_LIT
#undef TK_TRIVIA
	TK_COUNT
}TokenKind;

typedef uint8_t TokenFlags;
enum TokenFlags_ {
    TOKEN_FLAG_NONE           = 0,
    TOKEN_FLAG_START_OF_LINE  = 1 << 0,  // first token on this line
    TOKEN_FLAG_LEADING_SPACE  = 1 << 1,  // whitespace/trivia preceded this
    TOKEN_FLAG_JOINT          = 1 << 2,  // no space between this and next
};

typedef struct {
	uint32_t start;
	uint32_t length;
} Span;

typedef struct {
	TokenKind  kind;
	TokenFlags flags;
	Span span;
	uint32_t sym;
	union {
		int64_t  i64;
		double   f64;
	} as;
} Token;

