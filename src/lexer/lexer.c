#include "../include/lexer.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef LEXER_COMMENTS
#define LEXER_COMMENTS 1
#endif

/* ------------------------------------------------------------------ */
/* Macros de compilador                                                */
/* ------------------------------------------------------------------ */
#if defined(__GNUC__) || defined(__clang__)
#  define LX_LIKELY(x)   __builtin_expect(!!(x), 1)
#  define LX_UNLIKELY(x) __builtin_expect(!!(x), 0)
#  define LX_COLD        __attribute__((cold, noinline))
#  define LX_INLINE      static inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#  define LX_LIKELY(x)   (x)
#  define LX_UNLIKELY(x) (x)
#  define LX_COLD        __declspec(noinline)
#  define LX_INLINE      static __forceinline
#else
#  define LX_LIKELY(x)   (x)
#  define LX_UNLIKELY(x) (x)
#  define LX_COLD
#  define LX_INLINE      static inline
#endif

/* ------------------------------------------------------------------ */
/* Tabela de classificacao (256 bytes, 4 cache lines, 100% em .rodata) */
/* ------------------------------------------------------------------ */
enum {
    CT_DIGIT   = 0x01,  /* 0-9                                  */
    CT_IDSTART = 0x02,  /* a-z A-Z _                            */
    CT_IDENT   = 0x04,  /* a-z A-Z _ 0-9                        */
    CT_HEX     = 0x08,  /* 0-9 a-f A-F                          */
    CT_BLANK   = 0x10,  /* ' ' \t \r \f \v (nao inclui '\n')    */
    CT_STOP    = 0x20   /* '"' '\\' '\0' '\n' '\r' (fim de run em string) */
};

#define X0 0
#define XB CT_BLANK
#define XS CT_STOP
#define XR (CT_BLANK | CT_STOP)
#define XL (CT_IDSTART | CT_IDENT)
#define XH (CT_IDSTART | CT_IDENT | CT_HEX)
#define XD (CT_DIGIT | CT_IDENT | CT_HEX)

/* bytes 0x80..0xFF ficam 0 (inicializacao implicita) */
static const uint8_t lx_ctype[256] = {
/*00*/ XS,X0,X0,X0,X0,X0,X0,X0,X0,XB,XS,XB,XB,XR,X0,X0,
/*10*/ X0,X0,X0,X0,X0,X0,X0,X0,X0,X0,X0,X0,X0,X0,X0,X0,
/*20*/ XB,X0,XS,X0,X0,X0,X0,X0,X0,X0,X0,X0,X0,X0,X0,X0,
/*30*/ XD,XD,XD,XD,XD,XD,XD,XD,XD,XD,X0,X0,X0,X0,X0,X0,
/*40*/ X0,XH,XH,XH,XH,XH,XH,XL,XL,XL,XL,XL,XL,XL,XL,XL,
/*50*/ XL,XL,XL,XL,XL,XL,XL,XL,XL,XL,XL,X0,XS,X0,X0,XL,
/*60*/ X0,XH,XH,XH,XH,XH,XH,XL,XL,XL,XL,XL,XL,XL,XL,XL,
/*70*/ XL,XL,XL,XL,XL,XL,XL,XL,XL,XL,XL,X0,X0,X0,X0,X0,
};

#undef X0
#undef XB
#undef XS
#undef XR
#undef XL
#undef XH
#undef XD

#define CT(c) (lx_ctype[(unsigned char)(c)])

/* ------------------------------------------------------------------ */
/* Palavras-chave: hash perfeito (36 chaves -> 128 slots, sem colisao) */
/* h = (len*4 + s[0] + s[n-1] + s[n-2]*15) & 127                       */
/* ------------------------------------------------------------------ */
typedef struct { char text[9]; unsigned char len; TokenType tok; } KwEnt;

static const KwEnt lx_kw[128] = {
    [  3] = { "atomic", 6, KATOMIC },
    [ 13] = { "uint8", 5, KUINT8 },
    [ 15] = { "load", 4, KLOAD },
    [ 16] = { "break", 5, KBREAK },
    [ 17] = { "void", 4, KVOID },
    [ 18] = { "int16", 5, KINT16 },
    [ 20] = { "char", 4, KCHAR },
    [ 21] = { "static", 6, KSTATIC },
    [ 23] = { "else", 4, KELSE },
    [ 25] = { "extern", 6, KEXTERN },
    [ 29] = { "float", 5, KFLOAT },
    [ 33] = { "data", 4, KDATA },
    [ 34] = { "uint16", 6, KUINT16 },
    [ 40] = { "const", 5, KCONST },
    [ 44] = { "int32", 5, KINT32 },
    [ 50] = { "utfchar", 7, KUTFCHAR },
    [ 51] = { "call", 4, KCALL },
    [ 52] = { "coperate", 8, KCOPERATE },
    [ 53] = { "double", 6, KDOUBLE },
    [ 55] = { "do", 2, KDO },
    [ 60] = { "uint32", 6, KUINT32 },
    [ 61] = { "enum", 4, KENUM },
    [ 67] = { "continue", 8, KCONTINUE },
    [ 68] = { "while", 5, KWHILE },
    [ 71] = { "small", 5, KSMALL },
    [ 77] = { "jump", 4, KJUMP },
    [ 84] = { "bytes", 5, KBYTES },
    [ 88] = { "inline", 6, KINLINE },
    [ 89] = { "link", 4, KLINK },
    [ 91] = { "int64", 5, KINT64 },
    [ 97] = { "hexa", 4, KHEXA },
    [101] = { "for", 3, KFOR },
    [107] = { "uint64", 6, KUINT64 },
    [124] = { "big", 3, KBIG },
    [125] = { "int8", 4, KINT8 },
    [126] = { "if", 2, KIF },
    [127] = { "newtype", 7, KNEWTYPE },
};

LX_INLINE TokenType keyword_type(const char* s, size_t n) {
    if (n - 2 > 6) return IDENTIFIER;                       /* n < 2 || n > 8 */
    unsigned h = ((unsigned)n * 4u + (unsigned char)s[0] + (unsigned char)s[n - 1]
                  + (unsigned)(unsigned char)s[n - 2] * 15u) & 127u;
    const KwEnt* e = &lx_kw[h];
    if (e->len != n || e->text[0] != s[0]) return IDENTIFIER; /* rejeita ~99% dos idents */
    for (size_t i = 1; i < n; i++)
        if (e->text[i] != s[i]) return IDENTIFIER;
    return e->tok;
}

/* ------------------------------------------------------------------ */
/* Utilitarios                                                         */
/* ------------------------------------------------------------------ */
static inline const char* skip_utf8(const char* p) {
    p++;
    while (((unsigned char)*p & 0xC0) == 0x80) p++;
    return p;
}

#define LX_BAD_CP 0xFFFFFFFFu

/* Decodifica 1 codepoint UTF-8; em sequencia invalida devolve LX_BAD_CP e avanca >= 1 byte.
 * Nunca le alem do sentinela: '\0' nao e byte de continuacao. */
static uint32_t decode_utf8(const char* s, const char** end) {
    const unsigned char* u = (const unsigned char*)s;
    uint32_t c = u[0], cp;
    int n;
    if (c < 0xC2)      { *end = s + 1; return LX_BAD_CP; }
    else if (c < 0xE0) { n = 1; cp = c & 0x1F; }
    else if (c < 0xF0) { n = 2; cp = c & 0x0F; }
    else if (c < 0xF5) { n = 3; cp = c & 0x07; }
    else               { *end = s + 1; return LX_BAD_CP; }
    for (int i = 1; i <= n; i++) {
        if ((u[i] & 0xC0) != 0x80) { *end = s + i; return LX_BAD_CP; }
        cp = (cp << 6) | (u[i] & 0x3F);
    }
    *end = s + n + 1;
    return cp;
}

LX_INLINE size_t lx_col(const Lexer* lx, const char* p) {
    return (size_t)(p - lx->line_start) + 1;
}

/* Cria o token E avanca lx->current; 'end' fica em registrador, sem reler memoria. */
LX_INLINE Token mk(Lexer* lx, TokenType type, const char* start, const char* end,
                   size_t line, size_t col) {
    Token t;
    t.value.start = start;
    t.value.len = (size_t)(end - start);
    t.type = type;
    t.line = (uint32_t)line;
    t.col = (uint32_t)col;
    lx->current = end;
    return t;
}

/* ------------------------------------------------------------------ */
/* Diagnosticos (todo o caminho frio)                                  */
/* ------------------------------------------------------------------ */
LX_COLD static void lx_report(Lexer* lx, SeverityKind sev, size_t line, size_t col, size_t len,
                              const char* code, const char* fmt, ...) {
    if (sev >= ERROR) lx->error_count++;

    DiagnosticContext* ctx = lx->ctx;
    if (!ctx) return;

    char msg[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    DiagnosticKind prev = ctx->phase;
    diag_set_phase(ctx, LEXICAL);
    diag_emit_span(ctx, sev, lx->filename, line, col, len ? len : 1, code, "%s", msg);
    diag_set_phase(ctx, prev);
}

LX_COLD static void lx_note(Lexer* lx, size_t line, size_t col, size_t len, const char* fmt, ...) {
    DiagnosticContext* ctx = lx->ctx;
    if (!ctx) return;

    char msg[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    DiagnosticKind prev = ctx->phase;
    diag_set_phase(ctx, LEXICAL);
    diag_note_span(ctx, lx->filename, line, col, len ? len : 1, "%s", msg);
    diag_set_phase(ctx, prev);
}

/* Token de erro. Se o limite de erros (max_errors) estourou, joga 'current' no fim:
 * o proximo next_token devolve EOFF sem custo extra no caminho quente. */
LX_COLD static Token mk_err(Lexer* lx, const char* start, const char* end, size_t line, size_t col) {
    Token t = mk(lx, UNDEFINED, start, end, line, col);
    DiagnosticContext* ctx = lx->ctx;
    if (ctx && ctx->max_errors && lx->error_count >= ctx->max_errors)
        lx->current = lx->end;
    return t;
}

/* ------------------------------------------------------------------ */
/* Construcao                                                          */
/* ------------------------------------------------------------------ */
Lexer create_lexer_n(const char* filename, const char* src, size_t len, DiagnosticContext* ctx) {
    Lexer lexer = { 0 };
    if (!src) { src = ""; len = 0; }

    const char* end = src + len;
    if (len >= 3 && (unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB
                 && (unsigned char)src[2] == 0xBF)
        src += 3;                                  /* BOM: colunas da 1a linha agora corretas */

    lexer.filename   = filename ? filename : "<desconhecido>";
    lexer.src        = src;
    lexer.current    = src;
    lexer.start      = src;
    lexer.line_start = src;
    lexer.end        = end;
    lexer.line       = 1;
    lexer.ctx        = ctx;
    return lexer;
}

Lexer create_lexer(const char* filename, const char* src, DiagnosticContext* ctx) {
    if (!src) src = "";
    return create_lexer_n(filename, src, strlen(src), ctx);
}

/* ------------------------------------------------------------------ */
/* Espacos e comentarios                                               */
/* ------------------------------------------------------------------ */
static void skip_trivia(Lexer* lx) {
    const char* p = lx->current;

    for (;;) {
        while (CT(*p) & CT_BLANK) p++;

        if (*p == '\n') {
            p++;
            lx->line++;
            lx->line_start = p;
            continue;
        }
#if LEXER_COMMENTS
        if (*p == '/') {
            if (p[1] == '/') {
                /* memchr usa SIMD da libc; '\0' embutido em comentario e inofensivo */
                const char* from = p + 2;
                const char* nl = (const char*)memchr(from, '\n', (size_t)(lx->end - from));
                p = nl ? nl : lx->end;
                continue;
            }
            if (p[1] == '*') {
                size_t sline = lx->line;
                size_t scol = lx_col(lx, p);
                p += 2;
                for (;;) {
                    unsigned char c = (unsigned char)*p;
                    if (c == '*') {
                        if (p[1] == '/') { p += 2; break; }
                        p++;
                    } else if (c == '\n') {
                        p++;
                        lx->line++;
                        lx->line_start = p;
                    } else if (LX_UNLIKELY(c == '\0') && p == lx->end) {
                        lx->current = p;
                        lx_report(lx, ERROR, sline, scol, 2, "L009",
                                  "Comentario de bloco aberto com '/*' mas nunca fechado com '*/'");
                        lx_note(lx, lx->line, lx_col(lx, p), 1,
                                "fim do arquivo alcancado aqui, sem encontrar '*/'");
                        return;
                    } else {
                        p++;
                    }
                }
                continue;
            }
        }
#endif
        break;
    }
    lx->current = p;
}

/* ------------------------------------------------------------------ */
/* Identificadores                                                     */
/* ------------------------------------------------------------------ */
static Token lex_char_ex(Lexer* lx, const char* start, const char* q, TokenType ok,
                         size_t line, size_t col);
static Token lex_string_ex(Lexer* lx, const char* start, const char* q, TokenType ok,
                           size_t line, size_t col);

LX_INLINE Token lex_ident(Lexer* lx, const char* start, size_t line, size_t col) {
    const char* p = start + 1;
    while (CT(*p) & CT_IDENT) p++;                 /* '\0' nao e ident: para sozinho */

    /* W"..." e W'...' (wide): so o 'W' isolado colado na aspa. Custo no caminho quente: 1 cmp. */
    if (LX_UNLIKELY(*start == 'W') && p == start + 1) {
        if (*p == '"')  return lex_string_ex(lx, start, p, WSTRING_LIT, line, col);
        if (*p == '\'') return lex_char_ex(lx, start, p, WCHAR_LIT, line, col);
    }
    return mk(lx, keyword_type(start, (size_t)(p - start)), start, p, line, col);
}

/* ------------------------------------------------------------------ */
/* Numeros                                                             */
/* ------------------------------------------------------------------ */
LX_COLD static const char* num_suffix_hint(const char* bad, size_t n, bool is_hex, bool has_dot) {
    if (is_hex)                          return "; literais hexadecimais nao aceitam sufixo";
    if (*bad == 'e' || *bad == 'E')      return "; notacao cientifica (expoente) nao e suportada";
    if (n == 1) {
        if (*bad == 'b') return "; dica: use 'B' maiusculo (literal big)";
        if (*bad == 'u') return "; dica: use 'U' maiusculo (literal unsigned)";
        if (*bad == 'F' && has_dot) return "; dica: use 'f' minusculo (literal float)";
        if (*bad == 'f' && !has_dot) return "; dica: 'f' so vale com ponto, ex: 1.0f";
    }
    return "";
}

static Token lex_number(Lexer* lx, const char* start, size_t line, size_t col) {
    const char* p = start;
    TokenType type = INT_LIT;
    bool is_hex = false, has_dot = false;

    if (p[0] == '0' && (p[1] | 0x20) == 'x') {
        p += 2;
        const char* digits = p;
        while (CT(*p) & CT_HEX) p++;

        if (LX_UNLIKELY(p == digits)) {
            while (CT(*p) & CT_IDENT) p++;
            lx_report(lx, ERROR, line, col, (size_t)(p - start), "L007",
                      "Literal hexadecimal '%.*s' sem digitos depois de '0x'",
                      (int)(p - start), start);
            return mk_err(lx, start, p, line, col);
        }
        type = HEXA_LIT;
        is_hex = true;
    } else {
        while (CT(*p) & CT_DIGIT) p++;

        unsigned char c = (unsigned char)*p;
        if (c == '.' && (CT(p[1]) & CT_DIGIT)) {
            p += 2;
            while (CT(*p) & CT_DIGIT) p++;
            has_dot = true;
            if (*p == 'f') { p++; type = FLOAT_LIT; }
            else type = DOUBLE_LIT;
        } else if (c == 'B') {
            p++;
            type = BIG_LIT;
        } else if (c == 'U') {
            p++;
            type = UNSIGNED_LIT;
        }
    }

    if (LX_UNLIKELY(CT(*p) & CT_IDENT)) {
        const char* bad = p;
        while (CT(*p) & CT_IDENT) p++;
        lx_report(lx, ERROR, line, col + (size_t)(bad - start), (size_t)(p - bad), "L008",
                  "Sufixo '%.*s' invalido no literal numerico '%.*s' "
                  "(sufixos validos: B, U, e 'f' em literais com ponto, ex: 1.0f)%s",
                  (int)(p - bad), bad, (int)(p - start), start,
                  num_suffix_hint(bad, (size_t)(p - bad), is_hex, has_dot));
        return mk_err(lx, start, p, line, col);
    }

    return mk(lx, type, start, p, line, col);
}

/* ------------------------------------------------------------------ */
/* Escapes, chars e strings                                            */
/* ------------------------------------------------------------------ */
/* 'esc' aponta para a barra. Devolve o ponteiro logo depois do escape. */
static const char* scan_escape(Lexer* lx, const char* esc) {
    const char* p = esc + 1;
    unsigned char c = (unsigned char)*p;

    switch (c) {
    case 'n': case 't': case 'r': case '0': case 'a': case 'b': case 'f': case 'v':
    case '\\': case '\'': case '"':
        return p + 1;

    case 'x': {
        const char* d = p + 1;
        int n = 0;
        while (n < 2 && (CT(*d) & CT_HEX)) { d++; n++; }
        if (LX_UNLIKELY(n == 0))
            lx_report(lx, ERROR, lx->line, lx_col(lx, esc), (size_t)(d - esc), "L010",
                      "Escape '\\x' sem digitos hexadecimais (esperado 1 ou 2 digitos, ex: '\\x1B')");
        return d;
    }

    case '\0': case '\n': case '\r':
        return p;                       /* o chamador reporta "nao fechada" */

    default: {
        const char* end = (c >= 0x80) ? skip_utf8(p) : p + 1;
        lx_report(lx, WARNING, lx->line, lx_col(lx, esc), (size_t)(end - esc), "L005",
                  "Sequencia de escape desconhecida '%.*s' "
                  "(validas: \\n \\t \\r \\0 \\a \\b \\f \\v \\\\ \\' \\\" \\xHH)",
                  (int)(end - esc), esc);
        return end;
    }
    }
}

/* 'start' = inicio do token ('W' ou a aspa); 'q' = a aspa de abertura. */
static Token lex_char_ex(Lexer* lx, const char* start, const char* q, TokenType ok,
                         size_t line, size_t col) {
    const char* p = q + 1;
    unsigned char c = (unsigned char)*p;
    size_t open_len = (size_t)(q + 1 - start);           /* 1 para ', 2 para W' */

    if (c == '\'') {
        lx_report(lx, ERROR, line, col, open_len + 1, "L004",
                  "Literal de caractere vazio (use '\\0' para o caractere nulo)");
        return mk_err(lx, start, p + 1, line, col);
    }
    if (c == '\0' || c == '\n' || c == '\r') {
        lx_report(lx, ERROR, line, col, open_len, "L003", "Aspas simples abertas mas nunca fechadas");
        return mk_err(lx, start, p, line, col);
    }

    if (c == '\\')      p = scan_escape(lx, p);
    else if (c >= 0x80) p = skip_utf8(p);
    else                p++;

    if (LX_LIKELY(*p == '\'')) return mk(lx, ok, start, p + 1, line, col);

    /* recuperacao: procura o fecha-aspas na mesma linha */
    const char* r = p;
    while (*r && *r != '\n' && *r != '\r' && *r != '\'') {
        if (*r == '\\' && r[1] != '\0' && r[1] != '\n') r++;
        r++;
    }

    if (*r == '\'') {
        lx_report(lx, ERROR, line, col, (size_t)(r + 1 - start), "L006",
                  "Literal de caractere com mais de um caractere; use aspas duplas para strings");
        return mk_err(lx, start, r + 1, line, col);
    }
    lx_report(lx, ERROR, line, col, open_len, "L003", "Aspas simples abertas mas nunca fechadas");
    return mk_err(lx, start, r, line, col);
}

static Token lex_string_ex(Lexer* lx, const char* start, const char* q, TokenType ok,
                           size_t line, size_t col) {
    const char* p = q + 1;

    for (;;) {
        while (!(CT(*p) & CT_STOP)) p++;           /* 1 load + 1 teste por byte */
        unsigned char c = (unsigned char)*p;

        if (LX_LIKELY(c == '"')) return mk(lx, ok, start, p + 1, line, col);
        if (c == '\\') { p = scan_escape(lx, p); continue; }

        if (LX_UNLIKELY(c == '\0') && p != lx->end) {       /* NUL embutido */
            lx_report(lx, ERROR, lx->line, lx_col(lx, p), 1, "L012",
                      "Byte nulo (0x00) dentro de string");
            p++;
            continue;
        }

        lx_report(lx, ERROR, line, col, (size_t)(p - start), "L002",
                  "String aberta mas nunca fechada (faltou '\"' antes do %s)",
                  p == lx->end ? "fim do arquivo" : "fim da linha");
        return mk_err(lx, start, p, line, col);
    }
}

LX_INLINE Token lex_char(Lexer* lx, const char* start, size_t line, size_t col) {
    return lex_char_ex(lx, start, start, CHAR_LIT, line, col);
}
LX_INLINE Token lex_string(Lexer* lx, const char* start, size_t line, size_t col) {
    return lex_string_ex(lx, start, start, STRING_LIT, line, col);
}

/* ------------------------------------------------------------------ */
/* Caminhos frios de next_token                                        */
/* ------------------------------------------------------------------ */
LX_COLD static Token lex_nul(Lexer* lx, const char* start, size_t line, size_t col) {
    if (start == lx->end) return mk(lx, EOFF, start, start, line, col);
    lx_report(lx, ERROR, line, col, 1, "L012", "Byte nulo (0x00) no meio do arquivo-fonte");
    return mk_err(lx, start, start + 1, line, col);
}

LX_COLD static Token lex_invalid(Lexer* lx, const char* start, size_t line, size_t col) {
    unsigned char c = (unsigned char)*start;

    if (c >= 0x80) {
        const char* end;
        uint32_t cp = decode_utf8(start, &end);
        if (cp == LX_BAD_CP)                       /* engole bytes de continuacao soltos: 1 erro so */
            while (((unsigned char)*end & 0xC0) == 0x80) end++;
        int len = (int)(end - start);

        if (cp == LX_BAD_CP)
            lx_report(lx, ERROR, line, col, (size_t)len, "L001",
                      "Byte 0x%02X invalido: sequencia UTF-8 malformada", (unsigned)c);
        else if (cp == 0x201C || cp == 0x201D)
            lx_report(lx, ERROR, line, col, (size_t)len, "L001",
                      "Aspas tipograficas '%.*s' (U+%04X) invalidas; use aspas duplas ASCII (\")",
                      len, start, (unsigned)cp);
        else if (cp == 0x2018 || cp == 0x2019)
            lx_report(lx, ERROR, line, col, (size_t)len, "L001",
                      "Aspas tipograficas '%.*s' (U+%04X) invalidas; use aspas simples ASCII (')",
                      len, start, (unsigned)cp);
        else if (cp == 0x2013 || cp == 0x2014 || cp == 0x2212)
            lx_report(lx, ERROR, line, col, (size_t)len, "L001",
                      "Travessao/menos tipografico '%.*s' (U+%04X) invalido; use '-' ASCII",
                      len, start, (unsigned)cp);
        else if (cp == 0x00A0 || cp == 0x200B || cp == 0xFEFF)
            lx_report(lx, ERROR, line, col, (size_t)len, "L001",
                      "Espaco invisivel (U+%04X) invalido no codigo; apague ou troque por espaco comum",
                      (unsigned)cp);
        else
            lx_report(lx, ERROR, line, col, (size_t)len, "L001",
                      "Caractere nao-ASCII '%.*s' (U+%04X) invalido fora de string/char",
                      len, start, (unsigned)cp);
        return mk_err(lx, start, end, line, col);
    }

    if (c >= 0x20 && c < 0x7F) {
        const char* hint = "";
        if (c == '`')       hint = "; para strings use aspas duplas (\")";
        else if (c == '\\') hint = "; '\\' so e valido dentro de string/char";
        lx_report(lx, ERROR, line, col, 1, "L001",
                  "Caractere '%c' (ASCII %d) invalido, use um caractere valido da linguagem%s",
                  (int)c, (int)c, hint);
    } else {
        lx_report(lx, ERROR, line, col, 1, "L001",
                  "Caractere de controle (0x%02X) invalido no codigo-fonte", (unsigned)c);
    }
    return mk_err(lx, start, start + 1, line, col);
}

LX_COLD static Token lex_dotdot(Lexer* lx, const char* start, size_t line, size_t col) {
    lx_report(lx, ERROR, line, col, 2, "L011",
              "Operador '..' invalido; use '.' para acesso ou '...' para reticencias");
    return mk_err(lx, start, start + 2, line, col);
}

/* ------------------------------------------------------------------ */
/* next_token                                                          */
/* ------------------------------------------------------------------ */
Token next_token(Lexer* lx) {
    skip_trivia(lx);

    const char* start = lx->current;
    lx->start = start;

    const size_t line = lx->line;
    const size_t col = lx_col(lx, start);
    const unsigned char c = (unsigned char)*start;
    const unsigned char cls = lx_ctype[c];

#define TOK(t, n) return mk(lx, (t), start, start + (n), line, col)

    if (cls & CT_IDSTART) return lex_ident(lx, start, line, col);
    if (cls & CT_DIGIT)   return lex_number(lx, start, line, col);
    if (LX_UNLIKELY(c == '\0')) return lex_nul(lx, start, line, col);

    /* daqui para baixo start[0] != '\0', logo start[1] existe (no pior caso e o sentinela) */
    const unsigned char c1 = (unsigned char)start[1];

    switch (c) {                                   /* jump table */
    case ';': TOK(SEMICOLON, 1);
    case ':': TOK(COLON, 1);
    case ',': TOK(COMMA, 1);
    case '(': TOK(OPEN_PAREN, 1);
    case ')': TOK(CLOSE_PAREN, 1);
    case '{': TOK(OPEN_BRACE, 1);
    case '}': TOK(CLOSE_BRACE, 1);
    case '[': TOK(OPEN_BRACKET, 1);
    case ']': TOK(CLOSE_BRACKET, 1);
    case '~': TOK(DESC, 1);
    case '?': TOK(OPTERN, 1);

    case '.':
        if (c1 == '.') {
            if (start[2] == '.') TOK(ELLIPSES, 3);
            return lex_dotdot(lx, start, line, col);
        }
        TOK(DOT, 1);

    case '>':
        if (c1 == '=') TOK(OPGE, 2);
        if (c1 == '>') { if (start[2] == '=') TOK(OPSHRE, 3); TOK(OPSHR, 2); }
        TOK(OPGT, 1);

    case '<':
        if (c1 == '<') { if (start[2] == '=') TOK(OPSHLE, 3); TOK(OPSHL, 2); }
        if (c1 == '=') TOK(OPLE, 2);
        TOK(OPLT, 1);

    case '=': if (c1 == '=') TOK(OPEQ, 2);    TOK(ASSIGN, 1);
    case '!': if (c1 == '=') TOK(OPBANGE, 2); TOK(OPBANG, 1);

    case '&':
        if (c1 == '=') TOK(OPANDE, 2);
        if (c1 == '&') TOK(LOGAND, 2);
        TOK(OPAND, 1);

    case '|':
        if (c1 == '=') TOK(OPORE, 2);
        if (c1 == '|') TOK(LOGOR, 2);
        TOK(OPOR, 1);

    case '^': if (c1 == '=') TOK(OPXORE, 2);   TOK(OPXOR, 1);
    case '*': if (c1 == '=') TOK(OPSTARE, 2);  TOK(OPSTAR, 1);
    case '/': if (c1 == '=') TOK(OPSLASHE, 2); TOK(OPSLASH, 1);
    case '%': if (c1 == '=') TOK(OPMODE, 2);   TOK(OPMOD, 1);

    case '+':
        if (c1 == '+') TOK(OPPLUSPLUS, 2);
        if (c1 == '=') TOK(OPPLUSE, 2);
        TOK(OPPLUS, 1);

    case '-':
        if (c1 == '>') TOK(ARROW, 2);
        if (c1 == '-') TOK(OPMINUSMINUS, 2);
        if (c1 == '=') TOK(OPMINUSE, 2);
        TOK(OPMINUS, 1);

    case '\'': return lex_char(lx, start, line, col);
    case '"':  return lex_string(lx, start, line, col);

    default:
        return lex_invalid(lx, start, line, col);
    }
#undef TOK
}
