#include "canto/diagnostic.h"
#include "canto/common.h"
#include "canto/memory.h"
#include <stdio.h>
#include <stdlib.h>

void diag_init(DiagEngine* diags) {
	diags->count = 0;
	diags->capacity = 0;
	diags->warning_count = 0;
	diags->error_count = 0;
	diags->suppressed = 0;
	diags->diags = NULL;
}

bool diag_has_errors(const DiagEngine *diags) {
    return diags->error_count > 0;
}

void append_diag(DiagEngine* diags, const char* message, Span span,
				DiagPhase phase, DiagSeverity sev) {
	if (diags->count >= DIAG_MAX) {
        diags->suppressed++;
        return;
    }
	if (diags->capacity < diags->count + 1){
		diags->capacity = EXTEND_ARENA_CAPACITY(diags->capacity);
		diags->diags = EXTEND_ARENA(Diagnostic, diags->diags, diags->capacity);
	}

	Diagnostic* d = &diags->diags[diags->count];
	d->span = span;
	d->phase = phase;
	d->severity = sev;
	d->message = message;

	if (sev == DIAG_ERROR || sev == DIAG_FATAL) diags->error_count++;
	else if (sev == DIAG_WARNING) diags->warning_count++;
	
	diags->count++;
}

void diag_free(DiagEngine* diags) {
	free(diags->diags);
	diag_init(diags);
}

static const char *severity_color(DiagSeverity sev) {
    switch (sev) {
        case DIAG_INFO:    return ANSI_COLOR_CYAN;
        case DIAG_WARNING: return ANSI_COLOR_YELLOW;
        case DIAG_ERROR:   return ANSI_COLOR_RED;
        case DIAG_FATAL:   return ANSI_COLOR_RED ANSI_BOLD;
        default:           return ANSI_COLOR_WHITE;
    }
}

static const char *severity_word(DiagSeverity sev) {
    switch (sev) {
        case DIAG_INFO:    return "info";
        case DIAG_WARNING: return "warning";
        case DIAG_ERROR:   return "error";
        case DIAG_FATAL:   return "fatal";
        default:           return "?";
    }
}

static const char *phase_word(DiagPhase phase) {
    switch (phase) {
        case DIAG_PHASE_LEX:		 return "lex";
        case DIAG_PHASE_PARSE:		 return "parse";
        case DIAG_PHASE_SEMANTIC:    return "semantic";
        case DIAG_PHASE_CODEGEN:	 return "codegen";
        default:					 return "?";
    }
}

static uint32_t resolve_line(const SourceMap *map, uint32_t offset) {
    uint32_t lo = 0, hi = map->line.count, idx = 0;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (map->line.offsets[mid] <= offset) { idx = mid; lo = mid + 1; }
        else                                    hi = mid;
    }
    return idx;
}

static void render_caret(const SourceMap *map, const Diagnostic *d,
                         uint32_t line_idx, uint32_t col,
                         const char *color) {
    uint32_t line_start_off = map->line.offsets[line_idx];
    const char *line_start  = &map->source_buffer[line_start_off];

    // find end of line 
    const char *line_end = line_start;
    while (*line_end && *line_end != '\n' &&
           (uint32_t)(line_end - map->source_buffer) < map->source_length)
        line_end++;

    uint32_t line_len = (uint32_t)(line_end - line_start);
    uint32_t line_num = line_idx + 1;

    // source line with line number
    fprintf(stderr, ANSI_DIM " %3u " ANSI_RESET "│ %.*s\n",
            line_num, (int)line_len, line_start);

    // caret row — spaces up to col, then ^ and ~~~, then message
    fprintf(stderr, "     │ ");
    for (uint32_t i = 1; i < col; i++) fprintf(stderr, " ");

    uint32_t carets = d->span.length > 1 ? d->span.length : 1;
    fprintf(stderr, "%s^", color);
    for (uint32_t i = 1; i < carets; i++) fprintf(stderr, "~");
    fprintf(stderr, "  %s" ANSI_RESET "\n", d->message);
}

void diag_render_report(DiagEngine *diags, const SourceMap *map) {
    if (diags->count == 0) return;

    for (uint32_t i = 0; i < diags->count; i++) {
        Diagnostic *d    = &diags->diags[i];
        const char *col  = severity_color(d->severity);
        const char *sev  = severity_word(d->severity);
        const char *ph   = phase_word(d->phase);

        uint32_t line_idx = resolve_line(map, d->span.start);
        uint32_t line_num = line_idx + 1;
        uint32_t col_num  = (d->span.start - map->line.offsets[line_idx]) + 1;

        fprintf(stderr, "%s" ANSI_BOLD "%s" ANSI_RESET
                        ANSI_DIM " · %s   " ANSI_RESET
                        ANSI_COLOR_WHITE "%s  " ANSI_RESET
                        ANSI_DIM "%u:%u\n" ANSI_RESET,
                col, sev, ph, map->file_path, line_num, col_num);

        render_caret(map, d, line_idx, col_num, col);
        fprintf(stderr, "\n");
    }

    if (diags->suppressed > 0)
        fprintf(stderr, ANSI_DIM"~ %u further error%s not shown\n" ANSI_RESET,
                diags->suppressed,
                diags->suppressed == 1 ? "" : "s");

    if (diags->error_count > 0)
        fprintf(stderr, ANSI_COLOR_RED ANSI_BOLD "~ %u error%s" ANSI_RESET "\n",
                diags->error_count,
                diags->error_count == 1 ? "" : "s");

    if (diags->warning_count > 0)
        fprintf(stderr, ANSI_COLOR_YELLOW "~ %u warning%s" ANSI_RESET "\n",
                diags->warning_count,
                diags->warning_count == 1 ? "" : "s");
}

