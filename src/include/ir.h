#pragma once

#include "common.h"
#include "parser.h"
#include "typecheck.h"

#define IR_SCOPE_TABLE_SIZE  256
#define IR_FUNC_TABLE_SIZE   128
#define IR_LABEL_TABLE_SIZE  64
#define IR_GLOBAL_TABLE_SIZE 128
#define IR_INLINE_MAX_DEPTH 8
#define IR_INIT_BLOB_MIN_ITEMS 16

#define IR_INT_ARG_REGS     6
#define IR_FP_ARG_REGS      8
#define IR_SYSCALL_ARG_REGS 7 
#define IR_WIN_ARG_REGS 4

#define IRF_RESULT_UNUSED 1u
#define IRF_COLD_JMP      2u

#define TABLE_DEF(T) \
    struct { \
        T* items; \
        size_t count; \
        size_t capacity; \
    }

/*
 * CONTRATO DO IR (para o codegen)
 *
 * Todos os saltos usam dest = label:
 *   IR_JMP           dest=label
 *   IR_JMP_IF_ZERO   dest=label, src1=cond        IR_JMP_IF_NOT_ZERO idem
 *   IR_JMP_xx        dest=label, src1, src2       (cmp+jcc fundidos; sinal/float vem de src1.type; nunca float)
 *   IR_LABEL         dest=label
 *
 * Memoria:
 *   IR_LOAD_INDIRECT   dest, src1=endereco, aux=offset         tamanho = dest.type.size
 *   IR_STORE_INDIRECT  src1=endereco, src2=valor, aux=offset   tamanho = src2.type.size
 *   Endereco pode ser TEMP/SLOT(conteudo=ponteiro)/SLOT_ADDR/GLOBAL_ADDR (+field_offset).
 *   SLOT_ADDR / GLOBAL_ADDR = "lea": endereco do slot/global + field_offset (sem instrucao).
 *   SLOT / GLOBAL = o proprio objeto na memoria (ja endereçavel como [rbp-x+field_offset] / [rel nome+field_offset]).
 *
 * Atomicos (x86-64, sempre seq_cst). Tamanho = dest.type.size (store: src2.type.size). Apenas 1/2/4/8 bytes.
 *   IR_ATOMIC_LOAD   dest, src1=end, aux=off                    -> mov
 *   IR_ATOMIC_STORE  src1=end, src2=val, aux=off                -> xchg (seq_cst)
 *   IR_ATOMIC_XCHG   dest=antigo, src1=end, src2=novo, aux=off  -> xchg
 *   IR_ATOMIC_CAS    dest=valor encontrado (antigo), src1=end, src2=esperado, src3=desejado, aux=off
 *                    -> mov rax,esperado ; lock cmpxchg ; sucesso <=> dest == src2
 *   IR_ATOMIC_ADD    dest=antigo, src1=end, src2=valor, aux=off -> lock xadd (SUB ja chega negado)
 *   IR_ATOMIC_AND/OR/XOR  so sao emitidos com RESULT_UNUSED (dest=NONE) -> lock and/or/xor [m], r
 *   IR_FENCE                                                    -> mfence
 *   Operacoes sem versao nativa (fetch_and usado, shl, mul, div...) ja vem expandidas em loop LOAD/CAS no IR.
 *   Se IRF_RESULT_UNUSED: dest == NONE, emitir "lock add [m], r" em vez de xadd.
 *
 * Chamadas:  IR_ARG_STACK (ordem inversa, aux = total de args na pilha; se impar, alinhar antes),
 *            IR_ARG (aux = indice do registrador, classe por src1.type.is_float),
 *            IR_CALL dest (NONE se unused), src1=FUNC/BUILTIN, aux=qtde pilha, aux2=qtde xmm usados (para `al` em variadica).
 *
 * Slots: nao ha IR_SLOT_DECL. Layout do frame vem de IrFunctionRange.slots[] (id do slot = indice).
 *        Os primeiros param_count slots sao os parametros, em ordem. Arrays fixos tem slot com o tamanho total.
 *        VLA: slot de 8 bytes que recebe o ponteiro de IR_ALLOCA (precisa de rbp: uses_vla).
 *
 * IR_MEMZERO: src1=endereco (SLOT_ADDR/GLOBAL_ADDR/temp/slot-ponteiro de VLA), src2=bytes (CONST_INT ou temp).
 *   Codegen: <=64 bytes inline (xor + mov / pxor + movups); maior: rep stosb.
 *
 * IR_CAST: dest.type = destino, src1.type = origem (int<->float, extensao/truncamento).
 * IR_LNOT: !x (0/1).  IR_NOT: ~x.
 */
typedef enum {
    IR_ADD, IR_SUB, IR_MUL, IR_DIV, IR_MOD,
    IR_BAND, IR_BOR, IR_BXOR, IR_SHL, IR_SHR,
    IR_NEG, IR_NOT, IR_LNOT,
    IR_CMP_LT, IR_CMP_LE, IR_CMP_GT, IR_CMP_GE, IR_CMP_EQ, IR_CMP_NE,
    IR_CAST,
    IR_ASSIGN,
    IR_LABEL, IR_JMP, IR_JMP_IF_ZERO, IR_JMP_IF_NOT_ZERO,
    IR_JMP_LT, IR_JMP_LE, IR_JMP_GT, IR_JMP_GE, IR_JMP_EQ, IR_JMP_NE,
    IR_RETURN, IR_ARG, IR_ARG_STACK, IR_CALL,
    IR_LOAD_INDIRECT, IR_STORE_INDIRECT,
    IR_ALLOCA, IR_STACK_SAVE, IR_STACK_RESTORE, IR_MEMZERO,
    IR_FENCE, IR_ATOMIC_LOAD, IR_ATOMIC_STORE, IR_ATOMIC_XCHG, IR_ATOMIC_CAS,
    IR_ATOMIC_ADD, IR_ATOMIC_AND, IR_ATOMIC_OR, IR_ATOMIC_XOR,
    IR_VA_START, IR_INTRIN, IR_MEMCOPY
} IrOperators;

typedef enum {
    IR_VAL_NONE,
    IR_VAL_TEMP,
    IR_VAL_CONST_INT,
    IR_VAL_CONST_UINT,
    IR_VAL_CONST_FLOAT,
    IR_VAL_CONST_STRING,
    IR_VAL_SLOT,
    IR_VAL_SLOT_ADDR,
    IR_VAL_GLOBAL,
    IR_VAL_GLOBAL_ADDR,
    IR_VAL_LABEL,
    IR_VAL_FUNC,
    IR_VAL_BUILTIN
} IrValueKind;

typedef struct {
    uint32_t size;
    uint32_t step;
    uint8_t ptr_lvl;
    uint8_t is_float;
    uint8_t is_signed;
    uint8_t is_atomic;
} IrType;

typedef struct {
    IrValueKind kind;
    int field_offset;
    union {
        int temp_id;
        int slot_id;
        int global_id;
        int func_id;
        long long const_i;
        unsigned long long const_ui;
        int label_id;
        int string_id;
        int float_id;
    } as;
    IrType type;
} IrValue;

typedef struct {
    uint32_t offset;
    uint32_t size;       /* escalar: 1/2/4/8; is_bytes: capacidade do char[N] */
    uint8_t  is_bytes;   /* copia `bytes` (zero-fill ate size) em vez de `value` */
    View     bytes;
    IrValue  value;
} IrInitItem;

typedef struct {
    IrOperators op;
    uint32_t flags;
    int aux;
    int aux2;
    IrValue dest;
    IrValue src1;
    IrValue src2;
    IrValue src3;
} IrInstruction;

typedef TABLE_DEF(IrInstruction) IrInstructionList;

typedef struct {
    uint32_t size;
    uint32_t align;
    IrType type;
    bool is_param;
    bool is_array;
} IrSlotInfo;

typedef TABLE_DEF(IrSlotInfo) IrSlotInfoList;

typedef struct {
    Node* func_node;
    int func_id;
    size_t start;
    size_t end;
    int slot_count;
    int temp_count;
    int param_count;      /* inclui o slot oculto de sret, quando houver */
    IrSlotInfo* slots;
    bool has_calls;
    bool uses_vla;
    bool sret;            /* slot 0 = ponteiro de retorno oculto */
} IrFunctionRange;

typedef TABLE_DEF(IrFunctionRange) IrFunctionRangeList;

typedef struct IrFuncInfo {
    View name;
    int func_id;
    bool defined;
    bool _static;
    bool _extern;
    bool variadic;
    bool builtin;
    Node* inline_node;
    bool referenced;
    struct IrFuncInfo* next;
} IrFuncInfo;

typedef TABLE_DEF(IrFuncInfo*) IrFuncInfoList;

typedef struct IrGlobal {
    View name;
    int id;
    IrType type;
    uint32_t size;
    uint32_t align;
    bool _static;
    bool _extern;
    bool _const;
    bool has_init;
    bool is_array;
    size_t dim_count;
    IrValue* dim_strides;
    IrValue init;
    IrInitItem* init_items;   /* inicializador de lista (ordenado, sem zeros) */
    size_t init_count;
    struct IrGlobal* next;
} IrGlobal;

typedef TABLE_DEF(IrGlobal*) IrGlobalList;

typedef struct {
    View name;
    int slot_id;
    int global_id;
    IrType type;
    uint32_t bucket;
    int32_t prev;
    bool is_array;
    bool is_vla;
    bool indirect;        /* parametro aggregate: o slot guarda o PONTEIRO para o objeto */
    size_t dim_count;
    IrValue* dim_strides;
} IrSlotEntry;

typedef TABLE_DEF(IrSlotEntry) IrSymList;

typedef struct IrLabelEntry {
    View name;
    int label_id;
    struct IrLabelEntry* next;
} IrLabelEntry;

typedef struct {
    View text;
    int id;
} IrStringEntry;

typedef struct {
    IrStringEntry* items;
    size_t count;
    size_t capacity;
    int32_t* index;
    size_t index_cap;
} IrStringPool;

typedef struct {
    double value;
    int id;
    bool _single;
} IrFloatEntry;

typedef struct {
    IrFloatEntry* items;
    size_t count;
    size_t capacity;
    int32_t* index;
    size_t index_cap;
} IrFloatPool;

typedef struct { 
    size_t start, end; 
} IrColdRange;

typedef TABLE_DEF(IrColdRange) IrColdList;

typedef struct {
    Arena* arena;
    IrInstructionList instructions;
    IrFunctionRangeList functions;
    IrFuncInfoList funcs;
    IrFuncInfo* func_buckets[IR_FUNC_TABLE_SIZE];
    IrGlobalList globals;
    IrGlobal* global_buckets[IR_GLOBAL_TABLE_SIZE];
    IrLabelEntry* label_buckets[IR_LABEL_TABLE_SIZE];
    IrSymList syms;
    int32_t sym_heads[IR_SCOPE_TABLE_SIZE];
    IrStringPool strings;
    IrFloatPool floats;

    IrSlotInfoList cur_slots;
    IrColdList cold;
    IrType cur_ret;
    int sret_slot;
    int next_temp;
    int next_label;
    int next_static;
    int loop_start_label;
    int loop_end_label;
    bool in_loop;
    bool in_function;
    bool vla_valid;
    IrValue vla_outer;
    bool cur_has_calls;
    bool cur_uses_vla;
    int cur_func_id;
    int inline_depth;
    int inline_stack[IR_INLINE_MAX_DEPTH];
    int inline_ret_label;
    IrValue inline_ret_val;

    size_t unsupported;
} IrGenContext;

IrGenContext create_irgen_context(Arena *arena);
void irgen_program(IrGenContext *ctx, Node *program);

#undef TABLE_DEF
