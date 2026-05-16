#include "canto/diagnostic.h"
#include "canto/memory.h"
#include <stdio.h>
#include <stdlib.h>

void diag_init(DiagEngine* diags) {
	diags->count = 0;
	diags->capacity = 0;
	diags->warning_count = 0;
	diags->error_count = 0;
	diags->diags = NULL;
}

void append_diag(DiagEngine* diags, const char* message, Span span,
				DiagPhase phase, DiagSeverity severity) {
	if (diags->capacity < diags->count + 1){
		diags->capacity = EXTEND_ARENA_CAPACITY(diags->capacity);
		diags->diags = EXTEND_ARENA(Diagnostic, diags->diags, diags->capacity);
	}

	Diagnostic* d = &diags->diags[diags->count];
	d->span = span;
	d->phase = phase;
	d->severity = severity;
	d->message = message;

	if (d->severity == DIAG_ERROR) diags->error_count++;
	else if (d->severity == DIAG_WARNING) diags->warning_count++;
	
	diags->count++;
}

void diag_free(DiagEngine* diags) {
	free(diags->diags);
	diag_init(diags);
}

void diag_render_report(DiagEngine* diags, const SourceMap* map) {
	if (diags->count == 0) return;

	for (uint32_t i = 0; i < diags->count; ++i) {
		Diagnostic* d = &diags->diags[i];

		uint32_t line_num = 1;
		uint32_t col_num = 1;
		const char* line_start = map->source_buffer;

		for (uint32_t offset = 0; offset < d->span.start; ++offset) {
			if (map->source_buffer[offset] == '\n') {
				line_num++;
				col_num++;
				line_start = &map->source_buffer[offset + 1];
			} else col_num++;
		}

		const char* color = ANSI_COLOR_RED;
        const char* label = "error";
        
        if (d->severity == DIAG_WARNING) {
            color = ANSI_COLOR_YELLOW;
            label = "warning";
        } else if (d->severity == DIAG_INFO) {
            color = ANSI_COLOR_BLUE;
            label = "info";
        } else if (d->severity == DIAG_FATAL) {
            color = ANSI_COLOR_RED ANSI_COLOR_BOLD;
            label = "fatal error";
        }

		fprintf(stderr, ANSI_COLOR_BOLD "%s:%u:%u: %s%s:" ANSI_COLOR_RESET ANSI_COLOR_BOLD " %s\n",
                map->file_path, line_num, col_num, color, label, d->message);


		const char* line_end = line_start;
        while (*line_end != '\n' && *line_end != '\0' && 
               (uint32_t)(line_end - map->source_buffer) < map->source_length) {
            line_end++;
        }
        uint32_t line_len = (uint32_t)(line_end - line_start);
		fprintf(stderr, " %3u | %.*s\n", line_num, line_len, line_start);

		fprintf(stderr, "     | ");
        for (uint32_t spaces = 1; spaces < col_num; spaces++) {
            fprintf(stderr, " ");
        }

		fprintf(stderr, "%s^", color);

		uint32_t carets = d->span.length > 0 ? d->span.length : 1;
        for (uint32_t c_idx = 1; c_idx < carets; c_idx++) {
            fprintf(stderr, "~");
        }
        fprintf(stderr, ANSI_COLOR_RESET "\n\n");
	}

}

