#include "../include/fold.h"
#include "../include/typecheck.h"

static int wrap_width(TokenType base) {
    return base == KHEXA ? 64 : integerwidth(base);
}

static bool wrap_unsigned(TokenType base) {
    return base == KHEXA || isunsignedbase(base);
}

static int fold_width(TokenType base) {
    return base == KHEXA ? 64 : integerwidth(base);
}

static bool fold_is_unsigned(TokenType base) {
    return base == KHEXA || isunsignedbase(base);
}

void const_wrap(ConstInt *c, Typecheck type) {
    int width = wrap_width(type.base);
    if(width == 0) return;

    bool uns = wrap_unsigned(type.base);
    unsigned long long u = (unsigned long long)c->value;

    if(width < 64) {
        unsigned long long mask = (1ULL << width) - 1;
        u &= mask;
        if(!uns && (u >> (width - 1))) u |= ~mask;
    }

    c->value = (long long)u;
    c->big = uns && width == 64 && c->value < 0;
}

static int hex_digit(unsigned char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool decode_char_literal(View v, unsigned long long *out) {
    const unsigned char* s = (const unsigned char*)v.start;
    size_t n = v.len;
    if(!s || n == 0) return false;

    if(n >= 2 && s[0] == 'W' && s[1] == '\'') { s++; n--; }
    if(n >= 2 && s[0] == '\'' && s[n - 1] == '\'') { s++; n -= 2; }
    if(n == 0) return false;

    unsigned long long value = 0;
    size_t i = 0;

    if(s[0] == '\\') {
        if(n < 2) return false;
        i = 2;

        switch (s[1])
        {
        case 'n': value = '\n'; break;
        case 't': value = '\t'; break;
        case 'r': value = '\r'; break;
        case 'a': value = '\a'; break;
        case 'b': value = '\b'; break;
        case 'f': value = '\f'; break;
        case 'v': value = '\v'; break;
        case '\\': value = '\\'; break;
        case '\'': value = '\''; break;
        case '"': value = '"'; break;
        case '?': value = '?'; break;

        case 'x': case 'u': case 'U': {
            size_t max = s[1] == 'x' ? 8 : (s[1] == 'u' ? 4 : 8);
            size_t digits = 0;
            while(i < n && digits < max && hex_digit(s[i]) >= 0) {
                value = value * 16 + (unsigned)hex_digit(s[i]);
                i++; digits++;
            }
            if(digits == 0 || (s[1] != 'x' && digits != max)) return false;
            break;
        }

        case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7': {
            value = (unsigned)(s[1] - '0');
            for(int k = 0; k < 2 && i < n && s[i] >= '0' && s[i] <= '7'; ++k, ++i)
                value = value * 8 + (unsigned)(s[i] - '0');
            break;
        }

        default: return false;
        }
    }
    else if(s[0] < 0x80) {
        value = s[0];
        i = 1;
    }
    else {
        size_t extra;
        if((s[0] & 0xE0) == 0xC0)      { extra = 1; value = s[0] & 0x1F; }
        else if((s[0] & 0xF0) == 0xE0) { extra = 2; value = s[0] & 0x0F; }
        else if((s[0] & 0xF8) == 0xF0) { extra = 3; value = s[0] & 0x07; }
        else return false;

        if(n < 1 + extra) return false;
        for(size_t k = 1; k <= extra; ++k) {
            if((s[k] & 0xC0) != 0x80) return false;
            value = (value << 6) | (s[k] & 0x3F);
        }
        i = 1 + extra;
    }

    if(i != n) return false;
    *out = value;
    return true;
}

bool is_numeric_constant(Node *node) {
    if(!node) return false;

    if(node->kind == NODE_LITERAL) {
        switch (node->ast.literal.kind)
        {
        case INT: case BIG: case UNSIGNED: case HEXA:
        case FLOAT: case DOUBLE: case CHAR: case WIDE_CHAR:
            return true;
        default: return false;
        }
    }

    if(node->kind == NODE_UNARY_OP && (node->ast.unary.op == OPMINUS || node->ast.unary.op == OPPLUS))
        return is_numeric_constant(node->ast.unary.operand);

    return false;
}

typedef enum { FOLD_OK, FOLD_OVERFLOW, FOLD_SKIP } FoldResult;

static bool is_int_literal(Node *n) {
    return n && n->kind == NODE_LITERAL &&
           (n->ast.literal.kind == INT || n->ast.literal.kind == BIG ||
            n->ast.literal.kind == UNSIGNED || n->ast.literal.kind == HEXA);
}

static FoldResult fold_signed(TokenType op, long long a, long long b, int width, long long *out) {
    long long r;

    switch (op)
    {
    case OPPLUS:
        if((b > 0 && a > LLONG_MAX - b) || (b < 0 && a < LLONG_MIN - b)) return FOLD_OVERFLOW;
        r = a + b; break;
    case OPMINUS:
        if((b < 0 && a > LLONG_MAX + b) || (b > 0 && a < LLONG_MIN + b)) return FOLD_OVERFLOW;
        r = a - b; break;
    case OPSTAR:
        if(a != 0 && b != 0) {
            bool ovf = a > 0 ? (b > 0 ? a > LLONG_MAX / b : b < LLONG_MIN / a)
                             : (b > 0 ? a < LLONG_MIN / b : b < LLONG_MAX / a);
            if(ovf) return FOLD_OVERFLOW;
        }
        r = a * b; break;
    case OPSLASH:
        if(b == 0) return FOLD_SKIP;
        if(a == LLONG_MIN && b == -1) return FOLD_OVERFLOW;
        r = a / b; break;
    case OPMOD:
        if(b == 0) return FOLD_SKIP;
        r = (b == -1) ? 0 : a % b; break;
    case OPSHL:
        if(b < 0 || b >= 64) return FOLD_SKIP;
        r = (long long)((unsigned long long)a << b);
        if((r >> b) != a) return FOLD_OVERFLOW;
        break;
    case OPSHR:
        if(b < 0 || b >= 64) return FOLD_SKIP;
        r = a >> b; break;
    case OPAND: r = a & b; break;
    case OPOR:  r = a | b; break;
    case OPXOR: r = a ^ b; break;
    default: return FOLD_SKIP;
    }

    if(width < 64) {
        long long max = (1LL << (width - 1)) - 1;
        if(r > max || r < -max - 1) return FOLD_OVERFLOW;
    }

    *out = r;
    return FOLD_OK;
}

static FoldResult fold_unsigned(TokenType op, unsigned long long a, unsigned long long b,
                                int width, unsigned long long *out) {
    unsigned long long r;

    switch (op)
    {
    case OPPLUS:  r = a + b; if(r < a) return FOLD_OVERFLOW; break;
    case OPMINUS: if(a < b) return FOLD_OVERFLOW; r = a - b; break;
    case OPSTAR:  if(a && b > ULLONG_MAX / a) return FOLD_OVERFLOW; r = a * b; break;
    case OPSLASH: if(b == 0) return FOLD_SKIP; r = a / b; break;
    case OPMOD:   if(b == 0) return FOLD_SKIP; r = a % b; break;
    case OPSHL:
        if(b >= 64) return FOLD_SKIP;
        r = a << b;
        if((r >> b) != a) return FOLD_OVERFLOW;
        break;
    case OPSHR:   if(b >= 64) return FOLD_SKIP; r = a >> b; break;
    case OPAND:   r = a & b; break;
    case OPOR:    r = a | b; break;
    case OPXOR:   r = a ^ b; break;
    default: return FOLD_SKIP;
    }

    if(width < 64 && r > ((1ULL << width) - 1)) return FOLD_OVERFLOW;

    *out = r;
    return FOLD_OK;
}

static void fold_literal_binary(CheckContext *ctx, Node *node, Typecheck result) {
    Node* l = node->ast.binary.left;
    Node* r = node->ast.binary.right;
    TokenType op = node->ast.binary.op;

    if(!is_int_literal(l) || !is_int_literal(r) || !is_prim_scalar(result)) return;

    switch (op)
    {
    case OPPLUS: case OPMINUS: case OPSTAR: case OPSLASH: case OPMOD:
    case OPAND: case OPOR: case OPXOR: case OPSHL: case OPSHR: break;
    default: return;
    }

    LiteralKind kind;
    switch (result.base)
    {
    case KINT32:  kind = INT; break;
    case KBIG:    kind = BIG; break;
    case KUINT32: kind = UNSIGNED; break;
    case KHEXA:   kind = HEXA; break;
    default: return;
    }

    ConstInt a, b;
    if(!const_eval(l, &a) || !const_eval(r, &b)) return;

    int width = (kind == INT || kind == UNSIGNED) ? 32 : 64;
    bool is_unsigned = (kind == UNSIGNED || kind == HEXA);

    long long sres = 0;
    unsigned long long ures = 0;
    FoldResult fr;

    if(is_unsigned) {
        if((!a.big && a.value < 0) || (!b.big && b.value < 0)) return;
        fr = fold_unsigned(op, (unsigned long long)a.value, (unsigned long long)b.value, width, &ures);
    } else {
        if(a.big || b.big) return;
        fr = fold_signed(op, a.value, b.value, width, &sres);
    }

    if(fr == FOLD_SKIP) return;
    if(fr == FOLD_OVERFLOW) {
        SEMA_ERR(node, "Overflow em expressao constante: o resultado excede o alcance de '%s'", tname(result));
        return;
    }

    node->kind = NODE_LITERAL;
    node->ast.literal.kind = kind;
    if(is_unsigned) node->ast.literal.u64 = ures;
    else node->ast.literal.i64 = sres;
}

bool fold_const_binary(CheckContext *ctx, Node *node, Typecheck result) {
    TokenType op = node->ast.binary.op;

    if(!is_prim_scalar(result) || !isintegerbase(result.base)) return true;

    switch (op)
    {
    case OPPLUS: case OPMINUS: case OPSTAR: case OPSLASH: case OPMOD:
    case OPAND: case OPOR: case OPXOR: case OPSHL: case OPSHR: break;
    default: return true;
    }

    int width = wrap_width(result.base);
    if(width == 0) return true;
    bool is_unsigned = wrap_unsigned(result.base);

    ConstInt a, b;
    if(!const_eval(node->ast.binary.left, &a) || !const_eval(node->ast.binary.right, &b)) return true;

    long long sres = 0;
    unsigned long long ures = 0;
    FoldResult fr;

    if(is_unsigned) {
        if((!a.big && a.value < 0) || (!b.big && b.value < 0)) return true;
        fr = fold_unsigned(op, (unsigned long long)a.value, (unsigned long long)b.value, width, &ures);
    } else {
        if(a.big || b.big) return true;
        fr = fold_signed(op, a.value, b.value, width, &sres);
    }

    if(fr == FOLD_SKIP) return true;

    if(fr == FOLD_OVERFLOW) {
        SEMA_ERR(node, "Overflow em expressao constante: o resultado excede o alcance de '%s' (%d bits)",
                 tname(result), width);
        return false;
    }

    /* so existe literal para int32, big, uint32 e hexa; nos demais tipos
       (8, 16 bits e uint64) o no continua binario e o const_eval o avalia */
    LiteralKind kind;
    switch (result.base)
    {
    case KINT32:  kind = INT; break;
    case KBIG:    kind = BIG; break;
    case KUINT32: kind = UNSIGNED; break;
    case KHEXA:   kind = HEXA; break;
    default: return true;
    }

    node->kind = NODE_LITERAL;
    node->ast.literal.kind = kind;
    if(is_unsigned) node->ast.literal.u64 = ures;
    else node->ast.literal.i64 = sres;
    return true;
}
