#include "../include/typecheck.h"

#define ENUM_SIZE    4
#define POINTER_SIZE 8

#define ALLOW_HEXA_LITERAL_TO_INT 1

typedef struct {
    bool neg;
    unsigned long long mag;
} ConstMag;

static const View BUILTIN_SYSCALL = { "__syscall_builtin", sizeof("__syscall_builtin") - 1 };
static const View BUILTIN_ATOMIC  = { "__atomic",          sizeof("__atomic") - 1 };
static const View BUILTIN_VA_START = { "__va_start", sizeof("__va_start") - 1 };
static const View ENTRY_START     = { "start",             sizeof("start") - 1 };
static const View BUILTIN_EXPECT = { "__builtin_expected", sizeof("__builtin_expected") - 1 };
static const View BUILTIN_OFFSETOF = { "__offsetof", sizeof("__offsetof") - 1 };

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

static void check_var_decl(CheckContext *ctx, Node *node);
static void check_enum_decl(CheckContext *ctx, Node *node);
static void check_aggregate_decl(CheckContext *ctx, Node *node);
static Typecheck resolve_type_spec(CheckContext *ctx, TypeSpec spec, Node *node);
static Typecheck check_expr(CheckContext *ctx, Node *node);
static void check_statement(CheckContext *ctx, Node *node);
static void check_init_list(CheckContext *ctx, Node *list, Typecheck *target);
static bool is_function_name(CheckContext *ctx, View name);

static Typecheck make_type(TokenType base, size_t ptr_lvl) {
    Typecheck type = { 0 };
    type.ptr_lvl = ptr_lvl;
    type.base = base;
    type.repr = base;
    return type;
}

static bool is_int_literal(Node *n) {
    return n && n->kind == NODE_LITERAL &&
           (n->ast.literal.kind == INT || n->ast.literal.kind == BIG ||
            n->ast.literal.kind == UNSIGNED || n->ast.literal.kind == HEXA);
}

static Typecheck make_named(View alias, TypeClass cls, size_t ptr_lvl) {
    Typecheck type = make_type(IDENTIFIER, ptr_lvl);
    type.alias = alias;
    type.cls = cls;
    type.repr = (cls == TC_ENUM) ? KINT32 : IDENTIFIER;   /* ENUM_SIZE == 4 */
    return type;
}

static Typecheck type_error(void) {
    Typecheck type = { 0 };
    type.error = true;
    return type;
}

static bool type_is_error(Typecheck type) {
    return type.error;
}

static const char* base_type_name(TokenType type) {
    switch (type)
    {
    case KINT8: return "int8";
    case KINT16: return "int16";
    case KINT32: return "int32";
    case KINT64: return "int64";
    case KUINT8: return "uint8";
    case KUINT16: return "uint16";
    case KUINT32: return "uint32";
    case KUINT64: return "uint64";
    case KBIG: return "big";
    case KSMALL: return "small";
    case KHEXA: return "hexa";
    case KFLOAT: return "float";
    case KDOUBLE: return "double";
    case KLINK: return "link";
    case KVOID: return "void";
    case KCHAR: return "char";
    case KUTFCHAR: return "utfchar";
    default: return "?";
    }
}

static char* format_type(char* buffer, size_t size, Typecheck type) {
    int n;

    if(type.base == IDENTIFIER) {
        if(type.alias.len > 0) n = snprintf(buffer, size, "%.*s", (int)type.alias.len, type.alias.start);
        else n = snprintf(buffer, size, "<anonimo>");
    } else {
        n = snprintf(buffer, size, "%s", base_type_name(type.base));
    }

    size_t len = (n < 0) ? 0 : (size_t)n;
    if(len >= size) len = size - 1;

    for(size_t i = 0; i < type.ptr_lvl && len + 1 < size; ++i)
        buffer[len++] = '*';

    if(type._array) {
        size_t rank = type.array_rank ? type.array_rank : 1;
        for(size_t i = 0; i < rank && len + 1 < size; ++i) {
            int w = type.array_dims[i]
                ? snprintf(buffer + len, size - len, "[%zu]", (size_t)type.array_dims[i])
                : snprintf(buffer + len, size - len, "[]");
            if(w > 0) len += (size_t)w;
            if(len >= size) { len = size - 1; break; }
        }
    }

    buffer[len] = '\0';
    return buffer;
}

const char* tname(Typecheck t) {
    static char bufs[4][256];
    static unsigned idx = 0;
    return format_type(bufs[idx++ & 3], 256, t);
}

char* format_signature(char* buf, size_t size, View name, const FunctionSignature* sig) {
    char tmp[256];
    size_t len = 0;
    buf[0] = '\0';
#define APPEND(...) do { if(len < size) { int w = snprintf(buf + len, size - len, __VA_ARGS__); if(w > 0) len += (size_t)w; } } while(0)
    format_type(tmp, sizeof(tmp), sig->call_type);
    APPEND("%s %.*s(", tmp, (int)name.len, name.start);
    for(size_t i = 0; i < sig->param_count; ++i) {
        format_type(tmp, sizeof(tmp), sig->param_types[i]);
        APPEND("%s%s", i ? ", " : "", tmp);
    }
    if(sig->variadic) APPEND("%s...", sig->param_count ? ", " : "");
    APPEND(")");
#undef APPEND
    return buf;
}

static void ctx_scope_push(CheckContext *ctx) {
    ctx->current_scope = scope_push(ctx->arena, ctx->current_scope);
}

static void ctx_scope_pop(CheckContext *ctx) {
    ctx->current_scope = ctx->current_scope->next;
}

static SymbolEntry* ctx_scope_declare(CheckContext *ctx, View name, Typecheck type) {
    return scope_insert(ctx->arena, ctx->current_scope, name, type);
}

static FieldInfo* find_field(AggregateDef *def, View name) {
    for(size_t i = 0; i < def->count; ++i) {
        if(svcmp(def->fields[i].name, name)) return &def->fields[i];
    }

    return NULL;
}

bool isintegerbase(TokenType type) {
    switch (type)
    {
    case KINT8: case KINT16: case KINT32: case KINT64:
    case KUINT8: case KUINT16: case KUINT32: case KUINT64:
    case KCHAR: case KUTFCHAR: case KBIG: case KSMALL:
    case KHEXA:
        return true;
    default: return false;
    }
}

static bool isfloatbase(TokenType type) {
    return type == KFLOAT || type == KDOUBLE;
}

bool isunsignedbase(TokenType type) {
    switch (type)
    {
    case KUINT8: case KUINT16: case KUINT32: case KUINT64:
    case KCHAR: case KUTFCHAR:
        return true;
    default: return false;
    }
}

int integerwidth(TokenType type) {
    switch (type)
    {
    case KINT8: case KUINT8: case KCHAR: case KSMALL: return 8;
    case KINT16: case KUINT16: case KUTFCHAR: return 16;
    case KINT32: case KUINT32: return 32;
    case KINT64: case KUINT64: case KBIG: return 64;
    default: return 0;
    }
}

static int floatwidth(TokenType type) {
    switch (type)
    {
    case KFLOAT: return 32;
    case KDOUBLE: return 64;
    default: return 0;
    }
}

static size_t primitive_size(TokenType type) {
    switch (type)
    {
    case KINT8: case KUINT8: case KCHAR: case KSMALL: return 1;
    case KINT16: case KUINT16: case KUTFCHAR: return 2;
    case KINT32: case KUINT32: case KFLOAT: return 4;
    case KINT64: case KUINT64: case KDOUBLE: case KLINK: case KHEXA: case KBIG: return 8;
    default: return 0;
    }
}

static Typecheck decay(Typecheck type) {
    if(type._array) {
        type.array_rank = 0;
        memset(type.array_dims, 0, sizeof(type.array_dims));
        type._array = false;
        type._vla = false;
        type.array_size = 0;
        type.ptr_lvl++;
    }
    return type;
}

static Typecheck core_type(Typecheck type) {
    int guard = 0;

    while(type.cls == TC_NEWTYPE && type.ptr_lvl == 0 && !type._array && guard++ < 32) {
        TypeEntry* entry = newtype_table_lookup(type.alias);
        if(!entry) break;
        type = entry->alias;
    }

    return type;
}

bool is_prim_scalar(Typecheck type) {
    return type.cls == TC_PRIM && type.ptr_lvl == 0 && !type._array;
}

static bool isinteger(Typecheck type) { return is_prim_scalar(type) && isintegerbase(type.base); }
static bool isfloat(Typecheck type)   { return is_prim_scalar(type) && isfloatbase(type.base); }
static bool isnumeric(Typecheck type) { return isinteger(type) || isfloat(type); }

static bool isvoid(Typecheck type) {
    return type.base == KVOID && type.ptr_lvl == 0 && !type._array;
}

static bool isenum(Typecheck type) {
    type = core_type(type);
    return type.cls == TC_ENUM && type.ptr_lvl == 0 && !type._array;
}

static bool isaggregate(Typecheck type) {
    return type.cls == TC_AGGREGATE && type.ptr_lvl == 0 && !type._array;
}

static bool isnewtype(Typecheck type) {
    return type.cls == TC_NEWTYPE && type.ptr_lvl == 0 && !type._array;
}

static bool isptr(Typecheck type) {
    return type.ptr_lvl > 0 && type.base != KLINK;
}

static bool islink(Typecheck type) {
    return type.base == KLINK && type.ptr_lvl == 0 && !type._array;
}

static bool ishexatype(Typecheck type) {
    return type.base == KHEXA && type.ptr_lvl == 0 && !type._array;
}

static bool isisland(Typecheck type) {
    return ishexatype(type) || islink(type) || isptr(type);
}

bool type_equals(Typecheck first, Typecheck second) {
    if(first.base != second.base || first.ptr_lvl != second.ptr_lvl) return false;
    if(first.base != IDENTIFIER) return true;
    if(first.alias.len > 0 || second.alias.len > 0) return svcmp(first.alias, second.alias);
    return first.inline_define == second.inline_define;
}

size_t type_size_scalar(Typecheck type) {
    if(type.ptr_lvl > 0) return POINTER_SIZE;
    if(type.base != IDENTIFIER) return primitive_size(type.base);

    switch (type.cls)
    {
    case TC_ENUM: return ENUM_SIZE;
    case TC_AGGREGATE: return type.inline_define ? type.inline_define->size : 0;
    case TC_NEWTYPE: {
        Typecheck core = core_type(type);
        if(core.cls == TC_NEWTYPE) return 0;
        return type_size_scalar(core);
    }
    default: return 0;
    }
}

size_t type_align(Typecheck type) {
    if(type.ptr_lvl > 0) return POINTER_SIZE;
    if(type.base != IDENTIFIER) return primitive_size(type.base);

    switch (type.cls)
    {
    case TC_ENUM: return ENUM_SIZE;
    case TC_AGGREGATE: return type.inline_define ? type.inline_define->align : 0;
    case TC_NEWTYPE: {
        Typecheck core = core_type(type);
        if(core.cls == TC_NEWTYPE) return 0;
        return type_align(core);
    }
    default: return 0;
    }
}

static size_t type_size(Typecheck type) {
    size_t size = type_size_scalar(type);

    if(type._array) {
        if(type._vla) return 0;
        size *= type.array_size;
    }

    return size;
}

static ConstMag cmag_from(ConstInt c) {
    ConstMag m;
    if(c.big) {
        m.neg = false;
        m.mag = (unsigned long long)c.value;
    } else if(c.value < 0) {
        m.neg = true;
        m.mag = 0ULL - (unsigned long long)c.value;
    } else {
        m.neg = false;
        m.mag = (unsigned long long)c.value;
    }
    return m;
}

static bool cmag_to_const(ConstMag m, ConstInt *out) {
    if(m.mag == 0) m.neg = false;
    if(m.neg) {
        if(m.mag > 0x8000000000000000ULL) return false;
        out->value = (long long)(0ULL - m.mag);
        out->big = false;
    } else {
        out->value = (long long)m.mag;
        out->big = m.mag > 0x7FFFFFFFFFFFFFFFULL;
    }
    return true;
}

static int cmag_cmp(ConstMag a, ConstMag b) {
    if(a.mag == 0) a.neg = false;
    if(b.mag == 0) b.neg = false;
    if(a.neg != b.neg) return a.neg ? -1 : 1;
    int c = a.mag < b.mag ? -1 : (a.mag > b.mag ? 1 : 0);
    return a.neg ? -c : c;
}

/* Resultado de operacao em aneis (mod 2^64): bits crus, interpretados com sinal.
   O const_wrap do tipo do no ajusta largura, sinal e o flag big. */
static void const_set_bits(ConstInt *out, unsigned long long bits) {
    out->value = (long long)bits;
    out->big = false;
}

static void const_finish(ConstInt *out, Node *node) {
    Typecheck t = core_type(node->resolved_type);
    if(is_prim_scalar(t) && isintegerbase(t.base)) const_wrap(out, t);
}

static bool const_shift_count(ConstInt c, unsigned *k) {
    if(c.big || c.value < 0 || c.value >= 64) return false;
    *k = (unsigned)c.value;
    return true;
}

bool const_eval(Node *node, ConstInt *out) {
    if(!node) return false;

    switch (node->kind)
    {
    case NODE_LITERAL:
        switch (node->ast.literal.kind)
        {
        case INT: case BIG:
            out->value = node->ast.literal.i64;
            out->big = false;
            return true;
        case UNSIGNED: case HEXA: {
            unsigned long long raw = node->ast.literal.u64;
            out->value = (long long)raw;
            out->big = raw > 0x7FFFFFFFFFFFFFFFULL;
            return true;
        }
        case CHAR: case WIDE_CHAR: {
            unsigned long long ch;
            if(!decode_char_literal(node->ast.literal.character, &ch)) return false;
            if(ch > (node->ast.literal.kind == CHAR ? 0xFFULL : 0xFFFFULL)) return false;
            out->value = (long long)ch;
            out->big = false;
            return true;
        }
        default: return false;
        }

    case NODE_ENUM_CALL:
        out->value = node->ast.enum_access.iresolved_type;
        out->big = false;
        return true;

    case NODE_CAST: {
        Typecheck target = core_type(node->resolved_type);
        if(!is_prim_scalar(target) || !isintegerbase(target.base)) return false;

        ConstInt v;
        if(!const_eval(node->ast.cast.operand, &v)) return false;

        const_wrap(&v, target);
        *out = v;
        return true;
    }

    case NODE_UNARY_OP: {
        ConstInt v;
        if(!const_eval(node->ast.unary.operand, &v)) return false;

        unsigned long long bits = (unsigned long long)v.value;

        switch (node->ast.unary.op)
        {
        case OPPLUS:  *out = v; break;
        case OPMINUS: const_set_bits(out, 0ULL - bits); break;
        case DESC:    const_set_bits(out, ~bits); break;
        case OPBANG:  const_set_bits(out, v.value == 0 ? 1ULL : 0ULL); break;   /* big implica value != 0 */
        default: return false;
        }

        /* ~(uint8)5 precisa dar 250, ~(uint64)15U precisa dar 0xFF..F0 com big = true */
        const_finish(out, node);
        return true;
    }

    case NODE_BYTES: {
        if(node->ast.bytes.size == 0) return false;
        out->value = (long long)node->ast.bytes.size;
        out->big = false;
        return true;
    }

    case NODE_BINARY_OP: {
        TokenType op = node->ast.binary.op;
        ConstInt a, b, res;

        if(!const_eval(node->ast.binary.left, &a)) return false;

        if(op == LOGAND || op == LOGOR) {
            bool lhs = a.value != 0;
            if(op == LOGAND ? !lhs : lhs) {                  /* curto-circuito: direita nao precisa ser constante */
                const_set_bits(out, lhs ? 1ULL : 0ULL);
                const_finish(out, node);
                return true;
            }
            if(!const_eval(node->ast.binary.right, &b)) return false;
            const_set_bits(out, b.value != 0 ? 1ULL : 0ULL);
            const_finish(out, node);
            return true;
        }

        if(!const_eval(node->ast.binary.right, &b)) return false;

        unsigned long long ua = (unsigned long long)a.value;
        unsigned long long ub = (unsigned long long)b.value;
        ConstMag na = cmag_from(a), nb = cmag_from(b);
        unsigned k;

        switch (op)
        {
        case OPPLUS:  const_set_bits(&res, ua + ub); break;
        case OPMINUS: const_set_bits(&res, ua - ub); break;
        case OPSTAR:  const_set_bits(&res, ua * ub); break;
        case OPAND:   const_set_bits(&res, ua & ub); break;
        case OPOR:    const_set_bits(&res, ua | ub); break;
        case OPXOR:   const_set_bits(&res, ua ^ ub); break;

        case OPSLASH:
        case OPMOD: {
            if(nb.mag == 0) return false;
            ConstMag q;
            if(op == OPSLASH) { q.mag = na.mag / nb.mag; q.neg = na.neg != nb.neg; }
            else              { q.mag = na.mag % nb.mag; q.neg = na.neg; }
            if(!cmag_to_const(q, &res)) return false;
            break;
        }

        case OPSHL:
            if(!const_shift_count(b, &k)) return false;
            const_set_bits(&res, ua << k);
            break;

        case OPSHR: {
            if(!const_shift_count(b, &k)) return false;
            ConstMag q = na;
            q.mag = na.mag >> k;
            if(na.neg && (na.mag & ((1ULL << k) - 1)) != 0) q.mag++;   /* floor para negativos */
            if(!cmag_to_const(q, &res)) return false;
            break;
        }

        case OPLT:    const_set_bits(&res, cmag_cmp(na, nb) <  0); break;
        case OPLE:    const_set_bits(&res, cmag_cmp(na, nb) <= 0); break;
        case OPGT:    const_set_bits(&res, cmag_cmp(na, nb) >  0); break;
        case OPGE:    const_set_bits(&res, cmag_cmp(na, nb) >= 0); break;
        case OPEQ:    const_set_bits(&res, cmag_cmp(na, nb) == 0); break;
        case OPBANGE: const_set_bits(&res, cmag_cmp(na, nb) != 0); break;

        default: return false;
        }

        *out = res;
        const_finish(out, node);
        return true;
    }

    case NODE_TERNARY: {
        ConstInt c;
        if(!const_eval(node->ast.if_stmt.condition, &c)) return false;

        Node* pick = (c.value != 0) ? node->ast.if_stmt.then : node->ast.if_stmt.otherwise;
        if(!const_eval(pick, out)) return false;

        const_finish(out, node);
        return true;
    }

    default: return false;
    }
}

static bool const_fits(ConstInt c, TokenType base) {
    int width = integerwidth(base);
    if(width == 0) return false;

    if(isunsignedbase(base)) {
        if(c.big) return width == 64;
        if(c.value < 0) return false;
        if(width == 64) return true;
        return (unsigned long long)c.value <= ((1ULL << width) - 1);
    }

    if(c.big) return false;
    if(width == 64) return true;

    long long max = (1LL << (width - 1)) - 1;
    long long min = -max - 1;
    return c.value >= min && c.value <= max;
}

static bool signature_equal(FunctionSignature *first, FunctionSignature* second) {
    if(!type_equals(first->call_type, second->call_type)) return false;
    if(first->param_count != second->param_count) return false;
    if(first->variadic != second->variadic) return false;

    for(size_t i = 0; i < first->param_count; ++i) {
        if(!type_equals(first->param_types[i], second->param_types[i])) return false;
    }

    return true;
}

static bool is_null_constant(Node *node) {
    ConstInt c;
    return node && const_eval(node, &c) && !c.big && c.value == 0;
}

static CompatResult check_assignable(Typecheck from, Typecheck to, Node *node, bool _cast, const char** msg) {
    if(_cast) return COMPAT_OK;
    if(type_is_error(from) || type_is_error(to)) return COMPAT_OK;

    if(isvoid(from)) { *msg = "uma expressao 'void' nao possui valor"; return COMPAT_ERROR; }
    if(isvoid(to))   { *msg = "o destino nao pode ser 'void'"; return COMPAT_ERROR; }
    if(to._array)    { *msg = "arrays nao podem ser atribuidos diretamente"; return COMPAT_ERROR; }

    from = decay(from);

    if(type_equals(from, to)) return COMPAT_OK;

#if ALLOW_HEXA_LITERAL_TO_INT
    if(ishexatype(from) && isinteger(to) && node && node->kind == NODE_LITERAL && node->ast.literal.kind == HEXA)
        from = make_type(KUINT64, 0);
#endif

    if(isisland(to) || isisland(from)) {
        if(isisland(to) && !isisland(from) && isinteger(from) && is_null_constant(node))
            return COMPAT_OK;

        if(isisland(to) && isisland(from)) {
            if(islink(to) || islink(from) || ishexatype(to) || ishexatype(from)) return COMPAT_OK;
            *msg = "ponteiros de tipos diferentes exigem cast explicito (ou passe por 'link')";
            return COMPAT_ERROR;
        }

        *msg = isisland(to)
            ? "hexa, link e ponteiros nao aceitam valores nao-endereco sem cast explicito"
            : "nao e possivel converter hexa, link ou ponteiro para outro tipo sem cast explicito";
        return COMPAT_ERROR;
    }

    if(isnewtype(to) || isnewtype(from)) {
        if(isnewtype(to) && !isenum(to) && from.cls == TC_PRIM && from.ptr_lvl == 0 && is_numeric_constant(node)) {
            Typecheck core = core_type(to);
            if(core.cls != TC_NEWTYPE) return check_assignable(from, core, node, false, msg);
        }

        *msg = "newtype so converte para ele mesmo; use o mesmo newtype ou um cast explicito";
        return COMPAT_ERROR;
    }

    if(isenum(to)) {
        *msg = "um enum so aceita membros do proprio enum";
        return COMPAT_ERROR;
    }

    if(isenum(from)) {
        *msg = "enum nao converte implicitamente para outro tipo (use cast)";
        return COMPAT_ERROR;
    }

    if(isaggregate(to) || isaggregate(from) || to.cls != TC_PRIM || from.cls != TC_PRIM) {
        *msg = "tipos incompativeis";
        return COMPAT_ERROR;
    }

    bool from_int = isinteger(from), to_int = isinteger(to);
    bool from_float = isfloat(from), to_float = isfloat(to);

    if(from_int && to_int) {
        ConstInt c;
        if(node && const_eval(node, &c)) {
            if(const_fits(c, to.base)) return COMPAT_OK;
            *msg = "valor constante excede o alcance do tipo de destino";
            return COMPAT_ERROR;
        }

        if(integerwidth(to.base) >= integerwidth(from.base)) {
            bool sign_differs = isunsignedbase(from.base) != isunsignedbase(to.base);
            bool literal = node && node->kind == NODE_LITERAL;
            if(sign_differs && !literal &&
               (isunsignedbase(to.base) || integerwidth(to.base) == integerwidth(from.base))) {
                *msg = "mudanca de sinal";
                return COMPAT_WARNING;
            }
            return COMPAT_OK;
        }

        *msg = "perda de precisao";
        return COMPAT_WARNING;
    }

    if(from_int && to_float) return COMPAT_OK;

    if(from_float && to_int) {
        *msg = "perda de precisao";
        return COMPAT_WARNING;
    }

    if(from_float && to_float) {
        if(floatwidth(to.base) >= floatwidth(from.base)) return COMPAT_OK;
        *msg = "perda de precisao";
        return COMPAT_WARNING;
    }

    *msg = "tipos incompativeis";
    return COMPAT_ERROR;
}

static void report_assignable(CheckContext *ctx, Typecheck from, Typecheck to, Node *node, Node *site, const char* where) {
    const char* reason = NULL;
    CompatResult result = check_assignable(from, to, node, false, &reason);
    if(result == COMPAT_OK) return;

    if(result == COMPAT_ERROR)
        SEMA_ERR(site, "Nao e possivel converter de '%s' para '%s' %s: %s", tname(from), tname(to), where, reason);
    else
        SEMA_WARN(site, "Conversao de '%s' para '%s' %s pode causar %s", tname(from), tname(to), where, reason);
}

static bool check_castable(Typecheck from, Typecheck to) {
    from = decay(from);

    if(to._array) return false;
    if(isvoid(to)) return true;
    if(isvoid(from)) return false;

    Typecheck fc = core_type(from);
    Typecheck tc = core_type(to);

    if(isaggregate(fc) || isaggregate(tc)) return type_equals(from, to);
    if((isfloat(fc) && isisland(tc)) || (isisland(fc) && isfloat(tc))) return false;

    return true;
}

static Typecheck promote_from_arith(Typecheck first, Typecheck second) {
    TokenType first_base = (first.base == KCHAR) ? KUINT8 : first.base;
    TokenType second_base = (second.base == KCHAR) ? KUINT8 : second.base;

    if(isfloatbase(first_base) || isfloatbase(second_base)) {
        if(first_base == KDOUBLE || second_base == KDOUBLE) return make_type(KDOUBLE, 0);
        return make_type(KFLOAT, 0);
    }

    int first_width = integerwidth(first_base);
    int second_width = integerwidth(second_base);

    if(first_width > second_width) return make_type(first_base, 0);
    if(second_width > first_width) return make_type(second_base, 0);

    if(isunsignedbase(first_base)) return make_type(first_base, 0);
    return make_type(second_base, 0);
}

static bool islvalue(Node *node) {
    if(!node) return false;
    if(node->kind == NODE_UNARY_OP) return node->ast.unary.op == OPSTAR;
    return node->kind == NODE_VAR_CALL || node->kind == NODE_ARRAY || node->kind == NODE_FIELD_CALL;
}

bool isarith_compoundop(TokenType op) {
    switch (op)
    {
    case OPPLUSE: case OPMINUSE: case OPSLASHE:
    case OPSTARE: case OPMODE:
        return true;
    default: return false;
    }
}

bool isbitwise_compoundop(TokenType op) {
    switch (op)
    {
    case OPANDE: case OPXORE: case OPORE:
    case OPSHRE: case OPSHLE:
        return true;
    default: return false;
    }
}

static bool is_assign_op(TokenType op) {
    return op == ASSIGN || isarith_compoundop(op) || isbitwise_compoundop(op);
}

static bool has_side_effect(Node *n) {
    switch (n->kind)
    {
    case NODE_FUNC_CALL: case NODE_POSTFIX_OP: return true;
    case NODE_UNARY_OP:  return n->ast.unary.op == OPPLUSPLUS || n->ast.unary.op == OPMINUSMINUS;
    case NODE_BINARY_OP: return is_assign_op(n->ast.binary.op);
    case NODE_CAST:      return has_side_effect(n->ast.cast.operand);
    default: return false;
    }
}

static bool dimention_isommited(Node *dimention) {
    return dimention == NULL || dimention->kind == NODE_UNDEFINED;
}

static bool is_builtin_name(View name) {
    return svcmp(name, BUILTIN_SYSCALL) || svcmp(name, BUILTIN_ATOMIC) || svcmp(name, BUILTIN_VA_START)
        || svcmp(name, BUILTIN_EXPECT) || svcmp(name, BUILTIN_OFFSETOF)
        || intrinsic_find(name) != NULL;
}

static bool require_scalar(CheckContext *ctx, Node *node, Typecheck type) {
    if(type_is_error(type)) return false;

    Typecheck core = core_type(decay(type));
    if(isnumeric(core) || isisland(core) || isenum(core)) return true;

    SEMA_ERR(node, "A expressao precisa ser um valor escalar (numerico, endereco ou enum)");
    return false;
}

static void check_condition(CheckContext *ctx, Node *cond) {
    if(!cond) return;
    Typecheck type = check_expr(ctx, cond);
    require_scalar(ctx, cond, type);
}

static void compute_aggregate_layout(CheckContext *ctx, Node *node, AggregateDef *def) {
    NodeList* members = node->ast.aggregate.members;

    def->kind = node->kind;
    def->count = members->count;
    def->fields = (FieldInfo*)arena_alloc(ctx->arena, sizeof(FieldInfo) * (members->count ? members->count : 1));
    def->size = 0;
    def->align = 1;

    size_t running_offset = 0;
    size_t max_size = 0;
    size_t max_align = 1;

    for(size_t i = 0; i < members->count; ++i) {
        Node* field = members->items[i];
        View field_name = field->ast.var_decl.name;
        Typecheck field_type = resolve_type_spec(ctx, field->ast.var_decl.call_type, field);
        field->resolved_type = field_type;

        size_t field_size = type_size(field_type);
        size_t field_align = type_align(field_type);

        TypeSpec* fspec = &field->ast.var_decl.call_type;
        bool flexible = fspec->is_array &&
            (!fspec->array_dim || fspec->array_dim->count == 0 ||
             dimention_isommited(fspec->array_dim->items[0]));

        if(flexible && !type_is_error(field_type)) {
            if(node->kind != NODE_DATA)
                SEMA_ERR(field, "Membro flexivel so e permitido em 'data'");
            else if(i + 1 != members->count)
                SEMA_ERR(field, "O membro flexivel '%.*s' precisa ser o ultimo campo", VIEW_ARG(field_name));
            else if(members->count == 1)
                SEMA_ERR(field, "Membro flexivel exige ao menos outro campo antes dele");
            else if(fspec->array_dim && fspec->array_dim->count > 1)
                SEMA_ERR(field, "Membro flexivel so pode ter 1 dimensao");
            else
                def->flexible = true;
        }

        if(!type_is_error(field_type)) {
            if(field_type.base == KVOID && field_type.ptr_lvl == 0) {
                SEMA_ERR(field, "O campo '%.*s' nao pode ser do tipo void", VIEW_ARG(field_name));
            } else if(field_size == 0 && !flexible) {
                SEMA_ERR(field, "O campo '%.*s' possui tipo incompleto ou tamanho indeterminado", VIEW_ARG(field_name));
            } else if(field_type.ptr_lvl == 0 && field_type.cls == TC_AGGREGATE &&
                      field_type.inline_define && field_type.inline_define->flexible) {
                SEMA_ERR(field, "O campo '%.*s' nao pode conter um data com membro flexivel", VIEW_ARG(field_name));
            }
        }

        for(size_t j = 0; j < i; ++j) {
            if(svcmp(def->fields[j].name, field_name)) {
                SEMA_ERR(field, "Campo '%.*s' duplicado", VIEW_ARG(field_name));
                break;
            }
        }

        if(field_align == 0) field_align = 1;
        if(field_align > max_align) max_align = field_align;

        def->fields[i].name = field_name;
        def->fields[i].type = field_type;
        def->fields[i].size = field_size;

        if(node->kind == NODE_DATA) {
            size_t aligned = align_size(running_offset, field_align);
            def->fields[i].offset = aligned;
            running_offset = aligned + field_size;
        } else {
            def->fields[i].offset = 0;
            if(field_size > max_size) max_size = field_size;
        }
    }

    def->align = max_align;
    def->size = align_size((node->kind == NODE_DATA) ? running_offset : max_size, max_align);
    def->complete = true;
}

static bool enum_same_as(TypeEntry *e, Node *decl) {
    if(e->kind != TYPE_ENTRY_ENUM) return false;
    NodeList* m = decl->ast.enum_decl.members;
    if(!m || m->count != e->enumdef.member_count) return false;

    for(size_t i = 0; i < m->count; ++i)
        if(!svcmp(e->enumdef.members[i].name, m->items[i]->ast.enum_member.name)) return false;
    return true;
}

static bool aggregate_same_as(TypeEntry *e, Node *decl) {
    if(e->kind != TYPE_ENTRY_AGGREGATE || e->aggregate.kind != decl->kind) return false;
    if(!e->aggregate.complete) return false;

    NodeList* m = decl->ast.aggregate.members;
    if(!m || m->count != e->aggregate.count) return false;

    for(size_t i = 0; i < m->count; ++i)
        if(!svcmp(e->aggregate.fields[i].name, m->items[i]->ast.var_decl.name)) return false;
    return true;
}

static Typecheck declare_aggregate(CheckContext *ctx, Node *node) {
    if(node->resolved_type.inline_define || node->resolved_type.error) return node->resolved_type;

    View name = node->ast.aggregate.name;
    AggregateDef* def = NULL;
    Typecheck type = make_named(name, TC_AGGREGATE, 0);

    if(name.len > 0) {
        TypeEntry* prev = type_table_lookup(name);

        if(prev && prev->kind == TYPE_ENTRY_AGGREGATE && !prev->aggregate.complete) {
            if(prev->aggregate.kind != node->kind) {
                SEMA_ERR(node, "O tipo '%.*s' foi declarado antes como '%s'", VIEW_ARG(name),
                         prev->aggregate.kind == NODE_DATA ? "data" : "coperate");
                node->resolved_type = type_error();
                return node->resolved_type;
            }
            def = &prev->aggregate;
        } else {
            if(prev && aggregate_same_as(prev, node)) {
                type.inline_define = &prev->aggregate;
                node->resolved_type = type;
                return type;
            }

            if(prev || newtype_table_lookup(name)) {
                SEMA_ERR(node, "O tipo '%.*s' ja foi declarado anteriormente", VIEW_ARG(name));
                node->resolved_type = type_error();
                return node->resolved_type;
            }

            TypeEntry* entry = type_table_insert(name);
            entry->kind = TYPE_ENTRY_AGGREGATE;
            def = &entry->aggregate;
        }
    } else {
        def = (AggregateDef*)arena_alloc(ctx->arena, sizeof(AggregateDef));
        memset(def, 0, sizeof(*def));
    }

    type.inline_define = def;

    node->resolved_type = type;
    compute_aggregate_layout(ctx, node, def);
    return type;
}

static const char* keyword_name(TokenType keyword) {
    switch (keyword)
    {
    case KDATA: return "data";
    case KCOPERATE: return "coperate";
    case KENUM: return "enum";
    default: return "tipo";
    }
}

static Typecheck resolve_named(CheckContext *ctx, View name, TokenType keyword, Node *site) {
    TypeEntry* entry = type_table_lookup(name);

    if(entry) {
        if(entry->kind == TYPE_ENTRY_AGGREGATE) {
            bool ok = (keyword == IDENTIFIER) ||
                      (keyword == KDATA && entry->aggregate.kind == NODE_DATA) ||
                      (keyword == KCOPERATE && entry->aggregate.kind == NODE_COPERATE);
            if(ok) {
                Typecheck type = make_named(name, TC_AGGREGATE, 0);
                type.inline_define = &entry->aggregate;
                return type;
            }
        } else if(entry->kind == TYPE_ENTRY_ENUM) {
            if(keyword == IDENTIFIER || keyword == KENUM)
                return make_named(name, TC_ENUM, 0);
        }

        SEMA_ERR(site, "'%.*s' nao e do tipo '%s'", VIEW_ARG(name), keyword_name(keyword));
        return type_error();
    }

    TypeEntry* nt = newtype_table_lookup(name);
    if(nt) {
        if(keyword == IDENTIFIER) {
            Typecheck type = make_named(name, TC_NEWTYPE, 0);

            if(nt->alias.ptr_lvl > 0) {
                type.repr = KUINT64;                           /* newtype de ponteiro: 8 bytes */
            } else {
                type.repr = nt->alias.repr;                    /* bool -> KUINT8, enum -> KINT32, ... */
                type.inline_define = nt->alias.inline_define;  /* newtype de data/coperate */
            }
            return type;
        }

        SEMA_ERR(site, "'%.*s' e um newtype, nao um '%s'", VIEW_ARG(name), keyword_name(keyword));
        return type_error();
    }

    SEMA_ERR(site, "O tipo '%.*s' nao foi declarado", VIEW_ARG(name));
    return type_error();
}

static bool apply_array_info(CheckContext *ctx, Typecheck *type, TypeSpec spec, Node *site) {
    bool ok = true;
    type->_array = spec.is_array; 
    type->_vla = false; 
    type->array_size = 0;
    type->array_rank = 0;
    memset(type->array_dims, 0, sizeof(type->array_dims));
    if(!spec.is_array) return true;
    NodeList* dims = spec.array_dim;
    if(!dims || dims->count == 0) { type->array_rank = 1; return true; }
    if(dims->count > MAX_ARRAY_RANK) {
        SEMA_ERR(site, "Arrays suportam no maximo %d dimensoes", MAX_ARRAY_RANK);
        return false;
    }
    type->array_rank = (uint8_t)dims->count;

    size_t total = 1;
    bool omitted = false;

    for(size_t i = 0; i < dims->count; ++i) {
        Node* dim = dims->items[i];

        if(dimention_isommited(dim)) {
            if(i != 0) SEMA_ERR(site, "Somente a primeira dimensao do array pode ser omitida");
            omitted = true;
            continue;
        }

        Typecheck dtype = check_expr(ctx, dim);
        if(type_is_error(dtype)) { type->_vla = true; continue; }

        Typecheck core = core_type(dtype);
        if(!isinteger(core) && !isenum(core)) {
            SEMA_ERR(dim, "O tamanho do array deve ser inteiro");
            type->_vla = true;
            continue;
        }

        ConstInt c;
        if(!const_eval(dim, &c)) { type->_vla = true; continue; }

        if(c.big || c.value <= 0) {
            SEMA_ERR(dim, "O tamanho do array deve ser positivo");
            type->_vla = true;
            continue;
        }

        if(total > ((size_t)-1) / (size_t)c.value) {
            SEMA_ERR(dim, "O tamanho do array e grande demais");
            type->_vla = true;
            continue;
        }

        type->array_dims[i] = (size_t)c.value;
        total *= (size_t)c.value;
    }

    if(!type->_vla && !omitted) type->array_size = total;
    return ok;
}

static Typecheck resolve_type_spec(CheckContext *ctx, TypeSpec spec, Node *node) {
    Typecheck type;

    if(spec.nested) {
        Node* nested = spec.nested;

        if(spec.base == KENUM) {
            if(nested->ast.enum_decl.members == NULL) {
                type = resolve_named(ctx, nested->ast.enum_decl.name, KENUM, node);
            } else {
                check_enum_decl(ctx, nested);
                type = nested->resolved_type;
            }
        } else {
            if(nested->ast.aggregate.members == NULL) {
                TokenType keyword = (nested->kind == NODE_COPERATE) ? KCOPERATE : KDATA;
                type = resolve_named(ctx, nested->ast.aggregate.name, keyword, node);
            } else {
                type = declare_aggregate(ctx, nested);
            }
        }
    }
    else if(spec.base == KDATA || spec.base == KENUM || spec.base == KCOPERATE) {
        if(spec.alias.len == 0) {
            SEMA_ERR(node, "Tipo '%s' sem nome", keyword_name(spec.base));
            return type_error();
        }
        type = resolve_named(ctx, spec.alias, spec.base, node);
    }
    else if(spec.base == IDENTIFIER) {
        type = resolve_named(ctx, spec.alias, IDENTIFIER, node);
    }
    else {
        type = make_type(spec.base, 0);

        if(spec.base == KLINK && (spec.ptr_lvl > 0 || spec.is_array)) {
            SEMA_ERR(node, "Variaveis de tipo link nao podem ser ponteiros/arrays");
            return type_error();
        }
    }

    if(type_is_error(type)) return type;

    type.ptr_lvl += spec.ptr_lvl;
    if(!apply_array_info(ctx, &type, spec, node)) return type_error();
    return type;
}

static void check_enum_decl(CheckContext *ctx, Node *node) {
    if(node->resolved_type.base != EOFF || node->resolved_type.error) return;

    View name = node->ast.enum_decl.name;
    NodeList* members = node->ast.enum_decl.members;

    if(!members) return;

    if(name.len > 0) {
        TypeEntry* prev = type_table_lookup(name);

        if(prev && enum_same_as(prev, node)) {
            node->resolved_type = make_named(name, TC_ENUM, 0);
            return;
        }

        if(prev || newtype_table_lookup(name)) {
            SEMA_ERR(node, "O tipo '%.*s' ja foi declarado anteriormente", VIEW_ARG(name));
            node->resolved_type = type_error();
            return;
        }
    }

    EnumMemberInfo* infos = (EnumMemberInfo*)arena_alloc(ctx->arena,
        sizeof(EnumMemberInfo) * (members->count ? members->count : 1));

    long long next = 0;

    for(size_t i = 0; i < members->count; ++i) {
        Node* member = members->items[i];
        View member_name = member->ast.enum_member.name;
        Node* value = member->ast.enum_member.value;

        if(value && value->kind != NODE_UNDEFINED) {
            Typecheck vtype = check_expr(ctx, value);

            if(!type_is_error(vtype)) {
                Typecheck core = core_type(vtype);
                ConstInt c;

                if((!isinteger(core) && !isenum(core)) || !const_eval(value, &c)) {
                    SEMA_ERR(value, "Valor de membro de enum precisa ser uma constante inteira");
                } else if(c.big || c.value < -2147483647LL - 1 || c.value > 4294967295LL) {
                    SEMA_ERR(value, "Valor de membro de enum excede 32 bits");
                } else {
                    next = c.value;
                }
            }
        }

        for(size_t j = 0; j < i; ++j) {
            if(svcmp(infos[j].name, member_name)) {
                SEMA_ERR(member, "Membro '%.*s' duplicado no enum", VIEW_ARG(member_name));
                break;
            }
        }

        if(name.len == 0) {
            EnumConstEntry* global = enum_const_declare(member_name);
            if(!global) {
                SEMA_ERR(member, "O enumerador anonimo '%.*s' ja foi declarado em outro enum anonimo",
                         VIEW_ARG(member_name));
            } else {
                global->enum_name = name;
                global->value = next;
                global->decl_node = member;
            }
        }

        infos[i].name = member_name;
        infos[i].ivalue = next;
        next++;
    }

    if(name.len > 0) {
        TypeEntry* entry = type_table_insert(name);
        entry->kind = TYPE_ENTRY_ENUM;
        entry->enumdef.member_count = members->count;
        entry->enumdef.members = infos;
        node->resolved_type = make_named(name, TC_ENUM, 0);
    } else {
        node->resolved_type = make_type(KINT32, 0);
    }
}

static Typecheck check_enum_access(CheckContext *ctx, Node *node) {
    View enum_name = node->ast.enum_access.alias;
    View member_name = node->ast.enum_access.member;

    TypeEntry* entry = type_table_lookup(enum_name);
    if(!entry || entry->kind != TYPE_ENTRY_ENUM) {
        SEMA_ERR(node, "'%.*s' nao e um enum declarado", VIEW_ARG(enum_name));
        return type_error();
    }

    for(size_t i = 0; i < entry->enumdef.member_count; ++i) {
        if(svcmp(entry->enumdef.members[i].name, member_name)) {
            node->ast.enum_access.iresolved_type = (long long)entry->enumdef.members[i].ivalue;
            return make_named(enum_name, TC_ENUM, 0);
        }
    }

    SEMA_ERR(node, "'%.*s' nao e um membro de '%.*s'", VIEW_ARG(member_name), VIEW_ARG(enum_name));
    return type_error();
}

static void check_aggregate_decl(CheckContext *ctx, Node *node) {
    if(node->ast.aggregate.members != NULL) declare_aggregate(ctx, node);
    if(node->ast.aggregate.tailing) check_var_decl(ctx, node->ast.aggregate.tailing);
}

static TypeEntry* forward_declare_aggregate(CheckContext *ctx, View name, NodeKinds kind, Node *site) {
    TypeEntry* e = type_table_lookup(name);

    if(e) {
        if(e->kind != TYPE_ENTRY_AGGREGATE || e->aggregate.kind != kind) {
            SEMA_ERR(site, "O tipo '%.*s' ja foi declarado anteriormente com outra categoria", VIEW_ARG(name));
            return NULL;
        }
        return e;
    }

    if(newtype_table_lookup(name)) {
        SEMA_ERR(site, "O tipo '%.*s' ja foi declarado anteriormente", VIEW_ARG(name));
        return NULL;
    }

    e = type_table_insert(name);
    e->kind = TYPE_ENTRY_AGGREGATE;
    e->aggregate.kind = kind;
    e->aggregate.align = 1;
    e->aggregate.complete = false;
    return e;
}

static bool is_incomplete_aggregate(Typecheck t) {
    t = core_type(t);
    return t.cls == TC_AGGREGATE && t.ptr_lvl == 0 &&
           t.inline_define && !t.inline_define->complete;
}

static void check_newtype_decl(CheckContext *ctx, Node *node) {
    View name = node->ast.newtype.alias;
    
    TypeSpec* underlying_spec = &node->ast.newtype.underlying;

    if(underlying_spec->nested && underlying_spec->ptr_lvl == 0 && !underlying_spec->is_array) {
        Node* nested = underlying_spec->nested;

        if(underlying_spec->base == KENUM) {
            if(nested->ast.enum_decl.members && nested->ast.enum_decl.name.len == 0) {
                nested->ast.enum_decl.name = name;
                check_enum_decl(ctx, nested);
                return;
            }
        } else if(nested->ast.aggregate.members && nested->ast.aggregate.name.len == 0) {
            nested->ast.aggregate.name = name;
            declare_aggregate(ctx, nested);
            return;
        }
    }

    if(underlying_spec->ptr_lvl == 0 && !underlying_spec->is_array) {
        View tag = { 0 };
        NodeKinds kind = NODE_UNDEFINED;

        if(underlying_spec->nested) {
            Node* n = underlying_spec->nested;
            if(underlying_spec->base != KENUM && !n->ast.aggregate.members) {
                tag = n->ast.aggregate.name;
                kind = n->kind;
            }
        } else if(underlying_spec->base == KDATA || underlying_spec->base == KCOPERATE) {
            tag = underlying_spec->alias;
            kind = (underlying_spec->base == KDATA) ? NODE_DATA : NODE_COPERATE;
        }

        if(tag.len > 0) {
            TypeEntry* e = forward_declare_aggregate(ctx, tag, kind, node);
            if(!e) return;
            if(svcmp(tag, name)) return;
        }
    }

    Typecheck underlying = resolve_type_spec(ctx, *underlying_spec, node);
    if(type_is_error(underlying)) return;

    if(underlying._array) {
        SEMA_ERR(node, "newtype de array nao e suportado");
        return;
    }

    TypeEntry* prev = newtype_table_lookup(name);
    if(prev) {
        if(type_equals(prev->alias, underlying)) return;
        SEMA_ERR(node, "O newtype '%.*s' ja foi declarado com outro tipo base: antes '%s', agora '%s'",
                 VIEW_ARG(name), tname(prev->alias), tname(underlying));
        return;
    }

    if(type_table_lookup(name)) {
        SEMA_ERR(node, "O tipo '%.*s' ja foi declarado anteriormente", VIEW_ARG(name));
        return;
    }

    TypeEntry* entry = newtype_table_insert(name);
    entry->kind = TYPE_ENTRY_ALIAS;
    entry->alias = underlying;
}

static Typecheck check_bytes(CheckContext *ctx, Node *node) {
    Typecheck type;

    if(node->ast.bytes.is_expr) type = check_expr(ctx, node->ast.bytes.operand);
    else type = resolve_type_spec(ctx, node->ast.bytes.spec, node);

    if(type_is_error(type)) return type_error();

    size_t size = type_size(type);

    if(size == 0) {
        SEMA_ERR(node, "Nao foi possivel determinar o tamanho do tipo");
        return type_error();
    }

    node->ast.bytes.size = size;
    return make_type(KUINT64, 0);
}

static Typecheck check_literal(Node *node) {
    switch (node->ast.literal.kind)
    {
    case INT:
        return make_type(node->ast.literal.i64 > 2147483647LL ||
                         node->ast.literal.i64 < -2147483647LL - 1 ? KINT64 : KINT32, 0);
    case BIG: return make_type(KBIG, 0);
    case UNSIGNED:
        return make_type(node->ast.literal.u64 > 0xFFFFFFFFULL ? KUINT64 : KUINT32, 0);
    case FLOAT: return make_type(KFLOAT, 0);
    case DOUBLE: return make_type(KDOUBLE, 0);
    case HEXA: return make_type(KHEXA, 0);
    case CHAR: return make_type(KCHAR, 0);
    case STRING: return make_type(KCHAR, 1);
    case WIDE_CHAR: return make_type(KUTFCHAR, 0);
    case WIDE_STRING: return make_type(KUTFCHAR, 1);
    }

    return type_error();
}

static void node_become_literal(Node *node, const ConstValue *cv) {
    memset(&node->ast.literal, 0, sizeof(node->ast.literal));
    node->kind = NODE_LITERAL;
    node->ast.literal.kind = cv->lkind;

    switch (cv->lkind)
    {
    case FLOAT: case DOUBLE: node->ast.literal.f64 = cv->f; break;
    case UNSIGNED:           node->ast.literal.u64 = (unsigned long long)cv->i.value; break;
    default:                 node->ast.literal.i64 = cv->i.value; break;
    }
}

static bool const_value_of_init(Typecheck declared, Node *init, ConstValue *out) {
    memset(out, 0, sizeof(*out));

    if(!init || init->kind == NODE_INIT_LIST || type_is_error(declared)) return false;
    if(declared._array || declared.ptr_lvl > 0 || declared._atomic) return false;

    Typecheck core = core_type(declared);
    if(core._array || core.ptr_lvl > 0) return false;

    if(isinteger(core) || isenum(core)) {
        ConstInt c;
        if(!const_eval(init, &c)) return false;

        bool hexa = ishexatype(core);
        if(isinteger(core) && !hexa) const_wrap(&c, core);

        out->known = true;
        out->i = c;
        out->lkind = (c.big || hexa) ? UNSIGNED : INT;
        return true;
    }

    if(isfloat(core)) {
        long double v;

        if(init->kind == NODE_LITERAL &&
           (init->ast.literal.kind == FLOAT || init->ast.literal.kind == DOUBLE)) {
            v = init->ast.literal.f64;
        } else {
            ConstInt c;
            if(!const_eval(init, &c)) return false;
            v = c.big ? (long double)(unsigned long long)c.value : (long double)c.value;
        }

        if(core.base == KFLOAT) {
            v = (long double)(float)v;
            out->lkind = FLOAT;
        } else {
            v = (long double)(double)v;
            out->lkind = DOUBLE;
        }

        out->known = true;
        out->f = v;
        return true;
    }

    return false;
}

static Node* first_non_constant(CheckContext *ctx, Node *node) {
    if(!node) return NULL;

    switch (node->kind)
    {
    case NODE_LITERAL: case NODE_ENUM_CALL: case NODE_BYTES:
        return NULL;

    case NODE_VAR_CALL:
        return is_function_name(ctx, node->ast.var_call.name) ? NULL : node;

    case NODE_CAST:
        return first_non_constant(ctx, node->ast.cast.operand);

    case NODE_UNARY_OP: {
        TokenType op = node->ast.unary.op;
        if(op == OPAND) return NULL;
        if(op == OPSTAR || op == OPPLUSPLUS || op == OPMINUSMINUS) return node;
        return first_non_constant(ctx, node->ast.unary.operand);
    }

    case NODE_BINARY_OP: {
        if(is_assign_op(node->ast.binary.op)) return node;
        Node* bad = first_non_constant(ctx, node->ast.binary.left);
        return bad ? bad : first_non_constant(ctx, node->ast.binary.right);
    }

    case NODE_TERNARY: {
        Node* bad = first_non_constant(ctx, node->ast.if_stmt.condition);
        if(!bad) bad = first_non_constant(ctx, node->ast.if_stmt.then);
        if(!bad) bad = first_non_constant(ctx, node->ast.if_stmt.otherwise);
        return bad;
    }

    case NODE_DESIGNATOR:
        return first_non_constant(ctx, node->ast.designator.value);

    case NODE_INIT_LIST: {
        NodeList* elems = node->ast.init_list.elements;
        if(!elems) return NULL;
        for(size_t i = 0; i < elems->count; ++i) {
            Node* bad = first_non_constant(ctx, elems->items[i]);
            if(bad) return bad;
        }
        return NULL;
    }

    default:
        return node;
    }
}

static void report_non_constant(CheckContext *ctx, Node *bad, const char* what) {
    if(bad->kind == NODE_VAR_CALL) {
        View name = bad->ast.var_call.name;

        if(!scope_lookup_chain(ctx->current_scope, name)) {
            GlobalEntry* g = global_var_lookup(name);

            if(g && g->is_const && !g->cval.known) {
                SEMA_ERR(bad, "A variavel const '%.*s' nao tem valor conhecido em tempo de compilacao "
                              "(extern, sem inicializador, ou inicializador nao constante)", VIEW_ARG(name));
                if(g->decl_node) SEMA_NOTE(g->decl_node, "'%.*s' declarada aqui", VIEW_ARG(name));
                return;
            }
        }

        SEMA_ERR(bad, "'%.*s' nao e uma constante: o inicializador de uma variavel %s so aceita "
                      "literais, enumeradores, const com valor constante e enderecos",
                 VIEW_ARG(name), what);
        return;
    }

    SEMA_ERR(bad, "O inicializador de uma variavel %s precisa ser constante", what);
}

static Typecheck check_var_access(CheckContext *ctx, Node *node) {
    View name = node->ast.var_call.name;

    SymbolEntry* local = scope_lookup_chain(ctx->current_scope, name);
    if(local) {
        Typecheck type = local->type;
        if(!ctx->no_fold && local->cval.known) node_become_literal(node, &local->cval);
        return type;
    }

    GlobalEntry* global = global_var_lookup(name);
    if(global) {
        Typecheck type = global->type;
        if(!ctx->no_fold && global->cval.known) node_become_literal(node, &global->cval);
        return type;
    }

    /* so enumeradores de enums anonimos chegam aqui */
    EnumConstEntry* constant = enum_const_lookup(name);
    if(constant) {
        ConstValue cv = { true, INT, { constant->value, false }, 0 };
        node_become_literal(node, &cv);
        return make_type(KINT32, 0);
    }

    FuncEntry* fe = function_table_lookup(name);
    if(fe && !fe->_builtin) return make_type(KLINK, 0);

    SEMA_ERR(node, "A variavel '%.*s' nao foi declarada anteriormente", VIEW_ARG(name));
    return type_error();
}

static Typecheck arith_result(CheckContext *ctx, Node *node, Typecheck l, Typecheck r,
                              Node *left, Node *right, bool integer_only) {
    if(isenum(l) || isenum(r)) {
        SEMA_ERR(node, "Enums nao aceitam operadores aritmeticos/bit a bit sem cast");
        return type_error();
    }

    if(isnewtype(l) || isnewtype(r)) {
        Typecheck nt = isnewtype(l) ? l : r;
        Typecheck other = isnewtype(l) ? r : l;
        Node* other_node = isnewtype(l) ? right : left;

        bool ok = type_equals(l, r) || (other.cls == TC_PRIM && is_numeric_constant(other_node));
        if(!ok) {
            SEMA_ERR(node, "Operacao com newtype exige o mesmo newtype nos dois lados (ou constante literal)");
            return type_error();
        }

        Typecheck core = core_type(nt);
        if(integer_only ? !isinteger(core) : !isnumeric(core)) {
            SEMA_ERR(node, integer_only ? "O operador exige operandos inteiros" : "O operador exige tipos numericos");
            return type_error();
        }

        return nt;
    }

    TokenType op = node->ast.binary.op;
    if((ishexatype(l) || ishexatype(r)) && (op == OPOR || op == OPAND || op == OPXOR)) {
        Typecheck hx      = ishexatype(l) ? l : r;
        Typecheck other   = ishexatype(l) ? r : l;
        Node*     other_n = ishexatype(l) ? right : left;

        ConstInt c;
        bool ok = ishexatype(other) ||
                  (isinteger(other) && const_eval(other_n, &c) && (c.big || c.value >= 0));

        if(ok) return hx;

        SEMA_ERR(node, "Operadores bit a bit compostos em 'hexa' exigem outro 'hexa' ou uma constante inteira nao negativa");
        return type_error();
    }

    if(!isnumeric(l) || !isnumeric(r) || (integer_only && (isfloat(l) || isfloat(r)))) {
        SEMA_ERR(node, integer_only ? "O operador exige operandos inteiros"
                                    : "Operadores aritmeticos exigem tipos numericos");
        return type_error();
    }

    if(ishexatype(l) || ishexatype(r)) {
        if(isfloat(l) || isfloat(r)) {
            SEMA_ERR(node, "'hexa' nao pode ser misturado com float/double na aritmetica");
            return type_error();
        }
        return make_type(KHEXA, 0);
    }

    if(isinteger(l) && isinteger(r)) {
        ConstInt c;
        if(integerwidth(l.base) < integerwidth(r.base) && const_eval(right, &c) && const_fits(c, l.base)) return l;
        if(integerwidth(r.base) < integerwidth(l.base) && const_eval(left, &c) && const_fits(c, r.base)) return r;
    }

    return promote_from_arith(l, r);
}

static Typecheck check_comparison(CheckContext *ctx, Node *node, Typecheck l, Typecheck r, Node *left, Node *right) {
    /* hexa x inteiro: comparacao numerica normal */
    if((ishexatype(l) && isinteger(r)) || (ishexatype(r) && isinteger(l)))
        return make_type(KUINT8, 0);

    if(isisland(l) || isisland(r)) {
        if(!(isisland(l) && isisland(r))) {
            SEMA_ERR(node, "Comparacao entre endereco e valor nao-endereco exige cast");
            return type_error();
        }

        if(!islink(l) && !islink(r) && !ishexatype(l) && !ishexatype(r) && !type_equals(l, r)) {
            SEMA_ERR(node, "Comparacao entre ponteiros de tipos diferentes");
            return type_error();
        }

        return make_type(KUINT8, 0);
    }

    if(isenum(l) || isenum(r)) {
        if(!type_equals(l, r)) {
            SEMA_ERR(node, "Um enum so pode ser comparado com membros do mesmo enum");
            return type_error();
        }
        return make_type(KUINT8, 0);
    }

    if(isaggregate(l) || isaggregate(r)) {
        SEMA_ERR(node, "data/coperate nao podem ser comparados diretamente");
        return type_error();
    }

    if(isnewtype(l) || isnewtype(r)) {
        Typecheck other = isnewtype(l) ? r : l;
        Node* other_node = isnewtype(l) ? right : left;

        if(!type_equals(l, r) && !(other.cls == TC_PRIM && is_numeric_constant(other_node))) {
            SEMA_ERR(node, "Comparacao com newtype exige o mesmo newtype nos dois lados (ou constante literal)");
            return type_error();
        }
        return make_type(KUINT8, 0);
    }

    if(!isnumeric(l) || !isnumeric(r)) {
        SEMA_ERR(node, "Operandos incompativeis para a comparacao: '%s' e '%s'", tname(l), tname(r));
        return type_error();
    }

    return make_type(KUINT8, 0);
}

static void check_div_zero(CheckContext *ctx, Node *node, Node *divisor) {
    ConstInt c;
    if(const_eval(divisor, &c) && !c.big && c.value == 0)
        SEMA_ERRC(node, "T040", "Divisao por zero em expressao constante");
}

static Typecheck check_assignment_op(CheckContext *ctx, Node *node, Typecheck ltype, Typecheck rtype) {
    Node* left = node->ast.binary.left;
    Node* right = node->ast.binary.right;
    TokenType op = node->ast.binary.op;

    if(!islvalue(left)) {
        SEMA_ERR(node, "Lado esquerdo da atribuicao precisa ser variavel, campo, indice ou ponteiro desreferenciado");
        return type_error();
    }

    if(ltype._array) {
        SEMA_ERR(node, "Arrays nao podem ser atribuidos diretamente");
        return type_error();
    }

    if(op == ASSIGN) {
        report_assignable(ctx, rtype, ltype, right, node, "na atribuicao");
        return ltype;
    }

    if(ishexatype(ltype) && (op == OPORE || op == OPANDE || op == OPXORE)) {
        ConstInt c;
        bool rhs_ok = ishexatype(rtype) ||
                      (isinteger(rtype) && const_eval(right, &c) && (c.big || c.value >= 0));

        if(rhs_ok) return ltype;

        SEMA_ERR(node, "Operadores bit a bit compostos em 'hexa' exigem outro 'hexa' ou uma constante inteira nao negativa");
        return type_error();
    }

    Typecheck r = decay(rtype);

    if((op == OPPLUSE || op == OPMINUSE) && isptr(ltype) && isinteger(core_type(r))) return ltype;

    if((op == OPPLUSE || op == OPMINUSE) && ishexatype(ltype) && isinteger(core_type(r))) return ltype;

    if((op == OPMODE || op == OPSLASHE)) check_div_zero(ctx, node, right);

    bool integer_only = (op != OPPLUSE && op != OPMINUSE && op != OPSLASHE && op != OPSTARE);
    Typecheck result = arith_result(ctx, node, ltype, r, left, right, integer_only);

    if(type_is_error(result)) return type_error();
    return ltype;
}

static Typecheck check_binary_type(CheckContext *ctx, Node *node) {
    Node* left = node->ast.binary.left;
    Node* right = node->ast.binary.right;
    TokenType op = node->ast.binary.op;

    bool prev_no_fold = ctx->no_fold;
    if(is_assign_op(op)) ctx->no_fold = true;
    Typecheck ltype = check_expr(ctx, left);
    ctx->no_fold = prev_no_fold;

    Typecheck rtype = check_expr(ctx, right);

    if(type_is_error(ltype) || type_is_error(rtype)) return type_error();

    if(is_assign_op(op)) return check_assignment_op(ctx, node, ltype, rtype);

    Typecheck l = decay(ltype);
    Typecheck r = decay(rtype);

    switch (op)
    {
    case OPPLUS: case OPMINUS: {
        if(isptr(l) && isinteger(core_type(r))) return l;
        if(op == OPPLUS && isptr(r) && isinteger(core_type(l))) return r;

        if(op == OPMINUS && isptr(l) && isptr(r)) {
            if(!type_equals(l, r)) {
                SEMA_ERR(node, "Subtracao entre ponteiros de tipos diferentes");
                return type_error();
            }
            return make_type(KINT64, 0);
        }

        if(ishexatype(l) && isinteger(core_type(r))) return l;
        if(op == OPPLUS && ishexatype(r) && isinteger(core_type(l))) return r;
        if(op == OPMINUS && ishexatype(l) && ishexatype(r)) return make_type(KINT64, 0);

        return arith_result(ctx, node, l, r, left, right, false);
    }

    case OPSTAR: case OPSLASH:
        check_div_zero(ctx, node, right);
        return arith_result(ctx, node, l, r, left, right, false);

    case OPMOD:
        check_div_zero(ctx, node, right);
        return arith_result(ctx, node, l, r, left, right, true);

    case OPLT: case OPGT: case OPLE: case OPGE: case OPBANGE: case OPEQ:
        return check_comparison(ctx, node, l, r, left, right);

    case LOGAND: case LOGOR: {
        bool lok = require_scalar(ctx, left, l);
        bool rok = require_scalar(ctx, right, r);
        if(!lok || !rok) return type_error();
        return make_type(KUINT8, 0);
    }

    case OPSHL: case OPSHR: {
        /* o tipo do shift e o do operando da ESQUERDA: a regra de "constante cabe no tipo menor"
           do arith_result transformava (1U << uint8) em uint8 */
        Typecheck res = arith_result(ctx, node, l, r, left, right, true);
        if(type_is_error(res)) return res;
        if(isinteger(l) && !ishexatype(l)) return l;
        return res;
    }

    case OPAND: case OPOR: case OPXOR:
        return arith_result(ctx, node, l, r, left, right, true);

    default:
        SEMA_ERR(node, "Operador desconhecido");
        return type_error();
    }
}

static Typecheck check_binary_op(CheckContext *ctx, Node *node) {
    Typecheck t = check_binary_type(ctx, node);
    if(type_is_error(t)) return t;
    if(!fold_const_binary(ctx, node, t)) return type_error();
    return t;
}

static Typecheck array_elem(Typecheck t) {
    if(!t._array) { 
        t.ptr_lvl--; 
        return t; 
    }

    if(t.array_rank > 1) {
        size_t total = 1;
        bool unknown = false;
        for(size_t i = 1; i < t.array_rank; ++i) {
            t.array_dims[i - 1] = t.array_dims[i];
            if(t.array_dims[i] == 0) unknown = true; else total *= t.array_dims[i];
        }
        t.array_rank--;
        t.array_dims[t.array_rank] = 0;
        t.array_size = unknown ? 0 : total;
        t._vla = unknown;
        return t;
    }

    t._array = false; t._vla = false; t.array_size = 0;
    t.array_rank = 0; t.array_dims[0] = 0;
    return t;
}

static bool same_file(const char* a, const char* b) {
    return a == b || (a && b && strcmp(a, b) == 0);
}

static Typecheck check_array(CheckContext *ctx, Node *node) {
    Node* left = node->ast.binary.left;
    Node* right = node->ast.binary.right;

    Typecheck base = check_expr(ctx, left);
    Typecheck index = check_expr(ctx, right);

    if(type_is_error(base) || type_is_error(index)) return type_error();

    if(!base._array && !isptr(base)) {
        SEMA_ERR(node, "Indexacao '[]' so se aplica a ponteiro ou array, mas a base e '%s'", tname(base));
        return type_error();
    }

    if(!isinteger(core_type(index))) {
        SEMA_ERR(node, "Indices de arrays precisam ser valores inteiros");
        return type_error();
    }

    Typecheck elem = array_elem(base);

    if(isvoid(elem)) {
        SEMA_ERR(node, "Nao e possivel indexar 'void*'");
        return type_error();
    }

    return elem;
}

static Typecheck check_unary_op(CheckContext *ctx, Node *node) {
    Node* operand_node = node->ast.unary.operand;
    TokenType uop = node->ast.unary.op;

    bool prev_no_fold = ctx->no_fold;
    if(uop == OPAND || uop == OPPLUSPLUS || uop == OPMINUSMINUS) ctx->no_fold = true;
    Typecheck operand = check_expr(ctx, operand_node);
    ctx->no_fold = prev_no_fold;

    if(type_is_error(operand)) return type_error();

    switch (uop)
    {
    case OPPLUS:
    case OPMINUS: {
        if(!isnumeric(core_type(operand))) {
            SEMA_ERR(node, "Operador unario '-' exige tipo numerico, mas recebeu '%s'", tname(operand));
            return type_error();
        }

        if(node->kind == NODE_UNARY_OP && uop == OPMINUS && operand_node->kind == NODE_LITERAL) {
            LiteralKind lk = operand_node->ast.literal.kind;

            if(lk == INT || lk == BIG) {
                long long v = operand_node->ast.literal.i64;
                bool narrow = integerwidth(core_type(operand).base) <= 32;

                if(v == LLONG_MIN || (lk == INT && narrow && v == -2147483647LL - 1)) {
                    SEMA_ERR(node, "Overflow em expressao constante ao negar o literal");
                    return operand;
                }

                ConstValue cv = { true, lk, { -v, false }, 0 };
                node_become_literal(node, &cv);
            }
            else if(lk == FLOAT || lk == DOUBLE) {
                ConstValue cv = { true, lk, { 0, false }, -operand_node->ast.literal.f64 };
                node_become_literal(node, &cv);
            }
        }

        return operand;
    }

    case OPBANG: {
        if(!require_scalar(ctx, operand_node, operand)) return type_error();
        return make_type(KUINT8, 0);
    }

    case OPAND: {
        if(!islvalue(operand_node)) {
            SEMA_ERR(node, "O operador '&' exige um valor enderecavel");
            return type_error();
        }

        if(islink(operand)) {
            SEMA_ERR(node, "Nao e possivel pegar o endereco de um 'link'");
            return type_error();
        }

        if(operand._array) return decay(operand);

        operand.ptr_lvl++;
        return operand;
    }

    case OPSTAR: {
        operand = decay(operand);

        if(operand.ptr_lvl == 0) {
            SEMA_ERR(node, "Nao e possivel deferenciar um valor que nao e ponteiro (converta 'link'/'hexa' com cast)");
            return type_error();
        }

        operand.ptr_lvl--;

        if(isvoid(operand)) {
            SEMA_ERR(node, "Nao e possivel deferenciar 'void*'");
            return type_error();
        }

        return operand;
    }

    case OPPLUSPLUS: case OPMINUSMINUS: {
        if(!islvalue(operand_node)) {
            SEMA_ERR(node, "Os operadores '++'/'--' exigem um valor modificavel");
            return type_error();
        }

        if(isptr(operand) || isnumeric(core_type(operand))) return operand;

        SEMA_ERR(node, "Os operadores '++'/'--' exigem tipos numericos ou ponteiros");
        return type_error();
    }

    case DESC: {
        if(!isinteger(core_type(operand))) {
            SEMA_ERR(node, "Operador '~' exige um tipo numerico inteiro");
            return type_error();
        }
        return operand;
    }

    default:
        return operand;
    }
}

static Typecheck check_cast(CheckContext *ctx, Node *node) {
    Typecheck from = check_expr(ctx, node->ast.cast.operand);
    Typecheck target = resolve_type_spec(ctx, node->ast.cast.spec, node);

    if(type_is_error(target)) return type_error();
    if(type_is_error(from)) return target;

    if(!check_castable(from, target)) {
        char from_buffer[512], to_buffer[512];
        format_type(from_buffer, sizeof(from_buffer), from);
        format_type(to_buffer, sizeof(to_buffer), target);
        SEMA_ERR(node, "Cast invalido de '%s' para '%s'", from_buffer, to_buffer);
        return type_error();
    }

    return target;
}

static Typecheck check_ternary(CheckContext *ctx, Node *node) {
    check_condition(ctx, node->ast.if_stmt.condition);

    Typecheck ttype = check_expr(ctx, node->ast.if_stmt.then);
    Typecheck otype = check_expr(ctx, node->ast.if_stmt.otherwise);

    if(type_is_error(ttype) || type_is_error(otype)) return type_error();

    Typecheck t = decay(ttype);
    Typecheck o = decay(otype);

    if(type_equals(t, o)) return t;

    if(isisland(t) && isisland(o)) {
        if(islink(t) || islink(o)) return make_type(KLINK, 0);
        if(ishexatype(t) || ishexatype(o)) return make_type(KHEXA, 0);
    }
    else if(isnumeric(t) && isnumeric(o)) {
        return promote_from_arith(t, o);
    }

    SEMA_ERR(node, "Ramos do operador ternario tem tipos incompativeis");
    return type_error();
}

static Typecheck check_builtin_expect(CheckContext *ctx, Node *node) {
    NodeList* args = node->ast.func_call.args;
    size_t argc = args ? args->count : 0;

    if(argc != 2) {
        SEMA_ERRC(node, "T011", "Numero incorreto de argumentos para '__builtin_expect': esperado 2, recebeu %zu", argc);
        for(size_t i = 0; i < argc; ++i) check_expr(ctx, args->items[i]);
        return type_error();
    }

    Typecheck value = check_expr(ctx, args->items[0]);
    Typecheck expected = check_expr(ctx, args->items[1]);
    if(type_is_error(value) || type_is_error(expected)) return type_error();

    Typecheck v = decay(value);
    if(!require_scalar(ctx, args->items[0], v)) return type_error();

    ConstInt c;
    Typecheck ecore = core_type(expected);
    if((!isinteger(ecore) && !isenum(ecore)) || !const_eval(args->items[1], &c)) {
        SEMA_ERR(args->items[1], "O segundo argumento de '__builtin_expect' precisa ser uma constante inteira");
        return type_error();
    }

    return v;
}

static bool offsetof_apply_field(CheckContext *ctx, Node *site, Typecheck *cur, View name, size_t *off) {
    Typecheck core = core_type(*cur);
    if(cur->_array || !isaggregate(core) || !core.inline_define || !core.inline_define->complete) {
        SEMA_ERR(site, "'__offsetof': o tipo '%s' nao e um data/coperate completo, nao possui campos", tname(*cur));
        return false;
    }

    FieldInfo* f = find_field(core.inline_define, name);
    if(!f) {
        SEMA_ERR(site, "'__offsetof': o campo '%.*s' nao existe no tipo '%s'", VIEW_ARG(name), tname(*cur));
        return false;
    }

    *off += f->offset;
    *cur = f->type;
    return true;
}

/* Percorre campo | campo.campo | campo[const] ... partindo do tipo em *cur. */
static bool offsetof_walk(CheckContext *ctx, Node *n, Typecheck *cur, size_t *off) {
    switch (n->kind)
    {
    case NODE_VAR_CALL:
        return offsetof_apply_field(ctx, n, cur, n->ast.var_call.name, off);

    case NODE_FIELD_CALL:
        if(n->ast.field_access.arrow) {
            SEMA_ERR(n, "'__offsetof': o operador '->' nao e permitido no caminho do campo");
            return false;
        }
        if(!offsetof_walk(ctx, n->ast.field_access.base, cur, off)) return false;
        return offsetof_apply_field(ctx, n, cur, n->ast.field_access.name, off);

    case NODE_ARRAY: {
        if(!offsetof_walk(ctx, n->ast.binary.left, cur, off)) return false;

        if(!cur->_array) {
            SEMA_ERR(n, "'__offsetof': indexacao em algo que nao e array (tipo '%s')", tname(*cur));
            return false;
        }

        Node* ie = n->ast.binary.right;
        Typecheck it = check_expr(ctx, ie);
        if(type_is_error(it)) return false;

        Typecheck ic = core_type(it);
        ConstInt c;
        if((!isinteger(ic) && !isenum(ic)) || !const_eval(ie, &c) || c.big || c.value < 0) {
            SEMA_ERR(ie, "'__offsetof': o indice precisa ser uma constante inteira nao negativa");
            return false;
        }

        size_t len = cur->array_rank > 0 ? (size_t)cur->array_dims[0] : (size_t)cur->array_size;
        if(len != 0 && (size_t)c.value >= len) {
            SEMA_ERR(ie, "'__offsetof': indice %lld fora do array (tamanho %zu)", c.value, len);
            return false;
        }

        Typecheck el = array_elem(*cur);
        *off += (size_t)c.value * type_size(el);
        *cur = el;
        return true;
    }

    default:
        SEMA_ERR(n, "'__offsetof': o segundo argumento precisa ser um caminho de campo (campo, a.b, a[2].b)");
        return false;
    }
}

static Typecheck check_offsetof(CheckContext *ctx, Node *node) {
    NodeList* args = node->ast.func_call.args;
    size_t argc = args ? args->count : 0;

    if(argc != 2) {
        SEMA_ERRC(node, "T011", "Numero incorreto de argumentos para '__offsetof': esperado 2, recebeu %zu", argc);
        return type_error();
    }

    Node* tn = args->items[0];
    if(tn->kind != NODE_VAR_CALL) {
        SEMA_ERR(tn, "O primeiro argumento de '__offsetof' precisa ser o nome de um tipo data/coperate");
        return type_error();
    }

    Typecheck cur = resolve_named(ctx, tn->ast.var_call.name, IDENTIFIER, tn);
    if(type_is_error(cur)) return type_error();

    size_t off = 0;
    if(!offsetof_walk(ctx, args->items[1], &cur, &off)) return type_error();

    /* o no vira literal: funciona em const_eval, tamanho de array e inicializadores globais */
    ConstValue cv = { true, UNSIGNED, { (long long)off, false }, 0 };
    node_become_literal(node, &cv);
    return make_type(KUINT64, 0);
}

static Typecheck check_func_call(CheckContext *ctx, Node *node) {
    View name = node->ast.func_call.name;
    NodeList* args = node->ast.func_call.args;
    size_t argc = args ? args->count : 0;

    FuncEntry* entry = function_table_lookup(name);

    if(!entry) {
        SEMA_ERRC(node, "T010", "A funcao '%.*s' nao foi declarada antes desta chamada "
                  "(declare um prototipo, use 'extern', defina-a acima ou faca o 'extract' do header)",
                  VIEW_ARG(name));
        for(size_t i = 0; i < argc; ++i) check_expr(ctx, args->items[i]);
        return type_error();
    }

    entry->call_count++;

    if(svcmp(name, BUILTIN_VA_START) && !ctx->in_variadic)
        SEMA_ERR(node, "'__va_start' so pode ser usado dentro de uma funcao variadica");
    if(svcmp(name, BUILTIN_EXPECT)) return check_builtin_expect(ctx, node);
    if(svcmp(name, BUILTIN_OFFSETOF)) return check_offsetof(ctx, node);

    if(!entry->first_call) entry->first_call = node;

    FunctionSignature* signature = &entry->signature;

    /* extern = funcao externa: confiamos no header, sem checar contagem nem tipos */
    bool trusted = entry->_extern && !entry->has_body && !entry->_builtin;

    if(trusted) {
        for(size_t i = 0; i < argc; ++i) check_expr(ctx, args->items[i]);
        return signature->call_type;
    }

    bool count_ok = !(argc < signature->param_count || (!signature->variadic && argc > signature->param_count));

    if(!count_ok) {
        char sig[768];
        format_signature(sig, sizeof sig, name, signature);
        SEMA_ERRC(node, "T011", "Numero incorreto de argumentos para '%.*s': esperado %s%zu, recebeu %zu",
                  VIEW_ARG(name), signature->variadic ? "pelo menos " : "", signature->param_count, argc);
        if(entry->decl_node) SEMA_NOTE(entry->decl_node, "assinatura: %s", sig);
    }

    bool builtin = is_builtin_name(name);

    for(size_t i = 0; i < argc; ++i) {
        Node* arg = args->items[i];
        Typecheck atype = check_expr(ctx, arg);

        if(count_ok && !builtin && i < signature->param_count && !type_is_error(atype)) {
            char where[160];
            snprintf(where, sizeof where, "no argumento %zu de '%.*s'", i + 1, VIEW_ARG(name));
            report_assignable(ctx, atype, signature->param_types[i], arg, arg, where);
        }
    }

    return signature->call_type;
}

static Typecheck check_field_access(CheckContext *ctx, Node *node) {
    Node* base = node->ast.field_access.base;
    Typecheck type = check_expr(ctx, base);
    bool arrow = node->ast.field_access.arrow;

    if(type_is_error(type)) return type_error();

    if(type._array) {
        SEMA_ERR(node, "Indexe o array antes de acessar um campo");
        return type_error();
    }

    if(arrow && type.ptr_lvl != 1) {
        SEMA_ERR(node, "O operador '->' exige um ponteiro simples para data/coperate");
        return type_error();
    }

    if(!arrow && type.ptr_lvl > 0) {
        SEMA_ERR(node, "O operador '.' so pode ser usado quando nao houver ponteiro");
        return type_error();
    }

    Typecheck base_t = type;
    base_t.ptr_lvl = 0;
    base_t = core_type(base_t);
    AggregateDef* def = (base_t.cls == TC_AGGREGATE && base_t.ptr_lvl == 0) ? base_t.inline_define : NULL;
    if(!def) {
        SEMA_ERR(node, "O tipo '%s' nao e data/coperate, nao possui campos", tname(type));
        return type_error();
    }


    FieldInfo* field = find_field(def, node->ast.field_access.name);
    if(!field) {
        SEMA_ERR(node, "Campo '%.*s' nao existe no tipo", VIEW_ARG(node->ast.field_access.name));
        return type_error();
    }

    node->ast.field_access.offset = field->offset;
    node->ast.field_access.base_type = type;
    return field->type;
}

static Typecheck check_expr(CheckContext *ctx, Node *node) {
    if(!node) return type_error();

    Typecheck type = { 0 };

    switch (node->kind)
    {
    case NODE_LITERAL: type = check_literal(node); break;
    case NODE_VAR_CALL: type = check_var_access(ctx, node); break;
    case NODE_BINARY_OP: type = check_binary_op(ctx, node); break;
    case NODE_UNARY_OP: type = check_unary_op(ctx, node); break;
    case NODE_POSTFIX_OP: type = check_unary_op(ctx, node); break;
    case NODE_FUNC_CALL: type = check_func_call(ctx, node); break;
    case NODE_ENUM_CALL: type = check_enum_access(ctx, node); break;
    case NODE_FIELD_CALL: type = check_field_access(ctx, node); break;
    case NODE_ARRAY: type = check_array(ctx, node); break;
    case NODE_CAST: type = check_cast(ctx, node); break;
    case NODE_TERNARY: type = check_ternary(ctx, node); break;
    case NODE_BYTES: type = check_bytes(ctx, node); break;
    case NODE_INIT_LIST:
        SEMA_ERR(node, "Lista de inicializacao so e permitida como inicializador");
        return type_error();
    default:
        SEMA_ERR(node, "Expressao nao suportada pelo typechecker");
        return type_error();
    }

    node->resolved_type = type;
    return type;
}

size_t unescaped_length(View raw) {
    size_t n = 0;

    for(size_t i = 0; i < raw.len; ++i) {
        ++n;
        if(raw.start[i] != '\\') continue;

        size_t k = i + 1;
        if(k >= raw.len) break;

        if(raw.start[k] == 'x') {   /* \xHH: ate 2 digitos (mesma regra do irgen) */
            int digits = 0;
            while(k + 1 < raw.len && digits < 2 && isxdigit((unsigned char)raw.start[k + 1])) {
                ++k;
                ++digits;
            }
        }
        i = k;
    }

    return n;
}

static bool is_char_array(Typecheck type) {
    return type._array && type.ptr_lvl == 0 && type.base == KCHAR;
}

static bool is_string_literal(Node *n) {
    return n && n->kind == NODE_LITERAL && n->ast.literal.kind == STRING;
}

static size_t array_len(Typecheck t) {
    return t.array_rank > 0 ? (size_t)t.array_dims[0] : (size_t)t.array_size;
}

static void init_set_open_length(Typecheck *t, size_t n) {
    size_t inner = 1;
    for(size_t i = 1; i < t->array_rank; ++i) inner *= (size_t)t->array_dims[i];
    t->array_dims[0] = n;
    t->array_size = n * inner;
}

static const Typecheck* init_stable(CheckContext *ctx, const Typecheck *t) {
    Typecheck* copy = (Typecheck*)arena_alloc(ctx->arena, sizeof(Typecheck));
    *copy = *t;
    return copy;
}

static void init_push(CheckContext *ctx, InitBuilder *b, InitItem item) {
    if(b->count == b->capacity) {
        size_t cap = b->capacity ? b->capacity * 2 : 16;
        InitItem* items = (InitItem*)arena_alloc(ctx->arena, cap * sizeof(InitItem));
        if(b->count) memcpy(items, b->items, b->count * sizeof(InitItem));
        b->items = items;
        b->capacity = cap;
    }

    if(b->count && item.offset < b->items[b->count - 1].offset) b->unsorted = true;

    b->items[b->count++] = item;
    b->payload += item.size;
    if(item.offset + item.size > b->high) b->high = item.offset + item.size;
}

/* Remove os itens que intersectam [off, off+size): o ultimo inicializador vence. */
static bool init_clear_range(InitBuilder *b, size_t off, size_t size) {
    if(b->count == 0 || size == 0 || off >= b->high) return false;

    size_t end = off + size, w = 0;
    bool removed = false;

    for(size_t r = 0; r < b->count; ++r) {
        InitItem* it = &b->items[r];
        if(it->offset < end && it->offset + it->size > off) {
            b->payload -= it->size;
            removed = true;
            continue;
        }
        if(w != r) b->items[w] = *it;
        ++w;
    }

    b->count = w;
    return removed;
}

static int init_item_cmp(const void *a, const void *b) {
    size_t x = ((const InitItem*)a)->offset, y = ((const InitItem*)b)->offset;
    return (x > y) - (x < y);
}

/* Bytes que uma lista precisa cobrir para inicializar todos os campos (sem padding). */
static size_t init_payload(Typecheck t) {
    if(t._array) return array_len(t) * init_payload(array_elem(t));

    Typecheck core = core_type(t);
    if(isaggregate(core) && core.inline_define) {
        AggregateDef* d = core.inline_define;
        if(d->kind == NODE_COPERATE) return d->size;   /* conservador: so e "denso" se o membro cobrir tudo */

        size_t sum = 0, usable = d->count - (d->flexible ? 1 : 0);
        for(size_t i = 0; i < usable; ++i) sum += init_payload(d->fields[i].type);
        return sum;
    }

    return type_size(t);
}

static Node* init_wrap_list(CheckContext *ctx, Node *element) {
    Node* list = (Node*)arena_alloc(ctx->arena, sizeof(Node));
    memset(list, 0, sizeof(Node));
    list->kind = NODE_INIT_LIST;
    list->filepath = element->filepath;
    list->line = element->line;
    list->col = element->col;

    NodeList* el = (NodeList*)arena_alloc(ctx->arena, sizeof(NodeList) + sizeof(Node*));
    el->items = (Node**)(el + 1);
    el->capacity = 1;
    el->count = 1;
    el->items[0] = element;

    list->ast.init_list.elements = el;
    return list;
}

static void init_fill(CheckContext *ctx, InitBuilder *b, Node *list, Typecheck *type, size_t base);

static void init_string_item(CheckContext *ctx, InitBuilder *b, Node *lit, Typecheck *type, size_t off) {
    size_t len = unescaped_length(lit->ast.literal.string);
    size_t cap = array_len(*type);

    if(cap == 0) {
        cap = len + 1;
        init_set_open_length(type, cap);
    } else if(len > cap) {
        SEMA_ERR(lit, "A string possui %zu bytes, mas o array comporta %zu", len, cap);
        return;
    }

    if(init_clear_range(b, off, cap)) SEMA_WARN(lit, "Esta string sobrescreve valores ja inicializados");

    InitItem item = { off, cap, NULL, lit, 1 };
    init_push(ctx, b, item);
}

static void init_store(CheckContext *ctx, InitBuilder *b, Node *value, const Typecheck *slot, size_t off) {
    if(type_is_error(*slot)) return;

    if(value->kind == NODE_INIT_LIST) {
        if(init_clear_range(b, off, type_size(*slot)))
            SEMA_WARN(value, "Esta lista sobrescreve valores ja inicializados");

        Typecheck t = *slot;
        init_fill(ctx, b, value, &t, off);
        return;
    }

    Typecheck vt = check_expr(ctx, value);
    if(type_is_error(vt)) return;

    if(slot->_array) {
        if(slot->array_rank == 1 && is_char_array(*slot) && is_string_literal(value)) {
            Typecheck t = *slot;
            init_string_item(ctx, b, value, &t, off);
        } else {
            SEMA_ERR(value, "Um array precisa ser inicializado com uma lista { } (ou string literal, em char[])");
        }
        return;
    }

    if(isaggregate(core_type(*slot))) {
        SEMA_ERR(value, "O tipo '%s' precisa ser inicializado com uma lista { }", tname(*slot));
        return;
    }

    report_assignable(ctx, vt, *slot, value, value, "na inicializacao");

    size_t size = type_size(*slot);
    if(init_clear_range(b, off, size)) SEMA_WARN(value, "Este valor sobrescreve um valor ja inicializado");

    InitItem item = { off, size, slot, value, 0 };
    init_push(ctx, b, item);
}

/* Resolve ([expr] | .campo)+ a partir de *type. Devolve o tipo/offset do subobjeto designado e,
   em *first_pos, a posicao do primeiro nivel (para o cursor posicional da lista). */
static bool init_designate(CheckContext *ctx, Node *chain, Typecheck *type, size_t *off,
                           size_t *first_pos, bool open_first) {
    Typecheck cur = *type;
    bool first = true;

    for(Node* d = chain; d != NULL; d = d->ast.designator.rest) {
        if(d->ast.designator.is_index) {
            if(!cur._array) {
                SEMA_ERR(d, "O designador '[...]' so se aplica a arrays, mas o tipo e '%s'", tname(cur));
                return false;
            }

            Node* ie = d->ast.designator.index;
            Typecheck it = check_expr(ctx, ie);
            if(type_is_error(it)) return false;

            Typecheck ic = core_type(it);
            ConstInt c;
            if((!isinteger(ic) && !isenum(ic)) || !const_eval(ie, &c) || c.big) {
                SEMA_ERR(ie, "O indice do designador precisa ser uma constante inteira");
                return false;
            }
            if(c.value < 0) {
                SEMA_ERR(ie, "O indice do designador nao pode ser negativo");
                return false;
            }

            size_t n = array_len(cur);
            if(!(first && open_first) && (size_t)c.value >= n) {
                SEMA_ERR(ie, "Indice %lld fora do array (tamanho %zu)", c.value, n);
                return false;
            }

            Typecheck el = array_elem(cur);
            *off += (size_t)c.value * type_size(el);
            if(first) *first_pos = (size_t)c.value;
            cur = el;
        } else {
            Typecheck core = core_type(cur);
            if(cur._array || !isaggregate(core) || !core.inline_define) {
                SEMA_ERR(d, "O designador '.campo' so se aplica a data/coperate, mas o tipo e '%s'", tname(cur));
                return false;
            }

            AggregateDef* def = core.inline_define;
            FieldInfo* f = find_field(def, d->ast.designator.field);
            if(!f) {
                SEMA_ERR(d, "Campo '%.*s' nao existe no tipo '%s'", VIEW_ARG(d->ast.designator.field), tname(cur));
                return false;
            }

            size_t idx = (size_t)(f - def->fields);
            if(def->flexible && idx == def->count - 1) {
                SEMA_ERR(d, "O membro flexivel '%.*s' nao pode ser inicializado", VIEW_ARG(d->ast.designator.field));
                return false;
            }

            *off += f->offset;
            if(first) *first_pos = idx;
            cur = f->type;
        }

        first = false;
    }

    *type = cur;
    return true;
}

static void init_fill(CheckContext *ctx, InitBuilder *b, Node *list, Typecheck *type, size_t base) {
    if(list->ast.init_list._zero) {
        list->resolved_type = *type;
        return;
    }

    NodeList* elems = list->ast.init_list.elements;
    size_t count = elems ? elems->count : 0;

    bool is_array = type->_array;
    Typecheck core = is_array ? *type : core_type(*type);
    AggregateDef* def = NULL;
    const Typecheck* elem = NULL;
    size_t elem_size = 0, limit = 0;
    bool open = false;

    if(is_array) {
        Typecheck e = array_elem(*type);
        elem = init_stable(ctx, &e);
        elem_size = type_size(e);
        limit = array_len(*type);
        open = (limit == 0);

        if(elem_size == 0) {
            SEMA_ERR(list, "O tipo dos elementos do array e incompleto");
            return;
        }
    } else if(isaggregate(core) && core.inline_define) {
        def = core.inline_define;
        limit = def->count - (def->flexible ? 1 : 0);

        if(def->kind == NODE_COPERATE && count > 1) {
            SEMA_ERR(list, "coperate aceita apenas um valor no inicializador");
            count = 1;
        }
    } else {
        SEMA_ERR(list, "Lista de inicializacao so pode inicializar arrays, data ou coperate");
        return;
    }

    /* char s[N] = { "texto" } equivale a char s[N] = "texto" */
    if(is_array && count == 1 && type->array_rank == 1 && is_char_array(*type) &&
       is_string_literal(elems->items[0])) {
        check_expr(ctx, elems->items[0]);
        init_string_item(ctx, b, elems->items[0], type, base);
        list->resolved_type = *type;
        return;
    }

    size_t cursor = 0, high_water = 0;
    bool overflow = false;

    for(size_t i = 0; i < count; ++i) {
        Node* el = elems->items[i];
        Node* value = el;
        const Typecheck* slot;
        size_t off;

        if(el->kind == NODE_DESIGNATOR) {
            Typecheck cur = *type;
            size_t pos = cursor;
            off = base;

            if(!init_designate(ctx, el, &cur, &off, &pos, open)) continue;

            cursor = pos + 1;
            value = el->ast.designator.value;
            slot = init_stable(ctx, &cur);
        } else {
            if(!open && cursor >= limit) {
                if(!overflow) {
                    if(is_array) SEMA_ERR(el, "O inicializador excede o tamanho do array (%zu elementos)", limit);
                    else SEMA_ERR(el, "O inicializador possui elementos demais: o tipo aceita %zu campos", limit);
                    overflow = true;
                }
                continue;
            }

            if(is_array) {
                slot = elem;
                off = base + cursor * elem_size;
            } else {
                const FieldInfo* f = &def->fields[cursor];
                slot = &f->type;
                off = base + f->offset;
            }
            ++cursor;
        }

        if(open && cursor > high_water) high_water = cursor;
        init_store(ctx, b, value, slot, off);
    }

    if(open) {
        if(high_water == 0) SEMA_ERR(list, "Array sem tamanho precisa de ao menos um elemento no inicializador");
        else init_set_open_length(type, high_water);
    }

    list->resolved_type = *type;
}

static void check_init_list(CheckContext *ctx, Node *list, Typecheck *target) {
    list->ast.init_list.items = NULL;
    list->ast.init_list.item_count = 0;
    list->ast.init_list.dense = false;

    if(list->ast.init_list._zero) {
        if(target->_array && !target->_vla && array_len(*target) == 0) init_set_open_length(target, 1);
        list->resolved_type = *target;
        return;
    }

    if(target->_vla) {
        SEMA_ERR(list, "Array de tamanho dinamico so aceita o inicializador {0}");
        list->resolved_type = *target;
        return;
    }

    InitBuilder b = { 0 };
    init_fill(ctx, &b, list, target, 0);

    if(b.unsorted && b.count > 1) qsort(b.items, b.count, sizeof(InitItem), init_item_cmp);

    list->ast.init_list.items = b.items;
    list->ast.init_list.item_count = b.count;
    list->ast.init_list.dense = (b.payload == init_payload(*target));
    list->resolved_type = *target;
}

static bool is_function_name(CheckContext *ctx, View name) {
    Typecheck t;
    if(resolve_variable(ctx->current_scope, name, &t)) return false;

    FuncEntry* fe = function_table_lookup(name);
    return fe != NULL && !fe->_builtin;
}

static void check_while_loop(CheckContext *ctx, Node *node) {
    check_condition(ctx, node->ast.while_loop.condition);

    bool prev_in_loop = ctx->in_loop;
    ctx->in_loop = true;
    check_statement(ctx, node->ast.while_loop.body);
    ctx->in_loop = prev_in_loop;
}

static void check_do_while_loop(CheckContext *ctx, Node *node) {
    bool prev_in_loop = ctx->in_loop;
    ctx->in_loop = true;
    check_statement(ctx, node->ast.while_loop.body);
    ctx->in_loop = prev_in_loop;

    check_condition(ctx, node->ast.while_loop.condition);
}

static void check_for_loop(CheckContext *ctx, Node *node) {
    ctx_scope_push(ctx);

    Node* init = node->ast.for_loop.init;
    if(init) {
        if(init->kind == NODE_VAR_DECL) check_var_decl(ctx, init);
        else check_expr(ctx, init);
    }

    check_condition(ctx, node->ast.for_loop.condition);
    if(node->ast.for_loop.increment) check_expr(ctx, node->ast.for_loop.increment);

    bool prev_in_loop = ctx->in_loop;
    ctx->in_loop = true;
    check_statement(ctx, node->ast.for_loop.body);
    ctx->in_loop = prev_in_loop;

    ctx_scope_pop(ctx);
}

static void check_break_stmt(CheckContext *ctx, Node *node) {
    if(!ctx->in_loop) SEMA_ERR(node, "O 'break' pode ser usado somente em loops");
}

static void check_continue_stmt(CheckContext *ctx, Node *node) {
    if(!ctx->in_loop) SEMA_ERR(node, "O 'continue' pode ser usado somente em loops");
}

static void check_jump_label(CheckContext *ctx, Node *node) {
    if(!ctx->in_function) { 
        SEMA_ERR(node, "'jump' fora do escopo de uma funcao"); 
        return; 
    }

    label_reference(node->ast.jmp_label.name, node);
}

static void check_load_label(CheckContext *ctx, Node *node) {
    if(!ctx->in_function) {
        SEMA_ERR(node, "'jump' fora do escopo de uma funcao"); 
        return;
    }

    View name = node->ast.jmp_label.name;

    if(!label_declare(name, node))
        SEMA_ERR(node, "label '%.*s' ja foi definido no arquivo", VIEW_ARG(name));
}

static void check_var_decl(CheckContext *ctx, Node *node) {
    View name = node->ast.var_decl.name;
    TypeSpec* spec = &node->ast.var_decl.call_type;
    Node* init = node->ast.var_decl.init;

    Typecheck declared = resolve_type_spec(ctx, *spec, node);

    bool omitted_first = spec->is_array &&
        (!spec->array_dim || spec->array_dim->count == 0 || dimention_isommited(spec->array_dim->items[0]));

    if(!type_is_error(declared)) {
        if(declared.base == KVOID && declared.ptr_lvl == 0) {
            SEMA_ERR(node, "A variavel '%.*s' nao pode ser do tipo void", VIEW_ARG(name));
        }

        if(is_incomplete_aggregate(declared) && !node->ast.var_decl._extern)
            SEMA_ERR(node, "A variavel '%.*s' tem tipo incompleto '%s'", VIEW_ARG(name), tname(declared));

        if(declared._vla) {
            if(node->ast.var_decl._static)
                SEMA_ERR(node, "Array de tamanho dinamico nao pode ser static");
            if(!ctx->in_function)
                SEMA_ERR(node, "Array de tamanho dinamico so pode existir dentro de funcoes");
        }

        if(omitted_first && spec->array_dim && spec->array_dim->count > 1)
            SEMA_ERR(node, "Omitir a primeira dimensao so e suportado em arrays de 1 dimensao");
    }

    if(init) {
        /* char s[N] = "texto"  ->  char s[N] = { "texto" } (um unico caminho ate o irgen) */
        if(init->kind != NODE_INIT_LIST && declared.array_rank == 1 && is_char_array(declared) &&
           is_string_literal(init)) {
            init = init_wrap_list(ctx, init);
            node->ast.var_decl.init = init;
        }

        if(init->kind == NODE_INIT_LIST) {
            if(!type_is_error(declared)) check_init_list(ctx, init, &declared);
        }
        else {
            Typecheck current = check_expr(ctx, init);
            if(!type_is_error(declared) && !type_is_error(current))
                report_assignable(ctx, current, declared, init, node, "na inicializacao");
        }

        if((!ctx->in_function || node->ast.var_decl._static) && !type_is_error(declared)) {
            Node* bad = first_non_constant(ctx, init);
            if(bad) report_non_constant(ctx, bad, ctx->in_function ? "static" : "global");
        }
    }
    else if(omitted_first && !node->ast.var_decl._extern && !type_is_error(declared)) {
        SEMA_ERR(node, "Array '%.*s' sem tamanho exige um inicializador", VIEW_ARG(name));
    }

    if(node->ast.var_decl._atomic && !type_is_error(declared)) {
        bool valid = !declared._array && (isinteger(declared) || isptr(declared) || islink(declared));

        if(!valid || type_size(declared) > 8) {
            SEMA_ERR(node, "Qualificador atomic so e valido em inteiros ou ponteiros de ate 8 bytes");
        } else {
            declared._atomic = true;
        }
    }

    node->resolved_type = declared;

    /* valor constante conhecido (const + inicializador constante): vira literal nos usos */
    ConstValue cv = { 0 };
    if(node->ast.var_decl._const && !node->ast.var_decl._extern && init)
        const_value_of_init(declared, init, &cv);

    if(!ctx->in_function) {
        if(enum_const_lookup(name)) {
            SEMA_ERR(node, "'%.*s' ja foi declarado como enumerador", VIEW_ARG(name));
            return;
        }

        GlobalDeclStatus status;
        GlobalEntry* global = global_var_decl(name, declared, node, node->ast.var_decl._static,
                                              node->ast.var_decl._extern, init != NULL, &status);

        if(status == GLOBAL_REDEFINED) {
            SEMA_ERRC(node, "T030", "Redefinicao da variavel global '%.*s'", VIEW_ARG(name));
            if(global->decl_node) SEMA_NOTE(global->decl_node, "primeira definicao aqui");
        } else if(status == GLOBAL_TYPE_MISMATCH) {
            SEMA_ERRC(node, "T031", "A variavel global '%.*s' foi declarada com outro tipo: aqui '%s', antes '%s'",
                      VIEW_ARG(name), tname(declared), tname(global->type));
            if(global->decl_node) SEMA_NOTE(global->decl_node, "declaracao anterior aqui");
        } else {
            global->is_const = node->ast.var_decl._const;
            if(cv.known) global->cval = cv;
        }
        return;
    }

    SymbolEntry* local = ctx_scope_declare(ctx, name, declared);
    if(!local) {
        SEMA_ERR(node, "A variavel '%.*s' ja foi declarada nesse escopo", VIEW_ARG(name));
        return;
    }

    if(cv.known) local->cval = cv;
}

static void check_if_stmt(CheckContext *ctx, Node *node) {
    check_condition(ctx, node->ast.if_stmt.condition);

    if(node->ast.if_stmt.then->kind != NODE_BLOCK_STMT) ctx_scope_push(ctx);

    check_statement(ctx, node->ast.if_stmt.then);

    if(node->ast.if_stmt.then->kind != NODE_BLOCK_STMT) ctx_scope_pop(ctx);

    if(node->ast.if_stmt.otherwise) check_statement(ctx, node->ast.if_stmt.otherwise);
}

static void check_block_stmt(CheckContext *ctx, Node *node) {
    ctx_scope_push(ctx);

    NodeList* statements = node->ast.block.statements;
    if(statements) {
        for(size_t i = 0; i < statements->count; ++i)
            check_statement(ctx, statements->items[i]);
    }

    ctx_scope_pop(ctx);
}

static bool always_returns(Node *n) {
    if(!n) return false;
    switch (n->kind)
    {
    case NODE_CALL_STMT: case NODE_JUMP_LABEL: return true;
    case NODE_BLOCK_STMT: {
        NodeList* s = n->ast.block.statements;
        for(size_t i = 0; s && i < s->count; ++i) if(always_returns(s->items[i])) return true;
        return false;
    }
    case NODE_IF_STMT:
        return n->ast.if_stmt.otherwise && always_returns(n->ast.if_stmt.then)
            && always_returns(n->ast.if_stmt.otherwise);
    case NODE_WHILE_LOOP: case NODE_DO_WHILE_LOOP: {
        ConstInt c;
        Node* cond = n->ast.while_loop.condition;
        return cond && const_eval(cond, &c) && c.value != 0;
    }
    case NODE_FOR_LOOP: return n->ast.for_loop.condition == NULL;
    default: return false;
    }
}

static void check_func_decl(CheckContext *ctx, Node *node) {
    if(ctx->in_function) {
        SEMA_ERR(node, "Funcoes aninhadas nao sao suportadas pela linguagem");
        return;
    }

    if(is_builtin_name(node->ast.func_decl.name)) {
        SEMA_ERR(node, "'%.*s' e um nome reservado do compilador", VIEW_ARG(node->ast.func_decl.name));
        return;
    }

    Typecheck call_type = resolve_type_spec(ctx, node->ast.func_decl.call_type, node);

    if(!type_is_error(call_type) && call_type._array)
        SEMA_ERR(node, "Funcoes nao podem retornar arrays");

    View name = node->ast.func_decl.name;
    bool has_body = (node->ast.func_decl.body != NULL);
    bool is_extern = node->ast.func_decl._extern;
    bool is_inline = node->ast.func_decl.inline__;

    if(is_inline && is_extern) {
        SEMA_ERRC(node, "T015", "A funcao '%.*s' nao pode ser 'inline' e 'extern' ao mesmo tempo",
                  VIEW_ARG(name));
        is_inline = false;
    }

    NodeList* params = node->ast.func_decl.params;
    size_t param_count = params ? params->count : 0;
    Typecheck* ptype = (Typecheck*)arena_alloc(ctx->arena, sizeof(Typecheck) * (param_count ? param_count : 1));

    for(size_t i = 0; i < param_count; ++i) {
        Node* param = params->items[i];

        TypeSpec pspec = param->ast.var_decl.call_type;
        if(pspec.is_array) {
            pspec.is_array = 0;
            pspec.array_dim = NULL;
            pspec.ptr_lvl++;
        }

        ptype[i] = resolve_type_spec(ctx, pspec, param);

        if(!type_is_error(ptype[i]) && ptype[i].base == KVOID && ptype[i].ptr_lvl == 0) {
            SEMA_ERR(param, "O parametro '%.*s' nao pode ser do tipo void", VIEW_ARG(param->ast.var_decl.name));
        }

        param->resolved_type = ptype[i];
    }

    FunctionSignature signature = { 0 };
    signature.call_type = call_type;
    signature.param_types = ptype;
    signature.param_count = param_count;
    signature.variadic = node->ast.func_decl._variadic;

    FuncEntry* exist = function_table_lookup(name);

    if(exist && node->ast.func_decl._static && !exist->_static &&
       !same_file(exist->file, node->filepath))
        exist = NULL;

    if(exist) {
        bool exist_is_extern = exist->_extern && !exist->has_body;

        if(exist_is_extern) {
            /* a entrada anterior e externa: nada a conferir, a nova declaracao vence */
            exist->signature = signature;
            exist->decl_node = node;
            if(!has_body) return;

            exist->_extern = false;
            exist->inline__ = is_inline;
            exist->has_body = true;
            exist->_pending = false;
        }
        else if(is_extern && !has_body) {
            /* extern sobre algo que o usuario ja declarou/definiu: ignora */
            return;
        }
        else {
            if(!signature_equal(&exist->signature, &signature)) {
                char a[768], b[768];
                SEMA_ERRC(node, "T012", "Definicao de '%.*s' nao corresponde a declaracao anterior", VIEW_ARG(name));
                SEMA_NOTE(node, "aqui:    %s", format_signature(a, sizeof a, name, &signature));
                SEMA_NOTE(exist->decl_node ? exist->decl_node : node, "antes:   %s",
                          format_signature(b, sizeof b, name, &exist->signature));
                return;
            }

            if(exist->_static != node->ast.func_decl._static) {
                SEMA_ERRC(node, "T013", "Linkage conflitante para '%.*s' (static vs nao-static)", VIEW_ARG(name));
                return;
            }

            if(!has_body) {
                exist->inline__ = exist->inline__ || is_inline;
                return;
            }

            if(exist->has_body) {
                SEMA_ERRC(node, "T014", "Redefinicao da funcao '%.*s'", VIEW_ARG(name));
                if(exist->decl_node) SEMA_NOTE(exist->decl_node, "primeira definicao aqui");
                return;
            }

            exist->decl_node = node;
            exist->inline__ = exist->inline__ || is_inline;
            exist->has_body = true;
            exist->_pending = false;
        }
    } else {
        FuncEntry* entry = function_table_insert(name);
        entry->signature = signature;
        entry->_static = node->ast.func_decl._static;
        entry->_extern = is_extern;
        entry->inline__ = is_inline;
        entry->_pending = !has_body && !is_extern;
        entry->has_body = has_body;
        entry->decl_node = node;
    }

    if(!has_body) return;

    ctx_scope_push(ctx);

    for(size_t i = 0; i < param_count; ++i) {
        View pname = params->items[i]->ast.var_decl.name;
        if(pname.len == 0) continue;

        if(!ctx_scope_declare(ctx, pname, ptype[i]))
            SEMA_ERR(params->items[i], "O parametro '%.*s' esta duplicado", VIEW_ARG(pname));
    }

    ctx->current_call_type = call_type;
    ctx->in_function = true;
    ctx->in_loop = false;
    ctx->in_variadic = signature.variadic;

    check_statement(ctx, node->ast.func_decl.body);

    if(!isvoid(call_type) && !type_is_error(call_type) && !always_returns(node->ast.func_decl.body))
        SEMA_WARN(node, "A funcao '%.*s' pode terminar sem 'call' de um valor em algum caminho", VIEW_ARG(name));

    ctx->in_function = false;
    ctx->in_variadic = false;

    if(svcmp(name, ENTRY_START)) {
        if(ctx->entry_function != NULL) SEMA_ERR(node, "Multiplas definicoes do entry point 'start'");
        else ctx->entry_function = node;
    }

    ctx_scope_pop(ctx);
}

static void check_call_stmt(CheckContext *ctx, Node *node) {
    if(!ctx->in_function) {
        SEMA_ERR(node, "'call' fora do escopo de uma funcao");
        return;
    }

    bool call_void = isvoid(ctx->current_call_type);
    Node* value = node->ast.call_statement.result;

    if(value) {
        Typecheck type = check_expr(ctx, value);

        if(call_void) {
            SEMA_ERR(node, "Funcoes com retorno 'void' nao podem retornar um valor");
            return;
        }

        if(!type_is_error(type)) report_assignable(ctx, type, ctx->current_call_type, value, node, "no retorno ('call')");
    } else if(!call_void) {
        SEMA_ERR(node, "A funcao esperava um valor de retorno");
    }
}

static void check_expression_statement(CheckContext *ctx, Node *node) {
    if(!ctx->in_function) {
        SEMA_ERR(node, "Expressoes so podem aparecer dentro de funcoes");
        return;
    }
    
    check_expr(ctx, node);

    if(!has_side_effect(node)) SEMA_WARN(node, "Expressao sem efeito: o resultado e descartado");
}

static void check_statement(CheckContext *ctx, Node *node) {
    if(!node) return;

    switch (node->kind)
    {
    case NODE_VAR_DECL: check_var_decl(ctx, node); break;
    case NODE_IF_STMT: check_if_stmt(ctx, node); break;
    case NODE_BLOCK_STMT: check_block_stmt(ctx, node); break;
    case NODE_FUNC_DECL: check_func_decl(ctx, node); break;
    case NODE_BREAK_STMT: check_break_stmt(ctx, node); break;
    case NODE_CONTINUE_STMT: check_continue_stmt(ctx, node); break;
    case NODE_FOR_LOOP: check_for_loop(ctx, node); break;
    case NODE_WHILE_LOOP: check_while_loop(ctx, node); break;
    case NODE_DO_WHILE_LOOP: check_do_while_loop(ctx, node); break;
    case NODE_DATA:
    case NODE_COPERATE: check_aggregate_decl(ctx, node); break;
    case NODE_ENUM: check_enum_decl(ctx, node); break;
    case NODE_NEWTYPE: check_newtype_decl(ctx, node); break;
    case NODE_CALL_STMT: check_call_stmt(ctx, node); break;
    case NODE_JUMP_LABEL: check_jump_label(ctx, node); break;
    case NODE_LOAD_LABEL: check_load_label(ctx, node); break;

    case NODE_FUNC_CALL:
    case NODE_UNARY_OP:
    case NODE_BINARY_OP:
    case NODE_POSTFIX_OP:
    case NODE_BYTES:
    case NODE_VAR_CALL:
    case NODE_LITERAL:
    case NODE_ENUM_CALL:
    case NODE_FIELD_CALL:
    case NODE_ARRAY:
    case NODE_CAST:
    case NODE_TERNARY:
        check_expression_statement(ctx, node);
        break;

    case NODE_UNDEFINED:
        SEMA_ERR(node, "A expressao nao faz parte do sistema");
        break;

    default:
        break;
    }
}

static void register_builtin(CheckContext *ctx, View name, size_t param_count, Typecheck ret) {
    if(function_table_lookup(name)) return;

    Typecheck* params = (Typecheck*)arena_alloc(ctx->arena, sizeof(Typecheck) * (param_count ? param_count : 1));

    for(size_t i = 0; i < param_count; ++i) params[i] = make_type(KINT64, 0);

    FuncEntry* entry = function_table_insert(name);
    if(!entry) return;

    FunctionSignature signature = { 0 };
    signature.call_type = ret;
    signature.param_types = params;
    signature.param_count = param_count;
    signature.variadic = false;

    entry->signature = signature;
    entry->_static = false;
    entry->_extern = false;
    entry->_pending = false;
    entry->has_body = false;
    entry->decl_node = NULL;
    entry->_builtin = true;
}

static void register_builtins(CheckContext *ctx) {
    register_builtin(ctx, BUILTIN_SYSCALL, 7, make_type(KINT64, 0));
    register_builtin(ctx, BUILTIN_ATOMIC, 4, make_type(KINT64, 0));
    register_builtin(ctx, BUILTIN_VA_START, 0, make_type(KINT64, 1));
    register_builtin(ctx, BUILTIN_EXPECT, 0, make_type(KUINT8, 0));
    register_builtin(ctx, BUILTIN_OFFSETOF, 0, make_type(KUINT64, 0));

    for (int i = 0; i < INTRINSIC_COUNT; ++i) {
        View n = { INTRINSICS[i].name, strlen(INTRINSICS[i].name) };
        register_builtin(ctx, n, (size_t)INTRINSICS[i].argc, make_type(INTRINSICS[i].ret, 0));
    }
}

static void report_undefined_label(LabelEntry *entry, void *userdata) {
    CheckContext *ctx = (CheckContext*)userdata;

    if(!entry->_defined && entry->first_jump) {
        Node* node = entry->first_jump;
        SEMA_ERR(node, "label '%.*s' usado em 'jump' mas nunca definido com 'load'", VIEW_ARG(entry->name));
    }
}

static void check_unresolved_labels(CheckContext *ctx) {
    label_table_foreach(report_undefined_label, ctx);
}

void check_program(CheckContext *ctx, Node *program) {
    typecheck_set_file(program->filepath);
    ctx->current_call_type = make_type(KVOID, 0);
    label_table_reset();
    register_builtins(ctx);

    NodeList* statements = program->ast.program.statements;

    if(statements) {
        for(size_t i = 0; i < statements->count; ++i)
            check_statement(ctx, statements->items[i]);
    }

    check_unresolved_labels(ctx);
}