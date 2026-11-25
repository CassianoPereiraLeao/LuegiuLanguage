#pragma once

#include "common.h"
#include "arena.h"
#include "parser.h"
#include "typecheck.h"
#include "ir.h"

enum { 
    CG_LOC_NONE = 0, 
    CG_LOC_REG, CG_LOC_MEM 
};

typedef struct {
    uint8_t kind;
    uint8_t xmm;
    uint8_t reg;
    int32_t off;
} CgLoc;

typedef struct {
    FILE* out;
    Arena* arena;
    Node* entry;
    CheckContext* check_ctx;
    IrGenContext* ir;

    IrFunctionRange* fn;
    CgLoc* temp_loc;
    CgLoc* slot_loc;
    int32_t* slot_off;
    int frame_size;
    int out_area;
    uint32_t saved_gp;
    uint32_t saved_xmm;
    int saved_gp_off[16];
    int saved_xmm_off[16];
    int stack_arg_seen;
    int local_label;
    size_t cur;

    size_t unsupported;
} CodegenContext;

CodegenContext create_codegen(FILE* out, Arena *arena, Node *entry, CheckContext *check_ctx);
void emit_program(CodegenContext *ctx, IrGenContext *ir_ctx);