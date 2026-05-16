#pragma once

#include "span.h"
#include "source_map.h"
#include "canto/common.h"

#define DIAG_MAX 50

typedef enum {
	DIAG_INFO,
	DIAG_WARNING,
	DIAG_ERROR,
	DIAG_FATAL,
} DiagSeverity;

typedef enum {
	DIAG_PHASE_LEX,
	DIAG_PHASE_PARSE,
	DIAG_PHASE_SEMANTIC,
	DIAG_PHASE_CODEGEN,
} DiagPhase;

typedef struct {
	DiagSeverity severity;
	DiagPhase phase;
	Span span;
	const char* message;
} Diagnostic;

typedef struct {
	Diagnostic* diags;
	uint32_t count;
	uint32_t capacity;
	uint32_t error_count;
	uint32_t warning_count;
	uint32_t suppressed;
} DiagEngine;

void diag_init(DiagEngine* diags);
void append_diag(DiagEngine* diags, const char* message, Span span,
				DiagPhase phase, DiagSeverity sev);
void diag_free(DiagEngine* diags);
void diag_render_report(DiagEngine* diags, const SourceMap* map);
bool diag_has_errors(const DiagEngine *diags);

