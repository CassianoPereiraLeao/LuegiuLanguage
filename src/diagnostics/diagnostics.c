#include "../include/diagnostics.h"

#define DIAG_TAB_WIDTH 4

#define C_RESET   "\x1b[0m"
#define C_BOLD    "\x1b[1m"
#define C_RED     "\x1b[1;31m"
#define C_YELLOW  "\x1b[1;33m"
#define C_CYAN    "\x1b[1;36m"
#define C_GREEN   "\x1b[1;32m"

#ifdef _WIN32
#include <io.h>
#define DIAG_ISATTY(f) _isatty(_fileno(f))

__declspec(dllimport) void* __stdcall GetStdHandle(unsigned long);
__declspec(dllimport) int   __stdcall GetConsoleMode(void*, unsigned long*);
__declspec(dllimport) int   __stdcall SetConsoleMode(void*, unsigned long);

static bool enable_vt(void) {
    void* h = GetStdHandle((unsigned long)-12);
    unsigned long mode = 0;
    if(!h || h == (void*)(intptr_t)-1 || !GetConsoleMode(h, &mode)) return false;
    if(mode & 0x0004) return true;
    return SetConsoleMode(h, mode | 0x0004) != 0;
}
#else
#include <unistd.h>
#define DIAG_ISATTY(f) isatty(fileno(f))
#endif

static bool detect_color(FILE* f) {
    if(getenv("NO_COLOR")) return false;
    if(!DIAG_ISATTY(f)) return false;
#ifdef _WIN32
    return enable_vt();
#else
    const char* term = getenv("TERM");
    return term && strcmp(term, "dumb") != 0;
#endif
}

static const char* col_of(const DiagnosticContext *ctx, const char* c) {
    return ctx->use_color ? c : "";
}

static char* vformat(const char* fmt, va_list ap) {
    va_list copy;
    va_copy(copy, ap);
    int n = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if(n < 0) return NULL;

    char* buf = (char*)malloc((size_t)n + 1);
    if(!buf) return NULL;
    vsnprintf(buf, (size_t)n + 1, fmt, ap);
    return buf;
}

static SourceFile* find_source(DiagnosticContext *ctx, const char* path) {
    if(!path) return NULL;
    for(DiagnosticContext* c = ctx; c; c = c->parent)
        for(SourceFile* s = c->sources; s; s = s->next)
            if(strcmp(s->filepath, path) == 0) return s;
    return NULL;
}

static LineMap* find_linemap(DiagnosticContext *ctx, const char* path) {
    if(!path) return NULL;
    for(DiagnosticContext* c = ctx; c; c = c->parent)
        for(SourceFile* s = c->sources; s; s = s->next)
            if(s->map && strcmp(s->filepath, path) == 0) return s->map;
    return NULL;
}

static SourceFile* own_source(DiagnosticContext *ctx, const char* path) {
    for(SourceFile* s = ctx->sources; s; s = s->next)
        if(strcmp(s->filepath, path) == 0) return s;

    SourceFile* s = (SourceFile*)arena_alloc(ctx->arena, sizeof(SourceFile));
    if(!s) return NULL;
    memset(s, 0, sizeof(*s));

    s->filepath = arena_strdup(ctx->arena, path);
    if(!s->filepath) return NULL;
    s->arena = ctx->arena;
    s->next = ctx->sources;
    ctx->sources = s;
    return s;
}

static SourceFile* get_source(DiagnosticContext *ctx, const char* path) {
    SourceFile* s = find_source(ctx, path);
    return s ? s : own_source(ctx, path);
}

static void index_lines(SourceFile *sf) {
    size_t n = 1;
    for(size_t i = 0; i < sf->src_len; i++)
        if(sf->src[i] == '\n') n++;

    sf->line_offsets = (size_t*)arena_alloc(sf->arena, n * sizeof(size_t));
    if(!sf->line_offsets) { sf->line_count = 0; return; }

    size_t k = 0;
    sf->line_offsets[k++] = 0;
    for(size_t i = 0; i < sf->src_len; i++)
        if(sf->src[i] == '\n') sf->line_offsets[k++] = i + 1;

    sf->line_count = n;
}

static void ensure_loaded(SourceFile *sf) {
    if(sf->src || sf->load_failed) return;

    FILE* f = fopen(sf->filepath, "rb");
    if(!f) { sf->load_failed = true; return; }

    long size = -1;
    if(fseek(f, 0, SEEK_END) == 0) size = ftell(f);
    if(size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        sf->load_failed = true;
        return;
    }

    char* buf = (char*)arena_alloc(sf->arena, (size_t)size + 1);
    if(!buf) { fclose(f); sf->load_failed = true; return; }

    size_t rd = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[rd] = '\0';

    sf->src = buf;
    sf->src_len = rd;
    index_lines(sf);
}

static bool get_line(const SourceFile *sf, size_t line, const char** out, size_t *len) {
    if(!sf->src || !sf->line_offsets || line < 1 || line > sf->line_count) return false;

    size_t start = sf->line_offsets[line - 1];
    size_t end = (line < sf->line_count) ? sf->line_offsets[line] - 1 : sf->src_len;
    if(end > start && sf->src[end - 1] == '\r') end--;   /* CRLF */

    *out = sf->src + start;
    *len = end - start;
    return true;
}

void diag_register_source(DiagnosticContext *ctx, const char* filepath, const char* src) {
    if(!ctx || !filepath || !src) return;

    SourceFile* sf = own_source(ctx, filepath);
    if(!sf) return;

    size_t n = strlen(src);
    char* copy = (char*)arena_alloc(ctx->arena, n + 1);
    if(!copy) return;
    memcpy(copy, src, n + 1);

    sf->src = copy;
    sf->src_len = n;
    sf->load_failed = false;
    index_lines(sf);
}

void diag_set_linemap(DiagnosticContext *ctx, const char* filepath, LineMap *map) {
    if(!ctx || !filepath) return;
    SourceFile* sf = own_source(ctx, filepath);
    if(sf) sf->map = map;
}

void diag_resolve_location(DiagnosticContext *ctx, const char** filepath, size_t *line) {
    if(!ctx || !filepath || !*filepath || !line) return;

    LineMap* map = find_linemap(ctx, *filepath);
    if(!map) return;

    const char* file = NULL;
    int resolved = 0;
    linemap_resolve(map, *line, &file, &resolved);

    if(file && strcmp(file, "unknown") != 0 && resolved > 0) {
        *filepath = file;
        *line = (size_t)resolved;
    }
}

void diag_init(DiagnosticContext *ctx, Arena *arena) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->arena = arena;
    ctx->scope = DIAGNOSTIC_LOCAL_SCOPE;
    ctx->phase = INTERNAL;
    ctx->out = stderr;
    ctx->use_color = detect_color(stderr);
}

void diag_init_global(DiagnosticContext *ctx, Arena *arena) {
    diag_init(ctx, arena);
    ctx->scope = DIAGNOSTIC_GLOBAL_SCOPE;
}

void diag_init_local(DiagnosticContext *ctx, Arena *arena, DiagnosticContext *global) {
    diag_init(ctx, arena);
    ctx->scope = DIAGNOSTIC_LOCAL_SCOPE;
    ctx->parent = global;
}

void diag_reset(DiagnosticContext *ctx) {
    ctx->first = ctx->last = ctx->cursor = NULL;
    ctx->error_count = ctx->warning_count = ctx->note_count = 0;
}

void diag_unregister(DiagnosticContext *ctx) {
    ctx->sources = NULL;
}

void diag_set_warning_as_error(DiagnosticContext *ctx, bool v) { ctx->warning_as_error = v; }
void diag_set_max_errors(DiagnosticContext *ctx, size_t max)   { ctx->max_errors = max; }
void diag_set_phase(DiagnosticContext *ctx, DiagnosticKind p)  { ctx->phase = p; }
void diag_set_color(DiagnosticContext *ctx, bool v)            { ctx->use_color = v; }
void diag_set_output(DiagnosticContext *ctx, FILE *out)        { ctx->out = out ? out : stderr; }

static void push(DiagnosticContext *ctx, SeverityKind sev, const char* file,
                 size_t line, size_t col, size_t len, const char* code, const char* msg) {
    if(sev == WARNING && ctx->warning_as_error) sev = ERROR;

    const char* path = NULL;
    if(file) {
        SourceFile* sf = get_source(ctx, file);
        path = sf ? sf->filepath : file;
    }

    Diagnostic* last = ctx->last;
    if(sev != NOTE && last && last->severity == sev &&
       last->locale.line == line && last->locale.col == col &&
       last->locale.filepath == path && strcmp(last->message, msg) == 0)
        return;

    Diagnostic* d = (Diagnostic*)arena_alloc(ctx->arena, sizeof(Diagnostic));
    if(!d) return;
    memset(d, 0, sizeof(*d));

    d->message = arena_strdup(ctx->arena, msg);
    if(!d->message) return;
    d->code = arena_strdup(ctx->arena, code);
    d->kind = ctx->phase;
    d->severity = sev;
    d->locale.filepath = path;
    d->locale.line = line;
    d->locale.col = col;
    d->locale.len = len;

    if(ctx->last) ctx->last->next = d;
    else ctx->first = d;
    ctx->last = d;

    switch(sev) {
        case NOTE:    ctx->note_count++; break;
        case WARNING: ctx->warning_count++; break;
        case ERROR:
        case FATAL:   ctx->error_count++; break;
    }
}

static void emit_v(DiagnosticContext *ctx, SeverityKind sev, const char* file,
                   size_t line, size_t col, size_t len, const char* code,
                   const char* fmt, va_list ap) {
    if(!ctx || !fmt) return;

    char* msg = vformat(fmt, ap);
    if(!msg) return;

    if(file && line) diag_resolve_location(ctx, &file, &line);
    push(ctx, sev, file, line, col, len, code, msg);
    free(msg);

    if(sev == FATAL) {
        if(ctx->parent) diag_report_all(ctx->parent);
        diag_report_all(ctx);
        exit(COMPILE_ERROR);
    }

    if(ctx->max_errors > 0 && ctx->error_count >= ctx->max_errors) {
        if(ctx->parent) diag_report_all(ctx->parent);
        diag_report_all(ctx);
        fprintf(ctx->out, "\n(Limite maximo de erros atingido: %zu)\n", ctx->max_errors);
        exit(EXPLODE_MAX_ERRORS);
    }
}

void diag_emit(DiagnosticContext *ctx, SeverityKind sev, const char* file, size_t line, size_t col, const char* code, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    emit_v(ctx, sev, file, line, col, 1, code, fmt, ap);
    va_end(ap);
}

void diag_emit_span(DiagnosticContext *ctx, SeverityKind sev, const char* file, size_t line, size_t col, size_t len, const char* code, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    emit_v(ctx, sev, file, line, col, len, code, fmt, ap);
    va_end(ap);
}

void diag_error_code(DiagnosticContext *ctx, const char* file, size_t line, size_t col, const char* code, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    emit_v(ctx, ERROR, file, line, col, 1, code, fmt, ap);
    va_end(ap);
}

#define DEFINE_DIAG(NAME, SEV)                                                      \
    void NAME(DiagnosticContext *ctx, const char* file, size_t line, size_t col,    \
              const char* fmt, ...) {                                               \
        va_list ap; va_start(ap, fmt);                                              \
        emit_v(ctx, SEV, file, line, col, 1, NULL, fmt, ap);                        \
        va_end(ap);                                                                 \
    }

#define DEFINE_DIAG_SPAN(NAME, SEV)                                                 \
    void NAME(DiagnosticContext *ctx, const char* file, size_t line, size_t col,    \
              size_t len, const char* fmt, ...) {                                   \
        va_list ap; va_start(ap, fmt);                                              \
        emit_v(ctx, SEV, file, line, col, len, NULL, fmt, ap);                      \
        va_end(ap);                                                                 \
    }

DEFINE_DIAG(diag_note,    NOTE)
DEFINE_DIAG(diag_warning, WARNING)
DEFINE_DIAG(diag_error,   ERROR)
DEFINE_DIAG(diag_fatal,   FATAL)

DEFINE_DIAG_SPAN(diag_note_span,    NOTE)
DEFINE_DIAG_SPAN(diag_warning_span, WARNING)
DEFINE_DIAG_SPAN(diag_error_span,   ERROR)
DEFINE_DIAG_SPAN(diag_fatal_span,   FATAL)

#undef DEFINE_DIAG
#undef DEFINE_DIAG_SPAN

bool has_errors(DiagnosticContext *ctx)           { return ctx && ctx->error_count > 0; }
size_t diag_error_count(DiagnosticContext *ctx)   { return ctx ? ctx->error_count : 0; }
size_t diag_warning_count(DiagnosticContext *ctx) { return ctx ? ctx->warning_count : 0; }

static const char* severity_to_string(SeverityKind kind) {
    switch(kind) {
        case NOTE:    return "nota";
        case WARNING: return "aviso";
        case ERROR:   return "erro";
        case FATAL:   return "erro fatal";
    }
    return "?";
}

static const char* severity_color(SeverityKind kind) {
    switch(kind) {
        case NOTE:    return C_CYAN;
        case WARNING: return C_YELLOW;
        case ERROR:
        case FATAL:   return C_RED;
    }
    return "";
}

static size_t visual_width(const char* s, size_t n) {
    size_t w = 0;
    for(size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if(c == '\t') w += DIAG_TAB_WIDTH;
        else if((c & 0xC0) != 0x80) w++;
    }
    return w;
}

static void print_snippet(DiagnosticContext *ctx, const Diagnostic *d) {
    if(!d->locale.filepath || d->locale.line == 0) return;

    SourceFile* sf = find_source(ctx, d->locale.filepath);
    if(!sf) return;
    ensure_loaded(sf);

    const char* s; size_t n;
    if(!get_line(sf, d->locale.line, &s, &n)) return;

    FILE* out = ctx->out;
    int w = 1;
    for(size_t t = d->locale.line; t >= 10; t /= 10) w++;

    fprintf(out, " %*zu | ", w, d->locale.line);
    for(size_t i = 0; i < n; i++) {
        if(s[i] == '\t') fputs("    ", out);
        else fputc(s[i], out);
    }
    fputc('\n', out);

    if(d->locale.col == 0) return;

    size_t c0 = d->locale.col - 1;
    if(c0 > n) c0 = n;

    size_t end = c0 + (d->locale.len ? d->locale.len : 1);
    if(end > n) end = n;

    size_t pad = visual_width(s, c0);
    size_t width = (end > c0) ? visual_width(s + c0, end - c0) : 1;
    if(width == 0) width = 1;

    fprintf(out, " %*s | ", w, "");
    for(size_t i = 0; i < pad; i++) fputc(' ', out);
    fputs(col_of(ctx, severity_color(d->severity)), out);
    fputc('^', out);
    for(size_t i = 1; i < width; i++) fputc('~', out);
    fputs(col_of(ctx, C_RESET), out);
    fputc('\n', out);
}

static void print_path(FILE *out, const char* p) {
    for(; *p; ++p) {
#ifdef _WIN32
        fputc(*p == '/' ? '\\' : *p, out);
#else
        fputc(*p, out);
#endif
    }
}

static bool same_line(const Diagnostic *a, const Diagnostic *b) {
    if(!a || !b || a->locale.line != b->locale.line) return false;
    if(a->locale.filepath == b->locale.filepath) return true;
    return a->locale.filepath && b->locale.filepath &&
           strcmp(a->locale.filepath, b->locale.filepath) == 0;
}

static void print_diagnostic(DiagnosticContext *ctx, const Diagnostic *d) {
    FILE* out = ctx->out;

    if(d->severity == NOTE && same_line(ctx->cursor, d)) {
        fprintf(out, "    %s= nota:%s %s\n",
                col_of(ctx, C_CYAN), col_of(ctx, C_RESET), d->message);
        return;
    }

    if(ctx->cursor && d->severity != NOTE) fputc('\n', out);

    if(d->locale.filepath) {
        fputs(col_of(ctx, C_BOLD), out);
        print_path(out, d->locale.filepath);
        fputc(':', out);
        if(d->locale.line) {
            fprintf(out, "%zu:", d->locale.line);
            if(d->locale.col) fprintf(out, "%zu:", d->locale.col);
        }
        fputc(' ', out);
        fputs(col_of(ctx, C_RESET), out);
    }

    fputs(col_of(ctx, severity_color(d->severity)), out);
    fputs(severity_to_string(d->severity), out);
    if(d->code) fprintf(out, " [%s]", d->code);
    fputs(col_of(ctx, C_RESET), out);

    fprintf(out, ": %s%s%s\n", col_of(ctx, C_BOLD), d->message, col_of(ctx, C_RESET));

    print_snippet(ctx, d);
}

size_t diag_report_all(DiagnosticContext *ctx) {
    if(!ctx) return 0;

    size_t printed = 0;
    for(Diagnostic* d = ctx->cursor ? ctx->cursor->next : ctx->first; d; d = d->next) {
        print_diagnostic(ctx, d);
        ctx->cursor = d;
        printed++;
    }

    if(printed && (ctx->error_count || ctx->warning_count)) {
        size_t e = ctx->error_count, w = ctx->warning_count;
        fputc('\n', ctx->out);
        if(e) fprintf(ctx->out, "%zu erro%s", e, e == 1 ? "" : "s");
        if(e && w) fputs(" e ", ctx->out);
        if(w) fprintf(ctx->out, "%zu aviso%s", w, w == 1 ? "" : "s");
        fprintf(ctx->out, " gerado%s.\n", (e + w) == 1 ? "" : "s");
    }

    return ctx->error_count;
}
