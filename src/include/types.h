#pragma once

#include "common.h"
#include "lexer.h"
#include "string_helper.h"

#define MAX_ARRAY_RANK 8

typedef struct AggregateDef AggregateDef;

typedef enum {
    TC_PRIM,
    TC_ENUM,
    TC_AGGREGATE,
    TC_NEWTYPE
} TypeClass;

typedef struct {
    View alias;
    AggregateDef* inline_define;
    size_t array_size;
    TokenType base;
    TokenType repr;
    TypeClass cls;
    uint32_t array_dims[MAX_ARRAY_RANK];
    uint16_t ptr_lvl;
    uint8_t array_rank;
    bool _atomic;
    bool _array;
    bool _vla;
    bool error;
} Typecheck;
