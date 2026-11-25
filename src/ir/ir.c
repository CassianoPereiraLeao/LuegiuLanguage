#include "../include/ir.h"
#include <ctype.h>

#define ATOMIC_OP_FENCE       1
#define ATOMIC_OP_CAS         2
#define ATOMIC_OP_LOAD        3
#define ATOMIC_OP_STORE       4
#define ATOMIC_OP_XCHG        5
#define ATOMIC_OP_FETCH_ADD   6
#define ATOMIC_OP_FETCH_SUB   7
#define ATOMIC_OP_FETCH_AND   8
#define ATOMIC_OP_FETCH_OR    9
#define ATOMIC_OP_FETCH_XOR  10

#define VIEW_LIT(s) ((View){ (s), sizeof(s) - 1 })

#define LIST_ENSURE(arena, list, min_cap) \
    do { \
        if((list)->count >= (list)->capacity) \
            (list)->items = grow_array((arena), (list)->items, (list)->count, &(list)->capacity, \
                                       sizeof(*(list)->items), (min_cap)); \
    } while(0)

#define UNSUPPORTED(ctx, node, why) \
    do { \
        (ctx)->unsupported++; \
        fprintf(stderr, "irgen: nao suportado (%s) em %s:%u:%u [%s:%d]\n", (why), \
            (node) ? (node)->filepath : "?", (node) ? (unsigned)(node)->line : 0, \
            (node) ? (unsigned)(node)->col : 0, __func__, __LINE__); \
    } while(0)

typedef struct {
    int start, end;
    bool in_loop, vla_valid;
    IrValue vla_outer;
} LoopSave;

typedef struct {
    IrValue base;
    int offset;
    bool memory;
    bool partial;
    IrType type;
} IrLval;

static IrValue gen_expr_ex(IrGenContext *ctx, Node *node, bool unused);
static void emit_label(IrGenContext *ctx, int id);
static IrValue gen_expr(IrGenContext *ctx, Node *node) { return gen_expr_ex(ctx, node, false); }
static void gen_statement(IrGenContext *ctx, Node *node);
static void gen_cond_jump(IrGenContext *ctx, Node *node, int label, bool when_true);
static IrValue gen_array_address(IrGenContext *ctx, Node *node, bool *partial);
static IrValue gen_func_call_ex(IrGenContext *ctx, Node *node, bool unused, const IrLval *dst);
static void gen_agg_into(IrGenContext *ctx, const IrLval *dst, Node *src, bool fresh);
static IrValue agg_temp_slot(IrGenContext *ctx, IrType t);

static bool is_aggregate_ir(IrType t) {
    if(t.is_float || t.ptr_lvl > 0 || t.size == 0) return false;
    return t.size > 8 || (t.size & (t.size - 1)) != 0;   /* 3,5,6,7 e >8 */
}

static bool is_lvalue_node(Node *n) {
    return n->kind == NODE_VAR_CALL || n->kind == NODE_FIELD_CALL || n->kind == NODE_ARRAY ||
           (n->kind == NODE_UNARY_OP && n->ast.unary.op == OPSTAR);
}

static LoopSave loop_enter(IrGenContext *ctx, int cont, int end) {
    LoopSave s = { ctx->loop_start_label, ctx->loop_end_label, ctx->in_loop, ctx->vla_valid, ctx->vla_outer };
    ctx->loop_start_label = cont;
    ctx->loop_end_label = end;
    ctx->in_loop = true;
    ctx->vla_valid = false;
    return s;
}

static void loop_leave(IrGenContext *ctx, LoopSave s) {
    ctx->loop_start_label = s.start;
    ctx->loop_end_label = s.end;
    ctx->in_loop = s.in_loop;
    ctx->vla_valid = s.vla_valid;
    ctx->vla_outer = s.vla_outer;
}

static void* grow_array(Arena *arena, void *items, size_t count, size_t *cap, size_t esize, size_t min_cap) {
    size_t new_cap = (*cap > 0) ? *cap * 2 : min_cap;
    void* fresh = arena_alloc(arena, esize * new_cap);
    if(count > 0) memcpy(fresh, items, esize * count);
    *cap = new_cap;
    return fresh;
}

static IrInstruction* emit(IrGenContext *ctx, IrOperators op, IrValue dest, IrValue src1, IrValue src2, int aux);

static size_t primitive_size(TokenType type) {
    switch (type)
    {
    case KVOID: return 0;
    case KINT8: case KUINT8: case KCHAR: return 1;
    case KINT16: case KUINT16: return 2;
    case KINT32: case KUINT32: case KFLOAT: return 4;
    default: return 8;
    }
}

static size_t sizeof_tc(Typecheck t) {
    if(t.ptr_lvl > 0 || t._vla) return 8;
    return type_size_scalar(t);
}

static size_t align_tc(Typecheck t) {
    if(t.ptr_lvl > 0 || t._vla) return 8;
    size_t a = type_align(t);
    if(a) return a;
    return t.inline_define ? 8 : 1;
}

static IrType ir_type(Typecheck t) {
    IrType r = { 0 };
    r.is_atomic = t._atomic;

    if(t._array || t._vla) {
        Typecheck e = t;
        e._array = false;
        e._vla = false;
        size_t es = sizeof_tc(e);
        r.size = 8;
        r.ptr_lvl = (uint8_t)(t.ptr_lvl + 1);
        r.step = (uint32_t)(es ? es : 1);
        r.is_atomic = 0;
        return r;
    }

    if(t.ptr_lvl > 0) {
        Typecheck p = t;
        p.ptr_lvl--;
        size_t ps = sizeof_tc(p);
        r.size = 8;
        r.ptr_lvl = (uint8_t)t.ptr_lvl;
        r.step = (uint32_t)(ps ? ps : 1);
        return r;
    }

    r.size = (uint32_t)sizeof_tc(t);
    if(t.inline_define == NULL) {
        r.is_float = (t.repr == KFLOAT || t.repr == KDOUBLE);
        r.is_signed = (t.repr == KINT8 || t.repr == KINT16 || t.repr == KINT32 ||
                       t.repr == KINT64 || t.repr == KBIG || t.repr == KSMALL);
    }
    return r;
}

static bool is_agg_call(Node *n) {
    return n && n->kind == NODE_FUNC_CALL && is_aggregate_ir(ir_type(n->resolved_type));
}

static IrType type_i64(void) { IrType t = { 0 }; t.size = 8; t.is_signed = 1; return t; }
static IrType type_double(void) { IrType t = { 0 }; t.size = 8; t.is_float = 1; return t; }
static IrType type_ptr(void) { IrType t = { 0 }; t.size = 8; t.ptr_lvl = 1; t.step = 1; return t; }

static IrType common_type(IrType a, IrType b) {
    if(a.is_float || b.is_float) {
        if(a.is_float && b.is_float) return a.size >= b.size ? a : b;
        return a.is_float ? a : b;
    }
    if(a.ptr_lvl) return a;
    if(b.ptr_lvl) return b;
    IrType r = a.size >= b.size ? a : b;
    if(a.size == b.size) r.is_signed = a.is_signed && b.is_signed;
    return r;
}

static IrValue none_value(void) {
    IrValue v = { 0 };
    v.kind = IR_VAL_NONE;
    return v;
}

static IrValue const_int(long long value, IrType type) {
    IrValue v = { 0 };
    v.kind = IR_VAL_CONST_INT;
    v.as.const_i = value;
    v.type = type;
    return v;
}

static IrValue label_value(int id) {
    IrValue v = { 0 };
    v.kind = IR_VAL_LABEL;
    v.as.label_id = id;
    return v;
}

static bool is_const_int(IrValue v) {
    return v.kind == IR_VAL_CONST_INT || v.kind == IR_VAL_CONST_UINT;
}

static IrValue new_temp(IrGenContext *ctx, IrType type) {
    IrValue v = { 0 };
    v.kind = IR_VAL_TEMP;
    v.as.temp_id = ctx->next_temp++;
    v.type = type;
    return v;
}

static int new_label(IrGenContext *ctx) {
    return ctx->next_label++;
}

static IrValue addr_of(IrValue v) {
    if(v.kind == IR_VAL_SLOT) v.kind = IR_VAL_SLOT_ADDR;
    else if(v.kind == IR_VAL_GLOBAL) v.kind = IR_VAL_GLOBAL_ADDR;
    v.type = type_ptr();
    return v;
}

static bool needs_cast(IrType from, IrType to) {
    if(from.size == 0 || to.size == 0) return false;
    if(from.size > 8 || to.size > 8) return false;
    if(from.is_float != to.is_float) return true;
    return from.size != to.size;
}

static IrValue coerce(IrGenContext *ctx, IrValue v, IrType to) {
    if(v.kind == IR_VAL_NONE) return v;
    if(!needs_cast(v.type, to)) {
        if(v.kind != IR_VAL_LABEL) v.type = to;
        return v;
    }
    if(is_const_int(v) && !to.is_float) {
        v.type = to;
        return v;
    }
    IrValue d = new_temp(ctx, to);
    emit(ctx, IR_CAST, d, v, none_value(), 0);
    return d;
}

static IrValue mul_const(IrGenContext *ctx, IrValue v, long long k) {
    if(k == 1) return v;
    if(is_const_int(v)) {
        v.kind = IR_VAL_CONST_INT;
        v.as.const_i *= k;
        return v;
    }
    IrValue d = new_temp(ctx, type_i64());
    emit(ctx, IR_MUL, d, v, const_int(k, type_i64()), 0);
    return d;
}

static IrValue mul_value(IrGenContext *ctx, IrValue a, IrValue b) {
    if(is_const_int(b)) return mul_const(ctx, a, b.as.const_i);
    if(is_const_int(a)) return mul_const(ctx, b, a.as.const_i);
    IrValue d = new_temp(ctx, type_i64());
    emit(ctx, IR_MUL, d, a, b, 0);
    return d;
}

static IrValue negate_value(IrGenContext *ctx, IrValue v) {
    if(is_const_int(v)) {
        v.as.const_i = -v.as.const_i;
        return v;
    }
    IrValue d = new_temp(ctx, v.type);
    emit(ctx, IR_NEG, d, v, none_value(), 0);
    return d;
}

static IrValue step_const(IrGenContext *ctx, IrType t, long long amount);

static IrInstruction* emit(IrGenContext *ctx, IrOperators op, IrValue dest, IrValue src1, IrValue src2, int aux) {
    IrInstructionList* list = &ctx->instructions;
    LIST_ENSURE(ctx->arena, list, 1024);

    IrInstruction* in = &list->items[list->count++];
    memset(in, 0, sizeof(*in));
    in->op = op;
    in->dest = dest;
    in->src1 = src1;
    in->src2 = src2;
    in->aux = aux;
    return in;
}

static IrInstruction* emit_unused(IrGenContext *ctx, IrOperators op, IrValue dest, IrValue src1, IrValue src2, int aux) {
    IrInstruction* in = emit(ctx, op, dest, src1, src2, aux);
    in->flags |= IRF_RESULT_UNUSED;
    return in;
}

static unsigned char decode_escape(const char *s, size_t len, size_t *i) {
    size_t k = *i + 1;
    if(k >= len) return '\\';

    char c = s[k];
    unsigned char out;
    switch (c)
    {
    case 'n': out = 10; break;
    case 't': out = 9; break;
    case 'r': out = 13; break;
    case '0': out = 0; break;
    case 'a': out = 7; break;
    case 'b': out = 8; break;
    case 'f': out = 12; break;
    case 'v': out = 11; break;
    case 'x': {
        unsigned v = 0;
        int n = 0;
        while(k + 1 < len && n < 2 && isxdigit((unsigned char)s[k + 1])) {
            char h = s[++k];
            v = v * 16 + (unsigned)(h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
            n++;
        }
        out = (unsigned char)v;
        break;
    }
    default: out = (unsigned char)c; break;
    }

    *i = k;
    return out;
}

static View unescape_string(IrGenContext *ctx, View raw) {
    if(raw.len >= 2 && raw.start[0] == '"' && raw.start[raw.len - 1] == '"') {
        raw.start++;
        raw.len -= 2;
    }

    char* out = (char*)arena_alloc(ctx->arena, raw.len + 1);
    size_t n = 0;
    for(size_t i = 0; i < raw.len; ++i) {
        if(raw.start[i] == '\\') out[n++] = (char)decode_escape(raw.start, raw.len, &i);
        else out[n++] = raw.start[i];
    }
    out[n] = 0;
    return (View){ out, n };
}

typedef uint64_t (*PoolHashFn)(const void *items, size_t i);

static void pool_rebuild(Arena *arena, int32_t **index, size_t *index_cap, size_t count, const void *items, PoolHashFn hash) {
    size_t new_cap = *index_cap ? *index_cap * 2 : 64;
    int32_t* idx = (int32_t*)arena_alloc(arena, sizeof(int32_t) * new_cap);
    memset(idx, 0xFF, sizeof(int32_t) * new_cap);

    for(size_t i = 0; i < count; ++i) {
        size_t h = (size_t)hash(items, i) & (new_cap - 1);
        while(idx[h] != -1) h = (h + 1) & (new_cap - 1);
        idx[h] = (int32_t)i;
    }

    *index = idx;
    *index_cap = new_cap;
}

static uint64_t string_entry_hash(const void *items, size_t i) {
    return view_hash(((const IrStringEntry*)items)[i].text);
}

static int intern_string(IrGenContext *ctx, View raw) {
    View txt = unescape_string(ctx, raw);
    IrStringPool* p = &ctx->strings;

    if((p->count + 1) * 2 > p->index_cap)
        pool_rebuild(ctx->arena, &p->index, &p->index_cap, p->count, p->items, string_entry_hash);

    size_t mask = p->index_cap - 1;
    size_t h = (size_t)view_hash(txt) & mask;
    while(p->index[h] != -1) {
        IrStringEntry* e = &p->items[p->index[h]];
        if(e->text.len == txt.len && memcmp(e->text.start, txt.start, txt.len) == 0) return e->id;
        h = (h + 1) & mask;
    }

    LIST_ENSURE(ctx->arena, p, 16);
    int id = (int)p->count;
    p->items[p->count++] = (IrStringEntry){ txt, id };
    p->index[h] = id;
    return id;
}

static uint64_t float_hash_bits(double v, bool single) {
    uint64_t bits;
    memcpy(&bits, &v, sizeof(bits));
    bits ^= single ? 0x9E3779B97F4A7C15ull : 0;
    bits *= 0xff51afd7ed558ccdull;
    return bits ^ (bits >> 32);
}

static uint64_t float_entry_hash(const void *items, size_t i) {
    const IrFloatEntry* e = &((const IrFloatEntry*)items)[i];
    return float_hash_bits(e->value, e->_single);
}

static int intern_float(IrGenContext *ctx, double val, bool single) {
    if(single) val = (double)(float)val;
    IrFloatPool* p = &ctx->floats;

    if((p->count + 1) * 2 > p->index_cap)
        pool_rebuild(ctx->arena, &p->index, &p->index_cap, p->count, p->items, float_entry_hash);

    size_t mask = p->index_cap - 1;
    size_t h = (size_t)float_hash_bits(val, single) & mask;
    while(p->index[h] != -1) {
        IrFloatEntry* e = &p->items[p->index[h]];
        if(e->_single == single && memcmp(&e->value, &val, sizeof(double)) == 0) return e->id;
        h = (h + 1) & mask;
    }

    LIST_ENSURE(ctx->arena, p, 16);
    int id = (int)p->count;
    p->items[p->count++] = (IrFloatEntry){ val, id, single };
    p->index[h] = id;
    return id;
}

static IrValue step_const(IrGenContext *ctx, IrType t, long long amount) {
    if(t.is_float) {
        IrValue v = { 0 };
        v.kind = IR_VAL_CONST_FLOAT;
        v.type = t;
        v.as.float_id = intern_float(ctx, (double)amount, t.size == 4);
        return v;
    }
    return const_int(amount, t);
}

static size_t gen_scope_push(IrGenContext *ctx) {
    return ctx->syms.count;
}

static void gen_scope_pop(IrGenContext *ctx, size_t mark) {
    IrSymList* s = &ctx->syms;
    while(s->count > mark) {
        IrSlotEntry* e = &s->items[--s->count];
        ctx->sym_heads[e->bucket] = e->prev;
    }
}

static IrSlotEntry* sym_insert(IrGenContext *ctx, View name) {
    IrSymList* s = &ctx->syms;
    LIST_ENSURE(ctx->arena, s, 64);

    uint32_t bucket = (uint32_t)(view_hash(name) & (IR_SCOPE_TABLE_SIZE - 1));
    IrSlotEntry* e = &s->items[s->count];
    memset(e, 0, sizeof(*e));
    e->name = name;
    e->global_id = -1;
    e->slot_id = -1;
    e->bucket = bucket;
    e->prev = ctx->sym_heads[bucket];
    ctx->sym_heads[bucket] = (int32_t)s->count;
    s->count++;
    return e;
}

static IrSlotEntry* sym_lookup(IrGenContext *ctx, View name) {
    uint32_t bucket = (uint32_t)(view_hash(name) & (IR_SCOPE_TABLE_SIZE - 1));
    for(int32_t i = ctx->sym_heads[bucket]; i >= 0; i = ctx->syms.items[i].prev) {
        if(svcmp(ctx->syms.items[i].name, name)) return &ctx->syms.items[i];
    }
    return NULL;
}

static int slot_add(IrGenContext *ctx, IrType type, size_t size, size_t align, bool is_param, bool is_array) {
    IrSlotInfoList* l = &ctx->cur_slots;
    LIST_ENSURE(ctx->arena, l, 16);
    int id = (int)l->count;
    l->items[l->count++] = (IrSlotInfo){ (uint32_t)size, (uint32_t)(align ? align : 1), type, is_param, is_array };
    return id;
}

static int scope_declare(IrGenContext *ctx, View name, Typecheck tc, bool is_param) {
    IrType t = ir_type(tc);
    bool indirect = is_param && is_aggregate_ir(t);
    int slot = indirect ? slot_add(ctx, type_ptr(), 8, 8, true, false)
                        : slot_add(ctx, t, sizeof_tc(tc), align_tc(tc), is_param, false);
    IrSlotEntry* e = sym_insert(ctx, name);
    e->slot_id = slot;
    e->type = t;
    e->indirect = indirect;
    return slot;
}

static IrFuncInfo* func_get_or_create(IrGenContext *ctx, View name) {
    uint64_t h = view_hash(name) & (IR_FUNC_TABLE_SIZE - 1);
    for(IrFuncInfo* f = ctx->func_buckets[h]; f != NULL; f = f->next) {
        if(svcmp(f->name, name)) return f;
    }

    IrFuncInfo* f = (IrFuncInfo*)arena_alloc(ctx->arena, sizeof(IrFuncInfo));
    memset(f, 0, sizeof(*f));
    f->name = name;
    f->func_id = (int)ctx->funcs.count;
    f->next = ctx->func_buckets[h];
    ctx->func_buckets[h] = f;

    LIST_ENSURE(ctx->arena, &ctx->funcs, 32);
    ctx->funcs.items[ctx->funcs.count++] = f;
    return f;
}

static IrGlobal* global_find(IrGenContext *ctx, View name) {
    uint64_t h = view_hash(name) & (IR_GLOBAL_TABLE_SIZE - 1);
    for(IrGlobal* g = ctx->global_buckets[h]; g != NULL; g = g->next) {
        if(svcmp(g->name, name)) return g;
    }
    return NULL;
}

static IrGlobal* global_get_or_create(IrGenContext *ctx, View name, bool *created) {
    IrGlobal* g = global_find(ctx, name);
    if(g) { *created = false; return g; }

    uint64_t h = view_hash(name) & (IR_GLOBAL_TABLE_SIZE - 1);
    g = (IrGlobal*)arena_alloc(ctx->arena, sizeof(IrGlobal));
    memset(g, 0, sizeof(*g));
    g->name = name;
    g->id = (int)ctx->globals.count;
    g->next = ctx->global_buckets[h];
    ctx->global_buckets[h] = g;

    LIST_ENSURE(ctx->arena, &ctx->globals, 32);
    ctx->globals.items[ctx->globals.count++] = g;
    *created = true;
    return g;
}

static int lookup_or_create_label(IrGenContext *ctx, View name) {
    uint64_t h = view_hash(name) & (IR_LABEL_TABLE_SIZE - 1);
    for(IrLabelEntry* e = ctx->label_buckets[h]; e != NULL; e = e->next) {
        if(svcmp(e->name, name)) return e->label_id;
    }

    IrLabelEntry* e = (IrLabelEntry*)arena_alloc(ctx->arena, sizeof(IrLabelEntry));
    e->name = name;
    e->label_id = new_label(ctx);
    e->next = ctx->label_buckets[h];
    ctx->label_buckets[h] = e;
    return e->label_id;
}

static long long const_op_value(Node *node) {
    if(node->kind == NODE_LITERAL && (node->ast.literal.kind == INT || node->ast.literal.kind == BIG))
        return node->ast.literal.i64;
    if(node->kind == NODE_ENUM_CALL)
        return node->ast.enum_access.iresolved_type;
    return -1;
}

static bool iscompoundop(TokenType op) {
    return isarith_compoundop(op) || isbitwise_compoundop(op);
}

static bool is_cmp_op(TokenType op) {
    return op == OPLT || op == OPLE || op == OPGT || op == OPGE || op == OPEQ || op == OPBANGE;
}

static IrOperators compound_to_binop(TokenType op) {
    switch (op)
    {
    case OPPLUSE: return IR_ADD;
    case OPMINUSE: return IR_SUB;
    case OPSTARE: return IR_MUL;
    case OPSLASHE: return IR_DIV;
    case OPMODE: return IR_MOD;
    case OPSHLE: return IR_SHL;
    case OPSHRE: return IR_SHR;
    case OPANDE: return IR_BAND;
    case OPORE: return IR_BOR;
    case OPXORE: return IR_BXOR;
    default: return IR_ADD;
    }
}

static IrOperators binary_to_ir(TokenType op) {
    switch (op)
    {
    case OPPLUS: return IR_ADD;
    case OPMINUS: return IR_SUB;
    case OPSLASH: return IR_DIV;
    case OPMOD: return IR_MOD;
    case OPSTAR: return IR_MUL;
    case OPAND: return IR_BAND;
    case OPXOR: return IR_BXOR;
    case OPOR: return IR_BOR;
    case OPLT: return IR_CMP_LT;
    case OPLE: return IR_CMP_LE;
    case OPGT: return IR_CMP_GT;
    case OPGE: return IR_CMP_GE;
    case OPSHL: return IR_SHL;
    case OPSHR: return IR_SHR;
    case OPEQ: return IR_CMP_EQ;
    case OPBANGE: return IR_CMP_NE;
    default: return IR_ADD;
    }
}

static IrOperators fused_jump(TokenType op, bool when_true) {
    switch (op)
    {
    case OPLT: return when_true ? IR_JMP_LT : IR_JMP_GE;
    case OPLE: return when_true ? IR_JMP_LE : IR_JMP_GT;
    case OPGT: return when_true ? IR_JMP_GT : IR_JMP_LE;
    case OPGE: return when_true ? IR_JMP_GE : IR_JMP_LT;
    case OPEQ: return when_true ? IR_JMP_EQ : IR_JMP_NE;
    default: return when_true ? IR_JMP_NE : IR_JMP_EQ;
    }
}

static bool atomic_size_ok(uint32_t s) {
    return s == 1 || s == 2 || s == 4 || s == 8;
}

static IrType atomic_elem_type(IrType ptr) {
    IrType t = { 0 };
    if(ptr.ptr_lvl > 1) {
        t.size = 8;
        t.ptr_lvl = (uint8_t)(ptr.ptr_lvl - 1);
        t.step = 1;
        return t;
    }
    t.size = ptr.ptr_lvl ? ptr.step : 8;
    return t;
}

static IrValue gen_var_ref(IrGenContext *ctx, Node *node) {
    View name = node->ast.var_call.name;
    IrValue v = { 0 };
    v.type = ir_type(node->resolved_type);

    IrSlotEntry* e = sym_lookup(ctx, name);
    if(e) {
        bool fixed_array = e->is_array && !e->is_vla;
        if(e->global_id >= 0) {
            v.kind = fixed_array ? IR_VAL_GLOBAL_ADDR : IR_VAL_GLOBAL;
            v.as.global_id = e->global_id;
        } else {
            v.kind = fixed_array ? IR_VAL_SLOT_ADDR : IR_VAL_SLOT;
            v.as.slot_id = e->slot_id;
        }
        if(e->is_array) v.type = e->type;
        return v;
    }

    IrGlobal* g = global_find(ctx, name);
    if(g) {
        v.kind = g->is_array ? IR_VAL_GLOBAL_ADDR : IR_VAL_GLOBAL;
        v.as.global_id = g->id;
        if(g->is_array) v.type = g->type;
        return v;
    }

    FuncEntry* fe = function_table_lookup(name);
    if(fe && !fe->_builtin) {
        IrFuncInfo* f = func_get_or_create(ctx, name);
        v.kind = IR_VAL_FUNC;
        v.as.func_id = f->func_id;
        return v;
    }

    ctx->unsupported++;
    v.kind = IR_VAL_CONST_INT;
    return v;
}

static IrLval gen_lval(IrGenContext *ctx, Node *node) {
    IrLval lv;
    memset(&lv, 0, sizeof(lv));
    lv.type = ir_type(node->resolved_type);

    switch (node->kind)
    {
    case NODE_VAR_CALL: {
        IrSlotEntry* e = sym_lookup(ctx, node->ast.var_call.name);
        if(e && e->indirect) {                    /* parametro aggregate: o slot guarda o ponteiro */
            IrValue p = { 0 };
            p.kind = IR_VAL_SLOT;
            p.as.slot_id = e->slot_id;
            p.type = type_ptr();
            lv.base = p;
            lv.memory = true;
            lv.type = e->type;
            return lv;
        }
        lv.base = gen_var_ref(ctx, node);
        lv.type = lv.base.type;
        return lv;
    }

    case NODE_FIELD_CALL: {
        int off = (int)node->ast.field_access.offset;
        if(node->ast.field_access.arrow) {
            lv.base = gen_expr(ctx, node->ast.field_access.base);
            lv.memory = true;
            lv.offset = off;
            return lv;
        }
        IrType ft = lv.type;
        lv = gen_lval(ctx, node->ast.field_access.base);
        if(lv.memory) lv.offset += off;
        else lv.base.field_offset += off;
        lv.type = ft;
        if(!lv.memory) lv.base.type = ft;
        return lv;
    }

    case NODE_ARRAY:
        lv.base = gen_array_address(ctx, node, &lv.partial);
        lv.memory = true;
        return lv;

    case NODE_UNARY_OP:
        if(node->ast.unary.op == OPSTAR) {
            lv.base = gen_expr(ctx, node->ast.unary.operand);
            lv.memory = true;
            return lv;
        }
        break;

    case NODE_FUNC_CALL:                          /* f().campo */
    case NODE_TERNARY:                            /* (c ? a : b).campo */
        if(is_aggregate_ir(lv.type)) {
            lv.base = agg_temp_slot(ctx, lv.type);
            gen_agg_into(ctx, &lv, node, true);
            return lv;
        }
        break;

    default: break;
    }

    lv.base = gen_expr(ctx, node);
    return lv;
}

static void lval_address(const IrLval *lv, IrValue *addr, int *off) {
    if(lv->memory) { *addr = lv->base; *off = lv->offset; }
    else { *addr = addr_of(lv->base); *off = 0; }
}

static IrValue lval_addr_value(IrGenContext *ctx, const IrLval *lv) {
    IrValue a;
    int off;
    lval_address(lv, &a, &off);

    if(off != 0) {
        if(a.kind == IR_VAL_SLOT_ADDR || a.kind == IR_VAL_GLOBAL_ADDR) {
            a.field_offset += off;
        } else {
            IrValue d = new_temp(ctx, type_ptr());
            emit(ctx, IR_ADD, d, a, const_int(off, type_i64()), 0);
            a = d;
        }
    }
    a.type = type_ptr();
    return a;
}

static IrValue agg_temp_slot(IrGenContext *ctx, IrType t) {
    IrValue v = { 0 };
    v.kind = IR_VAL_SLOT;
    v.as.slot_id = slot_add(ctx, t, t.size, 8, false, false);
    v.type = t;
    return v;
}

static IrValue lval_load(IrGenContext *ctx, const IrLval *lv) {
    if(is_aggregate_ir(lv->type)) {          /* aggregate nunca cabe em registrador */
        ctx->unsupported++;
        return const_int(0, type_i64());
    }

    if(!lv->memory && !lv->type.is_atomic) return lv->base;

    IrValue addr;
    int off;
    lval_address(lv, &addr, &off);

    IrValue dest = new_temp(ctx, lv->type);
    emit(ctx, lv->type.is_atomic ? IR_ATOMIC_LOAD : IR_LOAD_INDIRECT, dest, addr, none_value(), off);
    return dest;
}

static void lval_store(IrGenContext *ctx, const IrLval *lv, IrValue value) {
    value = coerce(ctx, value, lv->type);

    if(!lv->memory && !lv->type.is_atomic) {
        emit(ctx, IR_ASSIGN, lv->base, value, none_value(), 0);
        return;
    }

    IrValue addr;
    int off;
    lval_address(lv, &addr, &off);
    emit(ctx, lv->type.is_atomic ? IR_ATOMIC_STORE : IR_STORE_INDIRECT, none_value(), addr, value, off);
}

static void gen_memcopy(IrGenContext *ctx, const IrLval *dst, const IrLval *src, size_t size) {
    IrValue da, sa;
    int doff, soff;
    lval_address(dst, &da, &doff);
    lval_address(src, &sa, &soff);

    IrInstruction* in = emit(ctx, IR_MEMCOPY, none_value(), da, sa, doff);
    in->aux2 = soff;
    in->src3 = const_int((long long)size, type_i64());
}

static void gen_agg_into(IrGenContext *ctx, const IrLval *dst, Node *src, bool fresh) {
    size_t size = dst->type.size;

    if(is_agg_call(src)) {
        if(fresh) {
            gen_func_call_ex(ctx, src, true, dst);
            return;
        }
        IrLval t;
        memset(&t, 0, sizeof(t));
        t.base = agg_temp_slot(ctx, dst->type);
        t.type = dst->type;
        gen_func_call_ex(ctx, src, true, &t);
        gen_memcopy(ctx, dst, &t, size);
        return;
    }

    if(src->kind == NODE_TERNARY) {
        int l_else = new_label(ctx), l_end = new_label(ctx);
        gen_cond_jump(ctx, src->ast.if_stmt.condition, l_else, false);
        gen_agg_into(ctx, dst, src->ast.if_stmt.then, fresh);
        emit(ctx, IR_JMP, label_value(l_end), none_value(), none_value(), 0);
        emit_label(ctx, l_else);
        gen_agg_into(ctx, dst, src->ast.if_stmt.otherwise, fresh);
        emit_label(ctx, l_end);
        return;
    }

    if(is_lvalue_node(src)) {
        IrLval sl = gen_lval(ctx, src);
        gen_memcopy(ctx, dst, &sl, size);
        return;
    }

    ctx->unsupported++;      /* unica porta de saida para origem de aggregate nao tratada */
}

/* Argumento indireto: copia para um temporario do chamador e devolve o endereco. */
static IrValue gen_agg_arg(IrGenContext *ctx, Node *arg, IrType t) {
    IrLval tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.base = agg_temp_slot(ctx, t);
    tmp.type = t;
    gen_agg_into(ctx, &tmp, arg, true);
    return lval_addr_value(ctx, &tmp);
}

static IrValue gen_atomic_rmw(IrGenContext *ctx, IrOperators bop, const IrLval *lv, IrValue operand,
                              bool unused, bool return_old) {
    IrValue addr;
    int off;
    lval_address(lv, &addr, &off);

    IrType t = lv->type;
    operand = coerce(ctx, operand, t);

    if(!atomic_size_ok(t.size) || t.is_float) ctx->unsupported++;

    if(bop == IR_ADD || bop == IR_SUB) {
        IrValue amount = (bop == IR_SUB) ? negate_value(ctx, operand) : operand;

        if(unused) {
            emit_unused(ctx, IR_ATOMIC_ADD, none_value(), addr, amount, off);
            return none_value();
        }

        IrValue old = new_temp(ctx, t);
        emit(ctx, IR_ATOMIC_ADD, old, addr, amount, off);
        if(return_old) return old;

        IrValue nw = new_temp(ctx, t);
        emit(ctx, IR_ADD, nw, old, amount, 0);
        return nw;
    }

    if(unused && (bop == IR_BAND || bop == IR_BOR || bop == IR_BXOR)) {
        IrOperators aop = bop == IR_BAND ? IR_ATOMIC_AND : (bop == IR_BOR ? IR_ATOMIC_OR : IR_ATOMIC_XOR);
        emit_unused(ctx, aop, none_value(), addr, operand, off);
        return none_value();
    }

    int loop = new_label(ctx);
    emit(ctx, IR_LABEL, label_value(loop), none_value(), none_value(), 0);

    IrValue old = new_temp(ctx, t);
    emit(ctx, IR_ATOMIC_LOAD, old, addr, none_value(), off);

    IrValue nw = new_temp(ctx, t);
    emit(ctx, bop, nw, old, operand, 0);

    IrValue found = new_temp(ctx, t);
    IrInstruction* cas = emit(ctx, IR_ATOMIC_CAS, found, addr, old, off);
    cas->src3 = nw;

    emit(ctx, IR_JMP_NE, label_value(loop), found, old, 0);
    return return_old ? old : nw;
}

static IrValue gen_atomic_call(IrGenContext *ctx, Node *node, bool unused) {
    IrValue zero = const_int(0, type_i64());
    NodeList* args = node->ast.func_call.args;

    if(!args || args->count < 1) { ctx->unsupported++; return zero; }

    long long op = const_op_value(args->items[0]);

    if(op == ATOMIC_OP_FENCE) {
        emit(ctx, IR_FENCE, none_value(), none_value(), none_value(), 0);
        return zero;
    }

    size_t need = (op == ATOMIC_OP_CAS) ? 4 : (op == ATOMIC_OP_LOAD ? 2 : 3);
    if(op < ATOMIC_OP_FENCE || op > ATOMIC_OP_FETCH_XOR || args->count < need) {
        ctx->unsupported++;
        return zero;
    }

    IrValue ptr = gen_expr(ctx, args->items[1]);
    IrType et = atomic_elem_type(ptr.type);
    IrType rt = ir_type(node->resolved_type);
    if(rt.size == et.size && !rt.is_float && !rt.ptr_lvl) et.is_signed = rt.is_signed;
    if(!atomic_size_ok(et.size)) ctx->unsupported++;

    switch (op)
    {
    case ATOMIC_OP_LOAD: {
        IrValue dest = new_temp(ctx, et);
        emit(ctx, IR_ATOMIC_LOAD, dest, ptr, none_value(), 0);
        return dest;
    }

    case ATOMIC_OP_STORE: {
        IrValue value = coerce(ctx, gen_expr(ctx, args->items[2]), et);
        emit(ctx, IR_ATOMIC_STORE, none_value(), ptr, value, 0);
        return zero;
    }

    case ATOMIC_OP_XCHG: {
        IrValue value = coerce(ctx, gen_expr(ctx, args->items[2]), et);
        IrValue dest = new_temp(ctx, et);
        emit(ctx, IR_ATOMIC_XCHG, dest, ptr, value, 0);
        return dest;
    }

    case ATOMIC_OP_CAS: {
        IrValue expected = coerce(ctx, gen_expr(ctx, args->items[2]), et);
        IrValue desired = coerce(ctx, gen_expr(ctx, args->items[3]), et);
        IrValue dest = new_temp(ctx, et);
        IrInstruction* in = emit(ctx, IR_ATOMIC_CAS, dest, ptr, expected, 0);
        in->src3 = desired;
        return dest;
    }

    default: {
        IrOperators bop = op == ATOMIC_OP_FETCH_ADD ? IR_ADD :
                          op == ATOMIC_OP_FETCH_SUB ? IR_SUB :
                          op == ATOMIC_OP_FETCH_AND ? IR_BAND :
                          op == ATOMIC_OP_FETCH_OR  ? IR_BOR  : IR_BXOR;
        IrValue operand = gen_expr(ctx, args->items[2]);
        IrLval lv;
        memset(&lv, 0, sizeof(lv));
        lv.base = ptr;
        lv.memory = true;
        lv.type = et;
        return gen_atomic_rmw(ctx, bop, &lv, operand, unused, true);
    }
    }
}

static IrValue gen_bytes(Node *node) {
    IrValue v = { 0 };
    v.kind = IR_VAL_CONST_UINT;
    v.as.const_ui = node->ast.bytes.size;
    v.type = ir_type(node->resolved_type);
    return v;
}

static IrValue gen_enum_access(Node *node) {
    return const_int(node->ast.enum_access.iresolved_type, ir_type(node->resolved_type));
}

static IrValue gen_literal(IrGenContext *ctx, Node *node) {
    IrValue v = { 0 };
    v.type = ir_type(node->resolved_type);

    switch (node->ast.literal.kind)
    {
    case UNSIGNED:
    case HEXA:
        v.kind = IR_VAL_CONST_UINT;
        v.as.const_ui = node->ast.literal.u64;
        break;
    case INT:
    case BIG:
        v.kind = IR_VAL_CONST_INT;
        v.as.const_i = node->ast.literal.i64;
        break;
    case DOUBLE:
    case FLOAT: {
        bool single = (node->resolved_type.repr == KFLOAT && node->resolved_type.ptr_lvl == 0);
        v.kind = IR_VAL_CONST_FLOAT;
        v.as.float_id = intern_float(ctx, node->ast.literal.f64, single);
        break;
    }
    case CHAR: {
        const char* s = node->ast.literal.character.start;
        size_t len = node->ast.literal.character.len;
        unsigned char c = len ? (unsigned char)s[0] : 0;
        if(c == '\\' && len > 1) { size_t i = 0; c = decode_escape(s, len, &i); }
        v.kind = IR_VAL_CONST_UINT;
        v.as.const_ui = c;
        break;
    }
    case STRING:
        v.kind = IR_VAL_CONST_STRING;
        v.as.string_id = intern_string(ctx, node->ast.literal.string);
        if(!v.type.ptr_lvl) v.type = type_ptr();
        break;
    default:
        v.kind = IR_VAL_CONST_INT;
        break;
    }

    return v;
}

static IrValue gen_var_call(IrGenContext *ctx, Node *node) {
    IrValue ref = gen_var_ref(ctx, node);
    if(ref.type.is_atomic && (ref.kind == IR_VAL_SLOT || ref.kind == IR_VAL_GLOBAL)) {
        IrValue dest = new_temp(ctx, ref.type);
        emit(ctx, IR_ATOMIC_LOAD, dest, addr_of(ref), none_value(), 0);
        return dest;
    }
    return ref;
}

static IrValue gen_array_address(IrGenContext *ctx, Node *node, bool *partial) {
    size_t depth = 0;
    for(Node* p = node; p->kind == NODE_ARRAY; p = p->ast.binary.left) depth++;

    Node* level_buf[8];
    Node** levels = depth <= 8 ? level_buf : (Node**)arena_alloc(ctx->arena, sizeof(Node*) * depth);

    Node* cur = node;
    for(size_t i = depth; i-- > 0; ) {
        levels[i] = cur;
        cur = cur->ast.binary.left;
    }

    Node* base = cur;
    IrValue* strides = NULL;
    size_t dim_total = 0;

    if(base->kind == NODE_VAR_CALL) {
        View name = base->ast.var_call.name;
        IrSlotEntry* e = sym_lookup(ctx, name);
        if(e) {
            if(e->is_array) { strides = e->dim_strides; dim_total = e->dim_count; }
        } else {
            IrGlobal* g = global_find(ctx, name);
            if(g && g->is_array) { strides = g->dim_strides; dim_total = g->dim_count; }
        }
    }

    IrValue addr = gen_expr(ctx, base);
    size_t dims_left = dim_total;

    for(size_t d = 0; d < depth; ++d) {
        IrValue idx = coerce(ctx, gen_expr(ctx, levels[d]->ast.binary.right), type_i64());

        IrValue off;
        if(dims_left > 0) {
            off = mul_value(ctx, idx, strides[dim_total - dims_left]);
            dims_left--;
        } else {
            off = mul_const(ctx, idx, addr.type.step ? addr.type.step : 1);
        }

        if(is_const_int(off) && (addr.kind == IR_VAL_SLOT_ADDR || addr.kind == IR_VAL_GLOBAL_ADDR)) {
            addr.field_offset += (int)off.as.const_i;
        } else {
            IrValue sum = new_temp(ctx, type_ptr());
            emit(ctx, IR_ADD, sum, addr, off, 0);
            addr = sum;
        }

        if(d + 1 < depth && dims_left == 0) {
            IrValue p = new_temp(ctx, ir_type(levels[d]->resolved_type));
            emit(ctx, IR_LOAD_INDIRECT, p, addr, none_value(), 0);
            addr = p;
        }
    }

    if(partial) *partial = dims_left > 0;
    addr.type = type_ptr();
    return addr;
}

static IrValue gen_lval_rvalue(IrGenContext *ctx, Node *node) {
    IrLval lv = gen_lval(ctx, node);
    if(lv.partial) return lv.base;

    /* campo do tipo array: o valor é o ENDEREÇO do campo (decay), não o conteúdo */
    if(node->kind == NODE_FIELD_CALL && node->resolved_type._array) {
        IrType pt = ir_type(node->resolved_type);   /* ptr_lvl+1, step = tamanho do elemento */
        IrValue addr;

        if(lv.memory) {
            if(lv.offset == 0) {
                addr = lv.base;
            } else {
                addr = new_temp(ctx, pt);
                emit(ctx, IR_ADD, addr, lv.base, const_int(lv.offset, type_i64()), 0);
            }
        } else {
            addr = addr_of(lv.base);   /* struct local: SLOT_ADDR + field_offset */
        }

        addr.type = pt;
        return addr;
    }

    return lval_load(ctx, &lv);
}

static IrValue gen_assign(IrGenContext *ctx, Node *node, bool unused) {
    (void)unused;
    IrLval lv = gen_lval(ctx, node->ast.binary.left);

    if(is_aggregate_ir(lv.type)) {
        gen_agg_into(ctx, &lv, node->ast.binary.right, false);
        return const_int(0, type_i64());
    }

    IrValue src = gen_expr(ctx, node->ast.binary.right);

    if(!lv.memory && !lv.type.is_atomic && (lv.base.kind == IR_VAL_SLOT || lv.base.kind == IR_VAL_GLOBAL)) {
        src = coerce(ctx, src, lv.type);
        emit(ctx, IR_ASSIGN, lv.base, src, none_value(), 0);
        return lv.base;
    }

    lval_store(ctx, &lv, src);
    return src;
}

static IrValue gen_compound(IrGenContext *ctx, Node *node, bool unused) {
    TokenType op = node->ast.binary.op;
    IrOperators bop = compound_to_binop(op);

    IrLval lv = gen_lval(ctx, node->ast.binary.left);
    IrValue operand = gen_expr(ctx, node->ast.binary.right);

    if(lv.type.ptr_lvl > 0 && (bop == IR_ADD || bop == IR_SUB)) {
        operand = mul_const(ctx, coerce(ctx, operand, type_i64()), lv.type.step);
    }

    if(lv.type.is_atomic) return gen_atomic_rmw(ctx, bop, &lv, operand, unused, false);

    bool shift = (bop == IR_SHL || bop == IR_SHR);

    if(!lv.memory) {
        if(!shift) operand = coerce(ctx, operand, lv.type);
        emit(ctx, bop, lv.base, lv.base, operand, 0);
        return lv.base;
    }

    IrValue current = lval_load(ctx, &lv);
    if(!shift) operand = coerce(ctx, operand, lv.type);
    IrValue result = new_temp(ctx, lv.type);
    emit(ctx, bop, result, current, operand, 0);
    lval_store(ctx, &lv, result);
    return result;
}

static IrValue gen_incdec(IrGenContext *ctx, Node *operand_node, long long delta, bool postfix, bool unused) {
    IrLval lv = gen_lval(ctx, operand_node);
    long long step = lv.type.ptr_lvl > 0 ? lv.type.step : 1;
    IrValue amount = step_const(ctx, lv.type, delta * step);

    if(lv.type.is_atomic) return gen_atomic_rmw(ctx, IR_ADD, &lv, amount, unused, postfix);

    if(!lv.memory) {
        if(postfix && !unused) {
            IrValue old = new_temp(ctx, lv.type);
            emit(ctx, IR_ASSIGN, old, lv.base, none_value(), 0);
            emit(ctx, IR_ADD, lv.base, lv.base, amount, 0);
            return old;
        }
        emit(ctx, IR_ADD, lv.base, lv.base, amount, 0);
        return lv.base;
    }

    IrValue current = lval_load(ctx, &lv);
    IrValue result = new_temp(ctx, lv.type);
    emit(ctx, IR_ADD, result, current, amount, 0);
    lval_store(ctx, &lv, result);
    return postfix ? current : result;
}

static IrValue gen_unary_op(IrGenContext *ctx, Node *node, bool unused) {
    Node* operand_node = node->ast.unary.operand;
    IrType rt = ir_type(node->resolved_type);

    switch (node->ast.unary.op)
    {
    case OPPLUSPLUS: return gen_incdec(ctx, operand_node, 1, node->kind == NODE_POSTFIX_OP, unused);
    case OPMINUSMINUS: return gen_incdec(ctx, operand_node, -1, node->kind == NODE_POSTFIX_OP, unused);

    case OPAND: {   /* &x */
        IrLval lv = gen_lval(ctx, operand_node);
        if(!lv.memory) {
            IrValue a = addr_of(lv.base);
            if(rt.ptr_lvl) a.type = rt;
            return a;
        }
        if(lv.offset == 0) { lv.base.type = rt.ptr_lvl ? rt : lv.base.type; return lv.base; }
        IrValue d = new_temp(ctx, rt.ptr_lvl ? rt : type_ptr());
        emit(ctx, IR_ADD, d, lv.base, const_int(lv.offset, type_i64()), 0);
        return d;
    }

    case OPSTAR: {
        IrLval lv = gen_lval(ctx, node);
        return lval_load(ctx, &lv);
    }

    case OPMINUS: {
        IrValue v = gen_expr(ctx, operand_node);
        if(v.kind == IR_VAL_CONST_INT) { v.as.const_i = -v.as.const_i; v.type = rt; return v; }
        v = coerce(ctx, v, rt);
        IrValue d = new_temp(ctx, rt);
        emit(ctx, IR_NEG, d, v, none_value(), 0);
        return d;
    }

    case OPBANG: {
        IrValue v = gen_expr(ctx, operand_node);
        IrValue d = new_temp(ctx, rt);
        emit(ctx, IR_LNOT, d, v, none_value(), 0);
        return d;
    }

    case DESC: {
        IrValue v = coerce(ctx, gen_expr(ctx, operand_node), rt);
        IrValue d = new_temp(ctx, rt);
        emit(ctx, IR_NOT, d, v, none_value(), 0);
        return d;
    }

    default: return gen_expr(ctx, operand_node);
    }
}

static IrValue gen_logical_value(IrGenContext *ctx, Node *node) {
    IrType rt = ir_type(node->resolved_type);
    if(rt.size == 0) { rt = type_i64(); }

    IrValue result = new_temp(ctx, rt);
    int l_false = new_label(ctx);
    int l_end = new_label(ctx);

    gen_cond_jump(ctx, node, l_false, false);
    emit(ctx, IR_ASSIGN, result, const_int(1, rt), none_value(), 0);
    emit(ctx, IR_JMP, label_value(l_end), none_value(), none_value(), 0);
    emit(ctx, IR_LABEL, label_value(l_false), none_value(), none_value(), 0);
    emit(ctx, IR_ASSIGN, result, const_int(0, rt), none_value(), 0);
    emit(ctx, IR_LABEL, label_value(l_end), none_value(), none_value(), 0);
    return result;
}

static IrValue gen_binary_op(IrGenContext *ctx, Node *node, bool unused) {
    TokenType op = node->ast.binary.op;
    Node* left = node->ast.binary.left;
    Node* right = node->ast.binary.right;

    if(op == ASSIGN) return gen_assign(ctx, node, unused);
    if(iscompoundop(op)) return gen_compound(ctx, node, unused);
    if(op == LOGAND || op == LOGOR) return gen_logical_value(ctx, node);

    IrType lt = ir_type(left->resolved_type);
    IrType rt = ir_type(right->resolved_type);
    IrType nt = ir_type(node->resolved_type);

    if(op == OPMINUS && lt.ptr_lvl > 0 && rt.ptr_lvl > 0) {
        IrValue a = gen_expr(ctx, left);
        IrValue b = gen_expr(ctx, right);
        IrValue raw = new_temp(ctx, type_i64());
        emit(ctx, IR_SUB, raw, a, b, 0);
        if(lt.step == 1) return raw;
        IrValue d = new_temp(ctx, nt.size ? nt : type_i64());
        emit(ctx, IR_DIV, d, raw, const_int(lt.step, type_i64()), 0);
        return d;
    }

    if((op == OPPLUS || op == OPMINUS) && (lt.ptr_lvl > 0) != (rt.ptr_lvl > 0)) {
        bool left_is_ptr = lt.ptr_lvl > 0;
        IrType pt = left_is_ptr ? lt : rt;

        IrValue lv = gen_expr(ctx, left);
        IrValue rv = gen_expr(ctx, right);
        IrValue p = left_is_ptr ? lv : rv;
        IrValue i = left_is_ptr ? rv : lv;

        IrValue scaled = mul_const(ctx, coerce(ctx, i, type_i64()), pt.step);
        IrValue d = new_temp(ctx, nt.ptr_lvl ? nt : pt);
        emit(ctx, op == OPPLUS ? IR_ADD : IR_SUB, d, p, scaled, 0);
        return d;
    }

    bool cmp = is_cmp_op(op);
    bool shift = (op == OPSHL || op == OPSHR);

    IrType ot = cmp ? common_type(lt, rt) : nt;
    if(ot.size == 0) ot = common_type(lt, rt);

    IrValue a = gen_expr(ctx, left);
    IrValue b = gen_expr(ctx, right);
    a = coerce(ctx, a, ot);
    if(!shift) b = coerce(ctx, b, ot);

    IrValue d = new_temp(ctx, nt.size ? nt : ot);
    emit(ctx, binary_to_ir(op), d, a, b, 0);
    return d;
}

static IrValue gen_va_start(IrGenContext *ctx, Node *node) {
    IrValue d = new_temp(ctx, ir_type(node->resolved_type));
    emit(ctx, IR_VA_START, d, none_value(), none_value(), 0);
    return d;
}

static IrValue gen_intrinsic(IrGenContext *ctx, Node *node, const IntrinInfo* info) {
    NodeList* a = node->ast.func_call.args;
    if (!a || (int)a->count != info->argc) { ctx->unsupported++; return const_int(0, type_i64()); }

    IrType word = type_i64();
    word.is_signed = 0;

    IrValue v[3];
    memset(v, 0, sizeof v);
    for (int i = 0; i < info->argc; ++i) {
        IrValue x = gen_expr(ctx, a->items[i]);

        if (info->id == INTR_FPBITS) { v[i] = x; continue; }

        if (info->id == INTR_CLZ) {
            IrType ct = word;
            if (!x.type.ptr_lvl && x.type.size > 0 && x.type.size <= 4) ct.size = 4;
            v[i] = coerce(ctx, x, ct);
            continue;
        }

        bool is_ptr = (i < 2 && info->id != INTR_CTZ && info->id != INTR_UMULH
                   && info->id != INTR_BITSD && info->id != INTR_BITSF); 
        v[i] = coerce(ctx, x, is_ptr ? type_ptr() : word);
    }

    bool has_ret = info->ret != KVOID;
    IrValue dest = has_ret ? new_temp(ctx, ir_type(node->resolved_type)) : none_value();
    IrInstruction* in = emit(ctx, IR_INTRIN, dest, v[0], v[1], info->id);
    in->src3 = v[2];
    return has_ret ? dest : const_int(0, type_i64());
}

static const IntrinInfo INTRINSICS[] = {
    { "__vdiff16",   INTR_VDIFF16,   2, KUINT32 },
    { "__veq64",     INTR_VEQ64,     2, KUINT32 },
    { "__ctz",       INTR_CTZ,       1, KUINT32 },
    { "__vcopy16x2", INTR_VCOPY16X2, 3, KVOID   },
    { "__vcopy32x2", INTR_VCOPY32X2, 3, KVOID   },
    { "__vcopy64",   INTR_VCOPY64,   2, KVOID   },
    { "__repmovsb",  INTR_REPMOVSB,  3, KVOID   },
    { "__vnull16",   INTR_VNULL16,   1, KUINT32 },
    { "__vnull64",   INTR_VNULL64,   1, KUINT32 },
    { "__vfill16x2", INTR_VFILL16X2, 3, KVOID },
    { "__vfill32x2", INTR_VFILL32X2, 3, KVOID },
    { "__vfill64",   INTR_VFILL64,   2, KVOID },
    { "__repstosb",  INTR_REPSTOSB,  3, KVOID },
    { "__vfill64x2", INTR_VFILL64X2, 3, KVOID   },
    { "__vfind16",   INTR_VFIND16,   2, KUINT32 },
    { "__vfind64",   INTR_VFIND64,   2, KUINT64 },
    { "__clz",       INTR_CLZ,       1, KUINT32 },
    { "__mem_barrier", INTR_MEM_BARRIER, 1, KVOID },
    { "__vfindw16",  INTR_VFINDW16,  2, KUINT32 },
    { "__vcmpz16",  INTR_VCMPZ16,  2, KUINT32 },
    { "__vfindz16", INTR_VFINDZ16, 2, KUINT32 },
    { "__vanyz64",  INTR_VANYZ64,  2, KUINT32 },
    { "__umulh",  INTR_UMULH,  2, KUINT64 },
    { "__fpbits", INTR_FPBITS, 1, KUINT64 },
    { "__bitsd",  INTR_BITSD,  1, KDOUBLE },
    { "__bitsf",  INTR_BITSF,  1, KFLOAT },
};

#define INTRINSIC_COUNT ((int)(sizeof(INTRINSICS) / sizeof(INTRINSICS[0])))

static inline const IntrinInfo* intrinsic_find(View name) {
    for (int i = 0; i < INTRINSIC_COUNT; ++i) {
        size_t n = strlen(INTRINSICS[i].name);
        if (n == name.len && memcmp(INTRINSICS[i].name, name.start, n) == 0) return &INTRINSICS[i];
    }
    return NULL;
}

static bool is_expect_call(Node *n) {
    return n && n->kind == NODE_FUNC_CALL && svcmp(n->ast.func_call.name, VIEW_LIT("__builtin_expected"));
}

static bool expect_value(Node *n, long long *out) {
    if(n->kind == NODE_LITERAL) {
        switch (n->ast.literal.kind)
        {
        case INT: case BIG: *out = n->ast.literal.i64; return true;
        case UNSIGNED: case HEXA: *out = (long long)n->ast.literal.u64; return true;
        default: return false;
        }
    }
    if(n->kind == NODE_ENUM_CALL) { *out = n->ast.enum_access.iresolved_type; return true; }
    if(n->kind == NODE_UNARY_OP && n->ast.unary.op == OPMINUS) {
        long long v;
        if(expect_value(n->ast.unary.operand, &v)) { *out = -v; return true; }
    }
    return false;
}

static int cond_hint(Node *cond) {
    int flip = 1;
    while(cond && cond->kind == NODE_UNARY_OP && cond->ast.unary.op == OPBANG) {
        flip = -flip;
        cond = cond->ast.unary.operand;
    }
    if(!is_expect_call(cond)) return 0;

    NodeList* a = cond->ast.func_call.args;
    long long v;
    if(!a || a->count != 2 || !expect_value(a->items[1], &v)) return 0;
    return (v != 0 ? 1 : -1) * flip;
}

static IrValue gen_expect(IrGenContext *ctx, Node *node) {
    return gen_expr(ctx, node->ast.func_call.args->items[0]);
}

static bool is_scalar_tc(Typecheck t) {
    return t.ptr_lvl > 0 || t._array || t._vla || t.inline_define == NULL;
}

static bool can_inline(IrGenContext *ctx, IrFuncInfo *fn, FuncEntry *sig, size_t argc) {
    if(!fn->inline_node || !sig || sig->signature.variadic) return false;
    if(fn->func_id == ctx->cur_func_id) return false;           /* recursão direta */
    if(ctx->inline_depth >= IR_INLINE_MAX_DEPTH) return false;
    for(int i = 0; i < ctx->inline_depth; ++i)
        if(ctx->inline_stack[i] == fn->func_id) return false;   /* recursão indireta */

    Node* def = fn->inline_node;
    Node* body = def->ast.func_decl.body;
    if(!body || body->kind != NODE_BLOCK_STMT) return false;

    NodeList* params = def->ast.func_decl.params;
    size_t pc = params ? params->count : 0;
    if(pc != argc) return false;

    for(size_t i = 0; i < pc; ++i) {
        Typecheck pt = params->items[i]->resolved_type;
        if(!is_scalar_tc(pt) || ir_type(pt).size == 0) return false;
    }
    return is_scalar_tc(sig->signature.call_type);              /* sem struct por valor */
}

static IrValue gen_inline_call(IrGenContext *ctx, Node *call, IrFuncInfo *fn);

static IrValue gen_func_call(IrGenContext *ctx, Node *node, bool unused) {
    return gen_func_call_ex(ctx, node, unused, NULL);
}

static IrValue gen_func_call_ex(IrGenContext *ctx, Node *node, bool unused, const IrLval *dst) {
    View name = node->ast.func_call.name;

    if(is_expect_call(node)) return gen_expect(ctx, node);
    if(svcmp(name, VIEW_LIT("__atomic"))) return gen_atomic_call(ctx, node, unused);
    if(svcmp(name, VIEW_LIT("__va_start"))) return gen_va_start(ctx, node);

    const IntrinInfo* intr = intrinsic_find(name);
    if (intr) return gen_intrinsic(ctx, node, intr);

    bool builtin = svcmp(name, VIEW_LIT("__syscall_builtin"));
    IrFuncInfo* fn = func_get_or_create(ctx, name);
    fn->builtin = builtin;

    FuncEntry* sig = function_table_lookup(name);
    if(sig) fn->variadic = sig->signature.variadic;

    NodeList* args = node->ast.func_call.args;
    size_t argc = args ? args->count : 0;

    if(!builtin && can_inline(ctx, fn, sig, argc))
        return gen_inline_call(ctx, node, fn);

    fn->referenced = true;

    /* sret: retorno aggregate vai para o ponteiro oculto passado como arg 0 */
    bool sret = !builtin && sig && is_aggregate_ir(ir_type(sig->signature.call_type));
    IrLval tmp_dst;
    IrValue sret_addr = none_value();
    if(sret) {
        if(!dst) {
            IrType rt = ir_type(sig->signature.call_type);
            memset(&tmp_dst, 0, sizeof(tmp_dst));
            tmp_dst.base = agg_temp_slot(ctx, rt);
            tmp_dst.type = rt;
            dst = &tmp_dst;
        }
        sret_addr = lval_addr_value(ctx, dst);
    }

    size_t skip = sret ? 1 : 0;
    size_t total = argc + skip;

    IrValue vbuf[16];
    int rbuf[16];
    IrValue* vals = total <= 16 ? vbuf : (IrValue*)arena_alloc(ctx->arena, sizeof(IrValue) * total);
    int* reg = total <= 16 ? rbuf : (int*)arena_alloc(ctx->arena, sizeof(int) * total);

    if(sret) vals[0] = sret_addr;

    for(size_t i = 0; i < argc; ++i) {
        Node* a = args->items[i];
        bool has_param = sig && i < sig->signature.param_count;
        IrType at = has_param ? ir_type(sig->signature.param_types[i]) : ir_type(a->resolved_type);

        IrValue v;
        if(!builtin && is_aggregate_ir(at)) {
            v = gen_agg_arg(ctx, a, at);                       /* f(s), f(g()), f(c ? a : b) */
        } else {
            v = gen_expr(ctx, a);
            if(has_param) v = coerce(ctx, v, at);
            else if(v.type.is_float && v.type.size == 4) v = coerce(ctx, v, type_double());   /* promocao variadica */
        }
        vals[i + skip] = v;
    }

    int ni = 0, nf = 0, nstack = 0;
    for(size_t i = 0; i < total; ++i) {
        if(builtin) reg[i] = (ni < IR_SYSCALL_ARG_REGS) ? ni++ : -1;
        else        reg[i] = (i < IR_WIN_ARG_REGS) ? (int)i : -1;

        if(reg[i] >= 0 && !builtin && vals[i].type.is_float) nf++;
        if(reg[i] < 0) nstack++;
    }

    for(size_t idx = total; idx > 0; --idx) {
        size_t i = idx - 1;
        if(reg[i] < 0) emit(ctx, IR_ARG_STACK, none_value(), vals[i], none_value(), nstack);
    }
    for(size_t i = 0; i < total; ++i) {
        if(reg[i] >= 0) emit(ctx, IR_ARG, none_value(), vals[i], none_value(), reg[i]);
    }

    IrValue ref = { 0 };
    ref.kind = builtin ? IR_VAL_BUILTIN : IR_VAL_FUNC;
    ref.as.func_id = fn->func_id;

    bool no_dest = unused || sret;
    IrValue dest = no_dest ? none_value() : new_temp(ctx, ir_type(node->resolved_type));
    IrInstruction* call = emit(ctx, IR_CALL, dest, ref, none_value(), nstack);
    call->aux2 = nf;
    if(no_dest) call->flags |= IRF_RESULT_UNUSED;

    ctx->cur_has_calls = true;
    return sret ? const_int(0, type_i64()) : dest;
}

static IrValue gen_inline_call(IrGenContext *ctx, Node *call, IrFuncInfo *fn) {
    Node* def = fn->inline_node;
    NodeList* params = def->ast.func_decl.params;
    NodeList* args = call->ast.func_call.args;
    size_t argc = params ? params->count : 0;
    FuncEntry* sig = function_table_lookup(fn->name);

    /* 1. argumentos avaliados no escopo do CHAMADOR */
    IrValue vbuf[16];
    IrValue* vals = argc <= 16 ? vbuf : (IrValue*)arena_alloc(ctx->arena, sizeof(IrValue) * argc);
    for(size_t i = 0; i < argc; ++i) {
        IrValue v = gen_expr(ctx, args->items[i]);
        vals[i] = coerce(ctx, v, ir_type(params->items[i]->resolved_type));
    }

    /* 2. salva estado do chamador */
    int32_t saved_heads[IR_SCOPE_TABLE_SIZE];
    IrLabelEntry* saved_labels[IR_LABEL_TABLE_SIZE];
    memcpy(saved_heads, ctx->sym_heads, sizeof(saved_heads));
    memcpy(saved_labels, ctx->label_buckets, sizeof(saved_labels));
    memset(ctx->sym_heads, 0xFF, sizeof(ctx->sym_heads));      /* callee nao enxerga locais do chamador */
    memset(ctx->label_buckets, 0, sizeof(ctx->label_buckets)); /* goto/labels isolados */

    LoopSave ls = loop_enter(ctx, -1, -1);   /* salva loop/VLA; break/continue nao cruzam a funcao */
    ctx->in_loop = false;

    IrType saved_ret = ctx->cur_ret;
    int saved_label = ctx->inline_ret_label;
    IrValue saved_val = ctx->inline_ret_val;

    IrType rt = ir_type(sig->signature.call_type);
    ctx->cur_ret = rt;
    ctx->inline_ret_label = new_label(ctx);
    ctx->inline_ret_val = rt.size ? new_temp(ctx, rt) : none_value();
    ctx->inline_stack[ctx->inline_depth++] = fn->func_id;

    /* 3. parametros viram slots do chamador (is_param = false!) */
    size_t mark = gen_scope_push(ctx);
    for(size_t i = 0; i < argc; ++i) {
        Node* p = params->items[i];
        int slot = scope_declare(ctx, p->ast.var_decl.name, p->resolved_type, false);
        IrValue dst = { 0 };
        dst.kind = IR_VAL_SLOT;
        dst.as.slot_id = slot;
        dst.type = ir_type(p->resolved_type);
        emit(ctx, IR_ASSIGN, dst, vals[i], none_value(), 0);
    }

    /* 4. corpo */
    NodeList* st = def->ast.func_decl.body->ast.block.statements;
    for(size_t i = 0; st && i < st->count; ++i) gen_statement(ctx, st->items[i]);
    gen_scope_pop(ctx, mark);
    emit_label(ctx, ctx->inline_ret_label);

    IrValue result = ctx->inline_ret_val.kind == IR_VAL_NONE ? const_int(0, type_i64())
                                                             : ctx->inline_ret_val;

    /* 5. restaura */
    ctx->inline_depth--;
    ctx->inline_ret_label = saved_label;
    ctx->inline_ret_val = saved_val;
    ctx->cur_ret = saved_ret;
    loop_leave(ctx, ls);
    memcpy(ctx->label_buckets, saved_labels, sizeof(saved_labels));
    memcpy(ctx->sym_heads, saved_heads, sizeof(saved_heads));

    return result;
}

static IrValue gen_cast(IrGenContext *ctx, Node *node) {
    IrType target = ir_type(node->resolved_type);
    IrValue v = gen_expr(ctx, node->ast.cast.operand);

    if(needs_cast(v.type, target) && !(is_const_int(v) && !target.is_float)) {
        IrValue d = new_temp(ctx, target);
        emit(ctx, IR_CAST, d, v, none_value(), 0);
        return d;
    }
    v.type = target;
    return v;
}

static IrValue gen_ternary(IrGenContext *ctx, Node *node) {
    IrType rt = ir_type(node->resolved_type);
    IrValue result = new_temp(ctx, rt);
    int l_else = new_label(ctx);
    int l_end = new_label(ctx);

    gen_cond_jump(ctx, node->ast.if_stmt.condition, l_else, false);
    IrValue a = coerce(ctx, gen_expr(ctx, node->ast.if_stmt.then), rt);
    emit(ctx, IR_ASSIGN, result, a, none_value(), 0);
    emit(ctx, IR_JMP, label_value(l_end), none_value(), none_value(), 0);
    emit(ctx, IR_LABEL, label_value(l_else), none_value(), none_value(), 0);
    IrValue b = coerce(ctx, gen_expr(ctx, node->ast.if_stmt.otherwise), rt);
    emit(ctx, IR_ASSIGN, result, b, none_value(), 0);
    emit(ctx, IR_LABEL, label_value(l_end), none_value(), none_value(), 0);
    return result;
}

static IrValue gen_expr_ex(IrGenContext *ctx, Node *node, bool unused) {
    switch (node->kind)
    {
    case NODE_BYTES: return gen_bytes(node);
    case NODE_LITERAL: return gen_literal(ctx, node);
    case NODE_VAR_CALL: return gen_var_call(ctx, node);
    case NODE_BINARY_OP: return gen_binary_op(ctx, node, unused);
    case NODE_POSTFIX_OP:
    case NODE_UNARY_OP: return gen_unary_op(ctx, node, unused);
    case NODE_FUNC_CALL: return gen_func_call(ctx, node, unused);
    case NODE_ENUM_CALL: return gen_enum_access(node);
    case NODE_FIELD_CALL:
    case NODE_ARRAY: return gen_lval_rvalue(ctx, node);
    case NODE_CAST: return gen_cast(ctx, node);
    case NODE_TERNARY: return gen_ternary(ctx, node);
    default: break;
    }

    ctx->unsupported++;
    return const_int(0, type_i64());
}

static void gen_cond_jump(IrGenContext *ctx, Node *node, int label, bool when_true) {
    IrValue target = label_value(label);

    if(is_expect_call(node)) {
        gen_cond_jump(ctx, node->ast.func_call.args->items[0], label, when_true);
        return;
    }

    if(node->kind == NODE_UNARY_OP && node->ast.unary.op == OPBANG) {
        gen_cond_jump(ctx, node->ast.unary.operand, label, !when_true);
        return;
    }

    if(node->kind == NODE_BINARY_OP) {
        TokenType op = node->ast.binary.op;
        Node* left = node->ast.binary.left;
        Node* right = node->ast.binary.right;

        if(op == LOGAND) {
            if(when_true) {
                int skip = new_label(ctx);
                gen_cond_jump(ctx, left, skip, false);
                gen_cond_jump(ctx, right, label, true);
                emit(ctx, IR_LABEL, label_value(skip), none_value(), none_value(), 0);
            } else {
                gen_cond_jump(ctx, left, label, false);
                gen_cond_jump(ctx, right, label, false);
            }
            return;
        }

        if(op == LOGOR) {
            if(when_true) {
                gen_cond_jump(ctx, left, label, true);
                gen_cond_jump(ctx, right, label, true);
            } else {
                int skip = new_label(ctx);
                gen_cond_jump(ctx, left, skip, true);
                gen_cond_jump(ctx, right, label, false);
                emit(ctx, IR_LABEL, label_value(skip), none_value(), none_value(), 0);
            }
            return;
        }

        if(is_cmp_op(op)) {
            IrType ct = common_type(ir_type(left->resolved_type), ir_type(right->resolved_type));
            if(!ct.is_float) {
                IrValue a = coerce(ctx, gen_expr(ctx, left), ct);
                IrValue b = coerce(ctx, gen_expr(ctx, right), ct);
                emit(ctx, fused_jump(op, when_true), target, a, b, 0);
                return;
            }
        }
    }

    IrValue c = gen_expr(ctx, node);
    if(is_const_int(c)) {
        if((c.as.const_i != 0) == when_true) emit(ctx, IR_JMP, target, none_value(), none_value(), 0);
        return;
    }
    emit(ctx, when_true ? IR_JMP_IF_NOT_ZERO : IR_JMP_IF_ZERO, target, c, none_value(), 0);
}

static bool decl_is_vla(Node *stmt) {
    if(stmt->kind != NODE_VAR_DECL) return false;
    return stmt->ast.var_decl.call_type.is_array && stmt->resolved_type._vla;
}

static bool block_has_vla(NodeList *list) {
    if(!list) return false;

    for(size_t i = 0; i < list->count; ++i) {
        if(decl_is_vla(list->items[i])) return true;
    }
    
    return false;
}

static bool compute_fixed_array(Node *node, size_t *n_out, IrValue **strides_out, size_t *total_out, IrGenContext *ctx) {
    TypeSpec* spec = &node->ast.var_decl.call_type;
    Typecheck rt = node->resolved_type;
    Typecheck et = rt;
    et._array = false;
    et._vla = false;

    if(!spec->array_dim) return false;
    size_t n = spec->array_dim->count;
    if(n == 0 || n > MAX_ARRAY_RANK) return false;

    long long dims[MAX_ARRAY_RANK];
    for(size_t i = 0; i < n; ++i) {
        dims[i] = (rt.array_rank == n) ? (long long)rt.array_dims[i]
                                       : const_op_value(spec->array_dim->items[i]);
        if(dims[i] <= 0) return false;
    }

    IrValue* strides = (IrValue*)arena_alloc(ctx->arena, sizeof(IrValue) * n);
    long long s = (long long)sizeof_tc(et);
    strides[n - 1] = const_int(s, type_i64());
    for(size_t i = n - 1; i-- > 0; ) {
        s *= dims[i + 1];
        strides[i] = const_int(s, type_i64());
    }

    *n_out = n;
    *strides_out = strides;
    *total_out = (size_t)(s * dims[0]);
    return true;
}

static void emit_memzero(IrGenContext *ctx, IrValue addr, IrValue size) {
    emit(ctx, IR_MEMZERO, none_value(), addr, size, 0);
}

static bool static_as_double(IrGenContext *ctx, IrValue v, double *d) {
    switch (v.kind)
    {
    case IR_VAL_CONST_INT: *d = (double)v.as.const_i; return true;
    case IR_VAL_CONST_UINT: *d = (double)v.as.const_ui; return true;
    case IR_VAL_CONST_FLOAT: *d = ctx->floats.items[v.as.float_id].value; return true;
    default: return false;
    }
}

static IrValue static_float(IrGenContext *ctx, double d, IrType want) {
    IrValue v = { 0 };
    v.kind = IR_VAL_CONST_FLOAT;
    v.type = want;
    v.as.float_id = intern_float(ctx, d, want.size == 4);
    return v;
}

static bool static_is_zero(IrGenContext *ctx, const IrValue *v) {
    switch (v->kind)
    {
    case IR_VAL_CONST_INT: return v->as.const_i == 0;
    case IR_VAL_CONST_UINT: return v->as.const_ui == 0;
    case IR_VAL_CONST_FLOAT: {
        double d = ctx->floats.items[v->as.float_id].value;
        return d == 0.0 && !signbit(d);   /* -0.0 nao e zero em bits */
    }
    default: return false;
    }
}

static int static_global_id(IrGenContext *ctx, View name) {
    IrSlotEntry* e = sym_lookup(ctx, name);
    if(e) return e->global_id;                /* -1: variavel local comum */

    IrGlobal* g = global_find(ctx, name);
    return g ? g->id : -1;
}

static bool static_array_stride(IrGenContext *ctx, Node *root, size_t level, long long *stride) {
    if(root->kind == NODE_VAR_CALL) {
        int gid = static_global_id(ctx, root->ast.var_call.name);
        if(gid < 0) return false;

        IrGlobal* g = ctx->globals.items[gid];
        if(!g->is_array || level >= g->dim_count) return false;

        *stride = g->dim_strides[level].as.const_i;
        return true;
    }

    /* campo array de um global: apenas 1 dimensao */
    if(root->kind == NODE_FIELD_CALL && root->resolved_type._array &&
       root->resolved_type.array_rank <= 1 && level == 0) {
        *stride = (long long)ir_type(root->resolved_type).step;
        return true;
    }

    return false;
}

/* Endereco constante: &global, &global.campo, &global[const]... */
static bool static_addr(IrGenContext *ctx, Node *n, IrValue *out) {
    switch (n->kind)
    {
    case NODE_VAR_CALL: {
        int gid = static_global_id(ctx, n->ast.var_call.name);
        if(gid < 0) return false;

        IrValue v = { 0 };
        v.kind = IR_VAL_GLOBAL_ADDR;
        v.as.global_id = gid;
        v.type = type_ptr();
        *out = v;
        return true;
    }

    case NODE_FIELD_CALL:
        if(n->ast.field_access.arrow) return false;
        if(!static_addr(ctx, n->ast.field_access.base, out)) return false;
        out->field_offset += (int)n->ast.field_access.offset;
        return true;

    case NODE_ARRAY: {
        size_t level = 0;
        Node* root = n->ast.binary.left;
        while(root->kind == NODE_ARRAY) { level++; root = root->ast.binary.left; }

        long long stride;
        ConstInt c;
        if(!static_array_stride(ctx, root, level, &stride)) return false;
        if(!const_eval(n->ast.binary.right, &c) || c.big) return false;
        if(!static_addr(ctx, n->ast.binary.left, out)) return false;

        out->field_offset += (int)(c.value * stride);
        return true;
    }

    default: return false;
    }
}

static bool static_eval(IrGenContext *ctx, Node *n, IrValue *out);

static bool static_convert(IrGenContext *ctx, IrValue v, IrType want, IrValue *out) {
    switch (v.kind)
    {
    case IR_VAL_CONST_INT:
    case IR_VAL_CONST_UINT:
    case IR_VAL_CONST_FLOAT: {
        double d;
        if(want.is_float) {
            if(!static_as_double(ctx, v, &d)) return false;
            *out = static_float(ctx, d, want);
            return true;
        }
        if(v.kind == IR_VAL_CONST_FLOAT) {          /* float -> inteiro: trunca */
            if(!static_as_double(ctx, v, &d)) return false;
            *out = const_int((long long)d, want);
            return true;
        }
        v.type = want;
        *out = v;
        return true;
    }

    default:                                         /* strings, enderecos e funcoes */
        if(want.is_float) return false;
        v.type = want;
        *out = v;
        return true;
    }
}

static bool static_eval(IrGenContext *ctx, Node *n, IrValue *out) {
    ConstInt c;

    if(const_eval(n, &c)) {
        if(c.big) {
            IrValue v = { 0 };
            v.kind = IR_VAL_CONST_UINT;
            v.as.const_ui = (unsigned long long)c.value;
            v.type = type_i64();
            v.type.is_signed = 0;
            *out = v;
        } else {
            *out = const_int(c.value, type_i64());
        }
        return true;
    }

    switch (n->kind)
    {
    case NODE_LITERAL:
        if(n->ast.literal.kind == FLOAT || n->ast.literal.kind == DOUBLE || n->ast.literal.kind == STRING) {
            *out = gen_literal(ctx, n);
            return true;
        }
        return false;

    case NODE_VAR_CALL: {                            /* nome de funcao */
        View name = n->ast.var_call.name;
        if(static_global_id(ctx, name) >= 0) return false;

        FuncEntry* fe = function_table_lookup(name);
        if(!fe || fe->_builtin) return false;

        IrFuncInfo* f = func_get_or_create(ctx, name);
        f->referenced = true;

        IrValue v = { 0 };
        v.kind = IR_VAL_FUNC;
        v.as.func_id = f->func_id;
        v.type = type_ptr();
        *out = v;
        return true;
    }

    case NODE_UNARY_OP: {
        Node* operand = n->ast.unary.operand;

        if(n->ast.unary.op == OPAND) return static_addr(ctx, operand, out);

        IrValue v;
        if(!static_eval(ctx, operand, &v)) return false;

        if(n->ast.unary.op == OPPLUS) { *out = v; return true; }
        if(n->ast.unary.op == OPMINUS && v.kind == IR_VAL_CONST_FLOAT) {
            double d;
            if(!static_as_double(ctx, v, &d)) return false;
            *out = static_float(ctx, -d, v.type);
            return true;
        }
        return false;
    }

    case NODE_CAST: {
        IrValue v;
        if(!static_eval(ctx, n->ast.cast.operand, &v)) return false;
        return static_convert(ctx, v, ir_type(n->resolved_type), out);
    }

    case NODE_BINARY_OP: {                           /* fold de ponto flutuante */
        IrType nt = ir_type(n->resolved_type);
        TokenType op = n->ast.binary.op;
        if(!nt.is_float || (op != OPPLUS && op != OPMINUS && op != OPSTAR && op != OPSLASH)) return false;

        IrValue a, b;
        double x, y;
        if(!static_eval(ctx, n->ast.binary.left, &a) || !static_eval(ctx, n->ast.binary.right, &b)) return false;
        if(!static_as_double(ctx, a, &x) || !static_as_double(ctx, b, &y)) return false;

        double r = op == OPPLUS ? x + y : op == OPMINUS ? x - y : op == OPSTAR ? x * y : x / y;
        *out = static_float(ctx, r, nt);
        return true;
    }

    case NODE_TERNARY:
        if(!const_eval(n->ast.if_stmt.condition, &c)) return false;
        return static_eval(ctx, (c.value != 0 || c.big) ? n->ast.if_stmt.then : n->ast.if_stmt.otherwise, out);

    default: return false;
    }
}

static bool static_value(IrGenContext *ctx, Node *n, IrType want, IrValue *out) {
    IrValue v;
    return static_eval(ctx, n, &v) && static_convert(ctx, v, want, out);
}

/* Itens estaticos de uma lista (zeros sao descartados: a memoria ja nasce zerada). */
static bool build_static_items(IrGenContext *ctx, Node *init, IrInitItem **out, size_t *count) {
    const InitItem* src = init->ast.init_list.items;
    size_t n = init->ast.init_list.item_count, k = 0;
    IrInitItem* items = (IrInitItem*)arena_alloc(ctx->arena, sizeof(IrInitItem) * (n ? n : 1));

    for(size_t i = 0; i < n; ++i) {
        const InitItem* s = &src[i];
        IrInitItem* d = &items[k];
        memset(d, 0, sizeof(*d));
        d->offset = (uint32_t)s->offset;
        d->size = (uint32_t)s->size;

        if(s->is_string) {
            View bytes = unescape_string(ctx, s->value->ast.literal.string);
            if(bytes.len == 0) continue;
            d->is_bytes = 1;
            d->bytes = bytes;
        } else {
            IrType t = ir_type(*s->type);
            if(!static_value(ctx, s->value, t, &d->value)) {
                fprintf(stderr, "  item #%zu da lista (offset %u, size %u, kind do no %d)\n",
                        i, (unsigned)s->offset, (unsigned)s->size, (int)s->value->kind);
                UNSUPPORTED(ctx, s->value, "item de inicializador nao estatico");
                return false;
            }
            if(static_is_zero(ctx, &d->value)) continue;
            d->value.type = t;
        }
        ++k;
    }

    *out = items;
    *count = k;
    return true;
}

static void global_list_init(IrGenContext *ctx, Node *init, IrGlobal *g) {
    if(init->ast.init_list._zero) return;            /* .bss */

    IrInitItem* items;
    size_t n;
    if(!build_static_items(ctx, init, &items, &n)) {
        fprintf(stderr, "  global: %.*s\n", (int)g->name.len, g->name.start);
        UNSUPPORTED(ctx, init, "inicializador global nao estatico");
        return;
    }
    if(n == 0) return;                               /* tudo zero: .bss */

    g->init_items = items;
    g->init_count = n;
    g->has_init = true;
}

static IrGlobal* declare_global_var(IrGenContext *ctx, Node *node, View sym_name) {
    bool created;
    IrGlobal* g = global_get_or_create(ctx, sym_name, &created);
    bool is_extern = node->ast.var_decl._extern;
    Node* init = node->ast.var_decl.init;

    if(created) g->_extern = is_extern;
    else if(!is_extern) g->_extern = false;
    else return g;

    g->_static = node->ast.var_decl._static;
    g->_const = node->ast.var_decl._const;

    Typecheck rt = node->resolved_type;
    g->type = ir_type(rt);

    if(node->ast.var_decl.call_type.is_array) {
        Typecheck et = rt;
        et._array = false;
        et._vla = false;
        size_t n, total;
        IrValue* strides;
        if(compute_fixed_array(node, &n, &strides, &total, ctx)) {
            g->is_array = true;
            g->dim_count = n;
            g->dim_strides = strides;
            g->size = (uint32_t)total;
        } else if(!is_extern) {
            fprintf(stderr, "  global: %.*s\n", (int)sym_name.len, sym_name.start);
            UNSUPPORTED(ctx, node, "dimensao de array global nao constante");
        } else {
            g->is_array = true;
            g->type = ir_type(rt);
        }
        g->align = (uint32_t)align_tc(et);
    } else {
        g->size = (uint32_t)sizeof_tc(rt);
        g->align = (uint32_t)align_tc(rt);
    }

    if(init && !is_extern) {
        if(init->kind == NODE_INIT_LIST) {
            global_list_init(ctx, init, g);
        } else {
            IrValue iv;
            if(static_value(ctx, init, g->type, &iv)) {
                /* zero nao-const vai para .bss (imagem menor) */
                if(!static_is_zero(ctx, &iv) || g->_const) {
                    iv.type = g->type.size ? g->type : iv.type;
                    g->init = iv;
                    g->has_init = true;
                }
            } else {
                fprintf(stderr, "  global: %.*s\n", (int)sym_name.len, sym_name.start);
                UNSUPPORTED(ctx, init, "init global nao constante");
            }
        }
    }

    return g;
}

static View make_static_name(IrGenContext *ctx, View name) {
    char* buf = (char*)arena_alloc(ctx->arena, name.len + 16);
    int n = snprintf(buf, name.len + 16, "%.*s.%d", (int)name.len, name.start, ctx->next_static++);
    return (View){ buf, (size_t)n };
}

static bool init_item_is_zero(const InitItem *it) {
    if(it->is_string) return unescaped_length(it->value->ast.literal.string) == 0;

    ConstInt c;
    if(const_eval(it->value, &c)) return !c.big && c.value == 0;

    Node* v = it->value;
    if(v->kind == NODE_LITERAL && (v->ast.literal.kind == FLOAT || v->ast.literal.kind == DOUBLE))
        return v->ast.literal.f64 == 0 && !signbit(v->ast.literal.f64);
    return false;
}

static void gen_init_item(IrGenContext *ctx, IrValue base, const InitItem *it, bool zeroed) {
    if(it->is_string) {
        int id = intern_string(ctx, it->value->ast.literal.string);
        size_t len = ctx->strings.items[id].text.len;
        size_t ncopy = len < it->size ? len + 1 : it->size;   /* inclui o NUL quando cabe */

        IrValue src = { 0 };
        src.kind = IR_VAL_CONST_STRING;
        src.as.string_id = id;
        src.type = type_ptr();

        IrInstruction* in = emit(ctx, IR_MEMCOPY, none_value(), base, src, (int)it->offset);
        in->aux2 = 0;
        in->src3 = const_int((long long)ncopy, type_i64());

        if(!zeroed && it->size > ncopy) {
            IrValue tail = base;
            tail.field_offset += (int)(it->offset + ncopy);
            emit_memzero(ctx, tail, const_int((long long)(it->size - ncopy), type_i64()));
        }
        return;
    }

    IrValue v = coerce(ctx, gen_expr(ctx, it->value), ir_type(*it->type));
    emit(ctx, IR_STORE_INDIRECT, none_value(), base, v, (int)it->offset);
}

static bool gen_init_blob(IrGenContext *ctx, Node *init, IrValue base, size_t total) {
    IrInitItem* items;
    size_t n, covered = 0;

    if(!build_static_items(ctx, init, &items, &n)) return false;
    for(size_t i = 0; i < n; ++i) covered += items[i].size;
    if(n < IR_INIT_BLOB_MIN_ITEMS || covered * 4 < total) return false;

    bool created;
    IrGlobal* g = global_get_or_create(ctx, make_static_name(ctx, VIEW_LIT("__init")), &created);
    g->_static = true;
    g->_const = true;
    g->has_init = true;
    g->size = (uint32_t)total;
    g->align = total >= 16 ? 16 : 8;
    g->init_items = items;
    g->init_count = n;

    IrValue src = { 0 };
    src.kind = IR_VAL_GLOBAL_ADDR;
    src.as.global_id = g->id;
    src.type = type_ptr();

    IrInstruction* in = emit(ctx, IR_MEMCOPY, none_value(), base, src, 0);
    in->aux2 = 0;
    in->src3 = const_int((long long)total, type_i64());
    return true;
}

/* base = SLOT_ADDR do objeto; total = tamanho em bytes. */
static void gen_init_list(IrGenContext *ctx, Node *init, IrValue base, size_t total) {
    const InitItem* items = init->ast.init_list.items;
    size_t n = init->ast.init_list.item_count;

    if(n == 0) {                                     /* {0}, {} */
        if(total) emit_memzero(ctx, base, const_int((long long)total, type_i64()));
        return;
    }

    if(n >= IR_INIT_BLOB_MIN_ITEMS && gen_init_blob(ctx, init, base, total)) return;

    bool zeroed = !init->ast.init_list.dense;
    if(zeroed) emit_memzero(ctx, base, const_int((long long)total, type_i64()));

    for(size_t i = 0; i < n; ++i) {
        if(zeroed && init_item_is_zero(&items[i])) continue;
        gen_init_item(ctx, base, &items[i], zeroed);
    }
}

static void gen_array_decl(IrGenContext *ctx, Node *node) {
    TypeSpec* spec = &node->ast.var_decl.call_type;
    Typecheck rt = node->resolved_type;
    Typecheck et = rt;
    et._array = false;
    et._vla = false;

    IrType arr_type = ir_type(rt);
    size_t n, total;
    IrValue* strides;

    if(!rt._vla) {
        if(!compute_fixed_array(node, &n, &strides, &total, ctx)) {
            UNSUPPORTED(ctx, node, "dimensao de array nao constante");
            return;
        }
        int slot = slot_add(ctx, arr_type, total, align_tc(et), false, true);
        IrSlotEntry* e = sym_insert(ctx, node->ast.var_decl.name);
        e->slot_id = slot;
        e->type = arr_type;
        e->is_array = true;
        e->dim_count = n;
        e->dim_strides = strides;

        Node* init = node->ast.var_decl.init;
        if(init) {
            if(init->kind == NODE_INIT_LIST) {
                IrValue a = { 0 };
                a.kind = IR_VAL_SLOT_ADDR;
                a.as.slot_id = slot;
                a.type = type_ptr();
                gen_init_list(ctx, init, a, total);
            } else {
                ctx->unsupported++;
            }
        }
        return;
    }

    n = spec->array_dim->count;
    IrValue* dvals = (IrValue*)arena_alloc(ctx->arena, sizeof(IrValue) * n);
    for(size_t i = 0; i < n; ++i) {
        dvals[i] = coerce(ctx, gen_expr(ctx, spec->array_dim->items[i]), type_i64());
    }

    strides = (IrValue*)arena_alloc(ctx->arena, sizeof(IrValue) * n);
    strides[n - 1] = const_int((long long)sizeof_tc(et), type_i64());
    for(size_t i = n - 1; i-- > 0; ) {
        strides[i] = mul_value(ctx, strides[i + 1], dvals[i + 1]);
    }
    IrValue total_bytes = mul_value(ctx, strides[0], dvals[0]);

    int slot = slot_add(ctx, arr_type, 8, 8, false, true);
    IrSlotEntry* e = sym_insert(ctx, node->ast.var_decl.name);
    e->slot_id = slot;
    e->type = arr_type;
    e->is_array = true;
    e->is_vla = true;
    e->dim_count = n;
    e->dim_strides = strides;

    IrValue dest = { 0 };
    dest.kind = IR_VAL_SLOT;
    dest.as.slot_id = slot;
    dest.type = arr_type;
    emit(ctx, IR_ALLOCA, dest, total_bytes, none_value(), 0);
    ctx->cur_uses_vla = true;

    Node* vinit = node->ast.var_decl.init;
    if(vinit) {   /* o typecheck so deixa passar {0} em VLA */
        if(vinit->kind == NODE_INIT_LIST && vinit->ast.init_list._zero) emit_memzero(ctx, dest, total_bytes);
        else ctx->unsupported++;
    }
}

static void gen_var_decl(IrGenContext *ctx, Node *node) {
    View name = node->ast.var_decl.name;

    if(!ctx->in_function) {
        declare_global_var(ctx, node, name);
        return;
    }

    if(node->ast.var_decl._static || node->ast.var_decl._extern) {
        View gname = node->ast.var_decl._extern ? name : make_static_name(ctx, name);
        IrGlobal* g = declare_global_var(ctx, node, gname);
        IrSlotEntry* e = sym_insert(ctx, name);
        e->global_id = g->id;
        e->type = g->type;
        e->is_array = g->is_array;
        e->dim_count = g->dim_count;
        e->dim_strides = g->dim_strides;
        return;
    }

    if(node->ast.var_decl.call_type.is_array) {
        gen_array_decl(ctx, node);
        return;
    }

    int slot = scope_declare(ctx, name, node->resolved_type, false);

    Node* init = node->ast.var_decl.init;
    if(!init) return;

    IrType t = ir_type(node->resolved_type);

    IrValue dest = { 0 };
    dest.kind = IR_VAL_SLOT;
    dest.as.slot_id = slot;
    dest.type = t;

    if(init->kind == NODE_INIT_LIST) {
        if(node->resolved_type.inline_define != NULL)
            gen_init_list(ctx, init, addr_of(dest), sizeof_tc(node->resolved_type));
        else
            emit(ctx, IR_ASSIGN, dest, step_const(ctx, t, 0), none_value(), 0);   /* escalar {0} */
        return;
    }

    if(is_aggregate_ir(t)) {                          /* Token t = mk(...); Big b = a; Big c = x ? a : b; */
        IrLval dl;
        memset(&dl, 0, sizeof dl);
        dl.base = dest;
        dl.type = t;
        gen_agg_into(ctx, &dl, init, true);
        return;
    }

    IrValue value = coerce(ctx, gen_expr(ctx, init), t);
    emit(ctx, IR_ASSIGN, dest, value, none_value(), 0);
}

static void emit_label(IrGenContext *ctx, int id) {
    emit(ctx, IR_LABEL, label_value(id), none_value(), none_value(), 0);
}

static IrInstruction* emit_jmp(IrGenContext *ctx, int id) {
    return emit(ctx, IR_JMP, label_value(id), none_value(), none_value(), 0);
}

static void relocate_cold(IrGenContext *ctx, size_t fstart);

static void gen_if_stmt(IrGenContext *ctx, Node *node) {
    Node* cond = node->ast.if_stmt.condition;
    Node* then = node->ast.if_stmt.then;
    Node* other = node->ast.if_stmt.otherwise;
    int hint = cond_hint(cond);
    bool then_cold = hint < 0;

    if(hint != 0 && (then_cold || other)) {
        Node* hot = then_cold ? other : then;
        Node* cold = then_cold ? then : other;
        int l_cold = new_label(ctx), l_end = new_label(ctx);

        gen_cond_jump(ctx, cond, l_cold, then_cold);
        gen_statement(ctx, hot);
        emit_label(ctx, l_end);

        size_t start = ctx->instructions.count;
        emit_label(ctx, l_cold);
        gen_statement(ctx, cold);
        emit_jmp(ctx, l_end)->flags |= IRF_COLD_JMP;

        LIST_ENSURE(ctx->arena, &ctx->cold, 8);
        ctx->cold.items[ctx->cold.count++] = (IrColdRange){ start, ctx->instructions.count };
        return;
    }

    int l_else = new_label(ctx);
    gen_cond_jump(ctx, cond, l_else, false);
    gen_statement(ctx, then);

    if(other) {
        int l_end = new_label(ctx);
        emit_jmp(ctx, l_end);
        emit_label(ctx, l_else);
        gen_statement(ctx, other);
        emit_label(ctx, l_end);
    } else {
        emit_label(ctx, l_else);
    }
}

static void relocate_cold(IrGenContext *ctx, size_t fstart) {
    IrColdList* cl = &ctx->cold;
    if(cl->count == 0) return;

    size_t n = ctx->instructions.count - fstart;
    int* owner = (int*)malloc(n * sizeof(int));
    IrInstruction* tmp = (IrInstruction*)malloc(n * sizeof(IrInstruction));
    if(!owner || !tmp) { 
        free(owner); 
        free(tmp); 
        return; 
    }

    for(size_t i = 0; i < n; ++i) owner[i] = -1;
    for(size_t r = 0; r < cl->count; ++r)
        for(size_t i = cl->items[r].start; i < cl->items[r].end; ++i)
            if(owner[i - fstart] < 0) owner[i - fstart] = (int)r;

    IrInstruction* base = ctx->instructions.items + fstart;
    size_t k = 0;
    for(size_t i = 0; i < n; ++i) if(owner[i] < 0) tmp[k++] = base[i];
    for(size_t r = 0; r < cl->count; ++r)
        for(size_t i = 0; i < n; ++i) if(owner[i] == (int)r) tmp[k++] = base[i];

    memcpy(base, tmp, n * sizeof(IrInstruction));
    free(owner);
    free(tmp);
    cl->count = 0;
}

static void gen_while_loop(IrGenContext *ctx, Node *node) {
    int l_body = new_label(ctx), l_cond = new_label(ctx), l_end = new_label(ctx);

    emit_jmp(ctx, l_cond);
    emit_label(ctx, l_body);

    LoopSave s = loop_enter(ctx, l_cond, l_end);
    gen_statement(ctx, node->ast.while_loop.body);
    loop_leave(ctx, s);

    emit_label(ctx, l_cond);
    gen_cond_jump(ctx, node->ast.while_loop.condition, l_body, true);
    emit_label(ctx, l_end);
}

static void gen_do_while_loop(IrGenContext *ctx, Node *node) {
    int l_body = new_label(ctx), l_cond = new_label(ctx), l_end = new_label(ctx);

    emit_label(ctx, l_body);
    LoopSave s = loop_enter(ctx, l_cond, l_end);
    gen_statement(ctx, node->ast.while_loop.body);
    loop_leave(ctx, s);

    emit_label(ctx, l_cond);
    gen_cond_jump(ctx, node->ast.while_loop.condition, l_body, true);
    emit_label(ctx, l_end);
}

static void gen_for_loop(IrGenContext *ctx, Node *node) {
    size_t mark = gen_scope_push(ctx);

    if(node->ast.for_loop.init) gen_statement(ctx, node->ast.for_loop.init);

    int l_body = new_label(ctx), l_cont = new_label(ctx), l_cond = new_label(ctx), l_end = new_label(ctx);

    if(node->ast.for_loop.condition) emit_jmp(ctx, l_cond);
    emit_label(ctx, l_body);

    LoopSave s = loop_enter(ctx, l_cont, l_end);
    gen_statement(ctx, node->ast.for_loop.body);
    loop_leave(ctx, s);

    emit_label(ctx, l_cont);
    if(node->ast.for_loop.increment) gen_expr_ex(ctx, node->ast.for_loop.increment, true);

    emit_label(ctx, l_cond);
    if(node->ast.for_loop.condition) gen_cond_jump(ctx, node->ast.for_loop.condition, l_body, true);
    else emit_jmp(ctx, l_body);

    emit_label(ctx, l_end);
    gen_scope_pop(ctx, mark);
}

static void emit_vla_restore_for_jump(IrGenContext *ctx) {
    if(ctx->vla_valid) emit(ctx, IR_STACK_RESTORE, none_value(), ctx->vla_outer, none_value(), 0);
}

static void gen_break_stmt(IrGenContext *ctx) {
    emit_vla_restore_for_jump(ctx);
    emit_jmp(ctx, ctx->loop_end_label);
}

static void gen_continue_stmt(IrGenContext *ctx) {
    emit_vla_restore_for_jump(ctx);
    emit_jmp(ctx, ctx->loop_start_label);
}

static void gen_call_stmt(IrGenContext *ctx, Node *node) {
    Node* r = node->ast.call_statement.result;

    if(ctx->inline_depth > 0) {
        if(r && ctx->inline_ret_val.kind != IR_VAL_NONE) {
            IrValue v = coerce(ctx, gen_expr(ctx, r), ctx->cur_ret);
            emit(ctx, IR_ASSIGN, ctx->inline_ret_val, v, none_value(), 0);
        } else if(r) {
            gen_expr_ex(ctx, r, true);
        }
        emit_vla_restore_for_jump(ctx);
        emit_jmp(ctx, ctx->inline_ret_label);
        return;
    }

    if(ctx->sret_slot >= 0) {                         /* funcao que retorna aggregate */
        IrValue ps = { 0 };
        ps.kind = IR_VAL_SLOT;
        ps.as.slot_id = ctx->sret_slot;
        ps.type = type_ptr();

        if(r) {
            IrLval dl;
            memset(&dl, 0, sizeof dl);
            dl.base = ps;
            dl.memory = true;                         /* o slot contem o ponteiro de destino */
            dl.type = ctx->cur_ret;
            gen_agg_into(ctx, &dl, r, true);          /* call mk(...) repassa o sret; call token copia */
        }
        emit_vla_restore_for_jump(ctx);
        emit(ctx, IR_RETURN, none_value(), ps, none_value(), 0);
        return;
    }

    if(r) {
        IrValue value = gen_expr(ctx, r);
        if(ctx->cur_ret.size) value = coerce(ctx, value, ctx->cur_ret);
        emit(ctx, IR_RETURN, none_value(), value, none_value(), 0);
    } else {
        emit(ctx, IR_RETURN, none_value(), none_value(), none_value(), 0);
    }
}

static void gen_block(IrGenContext *ctx, Node *node) {
    size_t mark = gen_scope_push(ctx);

    NodeList* statements = node->ast.block.statements;
    bool has_vla = block_has_vla(statements);
    bool took_outer = false;
    IrValue saved = none_value();

    if(has_vla) {
        saved = new_temp(ctx, type_ptr());
        emit(ctx, IR_STACK_SAVE, saved, none_value(), none_value(), 0);
        ctx->cur_uses_vla = true;
        if(!ctx->vla_valid) {
            ctx->vla_valid = true;
            ctx->vla_outer = saved;
            took_outer = true;
        }
    }

    for(size_t i = 0; statements && i < statements->count; ++i) gen_statement(ctx, statements->items[i]);

    if(has_vla) emit(ctx, IR_STACK_RESTORE, none_value(), saved, none_value(), 0);
    if(took_outer) ctx->vla_valid = false;

    gen_scope_pop(ctx, mark);
}

static void gen_func_decl(IrGenContext *ctx, Node *node) {
    View name = node->ast.func_decl.name;
    IrFuncInfo* fn = func_get_or_create(ctx, name);
    node->func_id = (size_t)fn->func_id;

    if(!fn->defined) {
        fn->_static = node->ast.func_decl._static;
        fn->_extern = node->ast.func_decl._extern;
        fn->variadic = node->ast.func_decl._variadic;
    }

    Node* body = node->ast.func_decl.body;
    if(!body) return;
    fn->defined = true;

    ctx->next_temp = 0;
    memset(ctx->label_buckets, 0, sizeof(ctx->label_buckets));
    memset(&ctx->cur_slots, 0, sizeof(ctx->cur_slots));
    ctx->in_function = true;
    ctx->in_loop = false;
    ctx->loop_start_label = ctx->loop_end_label = -1;
    ctx->vla_valid = false;
    ctx->cur_has_calls = false;
    ctx->cur_uses_vla = false;
    ctx->cold.count = 0;
    ctx->cur_func_id = fn->func_id;
    ctx->inline_depth = 0;

    FuncEntry* sig = function_table_lookup(name);
    ctx->cur_ret = sig ? ir_type(sig->signature.call_type) : (IrType){ 0 };
    bool sret = is_aggregate_ir(ctx->cur_ret);
    ctx->sret_slot = -1;

    size_t start = ctx->instructions.count;
    size_t mark = gen_scope_push(ctx);

    NodeList* params = node->ast.func_decl.params;
    size_t param_count = params ? params->count : 0;

    if(sret) ctx->sret_slot = slot_add(ctx, type_ptr(), 8, 8, true, false);   /* slot 0 */

    for(size_t i = 0; i < param_count; ++i) {
        Node* param = params->items[i];
        scope_declare(ctx, param->ast.var_decl.name, param->resolved_type, true);
    }

    if(body->kind == NODE_BLOCK_STMT) {
        NodeList* statements = body->ast.block.statements;
        for(size_t i = 0; statements && i < statements->count; ++i)
            gen_statement(ctx, statements->items[i]);
    }

    gen_scope_pop(ctx, mark);

    if(ctx->instructions.count == start || ctx->instructions.items[ctx->instructions.count - 1].op != IR_RETURN) {
        if(sret) {
            IrValue ps = { 0 };
            ps.kind = IR_VAL_SLOT;
            ps.as.slot_id = ctx->sret_slot;
            ps.type = type_ptr();
            emit(ctx, IR_RETURN, none_value(), ps, none_value(), 0);
        } else {
            emit(ctx, IR_RETURN, none_value(), none_value(), none_value(), 0);
        }
    }

    relocate_cold(ctx, start);

    IrFunctionRange range = { 0 };
    range.func_node = node;
    range.func_id = fn->func_id;
    range.start = start;
    range.end = ctx->instructions.count;
    range.slot_count = (int)ctx->cur_slots.count;
    range.temp_count = ctx->next_temp;
    range.param_count = (int)param_count + (sret ? 1 : 0);
    range.slots = ctx->cur_slots.items;
    range.has_calls = ctx->cur_has_calls;
    range.uses_vla = ctx->cur_uses_vla;
    range.sret = sret;

    LIST_ENSURE(ctx->arena, &ctx->functions, 8);
    ctx->functions.items[ctx->functions.count++] = range;

    ctx->in_function = false;
    ctx->sret_slot = -1;
}

static void gen_statement(IrGenContext *ctx, Node *node) {
    if(!node) return;

    switch (node->kind)
    {
    case NODE_VAR_DECL: gen_var_decl(ctx, node); break;
    case NODE_IF_STMT: gen_if_stmt(ctx, node); break;
    case NODE_BLOCK_STMT: gen_block(ctx, node); break;
    case NODE_BINARY_OP:
    case NODE_POSTFIX_OP:
    case NODE_UNARY_OP:
    case NODE_FUNC_CALL:
    case NODE_TERNARY:
    case NODE_CAST:
        gen_expr_ex(ctx, node, true);
        break;
    case NODE_FUNC_DECL: gen_func_decl(ctx, node); break;
    case NODE_WHILE_LOOP: gen_while_loop(ctx, node); break;
    case NODE_DO_WHILE_LOOP: gen_do_while_loop(ctx, node); break;
    case NODE_FOR_LOOP: gen_for_loop(ctx, node); break;
    case NODE_CONTINUE_STMT: gen_continue_stmt(ctx); break;
    case NODE_BREAK_STMT: gen_break_stmt(ctx); break;
    case NODE_CALL_STMT: gen_call_stmt(ctx, node); break;
    case NODE_LOAD_LABEL:
        emit_label(ctx, lookup_or_create_label(ctx, node->ast.jmp_label.name));
        break;
    case NODE_JUMP_LABEL:
        emit_jmp(ctx, lookup_or_create_label(ctx, node->ast.jmp_label.name));
        break;
    case NODE_DATA:
    case NODE_COPERATE:
        if(node->ast.aggregate.tailing) gen_var_decl(ctx, node->ast.aggregate.tailing);
        break;
    case NODE_ENUM:
    case NODE_UNDEFINED: break;
    default: break;
    }
}

IrGenContext create_irgen_context(Arena *arena) {
    IrGenContext ctx = { 0 };
    ctx.arena = arena;
    memset(ctx.sym_heads, 0xFF, sizeof(ctx.sym_heads));
    ctx.loop_end_label = -1;
    ctx.loop_start_label = -1;
    ctx.sret_slot = -1;
    return ctx;
}

void irgen_program(IrGenContext *ctx, Node *program) {
    size_t mark = gen_scope_push(ctx);

    NodeList* statements = program->ast.program.statements;

    for(size_t i = 0; statements && i < statements->count; ++i) {
        Node* s = statements->items[i];
        if(s->kind == NODE_FUNC_DECL && s->ast.func_decl.inline__ && s->ast.func_decl.body)
            func_get_or_create(ctx, s->ast.func_decl.name)->inline_node = s;
    }

    for(size_t i = 0; statements && i < statements->count; ++i)
        gen_statement(ctx, statements->items[i]);

    gen_scope_pop(ctx, mark);
}
