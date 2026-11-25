#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "../include/preprocess.h"

enum { C_OTHER = 0, C_WS, C_NL, C_IDENT, C_DIGIT, C_QUOTE, C_APOS, C_SLASH };

static uint8_t g_cls[256];
static uint8_t g_idc[256];
static bool g_tables_ready = false;

#define CLS(c)     (g_cls[(uint8_t)(c)])
#define IS_IDC(c)  (g_idc[(uint8_t)(c)])
#define IS_WS(c)   (CLS(c) == C_WS)

static void tables_init(void) {
    if(g_tables_ready) return;

    memset(g_cls, C_OTHER, sizeof g_cls);
    memset(g_idc, 0, sizeof g_idc);

    for(int ch = 'a'; ch <= 'z'; ch++) { g_cls[ch] = C_IDENT; g_idc[ch] = 1; }
    for(int ch = 'A'; ch <= 'Z'; ch++) { g_cls[ch] = C_IDENT; g_idc[ch] = 1; }
    g_cls['_'] = C_IDENT; g_idc['_'] = 1;
    for(int ch = '0'; ch <= '9'; ch++) { g_cls[ch] = C_DIGIT; g_idc[ch] = 1; }

    g_cls[' '] = g_cls['\t'] = g_cls['\r'] = g_cls['\f'] = g_cls['\v'] = C_WS;
    g_cls['\n'] = C_NL;
    g_cls['"'] = C_QUOTE;
    g_cls['\''] = C_APOS;
    g_cls['/'] = C_SLASH;

    g_tables_ready = true;
}

static inline const char* skip_literal(const char* p, const char* end) {
    char q = *p++;
    while(p < end) {
        char ch = *p;
        if(ch == '\\' && p + 1 < end && p[1] != '\n') { p += 2; continue; }
        if(ch == q) return p + 1;
        if(ch == '\n') return p;
        p++;
    }
    return p;
}

static inline bool is_comment_open(const char* p, const char* end) {
    if(p >= end || p[0] != '/') return false;
    if((size_t)(end - p) < COMMENT_OPEN_LEN) return false;
    if(memcmp(p, COMMENT_OPEN, COMMENT_OPEN_LEN) != 0) return false;
    return (p + COMMENT_OPEN_LEN >= end) || !IS_IDC(p[COMMENT_OPEN_LEN]);
}

static inline bool is_comment_close(const char* p, const char* end) {
    return p < end && p[0] == 'e' &&
           (size_t)(end - p) >= COMMENT_CLOSE_LEN &&
           memcmp(p, COMMENT_CLOSE, COMMENT_CLOSE_LEN) == 0;
}

static const char* skip_block_comment(const char* p, const char* end, size_t* nl) {
    p += COMMENT_OPEN_LEN;
    while(p < end) {
        if(is_comment_close(p, end)) return p + COMMENT_CLOSE_LEN;
        if(*p == '\n' && nl) (*nl)++;
        p++;
    }
    return end;
}

/* ------------------------------------------------------------------ */
/* StringBuilder                                                       */
/* ------------------------------------------------------------------ */

static void sb_init(StringBuilder *sb, Arena *arena, size_t cap) {
    sb->arena = arena;
    sb->len = 0;
    sb->capacity = cap;
    sb->data = arena ? (char*)arena_alloc(arena, cap) : (char*)malloc(cap);
    if(!sb->data) {
        fprintf(stderr, "Preprocess: falha ao alocar %zu bytes\n", cap);
        exit(ALLOCATE_ERROR);
    }
    sb->data[0] = '\0';
}

static NOINLINE void string_builder_grow(StringBuilder *sb, size_t extra) {
    size_t need = sb->len + extra + 1;

    if(need < sb->len || need > (size_t)MAX_CAP) {
        fprintf(stderr,
            "Preprocess: saida excedeu o limite de capacidade maximo de %zu MB. "
            "Possivel causa: macro bomb\n", (size_t)(MAX_CAP / MB));
        exit(BUFFER_OVERFLOW);
    }

    size_t cap = sb->capacity ? sb->capacity : 256;
    while(cap < need) cap <<= 1;
    if(cap > (size_t)MAX_CAP) cap = (size_t)MAX_CAP;

    if(sb->arena) {
        char* data = (char*)arena_alloc(sb->arena, cap);
        if(sb->len) memcpy(data, sb->data, sb->len);
        sb->data = data;
    } else {
        char* data = (char*)realloc(sb->data, cap);
        if(!data) {
            fprintf(stderr, "Preprocess: falha ao alocar %zu bytes\n", cap);
            exit(ALLOCATE_ERROR);
        }
        sb->data = data;
    }

    sb->capacity = cap;
}

static inline void string_builder_write(StringBuilder *sb, const char* string, size_t len) {
    if(len == 0) return;
    if(UNLIKELY(sb->len + len + 1 > sb->capacity)) string_builder_grow(sb, len);
    memcpy(sb->data + sb->len, string, len);
    sb->len += len;
}

static inline void string_builder_write_char(StringBuilder *sb, const char c) {
    if(UNLIKELY(sb->len + 2 > sb->capacity)) string_builder_grow(sb, 1);
    sb->data[sb->len++] = c;
}

/* ------------------------------------------------------------------ */
/* LineMap (array + busca binaria)                                     */
/* ------------------------------------------------------------------ */

void linemap_zero_init(LineMap *map) {
    map->count = 0;
    map->cap = 0;
    map->v = NULL;
}

static void linemap_push(LineMap *map, Arena *arena, size_t out, size_t src, const char* filename) {
    if(!map) return;

    if(map->count && map->v[map->count - 1].out == out) {
        map->v[map->count - 1].src = src;
        map->v[map->count - 1].filename = filename;
        return;
    }

    if(map->count == map->cap) {
        size_t ncap = map->cap ? map->cap * 2 : 256;
        LineMapEntry* nv = (LineMapEntry*)arena_alloc(arena, ncap * sizeof(LineMapEntry));
        if(map->count) memcpy(nv, map->v, map->count * sizeof(LineMapEntry));
        map->v = nv;
        map->cap = ncap;
    }

    LineMapEntry* e = &map->v[map->count++];
    e->out = out;
    e->src = src;
    e->filename = filename;
}

void linemap_resolve(LineMap *map, size_t out, const char** file_out, int *line_out) {
    if(!map || map->count == 0) {
        *file_out = "unknown";
        *line_out = (int)out;
        return;
    }

    /* ultimo indice com v[i].out <= out */
    size_t lo = 0, hi = map->count;
    while(lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if(map->v[mid].out <= out) lo = mid + 1;
        else hi = mid;
    }

    const LineMapEntry* e = &map->v[lo ? lo - 1 : 0];
    *file_out = e->filename;
    *line_out = (int)(e->src + (out - e->out));
}

/* ------------------------------------------------------------------ */
/* Caminhos e arquivos                                                 */
/* ------------------------------------------------------------------ */

static char* get_dir(const char* filepath, Arena *arena) {
    const char* separe = NULL;

    for(const char* peek = filepath; *peek; ++peek)
        if(*peek == '/' || *peek == '\\') separe = peek;

    if(!separe) return arena_strndup(arena, ".", 1);

    return arena_strndup(arena, filepath, (size_t)(separe - filepath));
}

static char* join_path(const char* dir, const char* filepath, Arena *arena) {
    size_t dir_len = strlen(dir);
    size_t file_len = strlen(filepath);
    char* path = (char*)arena_alloc(arena, dir_len + file_len + 2);
    memcpy(path, dir, dir_len);
    if(dir_len > 0 && dir[dir_len - 1] != '/' && dir[dir_len - 1] != '\\')
        path[dir_len++] = '/';

    memcpy(path + dir_len, filepath, file_len);
    path[dir_len + file_len] = '\0';
    return path;
}

static bool path_is_absolute(const char* filepath) {
    if(filepath[0] == '/' || filepath[0] == '\\') return true;
    if(filepath[0] != '\0' && filepath[1] == ':') return true;

    return false;
}

static char* canonical_path(const char* path, Arena *arena) {
    char buf[PATH_MAX];
#ifdef _WIN32
    if(_fullpath(buf, path, sizeof buf)) return arena_strdup(arena, buf);
#else
    if(realpath(path, buf)) return arena_strdup(arena, buf);
#endif
    return arena_strdup(arena, path);
}

/* Retorna NULL se nao conseguir abrir/ler. Encerra em erros graves. */
static char* read_content(const char* path, Arena *arena, size_t *out_len) {
    FILE* f = fopen(path, "rb");
    if(!f) return NULL;

    if(fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }

    long tell = ftell(f);

    if(tell < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    if((size_t)tell > MAX_CAP_PER_FILE) {
        fprintf(stderr,
            "Preprocess: arquivo '%s' excede o limite maximo por arquivo: "
            "%zu MB\n", path, (size_t)(MAX_CAP_PER_FILE / MB));
        fclose(f);
        exit(BUFFER_OVERFLOW);
    }

    char* buffer = (char*)arena_alloc(arena, (size_t)tell + 1);
    size_t nread = fread(buffer, 1, (size_t)tell, f);

    if(nread < (size_t)tell) {
        fprintf(stderr, "Preprocess: Bytes lidos sao menores que os bytes do arquivo '%s'\n", path);
        fclose(f);
        exit(READFILE_ERROR);
    }

    buffer[nread] = '\0';
    fclose(f);

    /* varias rotinas confiam no terminador '\0': um NUL no meio mudaria o comportamento */
    if(memchr(buffer, '\0', nread) != NULL) {
        fprintf(stderr, "Preprocess: arquivo '%s' contem byte NUL\n", path);
        exit(READFILE_ERROR);
    }

    if(nread >= 3 && (uint8_t)buffer[0] == 0xEF && (uint8_t)buffer[1] == 0xBB && (uint8_t)buffer[2] == 0xBF) {
        buffer += 3;
        nread -= 3;
    }

    *out_len = nread;
    return buffer;
}

static char* resolve_path(const char* root, const char* filepath, Arena *arena) {
    if(path_is_absolute(filepath)) {
        fprintf(stderr,
            "Preprocess: Caminho invalido no extract: '%s'\n", filepath);

        exit(WRONG_PATH);
    }

    return join_path(root, filepath, arena);
}

/* ------------------------------------------------------------------ */
/* use target -> single|multiple                                       */
/* ------------------------------------------------------------------ */

static IncludePolicy use_policy_directive(const char* src, const char** out) {
    IncludePolicy policy = TARGET_MULTIPLE;
    const char* peek = src;

    while(*peek == ' ' || *peek == '\t') peek++;

    if(strncmp(peek, "target", 6) != 0 || IS_IDC(peek[6])) {
        *out = src;
        return policy;
    }

    peek += 6;

    while(*peek == ' ' || *peek == '\t') peek++;

    if(peek[0] != '-' || peek[1] != '>') {
        *out = src;
        return policy;
    }

    peek += 2;

    while(*peek == ' ' || *peek == '\t') peek++;
    if(strncmp(peek, "single", 6) == 0 && !IS_IDC(peek[6])) {
        policy = TARGET_SINGLE;
        peek += 6;
    } else if(strncmp(peek, "multiple", 8) == 0 && !IS_IDC(peek[8])) {
        policy = TARGET_MULTIPLE;
        peek += 8;
    } else {
        fprintf(stderr, "Preprocess: Target do use invalido. Usos: 'use target -> single' ou 'use target -> multiple'\n");
        exit(INVALID_USE_TARGET);
    }

    while(*peek && *peek != '\n') peek++;
    if(*peek == '\n') peek++;

    *out = peek;
    return policy;
}

#define EXTRACT_USAGE "uso: extract -> [arquivo.lh]"

static bool target_is_single(const char* src, size_t len) {
    const char* end = src + len;
    const char* peek = src;

    for(;;) {
        while(peek < end && (*peek == ' ' || *peek == '\t' || *peek == '\r' || *peek == '\n')) peek++;

        if(peek + 1 < end && peek[0] == '/' && peek[1] == '/') {
            while(peek < end && *peek != '\n') peek++;
            continue;
        }

        if(is_comment_open(peek, end)) {
            peek = skip_block_comment(peek, end, NULL);
            continue;
        }

        break;
    }

    if(strncmp(peek, "use", 3) != 0 || IS_IDC(peek[3])) return false;

    const char* pass_key = peek + 3;
    while(*pass_key == ' ' || *pass_key == '\t') pass_key++;

    const char* pass = pass_key;
    IncludePolicy policy = use_policy_directive(pass_key, &pass);
    if(pass != pass_key) return policy == TARGET_SINGLE;

    return false;
}

/* ------------------------------------------------------------------ */
/* Contexto                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    const char* raw;
    size_t raw_len;
} ArgInfo;

typedef struct {
    StringBuilder norm;
    StringBuilder subst;
    StringBuilder exp[MAX_MACRO_PARAMS + 1];
    ArgInfo args[MAX_MACRO_PARAMS + 1];
} Level;

typedef struct {
    Arena* arena;
    const char* lib_dir;
    StringBuilder* sb;
    LineMap* map;

    Macro** table;
    size_t table_cap;
    size_t table_count;
    uint32_t first_mask[8];
    uint64_t len_mask;

    Level* levels[MAX_EXPANSION_DEPTH + 2];
    StringBuilder tmp;
    StringBuilder blk;

    IncludeFile* included;

    LinkItem* links;
    LinkItem* links_tail;
    size_t link_count;

    const char* cur_file;
    const char* cur_dir;
    const char* cur_end;
    size_t src_line;
    size_t out_line;
    unsigned counter;
    int include_depth;
} PP;

static void scan(PP* c, const char* p, const char* end, StringBuilder* dst, int depth);

static NORETURN PRINTF_FMT void pp_error(PP* c, int code, const char* fmt, ...) {
    va_list ap;
    fprintf(stderr, "Preprocess: %s:%zu: ", c->cur_file ? c->cur_file : "?", c->src_line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(code);
}

static NORETURN void depth_error(PP* c, Macro* m) {
    pp_error(c, MACRO_ERROR, "expansao de macros muito profunda (limite %d) em '%.*s'",
             MAX_EXPANSION_DEPTH, (int)m->name_len, m->name);
}

static bool path_equal(const char* a, const char* b) {
#ifdef _WIN32
    return _stricmp(a, b) == 0;
#else
    return strcmp(a, b) == 0;
#endif
}

static void link_add(PP* c, const char* path, LinkKind kind) {
    for(LinkItem* it = c->links; it; it = it->next)
        if(it->kind == kind && path_equal(it->path, path)) return;

    LinkItem* n = (LinkItem*)arena_alloc(c->arena, sizeof(LinkItem));
    n->path = arena_strdup(c->arena, path);
    n->kind = kind;
    n->next = NULL;

    if(c->links_tail) c->links_tail->next = n; else c->links = n;
    c->links_tail = n;
    c->link_count++;
}

static char* obj_path_for(PP* c, const char* resolved) {
    size_t n = strlen(resolved);
    char* o = (char*)arena_alloc(c->arena, n + 3);
    memcpy(o, resolved, n - 3);
    memcpy(o + n - 3, ".obj", 5);
    return o;
}

static bool include_verify(PP* c, const char* filepath) {
    for(IncludeFile* file = c->included; file != NULL; file = file->next)
        if(path_equal(file->path, filepath)) return true;

    return false;
}

static void include_add(PP* c, const char* path) {
    IncludeFile* file = (IncludeFile*)arena_alloc(c->arena, sizeof(IncludeFile));
    file->path = arena_strdup(c->arena, path);
    file->next = c->included;
    c->included = file;
}

static Level* get_level(PP* c, int depth) {
    Level* lv = c->levels[depth];
    if(UNLIKELY(!lv)) {
        lv = (Level*)calloc(1, sizeof(Level));
        if(!lv) {
            fprintf(stderr, "Preprocess: falha ao alocar nivel de expansao\n");
            exit(ALLOCATE_ERROR);
        }
        c->levels[depth] = lv;
    }
    return lv;
}

/* ------------------------------------------------------------------ */
/* Tabela de macros                                                    */
/* ------------------------------------------------------------------ */

static inline uint32_t hash_name(const char* s, size_t n) {
    uint32_t h = 2166136261u;                       /* FNV-1a */
    for(size_t i = 0; i < n; i++) {
        h ^= (uint8_t)s[i];
        h *= 16777619u;
    }
    return h;
}

static void table_grow(PP* c) {
    size_t ncap = c->table_cap ? c->table_cap * 2 : 64;
    Macro** nt = (Macro**)calloc(ncap, sizeof(Macro*));
    if(!nt) {
        fprintf(stderr, "Preprocess: falha ao alocar tabela de macros\n");
        exit(ALLOCATE_ERROR);
    }

    for(size_t i = 0; i < c->table_cap; i++) {
        for(Macro* m = c->table[i]; m; ) {
            Macro* next = m->next;
            size_t idx = m->hash & (ncap - 1);
            m->next = nt[idx];
            nt[idx] = m;
            m = next;
        }
    }

    free(c->table);
    c->table = nt;
    c->table_cap = ncap;
}

static Macro* macro_find(PP* c, const char* s, size_t n, uint32_t h) {
    if(!c->table_cap) return NULL;

    for(Macro* m = c->table[h & (c->table_cap - 1)]; m; m = m->next)
        if(m->hash == h && m->name_len == n && memcmp(m->name, s, n) == 0) return m;

    return NULL;
}

static inline Macro* macro_lookup(PP* c, const char* s, size_t n) {
    uint8_t f = (uint8_t)s[0];
    if(!(c->first_mask[f >> 5] & (1u << (f & 31)))) return NULL;
    if(!(c->len_mask & (1ULL << (n < 63 ? n : 63)))) return NULL;

    return macro_find(c, s, n, hash_name(s, n));
}

static void macro_define(PP* c, const Macro* def) {
    Macro tmp = *def;
    tmp.hash = hash_name(tmp.name, tmp.name_len);

    Macro* old = macro_find(c, tmp.name, tmp.name_len, tmp.hash);
    if(old) {
        bool same = old->kind == tmp.kind && old->nparams == tmp.nparams && old->value_len == tmp.value_len &&
                    (tmp.value_len == 0 || memcmp(old->value, tmp.value, tmp.value_len) == 0);
        if(!same)
            fprintf(stderr, "Preprocess: %s:%zu: aviso: macro '%.*s' redefinido\n",
                    c->cur_file, c->src_line, (int)tmp.name_len, tmp.name);

        Macro* next = old->next;
        *old = tmp;
        old->next = next;
        return;
    }

    /* fator de carga maximo 0.75 */
    if((c->table_count + 1) * 4 > c->table_cap * 3) table_grow(c);

    Macro* m = (Macro*)arena_alloc(c->arena, sizeof(Macro));
    *m = tmp;

    size_t idx = m->hash & (c->table_cap - 1);
    m->next = c->table[idx];
    c->table[idx] = m;
    c->table_count++;

    uint8_t f = (uint8_t)m->name[0];
    c->first_mask[f >> 5] |= 1u << (f & 31);
    c->len_mask |= 1ULL << (m->name_len < 63 ? m->name_len : 63);
}

static void builtin_define(PP* c, const char* name, MacroKind kind) {
    Macro m;
    memset(&m, 0, sizeof m);
    m.name = name;
    m.name_len = strlen(name);
    m.kind = kind;
    macro_define(c, &m);
}

static bool name_is_reserved(const char* s, size_t n) {
    return (n == 8  && memcmp(s, "__LINE__", 8) == 0) ||
           (n == 8  && memcmp(s, "__FILE__", 8) == 0) ||
           (n == 11 && memcmp(s, "__COUNTER__", 11) == 0);
}

static void write_newlines(PP* c, size_t n) {
    for(size_t i = 0; i < n; i++) string_builder_write_char(c->sb, '\n');
    c->out_line += n;
    c->src_line += n;
}

static bool value_has_ident(const char* b, size_t n) {
    size_t i = 0;
    while(i < n) {
        uint8_t ch = (uint8_t)b[i];

        if(ch == '"' || ch == '\'') { i = (size_t)(skip_literal(b + i, b + n) - b); continue; }
        if(CLS(ch) == C_IDENT) return true;
        if(CLS(ch) == C_DIGIT) {
            while(i < n && (IS_IDC(b[i]) || b[i] == '.')) i++;
            continue;
        }
        i++;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* preprocess NOME -> valor                                            */
/* ------------------------------------------------------------------ */

static const char* handle_preprocess(PP* c, const char* p) {
    const char* end = c->cur_end;

    while(*p == ' ' || *p == '\t') p++;
    if(CLS(*p) != C_IDENT)
        pp_error(c, INVALID_PREPROCESS, "esperado o nome do macro depois de 'preprocess'");

    const char* name = p;
    while(IS_IDC(*p)) p++;
    size_t name_len = (size_t)(p - name);

    if(name_is_reserved(name, name_len))
        pp_error(c, INVALID_PREPROCESS, "'%.*s' e um nome reservado", (int)name_len, name);

    if(*p == '(')
        pp_error(c, INVALID_PREPROCESS,
                 "'%.*s': 'preprocess' nao aceita parametros (use 'funcpreprocess' ou 'structpreprocess')",
                 (int)name_len, name);

    Macro m;
    memset(&m, 0, sizeof m);
    m.name = name;
    m.name_len = name_len;
    m.kind = MACRO_OBJECT;

    while(*p == ' ' || *p == '\t') p++;
    if(p[0] == '-' && p[1] == '>') {
        p += 2;
        while(*p == ' ' || *p == '\t') p++;
    } else if(!(*p == '\0' || *p == '\n' || *p == '\r' ||
                (p[0] == '/' && p[1] == '/') || is_comment_open(p, end))) {
        pp_error(c, INVALID_PREPROCESS, "esperado '->' depois de '%.*s'", (int)name_len, name);
    }

    StringBuilder* v = &c->tmp;
    v->len = 0;
    size_t consumed = 0;

    for(;;) {
        uint8_t ch = (uint8_t)*p;
        if(ch == '\0') break;
        if(ch == '\n') { p++; consumed++; break; }

        if(ch == '\\') {
            const char* q = p + 1;
            if(*q == '\r') q++;
            if(*q == '\n') {
                p = q + 1;
                consumed++;
                string_builder_write_char(v, ' ');
                continue;
            }
        }

        if(ch == '"' || ch == '\'') {
            const char* e = skip_literal(p, end);
            string_builder_write(v, p, (size_t)(e - p));
            p = e;
            continue;
        }

        if(ch == '/' && p[1] == '/') {
            while(*p && *p != '\n') p++;
            continue;
        }

        if(is_comment_open(p, end)) {
            p = skip_block_comment(p, end, &consumed);
            string_builder_write_char(v, ' ');
            continue;
        }

        string_builder_write_char(v, (char)ch);
        p++;
    }

    while(v->len && IS_WS(v->data[v->len - 1])) v->len--;

    size_t lead = 0;
    while(lead < v->len && IS_WS(v->data[lead])) lead++;
    size_t vlen = v->len - lead;

    char* val = (char*)arena_alloc(c->arena, vlen + 1);
    if(vlen) memcpy(val, v->data + lead, vlen);
    val[vlen] = '\0';
    m.value = val;
    m.value_len = vlen;
    m.has_ident = value_has_ident(val, vlen);

    macro_define(c, &m);

    write_newlines(c, consumed);
    return p;
}

/* ------------------------------------------------------------------ */
/* funcpreprocess / structpreprocess                                   */
/* ------------------------------------------------------------------ */

/* O fechamento '<-' so e aceito quando:
     - esta no inicio do corpo ou precedido de espaco/tab/newline, e
     - depois dele so ha espacos ate o fim da linha (ou do arquivo),
       opcionalmente seguido de um comentario '//'.
   Assim 'if(x<-1)' e 'x <- 1' no meio do codigo nao fecham o bloco. */
static const char* find_block_end(const char* p, const char* end, const char* start) {
    while(p < end) {
        char ch = *p;

        if(ch == '"' || ch == '\'') { p = skip_literal(p, end); continue; }

        if(ch == '/' && p + 1 < end && p[1] == '/') {
            while(p < end && *p != '\n') p++;
            continue;
        }

        if(is_comment_open(p, end)) { p = skip_block_comment(p, end, NULL); continue; }

        if(ch == '<' && p + 1 < end && p[1] == '-') {
            bool before_ok = (p == start) || p[-1] == ' ' || p[-1] == '\t' ||
                             p[-1] == '\n' || p[-1] == '\r';
            if(before_ok) {
                const char* q = p + 2;
                while(q < end && (*q == ' ' || *q == '\t' || *q == '\r')) q++;
                if(q >= end || *q == '\n' || (q + 1 < end && q[0] == '/' && q[1] == '/'))
                    return p;
            }
        }
        p++;
    }
    return NULL;
}

static int param_index(const char* s, size_t n, const char* pn[], const size_t pl[], int np) {
    for(int i = 0; i < np; i++)
        if(pl[i] == n && memcmp(pn[i], s, n) == 0) return i;
    return -1;
}

/* Parametros no corpo:
     #{nome}  -> sempre delimitado (permite '#{T}_novo', '#{T}s')
     #nome    -> le o identificador INTEIRO e so substitui se for exatamente um parametro
   Logo, com parametros T e Type, '#Type_new' nao vira 'T' + 'ype_new': use '#{Type}_new'. */
static void compile_template(PP* c, Macro* m, const char* pn[], const size_t pl[]) {
    const char* b = m->value;
    size_t n = m->value_len;

    MacroPart* tmp = (MacroPart*)malloc((2 * n + 2) * sizeof(MacroPart));
    if(!tmp) {
        fprintf(stderr, "Preprocess: falha ao alocar corpo de modelo\n");
        exit(ALLOCATE_ERROR);
    }

    size_t np = 0, i = 0, ts = 0;

#define FLUSH_TEXT(upto) do { \
        size_t u_ = (upto); \
        if(u_ > ts) { tmp[np].text = b + ts; tmp[np].len = (uint32_t)(u_ - ts); \
                      tmp[np].param = -1; tmp[np].kind = PART_TEXT; np++; } \
    } while(0)

    while(i < n) {
        uint8_t ch = (uint8_t)b[i];

        if(ch == '"' || ch == '\'') { i = (size_t)(skip_literal(b + i, b + n) - b); continue; }

        if(ch == '#' && m->nparams > 0) {
            int best = -1;
            size_t consume = 0;

            if(i + 1 < n && b[i + 1] == '{') {
                size_t j = i + 2;
                while(j < n && IS_IDC(b[j])) j++;
                if(j < n && b[j] == '}' && j > i + 2) {
                    int k = param_index(b + i + 2, j - (i + 2), pn, pl, m->nparams);
                    if(k >= 0) { best = k; consume = j + 1 - i; }
                }
            } else {
                size_t j = i + 1;
                while(j < n && IS_IDC(b[j])) j++;
                if(j > i + 1) {
                    int k = param_index(b + i + 1, j - (i + 1), pn, pl, m->nparams);
                    if(k >= 0) { best = k; consume = j - i; }
                }
            }

            if(best >= 0) {
                FLUSH_TEXT(i);
                tmp[np].text = NULL; tmp[np].len = 0; tmp[np].param = best; tmp[np].kind = PART_PARAM; np++;
                i += consume;
                ts = i;
                continue;
            }
        }

        i++;
    }
    FLUSH_TEXT(n);
#undef FLUSH_TEXT

    m->nparts = np;
    m->parts = NULL;
    if(np) {
        m->parts = (MacroPart*)arena_alloc(c->arena, np * sizeof(MacroPart));
        memcpy(m->parts, tmp, np * sizeof(MacroPart));
    }
    free(tmp);
}

static const char* find_call_end(const char* open, const char* end, bool* needs_norm, size_t* nl) {
    int depth = 0;
    const char* s = open;

    while(s < end) {
        switch((uint8_t)*s) {
        case '(': depth++; s++; break;
        case ')': if(--depth == 0) return s + 1; s++; break;
        case '"': case '\'': s = skip_literal(s, end); break;
        case '\n': (*nl)++; *needs_norm = true; s++; break;
        case '/':
            if(s + 1 < end && s[1] == '/') {
                *needs_norm = true;
                s += 2;
                while(s < end && *s != '\n') s++;
            } else if(is_comment_open(s, end)) {
                *needs_norm = true;
                s = skip_block_comment(s, end, nl);
            } else {
                s++;
            }
            break;
        default: s++;
        }
    }

    return NULL;
}

static void normalize_call(StringBuilder* out, const char* s, const char* e) {
    out->len = 0;
    while(s < e) {
        uint8_t ch = (uint8_t)*s;

        if(ch == '"' || ch == '\'') {
            const char* q = skip_literal(s, e);
            string_builder_write(out, s, (size_t)(q - s));
            s = q;
        } else if(ch == '\n' || ch == '\r') {
            string_builder_write_char(out, ' ');
            s++;
        } else if(ch == '/' && s + 1 < e && s[1] == '/') {
            while(s < e && *s != '\n') s++;
            string_builder_write_char(out, ' ');
        } else if(is_comment_open(s, e)) {
            s = skip_block_comment(s, e, NULL);
            string_builder_write_char(out, ' ');
        } else {
            string_builder_write_char(out, (char)ch);
            s++;
        }
    }
}

static inline void trim_span(const char** s, const char** e) {
    while(*s < *e && IS_WS(**s)) (*s)++;
    while(*e > *s && IS_WS((*e)[-1])) (*e)--;
}

static const char* parse_call(PP* c, Macro* m, Level* lv, const char* open, const char* end, size_t* nl_out) {
    bool needs_norm = false;
    size_t nl = 0;

    const char* close = find_call_end(open, end, &needs_norm, &nl);
    if(!close)
        pp_error(c, MACRO_ERROR, "chamada de '%.*s' sem ')' de fechamento", (int)m->name_len, m->name);

    const char* inner = open + 1;
    const char* inner_end = close - 1;

    if(needs_norm) {
        normalize_call(&lv->norm, inner, inner_end);
        inner = lv->norm.data;
        inner_end = inner + lv->norm.len;
    }

    int count = 0;
    const char* s = inner;
    const char* t = inner;
    int pd = 0;   /* profundidade de (), [] e {} */

    for(;;) {
        while(t < inner_end) {
            uint8_t ch = (uint8_t)*t;
            if(ch == '"' || ch == '\'') { t = skip_literal(t, inner_end); continue; }
            if(ch == '(' || ch == '[' || ch == '{') pd++;
            else if(ch == ')' || ch == ']' || ch == '}') pd--;
            else if(ch == ',' && pd == 0) break;
            t++;
        }

        if(count >= MAX_MACRO_PARAMS)
            pp_error(c, MACRO_ERROR, "argumentos demais na chamada de '%.*s'", (int)m->name_len, m->name);

        const char* as = s;
        const char* ae = t;
        trim_span(&as, &ae);
        lv->args[count].raw = as;
        lv->args[count].raw_len = (size_t)(ae - as);
        count++;

        if(t >= inner_end) break;
        t++;
        s = t;
    }

    if(m->nparams == 0) {
        if(!(count == 1 && lv->args[0].raw_len == 0))
            pp_error(c, MACRO_ERROR, "'%.*s' nao recebe argumentos", (int)m->name_len, m->name);
    } else if(count != m->nparams) {
        pp_error(c, MACRO_ERROR, "'%.*s' espera %d argumento(s), recebeu %d",
                 (int)m->name_len, m->name, m->nparams, count);
    }

    *nl_out = nl;
    return close;
}

/* true se o argumento contem algum identificador que seja uma macro expansivel agora */
static bool arg_has_macro(PP* c, const char* s, size_t n) {
    size_t i = 0;
    while(i < n) {
        uint8_t ch = (uint8_t)s[i];

        if(ch == '"' || ch == '\'') { i = (size_t)(skip_literal(s + i, s + n) - s); continue; }

        if(CLS(ch) == C_DIGIT) {
            while(i < n && (IS_IDC(s[i]) || s[i] == '.')) i++;
            continue;
        }

        if(CLS(ch) == C_IDENT) {
            size_t st = i;
            while(i < n && IS_IDC(s[i])) i++;
            Macro* m = macro_lookup(c, s + st, i - st);
            if(m && !m->disabled) return true;
            continue;
        }

        i++;
    }
    return false;
}

static void apply_macro(PP* c, Macro* m, Level* lv, int depth, StringBuilder* dst) {
    if(UNLIKELY(depth + 1 >= MAX_EXPANSION_DEPTH)) depth_error(c, m);

    /* prescan: expande os argumentos ANTES de desabilitar 'm' (como em C).
       Permite MAX(MAX(a,b),c) e faz __COUNTER__ ser avaliado uma unica vez. */
    for(int i = 0; i < m->nparams; i++) {
        ArgInfo* a = &lv->args[i];
        if(a->raw_len == 0 || !arg_has_macro(c, a->raw, a->raw_len)) continue;

        StringBuilder* ex = &lv->exp[i];
        ex->len = 0;
        scan(c, a->raw, a->raw + a->raw_len, ex, depth + 1);
        a->raw = ex->data;
        a->raw_len = ex->len;
    }

    StringBuilder* out = &lv->subst;
    out->len = 0;

    for(size_t i = 0; i < m->nparts; i++) {
        const MacroPart* pt = &m->parts[i];
        if(pt->kind == PART_TEXT) {
            string_builder_write(out, pt->text, pt->len);
        } else {
            const ArgInfo* a = &lv->args[pt->param];
            string_builder_write(out, a->raw, a->raw_len);
        }
    }

    if(out->len == 0) return;

    m->disabled = true;
    scan(c, out->data, out->data + out->len, dst, depth + 1);
    m->disabled = false;
}

static const char* handle_template(PP* c, const char* p, const char* kw) {
    const char* end = c->cur_end;

    while(*p == ' ' || *p == '\t') p++;
    if(CLS(*p) != C_IDENT)
        pp_error(c, INVALID_PREPROCESS, "esperado o nome do modelo depois de '%s'", kw);

    const char* name = p;
    while(IS_IDC(*p)) p++;
    size_t name_len = (size_t)(p - name);

    if(name_is_reserved(name, name_len))
        pp_error(c, INVALID_PREPROCESS, "'%.*s' e um nome reservado", (int)name_len, name);

    Macro m;
    memset(&m, 0, sizeof m);
    m.name = name;
    m.name_len = name_len;
    m.kind = MACRO_OBJECT;

    const char* pn[MAX_MACRO_PARAMS];
    size_t pl[MAX_MACRO_PARAMS];
    int np = 0;

    while(*p == ' ' || *p == '\t') p++;

    if(*p == '(') {
        m.kind = MACRO_FUNCTION;
        p++;
        while(*p == ' ' || *p == '\t') p++;

        if(*p == ')') {
            p++;
        } else {
            for(;;) {
                while(*p == ' ' || *p == '\t') p++;

                if(CLS(*p) != C_IDENT)
                    pp_error(c, INVALID_PREPROCESS, "parametro invalido na lista de '%.*s'", (int)name_len, name);

                const char* ps = p;
                while(IS_IDC(*p)) p++;
                size_t plen = (size_t)(p - ps);

                if(np >= MAX_MACRO_PARAMS)
                    pp_error(c, INVALID_PREPROCESS, "parametros demais em '%.*s'", (int)name_len, name);
                if(param_index(ps, plen, pn, pl, np) >= 0)
                    pp_error(c, INVALID_PREPROCESS, "parametro '%.*s' repetido em '%.*s'",
                             (int)plen, ps, (int)name_len, name);

                pn[np] = ps; pl[np] = plen; np++;

                while(*p == ' ' || *p == '\t') p++;
                if(*p == ',') { p++; continue; }
                if(*p == ')') { p++; break; }
                pp_error(c, INVALID_PREPROCESS, "esperado ',' ou ')' na lista de parametros de '%.*s'", (int)name_len, name);
            }
        }
    }
    m.nparams = np;

    while(*p == ' ' || *p == '\t') p++;
    if(p[0] != '-' || p[1] != '>')
        pp_error(c, INVALID_PREPROCESS, "esperado '->' depois de '%.*s'", (int)name_len, name);
    p += 2;

    const char* body = p;
    const char* term = find_block_end(body, end, body);
    if(!term)
        pp_error(c, INVALID_PREPROCESS,
                 "'%.*s': bloco de '%s' sem '<-' de fechamento (o '<-' deve ficar no fim de uma linha)",
                 (int)name_len, name, kw);

    StringBuilder* b = &c->blk;
    b->len = 0;
    size_t nl = 0;

    /* copia o corpo: junta linhas terminadas em '\', remove comentarios,
       preserva literais. Toda quebra de linha do trecho original e contada em 'nl'
       para manter a numeracao de linhas. */
    for(const char* q = body; q < term; ) {
        char ch = *q;

        if(ch == '\\') {
            const char* r = q + 1;
            if(r < term && *r == '\r') r++;
            if(r < term && *r == '\n') {
                nl++;
                string_builder_write_char(b, ' ');
                q = r + 1;
                continue;
            }
        }

        if(ch == '"' || ch == '\'') {
            const char* e = skip_literal(q, term);
            string_builder_write(b, q, (size_t)(e - q));
            q = e;
            continue;
        }

        if(ch == '/' && q + 1 < term && q[1] == '/') {
            while(q < term && *q != '\n') q++;
            continue;
        }

        if(is_comment_open(q, term)) {
            q = skip_block_comment(q, term, &nl);
            string_builder_write_char(b, ' ');
            continue;
        }

        if(ch == '\n') nl++;
        string_builder_write_char(b, ch);
        q++;
    }

    size_t lead = 0, tail = b->len;
    while(lead < tail && (IS_WS(b->data[lead]) || b->data[lead] == '\n')) lead++;
    while(tail > lead && (IS_WS(b->data[tail - 1]) || b->data[tail - 1] == '\n')) tail--;

    if(tail == lead)
        pp_error(c, INVALID_PREPROCESS, "'%.*s': bloco de '%s' vazio", (int)name_len, name, kw);

    size_t vlen = tail - lead;
    char* val = (char*)arena_alloc(c->arena, vlen + 1);
    memcpy(val, b->data + lead, vlen);
    val[vlen] = '\0';
    m.value = val;
    m.value_len = vlen;
    m.has_ident = value_has_ident(val, vlen);

    if(m.kind == MACRO_FUNCTION) compile_template(c, &m, pn, pl);
    macro_define(c, &m);

    write_newlines(c, nl);
    return term + 2;
}

/* ------------------------------------------------------------------ */
/* extract                                                             */
/* ------------------------------------------------------------------ */

static void process_file(PP* c, const char* filepath, const char* src, size_t len);

static bool header_needs_obj(const char* src, size_t len, bool* has_dll_out) {
    const char* p = src;
    const char* end = src + len;
    int depth = 0;
    bool has_proto = false, has_dll = false;
    bool has_paren = false, has_assign = false;
    char last = 0;

    while(p < end) {
        char ch = *p;

        if(ch == '"' || ch == '\'') { p = skip_literal(p, end); last = ch; continue; }
        if(ch == '/' && p + 1 < end && p[1] == '/') { while(p < end && *p != '\n') p++; continue; }
        if(is_comment_open(p, end)) { p = skip_block_comment(p, end, NULL); continue; }

        if(CLS(ch) == C_IDENT) {
            const char* s = p;
            while(p < end && IS_IDC(*p)) p++;

            if(depth == 0 && p - s == 7 && memcmp(s, "extract", 7) == 0) {
                const char* q = p;
                while(q < end && (*q == ' ' || *q == '\t')) q++;
                if(q + 1 < end && q[0] == '-' && q[1] == '>') {
                    q += 2;
                    while(q < end && (*q == ' ' || *q == '\t')) q++;
                    if(q < end && *q == '[') {
                        const char* cl = (const char*)memchr(q, ']', (size_t)(end - q));
                        const char* nl = (const char*)memchr(q, '\n', (size_t)(end - q));
                        bool is_lh = cl && cl - q >= 4 && memcmp(cl - 3, ".lh", 3) == 0;
                        if(cl && (!nl || cl < nl) && !is_lh) has_dll = true;
                    }
                }
                while(p < end && *p != '\n') p++;   /* ignora o resto da linha */
                continue;
            }

            last = 'a';
            continue;
        }

        if(ch == '{') {
            if(depth++ == 0) { has_paren = has_assign = false; last = 0; }
        } else if(ch == '}') {
            if(depth > 0 && --depth == 0) { has_paren = has_assign = false; last = 0; }
        } else if(depth == 0) {
            if(ch == '(') has_paren = true;
            else if(ch == '=') has_assign = true;
            else if(ch == ';') {
                if(has_paren && last == ')' && !has_assign) has_proto = true;
                has_paren = has_assign = false;
                last = 0;
                p++;
                continue;
            }
            if(!IS_WS(ch) && ch != '\n') last = ch;
        }
        p++;
    }

    if(has_dll_out) *has_dll_out = has_dll;
    return has_proto;
}

static char* target_extract_directive(PP* c, const char** ptr, ExtractType* type, bool* is_dll) {
    const char* p = *ptr;

    while(*p == ' ' || *p == '\t') p++;
    if(p[0] != '-' || p[1] != '>')
        pp_error(c, INVALID_EXTRACT_TARGET, "sintaxe invalida para o extract (%s)", EXTRACT_USAGE);
    p += 2;

    while(*p == ' ' || *p == '\t') p++;

    char close;
    if(*p == '"')      { *type = USER_DIRECTIVE;    close = '"'; }
    else if(*p == '[') { *type = STANDARD_LIBRARY;  close = ']'; }
    else
        pp_error(c, INVALID_EXTRACT_TARGET, "sintaxe invalida para o extract (%s)", EXTRACT_USAGE);
    p++;

    while(*p == ' ' || *p == '\t') p++;
    const char* start = p;
    while(*p && *p != close && *p != '\n' && *p != '\r') p++;

    if(*p != close)
        pp_error(c, INVALID_EXTRACT_TARGET, "delimitador '%c' do extract nao encontrado (%s)",
                 close, EXTRACT_USAGE);

    const char* stop = p;
    while(stop > start && (stop[-1] == ' ' || stop[-1] == '\t')) stop--;
    size_t len = (size_t)(stop - start);
    p++;

    if(len == 0)
        pp_error(c, INVALID_EXTRACT_TARGET, "extract vazio (%s)", EXTRACT_USAGE);

    bool has_lh = len > 3 && memcmp(stop - 3, ".lh", 3) == 0;
    char* target = arena_strndup(c->arena, start, len);
    *is_dll = false;

    if(*type == USER_DIRECTIVE) {
        if(!has_lh)
            pp_error(c, INVALID_EXTRACT_TARGET, "o extract exige a extensao '.lh': '%s' (%s)",
                     target, EXTRACT_USAGE);
    } else if(!has_lh) {
        for(size_t i = 0; i < len; i++) {
            if(target[i] >= 'A' && target[i] <= 'Z') target[i] += 32;
            if(!IS_IDC(target[i]) && target[i] != '-' && target[i] != '.')
                pp_error(c, INVALID_EXTRACT_TARGET, "nome de dll invalido: '%s'", target);
        }

        if(len > 4 && strcmp(target + len - 4, ".dll") == 0) target[len -= 4] = '\0';
        if(strcmp(target, "kernel") == 0) target = arena_strdup(c->arena, "kernel32");

        *is_dll = true;
    }

    *ptr = p;
    return target;
}

static const char* handle_extract(PP* c, const char* p) {
    while(*p == ' ' || *p == '\t') p++;

    ExtractType type;
    bool is_dll;
    char* target = target_extract_directive(c, &p, &type, &is_dll);

    while(*p && *p != '\n') p++;
    size_t after_line = c->src_line;
    if(*p == '\n') { p++; after_line++; }

    if(is_dll) {
        link_add(c, target, LINK_DLL);
        if(after_line > c->src_line) write_newlines(c, 1);
        return p;
    }

    char* resolved;
    if(type == STANDARD_LIBRARY) {
        if(!c->lib_dir)
            pp_error(c, INVALID_EXTRACT_TARGET, "stdlib nao configurada, impossivel resolver [%s]", target);

        resolved = resolve_path(c->lib_dir, target, c->arena);
    } else {
        resolved = resolve_path(c->cur_dir, target, c->arena);
    }

    char* canon = canonical_path(resolved, c->arena);

    if(!include_verify(c, canon)) {
        size_t flen = 0;
        char* content = read_content(resolved, c->arena, &flen);
        if(!content) {
            if(type == STANDARD_LIBRARY)
                pp_error(c, READFILE_ERROR, "'%s' nao encontrado na stdlib ('%s')", target, c->lib_dir);
            pp_error(c, READFILE_ERROR, "'%s' nao encontrado em '%s'", target, c->cur_dir);
        }

        if(target_is_single(content, flen)) include_add(c, canon);

        if(type == STANDARD_LIBRARY) {
            char* obj = obj_path_for(c, resolved);
            FILE* f = fopen(obj, "rb");

            if(f) {
                fclose(f);
                link_add(c, canonical_path(obj, c->arena), LINK_OBJECT);
            } else {
                bool header_has_dll = false;
                if(header_needs_obj(content, flen, &header_has_dll)) {
                    if(header_has_dll) {
                        fprintf(stderr,
                                "Preprocess: %s:%zu: aviso: '%s' declara funcoes sem corpo e o objeto '%s' "
                                "nao foi encontrado; se algumas nao forem da dll, havera 'undefined reference' no link\n",
                                c->cur_file, c->src_line, target, obj);
                    } else {
                        pp_error(c, READFILE_ERROR,
                                 "'%s' declara funcoes sem corpo, mas o objeto '%s' nao foi encontrado "
                                 "(compile a stdlib antes)", target, obj);
                    }
                }
            }
        }

        process_file(c, resolved, content, flen);

        if(c->sb->len > 0 && c->sb->data[c->sb->len - 1] != '\n') {
            string_builder_write_char(c->sb, '\n');
            c->out_line++;
        }
    }

    c->src_line = after_line;
    linemap_push(c->map, c->arena, c->out_line + 1, c->src_line, c->cur_file);
    return p;
}

/* ------------------------------------------------------------------ */
/* Expansao                                                            */
/* ------------------------------------------------------------------ */

static void emit_builtin(PP* c, Macro* m, StringBuilder* dst) {
    char num[32];

    switch(m->kind) {
    case MACRO_BUILTIN_LINE: {
        int n = snprintf(num, sizeof num, "%zu", c->src_line);
        string_builder_write(dst, num, (size_t)n);
        break;
    }
    case MACRO_BUILTIN_COUNTER: {
        int n = snprintf(num, sizeof num, "%u", c->counter++);
        string_builder_write(dst, num, (size_t)n);
        break;
    }
    case MACRO_BUILTIN_FILE: {
        string_builder_write_char(dst, '"');
        for(const char* f = c->cur_file; *f; f++) {
            if(*f == '\\' || *f == '"') string_builder_write_char(dst, '\\');
            string_builder_write_char(dst, *f);
        }
        string_builder_write_char(dst, '"');
        break;
    }
    default: break;
    }
}

/* Copy-through: o texto que nao muda (identificadores nao-macro, espacos, numeros,
   literais, comentarios de linha, quebras de linha) NAO e copiado token a token.
   'flush' marca o inicio do trecho ainda nao escrito; ele so e despejado
   (FLUSH_TO) quando algo precisa ser alterado: macro, comentario de bloco, diretiva. */
static void scan(PP* c, const char* p, const char* end, StringBuilder* dst, int depth) {
    const bool top = (depth == 0);
    bool bol = top;
    const char* flush = p;

#define FLUSH_TO(upto) do { \
        const char* u_ = (upto); \
        if(u_ > flush) string_builder_write(dst, flush, (size_t)(u_ - flush)); \
    } while(0)

    while(p < end) {
        const char* run = p;
        while(p < end && CLS(*p) == C_OTHER) p++;
        if(p > run) {
            bol = false;
            if(p >= end) break;
        }

        switch(CLS(*p)) {

        case C_WS:
            while(p < end && CLS(*p) == C_WS) p++;
            break;

        case C_NL:
            p++;
            if(top) { c->src_line++; c->out_line++; bol = true; }
            break;

        case C_QUOTE:
        case C_APOS:
            p = skip_literal(p, end);
            bol = false;
            break;

        case C_SLASH:
            if(p + 1 < end && p[1] == '/') {
                while(p < end && *p != '\n') p++;
            } else if(is_comment_open(p, end)) {
                const char* start = p;
                size_t nl = 0;
                p = skip_block_comment(p, end, &nl);

                if(top && ((size_t)(p - start) < COMMENT_OPEN_LEN + COMMENT_CLOSE_LEN ||
                        memcmp(p - COMMENT_CLOSE_LEN, COMMENT_CLOSE, COMMENT_CLOSE_LEN) != 0))
                    pp_error(c, INVALID_PREPROCESS, "comentario '/comment' sem 'endcomment/' de fechamento");

                FLUSH_TO(start);
                string_builder_write_char(dst, ' ');
                for(size_t k = 0; k < nl; k++) string_builder_write_char(dst, '\n');
                if(top) { c->src_line += nl; c->out_line += nl; }
                flush = p;
            } else {
                p++;
                bol = false;
            }
            break;

        case C_DIGIT:
            while(p < end) {
                char ch = *p;
                if(ch == '.' && p + 1 < end && p[1] == '.') break;   /* '0..N' nao engole o '..' */
                if(IS_IDC(ch) || ch == '.') {
                    if((ch == 'e' || ch == 'E' || ch == 'p' || ch == 'P') &&
                       p + 1 < end && (p[1] == '+' || p[1] == '-')) p++;
                    p++;
                } else break;
            }
            bol = false;
            break;

        default: {
            const char* s = p++;
            while(p < end && IS_IDC(*p)) p++;
            size_t n = (size_t)(p - s);

            if(top && bol) {
                if(n == 10 && memcmp(s, "preprocess", 10) == 0) {
                    FLUSH_TO(s);
                    p = handle_preprocess(c, p);
                    flush = p;
                    bol = true;
                    break;
                }
                if(n == 14 && memcmp(s, "funcpreprocess", 14) == 0) {
                    FLUSH_TO(s);
                    p = handle_template(c, p, "funcpreprocess");
                    flush = p;
                    bol = false;
                    break;
                }
                if(n == 16 && memcmp(s, "structpreprocess", 16) == 0) {
                    FLUSH_TO(s);
                    p = handle_template(c, p, "structpreprocess");
                    flush = p;
                    bol = false;
                    break;
                }
                if(n == 7 && memcmp(s, "extract", 7) == 0) {
                    FLUSH_TO(s);
                    p = handle_extract(c, p);
                    flush = p;
                    bol = true;
                    break;
                }
                if(n == 3 && memcmp(s, "use", 3) == 0) {
                    const char* q = p;
                    while(*q == ' ' || *q == '\t') q++;
                    const char* after = q;
                    use_policy_directive(q, &after);
                    if(after != q) {
                        FLUSH_TO(s);
                        p = after;
                        write_newlines(c, 1);
                        flush = p;
                        bol = true;
                        break;
                    }
                }
            }
            bol = false;

            Macro* m = macro_lookup(c, s, n);
            if(!m || m->disabled) break;               /* passa direto, sem copiar agora */

            const char* resume = p;
            size_t nl = 0;
            Level* lv = NULL;

            if(m->kind == MACRO_FUNCTION) {
                const char* r = p;
                size_t pre = 0;
                while(r < end && (CLS(*r) == C_WS || CLS(*r) == C_NL)) {
                    if(CLS(*r) == C_NL) pre++;
                    r++;
                }

                if(r >= end || *r != '(') break;       /* nome sem '(': texto normal */

                lv = get_level(c, depth);
                size_t inner_nl = 0;
                resume = parse_call(c, m, lv, r, end, &inner_nl);
                nl = pre + inner_nl;
            }

            FLUSH_TO(s);
            size_t before = dst->len;

            if(m->kind >= MACRO_BUILTIN_LINE) {
                emit_builtin(c, m, dst);
            } else if(m->kind == MACRO_OBJECT) {
                if(m->value_len == 0) {
                } else if(!m->has_ident) {
                    string_builder_write(dst, m->value, m->value_len);
                } else {
                    if(UNLIKELY(depth + 1 >= MAX_EXPANSION_DEPTH)) depth_error(c, m);
                    m->disabled = true;
                    scan(c, m->value, m->value + m->value_len, dst, depth + 1);
                    m->disabled = false;
                }
            } else {
                apply_macro(c, m, lv, depth, dst);
            }

            if(top) {
                size_t emitted = 0;
                for(size_t k = before; k < dst->len; k++) if(dst->data[k] == '\n') emitted++;
                for(size_t k = 0; k < nl; k++) string_builder_write_char(dst, '\n');
                c->out_line += emitted + nl;
                c->src_line += nl;
                if(emitted) linemap_push(c->map, c->arena, c->out_line + 1, c->src_line, c->cur_file);
            }

            p = resume;
            flush = p;
            break;
        }
        }
    }

    FLUSH_TO(end);
#undef FLUSH_TO
}

static void process_file(PP* c, const char* filepath, const char* src, size_t len) {
    if(c->include_depth >= MAX_INCLUDE_DEPTH)
        pp_error(c, INCLUDE_DEPTH_ERROR, "extract aninhado demais (limite %d): possivel ciclo com 'use target -> multiple'",
                 MAX_INCLUDE_DEPTH);

    const char* saved_file = c->cur_file;
    const char* saved_dir = c->cur_dir;
    const char* saved_end = c->cur_end;
    size_t saved_line = c->src_line;

    c->include_depth++;
    c->cur_file = filepath;
    c->cur_dir = get_dir(filepath, c->arena);
    c->cur_end = src + len;
    c->src_line = 1;

    linemap_push(c->map, c->arena, c->out_line + 1, 1, filepath);
    scan(c, src, src + len, c->sb, 0);

    c->include_depth--;
    c->cur_file = saved_file;
    c->cur_dir = saved_dir;
    c->cur_end = saved_end;
    c->src_line = saved_line;
}

void preprocess_run(Preprocess *pp, const char* filepath, const char* lib_dir,
                    Arena *arena, LineMap *map) {
    tables_init();
    if(map) linemap_zero_init(map);

    PP* c = (PP*)calloc(1, sizeof(PP));
    if(!c) {
        fprintf(stderr, "Preprocess: falha ao alocar contexto\n");
        exit(ALLOCATE_ERROR);
    }

    c->arena = arena;
    c->lib_dir = lib_dir;
    c->map = map;

    builtin_define(c, "__LINE__", MACRO_BUILTIN_LINE);
    builtin_define(c, "__FILE__", MACRO_BUILTIN_FILE);
    builtin_define(c, "__COUNTER__", MACRO_BUILTIN_COUNTER);

    size_t len = 0;
    char* src = read_content(filepath, arena, &len);
    if(!src) {
        fprintf(stderr, "Preprocess: nao foi possivel abrir '%s'\n", filepath);
        exit(READFILE_ERROR);
    }

    StringBuilder sb;
    size_t cap = len + len / 4 + STRING_BUILDER_INIT_CAPACITY;
    if(cap > (size_t)MAX_CAP) cap = (size_t)MAX_CAP;
    sb_init(&sb, NULL, cap);
    c->sb = &sb;

    if(target_is_single(src, len)) include_add(c, canonical_path(filepath, arena));

    pp->original_src = src;
    process_file(c, arena_strdup(arena, filepath), src, len);

    pp->links = c->links;
    pp->link_count = c->link_count;

    pp->process_src = arena_strndup(arena, sb.data, sb.len);
    pp->process_len = sb.len;
    free(sb.data);

    for(int i = 0; i < MAX_EXPANSION_DEPTH + 2; i++) {
        Level* lv = c->levels[i];
        if(!lv) continue;
        free(lv->norm.data);
        free(lv->subst.data);
        for(int k = 0; k <= MAX_MACRO_PARAMS; k++) free(lv->exp[k].data);
        free(lv);
    }

    free(c->tmp.data);
    free(c->blk.data);
    free(c->table);
    free(c);
}
