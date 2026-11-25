#pragma once

#include "common.h"
#include "arena.h"
#include "preprocess.h"
#include "string_helper.h"

typedef struct Diagnostic Diagnostic;
typedef struct SourceFile SourceFile;
typedef struct DiagnosticContext DiagnosticContext;

typedef enum {
    PREPROCESS,
    LEXICAL,
    SYNTAX,
    SEMANTICAL,
    INTERNAL
} DiagnosticKind;

typedef enum {
    NOTE,
    WARNING,
    ERROR,
    FATAL
} SeverityKind;

typedef enum {
    DIAGNOSTIC_GLOBAL_SCOPE,
    DIAGNOSTIC_LOCAL_SCOPE,
} DiagnosticScope;

typedef struct {
    const char* filepath;
    size_t line;
    size_t col;
    size_t len;
} DiagnosticLocale;

struct Diagnostic {
    DiagnosticKind kind;
    SeverityKind severity;
    DiagnosticLocale locale;
    char* message;
    const char* code;
    struct Diagnostic* next;
};

struct SourceFile {
    const char* filepath;
    const char* src;
    size_t src_len;
    size_t* line_offsets;
    size_t line_count;
    LineMap* map;
    Arena* arena;
    bool load_failed;
    struct SourceFile* next;
};

struct DiagnosticContext {
    Arena *arena;
    Diagnostic* first;
    Diagnostic* last;
    Diagnostic* cursor;

    size_t error_count;
    size_t warning_count;
    size_t note_count;

    size_t max_errors;
    bool warning_as_error;
    bool use_color;

    DiagnosticKind phase;
    FILE* out;

    SourceFile* sources;

    DiagnosticScope scope;
    struct DiagnosticContext* parent;
};

void diag_init(DiagnosticContext *ctx, Arena *arena);
void diag_init_global(DiagnosticContext *ctx, Arena *arena);
void diag_init_local(DiagnosticContext *ctx, Arena *arena, DiagnosticContext *global);
void diag_reset(DiagnosticContext *ctx);
void diag_unregister(DiagnosticContext *ctx);

void diag_set_warning_as_error(DiagnosticContext *ctx, bool value);
void diag_set_max_errors(DiagnosticContext *ctx, size_t max);
void diag_set_phase(DiagnosticContext *ctx, DiagnosticKind phase);
void diag_set_color(DiagnosticContext *ctx, bool value);
void diag_set_output(DiagnosticContext *ctx, FILE *out);

void diag_register_source(DiagnosticContext *ctx, const char* filepath, const char* src);
void diag_set_linemap(DiagnosticContext *ctx, const char* filepath, LineMap *map);
void diag_resolve_location(DiagnosticContext *ctx, const char** filepath, size_t *line);

void diag_emit(DiagnosticContext *ctx, SeverityKind severity, const char* filepath, size_t line, size_t col, const char* code, const char* fmt, ...);
void diag_note(DiagnosticContext *ctx, const char* filepath, size_t line, size_t col, const char* fmt, ...);
void diag_warning(DiagnosticContext *ctx, const char* filepath, size_t line, size_t col, const char* fmt, ...);
void diag_error(DiagnosticContext *ctx, const char* filepath, size_t line, size_t col, const char* fmt, ...);
void diag_fatal(DiagnosticContext *ctx, const char* filepath, size_t line, size_t col, const char* fmt, ...);
void diag_error_code(DiagnosticContext *ctx, const char* filepath, size_t line, size_t col, const char* code, const char* fmt, ...);

void diag_emit_span(DiagnosticContext *ctx, SeverityKind severity, const char* filepath, size_t line, size_t col, size_t len, const char* code, const char* fmt, ...);
void diag_note_span(DiagnosticContext *ctx, const char* filepath, size_t line, size_t col, size_t len, const char* fmt, ...);
void diag_warning_span(DiagnosticContext *ctx, const char* filepath, size_t line, size_t col, size_t len, const char* fmt, ...);
void diag_error_span(DiagnosticContext *ctx, const char* filepath, size_t line, size_t col, size_t len, const char* fmt, ...);
void diag_fatal_span(DiagnosticContext *ctx, const char* filepath, size_t line, size_t col, size_t len, const char* fmt, ...);

bool has_errors(DiagnosticContext *ctx);
size_t diag_error_count(DiagnosticContext *ctx);
size_t diag_warning_count(DiagnosticContext *ctx);
size_t diag_report_all(DiagnosticContext *ctx);
