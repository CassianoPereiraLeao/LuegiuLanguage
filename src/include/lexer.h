#pragma once

#include <stdint.h>
#include "common.h"
#include "string_helper.h"
#include "diagnostics.h"

typedef struct {
    View value;
    TokenType type;
    uint32_t line;
    uint32_t col;
} Token;

_Static_assert(sizeof(Token) == 32, "Token deve ter 32 bytes (2 por cache line)");

typedef struct {
    const char* filename;
    const char* src;
    const char* current;
    const char* start;
    const char* line_start;
    const char* end;

    size_t line;
    size_t error_count;

    DiagnosticContext* ctx;
} Lexer;

/* src DEVE terminar em '\0' (sentinela). Usa strlen() para achar o fim. */
Lexer create_lexer(const char* filename, const char* src, DiagnosticContext *ctx);

/* Igual, mas com tamanho conhecido (ex.: fstat/mmap): evita o strlen e
 * permite distinguir '\0' embutido no arquivo do fim real. Exige src[len] == '\0'. */
Lexer create_lexer_n(const char* filename, const char* src, size_t len, DiagnosticContext *ctx);

Token next_token(Lexer *lexer);