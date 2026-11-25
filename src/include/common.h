#pragma once

#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <ctype.h>

#define newtype_array_definition(TYPE, ALIAS) \
    struct ALIAS { \
        size_t capacity; \
        size_t count; \
        TYPE* items;  \
    }

#define KB 1024
#define MB (KB * KB)

#define INIT_CAP (KB)
#define MAX_CAP (MB * 1024)

#if defined(__GNUC__) || defined(__clang__)
#  define LIKELY(x)    __builtin_expect(!!(x), 1)
#  define UNLIKELY(x)  __builtin_expect(!!(x), 0)
#  define NOINLINE     __attribute__((noinline))
#  define NORETURN     __attribute__((noreturn))
#  define PRINTF_FMT   __attribute__((format(printf, 3, 4)))
#else
#  define LIKELY(x)    (x)
#  define UNLIKELY(x)  (x)
#  define NOINLINE
#  define NORETURN
#  define PRINTF_FMT
#endif

typedef enum {
    WRONG_PATH = 1,
    ALLOCATE_ERROR,
    READFILE_ERROR,
    OUT_OF_PATH,
    OUT_OF_SOURCES,
    BUFFER_OVERFLOW,
    INVALID_USE_TARGET,
    INVALID_EXTRACT_TARGET,
    COMPILE_ERROR,
    EXPLODE_MAX_ERRORS,
    INVALID_PREPROCESS,
    MACRO_ERROR,
    INCLUDE_DEPTH_ERROR,
} ErrorCodesSystem;

typedef enum {
    EOFF, UNDEFINED,

    IDENTIFIER, STRING_LIT, CHAR_LIT, HEXA_LIT,
    INT_LIT, FLOAT_LIT, DOUBLE_LIT, UNSIGNED_LIT,
    BIG_LIT, WSTRING_LIT, WCHAR_LIT,

    OPEN_PAREN, CLOSE_PAREN, OPEN_BRACKET,
    CLOSE_BRACKET, OPEN_BRACE, CLOSE_BRACE,

    COLON, SEMICOLON, ELLIPSES, COMMA, DOT,

    OPSHL, OPSHR, OPPLUS, OPPLUSPLUS,
    OPMINUS, OPMINUSMINUS, OPMOD, OPSLASH,
    OPSTAR, OPBANG, OPXOR, OPAND,
    OPOR, OPGT, OPLT, DESC,
    ARROW, ASSIGN,

    OPEQ, OPSHLE, OPSHRE, OPPLUSE,
    OPMINUSE, OPSLASHE, OPBANGE, OPMODE,
    OPSTARE, LOGOR, LOGAND, OPXORE,
    OPORE, OPANDE, OPGE, OPLE,
    OPTERN,

    KDATA, KENUM, KCOPERATE, KNEWTYPE, KWHILE, KFOR,
    KDO, KIF, KELSE, KCONST, KSTATIC, KATOMIC, KLOAD,
    KCALL, KBREAK, KCONTINUE, KJUMP, KEXTERN, KBYTES,

    KVOID, KSMALL, KLINK, KHEXA, KCHAR, KUTFCHAR,
    KFLOAT, KDOUBLE, KBIG, KUINT8, KUINT16, KUINT32,
    KUINT64, KINT8, KINT16, KINT32, KINT64, KINLINE
} TokenType;

typedef enum {
    NODE_UNDEFINED,
    NODE_VAR_DECL,
    NODE_FUNC_DECL,
    NODE_VAR_CALL,
    NODE_FUNC_CALL,
    NODE_LITERAL,
    NODE_BYTES,
    NODE_IF_STMT,
    NODE_BLOCK_STMT,
    NODE_CALL_STMT,
    NODE_BREAK_STMT,
    NODE_CONTINUE_STMT,
    NODE_FOR_LOOP,
    NODE_WHILE_LOOP,
    NODE_DO_WHILE_LOOP,
    NODE_DATA,
    NODE_ENUM,
    NODE_ENUM_CALL,
    NODE_ENUM_MEMBER,
    NODE_COPERATE,
    NODE_TERNARY,
    NODE_NEWTYPE,
    NODE_FIELD_CALL,
    NODE_CAST,
    NODE_PROGRAM,
    NODE_BINARY_OP,
    NODE_UNARY_OP,
    NODE_POSTFIX_OP,
    NODE_ARRAY,
    NODE_LOAD_LABEL,
    NODE_JUMP_LABEL,
    NODE_INIT_LIST,
    NODE_DESIGNATOR,
} NodeKinds;

typedef enum {
    INT, BIG, UNSIGNED,
    CHAR, FLOAT, DOUBLE,
    HEXA, STRING, WIDE_CHAR,
    WIDE_STRING
} LiteralKind;

typedef struct {
    const char* start;
    size_t len;
} View;
