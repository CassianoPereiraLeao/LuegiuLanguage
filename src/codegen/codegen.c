#include "../include/codegen.h"
#include <limits.h>
#include <math.h>
#include <ctype.h>

enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 };

static const char* GP64[16] = { "rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi","r8","r9","r10","r11","r12","r13","r14","r15" };
static const char* GP32[16] = { "eax","ecx","edx","ebx","esp","ebp","esi","edi","r8d","r9d","r10d","r11d","r12d","r13d","r14d","r15d" };
static const char* GP16[16] = { "ax","cx","dx","bx","sp","bp","si","di","r8w","r9w","r10w","r11w","r12w","r13w","r14w","r15w" };
static const char* GP8[16]  = { "al","cl","dl","bl","spl","bpl","sil","dil","r8b","r9b","r10b","r11b","r12b","r13b","r14b","r15b" };

static const int ARG_GP[4] = { RCX, RDX, R8, R9 };

static const int POOL_GP_VOL[]  = { R10 };
static const int POOL_GP_CALLEE[] = { RBX, RSI, RDI, R12, R13, R14, R15 };
static const int POOL_XMM_VOL[] = { 4, 5 };
static const int POOL_XMM_CALLEE[] = { 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define EM(...) fprintf(c->out, "    " __VA_ARGS__)

static const char* gp(int r, int size) {
    switch (size) {
    case 1: return GP8[r];
    case 2: return GP16[r];
    case 4: return GP32[r];
    default: return GP64[r];
    }
}

static const char* kw(int size) {
    switch (size) {
    case 1: return "byte";
    case 2: return "word";
    case 4: return "dword";
    case 8: return "qword";
    case 16: return "oword";
    }
    return "";
}

static bool is_fp(IrType t) { return t.is_float && t.ptr_lvl == 0; }
static bool is_sg(IrType t) { return t.is_signed && t.ptr_lvl == 0; }
static int  tsize(IrType t) { if (t.ptr_lvl) return 8; return t.size ? (int)t.size : 8; }
static bool fits32(long long v) { return v >= INT_MIN && v <= INT_MAX; }

static long long canon_imm(long long v, IrType t) {
    bool sg = is_sg(t);
    switch (tsize(t)) {
    case 1: return sg ? (long long)(int8_t)v  : (long long)(uint8_t)v;
    case 2: return sg ? (long long)(int16_t)v : (long long)(uint16_t)v;
    case 4: return sg ? (long long)(int32_t)v : (long long)(uint32_t)v;
    default: return v;
    }
}

typedef enum { O_NONE, O_IMM, O_REG, O_XREG, O_MEM, O_ADDR } OpKind;

typedef struct {
    int base;
    char sym[128];
    long long disp;
} Mem;

typedef struct {
    OpKind k;
    int reg;
    long long imm;
    Mem mem;
} Opnd;

static void mfmt(char* b, size_t n, const Mem* m, int size, long long extra) {
    char k[16] = "";
    if (size) snprintf(k, sizeof k, "%s ", kw(size));
    long long d = m->disp + extra;
    if (m->sym[0]) snprintf(b, n, "%s[rel %s%+lld]", k, m->sym, d);
    else           snprintf(b, n, "%s[%s%+lld]", k, GP64[m->base], d);
}

static bool view_eq(View a, View b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.start, b.start, a.len) == 0);
}

static bool view_is(View a, const char* s) {
    size_t l = strlen(s);
    return a.len == l && memcmp(a.start, s, l) == 0;
}

static void builtin_symbol(char* b, size_t n, View name) {
    snprintf(b, n, "$%.*s", (int)name.len, name.start);
}

static IrFuncInfo* find_func(CodegenContext* c, int id) {
    for (size_t i = 0; i < c->ir->funcs.count; ++i)
        if (c->ir->funcs.items[i] && c->ir->funcs.items[i]->func_id == id)
            return c->ir->funcs.items[i];
    return NULL;
}

static IrGlobal* find_global(CodegenContext* c, int id) {
    for (size_t i = 0; i < c->ir->globals.count; ++i)
        if (c->ir->globals.items[i] && c->ir->globals.items[i]->id == id)
            return c->ir->globals.items[i];
    return NULL;
}

static void func_sym(CodegenContext* c, int id, bool builtin, char* b, size_t n) {
    IrFuncInfo* f = find_func(c, id);
    if (!f) { snprintf(b, n, "$__unknown_func_%d", id); return; }
    if (builtin || f->builtin) builtin_symbol(b, n, f->name);
    else snprintf(b, n, "$%.*s", (int)f->name.len, f->name.start);
}

static bool float_info(CodegenContext* c, int id, double* val, bool* single) {
    for (size_t i = 0; i < c->ir->floats.count; ++i) {
        if (c->ir->floats.items[i].id == id) {
            if (val) *val = c->ir->floats.items[i].value;
            if (single) *single = c->ir->floats.items[i]._single;
            return true;
        }
    }
    return false;
}

static Opnd get_opnd(CodegenContext* c, IrValue v) {
    Opnd o;
    memset(&o, 0, sizeof o);
    o.k = O_NONE;

    switch (v.kind) {
    case IR_VAL_CONST_INT:  o.k = O_IMM; o.imm = v.as.const_i; break;
    case IR_VAL_CONST_UINT: o.k = O_IMM; o.imm = (long long)v.as.const_ui; break;
    case IR_VAL_CONST_FLOAT:
        o.k = O_MEM;
        snprintf(o.mem.sym, sizeof o.mem.sym, "__flt%d", v.as.float_id);
        break;
    case IR_VAL_CONST_STRING:
        o.k = O_ADDR;
        snprintf(o.mem.sym, sizeof o.mem.sym, "__str%d", v.as.string_id);
        break;
    case IR_VAL_SLOT_ADDR:
        o.k = O_ADDR;
        o.mem.base = RBP;
        o.mem.disp = c->slot_off[v.as.slot_id] + v.field_offset;
        break;
    case IR_VAL_GLOBAL:
    case IR_VAL_GLOBAL_ADDR: {
        IrGlobal* g = find_global(c, v.as.global_id);
        o.k = (v.kind == IR_VAL_GLOBAL) ? O_MEM : O_ADDR;
        if (g) snprintf(o.mem.sym, sizeof o.mem.sym, "$%.*s", (int)g->name.len, g->name.start);
        else   snprintf(o.mem.sym, sizeof o.mem.sym, "$__unknown_global_%d", v.as.global_id);
        o.mem.disp = v.field_offset;
        break;
    }
    case IR_VAL_FUNC:
    case IR_VAL_BUILTIN:
        o.k = O_ADDR;
        func_sym(c, v.as.func_id, v.kind == IR_VAL_BUILTIN, o.mem.sym, sizeof o.mem.sym);
        break;
    case IR_VAL_TEMP: {
        CgLoc l = c->temp_loc[v.as.temp_id];
        if (l.kind == CG_LOC_REG) { o.k = l.xmm ? O_XREG : O_REG; o.reg = l.reg; }
        else { o.k = O_MEM; o.mem.base = RBP; o.mem.disp = l.off; }
        break;
    }
    case IR_VAL_SLOT: {
        CgLoc l = c->slot_loc[v.as.slot_id];
        if (l.kind == CG_LOC_REG) { o.k = l.xmm ? O_XREG : O_REG; o.reg = l.reg; }
        else { o.k = O_MEM; o.mem.base = RBP; o.mem.disp = c->slot_off[v.as.slot_id] + v.field_offset; }
        break;
    }
    default: break;
    }
    return o;
}

static void normalize(CodegenContext* c, int r, IrType t) {
    int s = tsize(t);
    if (s == 1) EM("%s %s, %s\n", is_sg(t) ? "movsx" : "movzx", GP32[r], GP8[r]);
    else if (s == 2) EM("%s %s, %s\n", is_sg(t) ? "movsx" : "movzx", GP32[r], GP16[r]);
}

static void ext64(CodegenContext* c, int r, IrType from) {
    if (tsize(from) >= 8) return;
    if (is_sg(from)) EM("movsxd %s, %s\n", GP64[r], GP32[r]);
    else             EM("mov %s, %s\n", GP32[r], GP32[r]);
}

static void convert_reg(CodegenContext* c, int r, IrType from, IrType to) {
    int fs = tsize(from), ts = tsize(to);
    if (ts == 8) { if (fs < 8) ext64(c, r, from); return; }
    if (ts == 4) return;
    if (fs == ts && is_sg(from) == is_sg(to)) return;
    normalize(c, r, to);
}

static void load_imm(CodegenContext* c, int r, long long v, int sz) {
    if (sz <= 4) { EM("mov %s, %lld\n", GP32[r], v); return; }
    if (v >= 0 && v <= 0xFFFFFFFFLL) { EM("mov %s, %lld\n", GP32[r], v); return; }
    if (v < 0 && fits32(v)) { EM("mov %s, %lld\n", GP64[r], v); return; }
    EM("mov %s, 0x%llx\n", GP64[r], (unsigned long long)v);
}

static void load_mem(CodegenContext* c, int r, const Mem* m, IrType t) {
    char b[200];
    int sz = tsize(t);
    mfmt(b, sizeof b, m, sz, 0);
    switch (sz) {
    case 1: EM("%s %s, %s\n", is_sg(t) ? "movsx" : "movzx", GP32[r], b); break;
    case 2: EM("%s %s, %s\n", is_sg(t) ? "movsx" : "movzx", GP32[r], b); break;
    case 4: EM("mov %s, %s\n", GP32[r], b); break;
    default: EM("mov %s, %s\n", GP64[r], b); break;
    }
}

static void load_gp(CodegenContext* c, IrValue v, int r) {
    Opnd o = get_opnd(c, v);
    int sz = tsize(v.type);
    char b[200];
    switch (o.k) {
    case O_IMM:  load_imm(c, r, canon_imm(o.imm, v.type), sz); break;
    case O_REG:
        if (o.reg != r) EM("mov %s, %s\n", sz == 8 ? GP64[r] : GP32[r], sz == 8 ? GP64[o.reg] : GP32[o.reg]);
        break;
    case O_MEM:  load_mem(c, r, &o.mem, v.type); break;
    case O_ADDR: mfmt(b, sizeof b, &o.mem, 0, 0); EM("lea %s, %s\n", GP64[r], b); break;
    case O_XREG: EM("movq %s, xmm%d\n", GP64[r], o.reg); break;
    default: EM("xor %s, %s\n", GP32[r], GP32[r]); break;
    }
}

static void store_gp(CodegenContext* c, IrValue d, int r) {
    Opnd o = get_opnd(c, d);
    char b[200];
    int sz = tsize(d.type);
    if (o.k == O_REG) {
        if (o.reg != r) EM("mov %s, %s\n", sz == 8 ? GP64[o.reg] : GP32[o.reg], sz == 8 ? GP64[r] : GP32[r]);
    } else if (o.k == O_MEM) {
        if (d.kind == IR_VAL_TEMP) sz = 8;
        mfmt(b, sizeof b, &o.mem, sz, 0);
        EM("mov %s, %s\n", b, gp(r, sz));
    }
}

static int work_gp(CodegenContext* c, IrValue d, const IrValue* s2) {
    Opnd od = get_opnd(c, d);
    if (od.k != O_REG) return RAX;
    if (s2) {
        Opnd o2 = get_opnd(c, *s2);
        if (o2.k == O_REG && o2.reg == od.reg) return RAX;
    }
    return od.reg;
}

#define F_IMM 1
#define F_MEM 2

static void src_str(CodegenContext* c, IrValue v, IrType t, int os, int scratch, int flags, char* b, size_t n) {
    Opnd o = get_opnd(c, v);
    int vs = tsize(v.type);
    if (o.k == O_IMM && (flags & F_IMM)) {
        long long k = canon_imm(o.imm, t);
        if (os == 4 || fits32(k)) { snprintf(b, n, "%lld", k); return; }
    }
    if (o.k == O_REG && (vs == os || (vs < 4 && os == 4))) { snprintf(b, n, "%s", gp(o.reg, os)); return; }
    if (o.k == O_MEM && (flags & F_MEM) && vs == os) { mfmt(b, n, &o.mem, os, 0); return; }
    load_gp(c, v, scratch);
    if (os == 8 && vs < 8) ext64(c, scratch, v.type);
    snprintf(b, n, "%s", gp(scratch, os));
}

static bool looks_imm(const char* s) { return s[0] == '-' || isdigit((unsigned char)s[0]); }

static const char* fsfx(IrType t) { return tsize(t) == 4 ? "ss" : "sd"; }

static void load_xmm(CodegenContext* c, IrValue v, int x) {
    Opnd o = get_opnd(c, v);
    bool dbl = tsize(v.type) == 8;
    char b[200];
    switch (o.k) {
    case O_XREG:
        if (o.reg != x) EM("movaps xmm%d, xmm%d\n", x, o.reg);
        break;
    case O_MEM: {
        double val; bool single;
        if (v.kind == IR_VAL_CONST_FLOAT && float_info(c, v.as.float_id, &val, &single) && val == 0.0 && !signbit(val)) {
            EM("xorps xmm%d, xmm%d\n", x, x);
        } else {
            mfmt(b, sizeof b, &o.mem, dbl ? 8 : 4, 0);
            EM("%s xmm%d, %s\n", dbl ? "movsd" : "movss", x, b);
        }
        break;
    }
    case O_REG:
        EM("%s xmm%d, %s\n", dbl ? "movq" : "movd", x, gp(o.reg, dbl ? 8 : 4));
        break;
    case O_IMM:
        load_imm(c, RAX, o.imm, 8);
        EM("%s xmm%d, %s\n", dbl ? "movq" : "movd", x, dbl ? "rax" : "eax");
        break;
    default:
        EM("; carga xmm invalida\n");
        c->unsupported++;
        break;
    }
}

static int xreg_of(CodegenContext* c, IrValue v, int scratch) {
    Opnd o = get_opnd(c, v);
    if (o.k == O_XREG) return o.reg;
    load_xmm(c, v, scratch);
    return scratch;
}

static void xsrc_str(CodegenContext* c, IrValue v, int scratch, char* b, size_t n) {
    Opnd o = get_opnd(c, v);
    if (o.k == O_XREG) { snprintf(b, n, "xmm%d", o.reg); return; }
    if (o.k == O_MEM) { mfmt(b, n, &o.mem, tsize(v.type) == 8 ? 8 : 4, 0); return; }
    load_xmm(c, v, scratch);
    snprintf(b, n, "xmm%d", scratch);
}

static void store_xmm(CodegenContext* c, IrValue d, int x) {
    Opnd o = get_opnd(c, d);
    char b[200];
    if (o.k == O_XREG) {
        if (o.reg != x) EM("movaps xmm%d, xmm%d\n", o.reg, x);
    } else if (o.k == O_MEM) {
        bool dbl = tsize(d.type) == 8;
        mfmt(b, sizeof b, &o.mem, dbl ? 8 : 4, 0);
        EM("%s %s, xmm%d\n", dbl ? "movsd" : "movss", b, x);
    }
}

static int work_x(CodegenContext* c, IrValue d, const IrValue* s2) {
    Opnd od = get_opnd(c, d);
    if (od.k != O_XREG) return 0;
    if (s2) {
        Opnd o2 = get_opnd(c, *s2);
        if (o2.k == O_XREG && o2.reg == od.reg) return 0;
    }
    return od.reg;
}

static Mem addr_mem(CodegenContext* c, IrValue a, int extra, int scratch) {
    Mem m;
    memset(&m, 0, sizeof m);
    m.disp = extra;
    Opnd o = get_opnd(c, a);

    switch (a.kind) {
    case IR_VAL_SLOT_ADDR:
    case IR_VAL_GLOBAL_ADDR:
    case IR_VAL_CONST_STRING:
    case IR_VAL_FUNC:
    case IR_VAL_BUILTIN:
        o.mem.disp += extra;
        return o.mem;
    case IR_VAL_TEMP:
    case IR_VAL_SLOT:
    case IR_VAL_GLOBAL:
        if (o.k == O_REG) {
            m.base = o.reg;
        } else {
            char b[200];
            mfmt(b, sizeof b, &o.mem, 8, 0);
            EM("mov %s, %s\n", GP64[scratch], b);
            m.base = scratch;
        }
        if (a.kind == IR_VAL_TEMP) m.disp += a.field_offset;
        return m;
    case IR_VAL_CONST_INT:
    case IR_VAL_CONST_UINT:
        load_imm(c, scratch, o.imm, 8);
        m.base = scratch;
        return m;
    default:
        EM("; endereco invalido\n");
        c->unsupported++;
        m.base = scratch;
        return m;
    }
}

static void copy_mem(CodegenContext* c, const Mem* d, const Mem* s, int size) {
    char a[200], b[200];
    int off = 0;
    while (size - off >= 16) {
        mfmt(a, sizeof a, s, 16, off); mfmt(b, sizeof b, d, 16, off);
        EM("movups xmm0, %s\n", a); EM("movups %s, xmm0\n", b);
        off += 16;
    }
    static const int chunks[] = { 8, 4, 2, 1 };
    for (int k = 0; k < 4; ++k) {
        int ch = chunks[k];
        while (size - off >= ch) {
            mfmt(a, sizeof a, s, ch, off); mfmt(b, sizeof b, d, ch, off);
            EM("mov %s, %s\n", gp(RAX, ch), a); EM("mov %s, %s\n", b, gp(RAX, ch));
            off += ch;
        }
    }
}

static const char* cc_name(int idx, bool sg) {
    static const char* S[] = { "l", "le", "g", "ge", "e", "ne" };
    static const char* U[] = { "b", "be", "a", "ae", "e", "ne" };
    return sg ? S[idx] : U[idx];
}

static void emit_cmp(CodegenContext* c, IrValue a, IrValue b) {
    IrType t = a.type;
    int sz = tsize(t), os = sz == 8 ? 8 : 4;
    Opnd oa = get_opnd(c, a);
    char sa[200], sb[200];
    bool amem = false;

    if (oa.k == O_REG && (sz == os || sz < 4)) snprintf(sa, sizeof sa, "%s", gp(oa.reg, os));
    else if (oa.k == O_MEM && sz == os) { mfmt(sa, sizeof sa, &oa.mem, os, 0); amem = true; }
    else {
        load_gp(c, a, RAX);
        if (os == 8 && sz < 8) ext64(c, RAX, t);
        snprintf(sa, sizeof sa, "%s", gp(RAX, os));
    }
    src_str(c, b, t, os, RCX, F_IMM | (amem ? 0 : F_MEM), sb, sizeof sb);
    EM("cmp %s, %s\n", sa, sb);
}

static void emit_test(CodegenContext* c, IrValue v) {
    Opnd o = get_opnd(c, v);
    int sz = tsize(v.type), os = sz == 8 ? 8 : 4;
    char b[200];
    if (o.k == O_REG && (sz == os || sz < 4)) { EM("test %s, %s\n", gp(o.reg, os), gp(o.reg, os)); return; }
    if (o.k == O_MEM) { mfmt(b, sizeof b, &o.mem, sz, 0); EM("cmp %s, 0\n", b); return; }
    load_gp(c, v, RAX);
    EM("test %s, %s\n", gp(RAX, os), gp(RAX, os));
}

static void emit_setcc(CodegenContext* c, IrValue d, const char* cc) {
    Opnd od = get_opnd(c, d);
    int r = od.k == O_REG ? od.reg : RAX;
    EM("set%s %s\n", cc, GP8[r]);
    EM("movzx %s, %s\n", GP32[r], GP8[r]);
    if (od.k != O_REG) store_gp(c, d, r);
}

static void emit_fcmp(CodegenContext* c, IrOperators op, IrValue d, IrValue a, IrValue b) {
    int idx = op - IR_CMP_LT;
    IrValue x = a, y = b;
    const char* cc = "a";
    if (idx == 0) { x = b; y = a; cc = "a"; }
    else if (idx == 1) { x = b; y = a; cc = "ae"; }
    else if (idx == 2) cc = "a";
    else if (idx == 3) cc = "ae";
    else cc = idx == 4 ? "e" : "ne";

    int xr = xreg_of(c, x, 0);
    char ys[200];
    xsrc_str(c, y, 1, ys, sizeof ys);
    EM("ucomis%c xmm%d, %s\n", tsize(a.type) == 8 ? 'd' : 's', xr, ys);

    Opnd od = get_opnd(c, d);
    int r = od.k == O_REG ? od.reg : RAX;
    if (idx == 4) {
        EM("sete %s\n", GP8[r]); EM("setnp cl\n"); EM("and %s, cl\n", GP8[r]);
    } else if (idx == 5) {
        EM("setne %s\n", GP8[r]); EM("setp cl\n"); EM("or %s, cl\n", GP8[r]);
    } else {
        EM("set%s %s\n", cc, GP8[r]);
    }
    EM("movzx %s, %s\n", GP32[r], GP8[r]);
    if (od.k != O_REG) store_gp(c, d, r);
}

static void emit_alu(CodegenContext* c, IrInstruction* in, const char* mn) {
    IrType t = in->dest.type;
    int sz = tsize(t), os = sz == 8 ? 8 : 4;
    int w = work_gp(c, in->dest, &in->src2);
    char b[200];

    load_gp(c, in->src1, w);
    convert_reg(c, w, in->src1.type, t);
    src_str(c, in->src2, t, os, RCX, F_IMM | F_MEM, b, sizeof b);

    if (!strcmp(mn, "imul") && looks_imm(b)) EM("imul %s, %s, %s\n", gp(w, os), gp(w, os), b);
    else EM("%s %s, %s\n", mn, gp(w, os), b);

    if (sz < 4) normalize(c, w, t);
    store_gp(c, in->dest, w);
}

static void emit_falu(CodegenContext* c, IrInstruction* in, const char* mn) {
    int w = work_x(c, in->dest, &in->src2);
    char b[200];
    load_xmm(c, in->src1, w);
    xsrc_str(c, in->src2, 1, b, sizeof b);
    EM("%s%s xmm%d, %s\n", mn, fsfx(in->dest.type), w, b);
    store_xmm(c, in->dest, w);
}

static int log2_exact(unsigned long long v) {
    if (v == 0 || (v & (v - 1))) return -1;
    int k = 0;
    while (v > 1) { v >>= 1; ++k; }
    return k;
}

static void emit_divmod(CodegenContext* c, IrInstruction* in, bool is_mod) {
    IrType t = in->dest.type;
    int sz = tsize(t), os = sz == 8 ? 8 : 4;
    bool sg = is_sg(t);
    Opnd o2 = get_opnd(c, in->src2);
    char b[200];

    if (!sg && o2.k == O_IMM) {
        long long k = canon_imm(o2.imm, t);
        int lg = log2_exact((unsigned long long)k);
        if (lg >= 0 && (!is_mod || fits32(k - 1))) {
            int w = work_gp(c, in->dest, NULL);
            load_gp(c, in->src1, w);
            convert_reg(c, w, in->src1.type, t);
            if (is_mod) { if (k > 1) EM("and %s, %lld\n", gp(w, os), k - 1); else EM("xor %s, %s\n", GP32[w], GP32[w]); }
            else if (lg > 0) EM("shr %s, %d\n", gp(w, os), lg);
            store_gp(c, in->dest, w);
            return;
        }
    }

    load_gp(c, in->src1, RAX);
    convert_reg(c, RAX, in->src1.type, t);
    src_str(c, in->src2, t, os, RCX, F_MEM, b, sizeof b);
    if (sg) { fprintf(c->out, "    %s\n", os == 8 ? "cqo" : "cdq"); EM("idiv %s\n", b); }
    else    { EM("xor edx, edx\n"); EM("div %s\n", b); }
    store_gp(c, in->dest, is_mod ? RDX : RAX);
}

static void emit_shift(CodegenContext* c, IrInstruction* in, bool left) {
    IrType t = in->dest.type;
    int sz = tsize(t), os = sz == 8 ? 8 : 4;
    int w = work_gp(c, in->dest, &in->src2);
    const char* mn = left ? "shl" : (is_sg(t) ? "sar" : "shr");

    load_gp(c, in->src1, w);
    convert_reg(c, w, in->src1.type, t);

    Opnd o2 = get_opnd(c, in->src2);
    if (o2.k == O_IMM) {
        EM("%s %s, %lld\n", mn, gp(w, os), o2.imm & (os * 8 - 1));
    } else {
        load_gp(c, in->src2, RCX);
        EM("%s %s, cl\n", mn, gp(w, os));
    }
    if (left && sz < 4) normalize(c, w, t);
    store_gp(c, in->dest, w);
}

static void emit_unary(CodegenContext* c, IrInstruction* in) {
    IrType t = in->dest.type;
    if (in->op == IR_NEG && is_fp(t)) {
        int w = work_x(c, in->dest, NULL);
        load_xmm(c, in->src1, w);
        EM("xorps xmm%d, oword [rel %s]\n", w, tsize(t) == 8 ? "__fsgn64" : "__fsgn32");
        store_xmm(c, in->dest, w);
        return;
    }
    int sz = tsize(t), os = sz == 8 ? 8 : 4;
    int w = work_gp(c, in->dest, NULL);
    load_gp(c, in->src1, w);
    convert_reg(c, w, in->src1.type, t);
    EM("%s %s\n", in->op == IR_NEG ? "neg" : "not", gp(w, os));
    if (sz < 4) normalize(c, w, t);
    store_gp(c, in->dest, w);
}

static void emit_lnot(CodegenContext* c, IrInstruction* in) {
    if (is_fp(in->src1.type)) {
        int xr = xreg_of(c, in->src1, 0);
        EM("xorps xmm1, xmm1\n");
        EM("ucomis%c xmm%d, xmm1\n", tsize(in->src1.type) == 8 ? 'd' : 's', xr);
        Opnd od = get_opnd(c, in->dest);
        int r = od.k == O_REG ? od.reg : RAX;
        EM("sete %s\n", GP8[r]); EM("setnp cl\n"); EM("and %s, cl\n", GP8[r]);
        EM("movzx %s, %s\n", GP32[r], GP8[r]);
        if (od.k != O_REG) store_gp(c, in->dest, r);
        return;
    }
    emit_test(c, in->src1);
    emit_setcc(c, in->dest, "e");
}

static void emit_cast(CodegenContext* c, IrInstruction* in) {
    IrType from = in->src1.type, to = in->dest.type;
    bool ff = is_fp(from), tf = is_fp(to);

    if (!ff && !tf) {
        Opnd od = get_opnd(c, in->dest);
        int w = od.k == O_REG ? od.reg : RAX;
        load_gp(c, in->src1, w);
        convert_reg(c, w, from, to);
        store_gp(c, in->dest, w);
        return;
    }

    if (ff && tf) {
        if (tsize(from) == tsize(to)) { int x = work_x(c, in->dest, NULL); load_xmm(c, in->src1, x); store_xmm(c, in->dest, x); return; }
        Opnd od = get_opnd(c, in->dest);
        int x = od.k == O_XREG ? od.reg : 0;
        char b[200];
        xsrc_str(c, in->src1, 0, b, sizeof b);
        EM("%s xmm%d, %s\n", tsize(from) == 4 ? "cvtss2sd" : "cvtsd2ss", x, b);
        store_xmm(c, in->dest, x);
        return;
    }

    if (!ff && tf) {
        Opnd od = get_opnd(c, in->dest);
        int x = od.k == O_XREG ? od.reg : 0;
        const char* sfx = tsize(to) == 4 ? "ss" : "sd";
        int fs = tsize(from);
        bool sg = is_sg(from);
        load_gp(c, in->src1, RAX);
        EM("pxor xmm%d, xmm%d\n", x, x);
        if (fs == 8 && !sg) {
            int id = c->local_label++;
            EM("test rax, rax\n");
            EM("js .Lx%d\n", id);
            EM("cvtsi2%s xmm%d, rax\n", sfx, x);
            EM("jmp .Ly%d\n", id);
            fprintf(c->out, ".Lx%d:\n", id);
            EM("mov rcx, rax\n"); EM("shr rcx, 1\n"); EM("and eax, 1\n"); EM("or rcx, rax\n");
            EM("cvtsi2%s xmm%d, rcx\n", sfx, x);
            EM("add%s xmm%d, xmm%d\n", sfx, x, x);
            fprintf(c->out, ".Ly%d:\n", id);
        } else if (fs == 8) {
            EM("cvtsi2%s xmm%d, rax\n", sfx, x);
        } else if (fs == 4 && !sg) {
            EM("mov eax, eax\n");
            EM("cvtsi2%s xmm%d, rax\n", sfx, x);
        } else {
            EM("cvtsi2%s xmm%d, eax\n", sfx, x);
        }
        store_xmm(c, in->dest, x);
        return;
    }

    {
        Opnd od = get_opnd(c, in->dest);
        int r = od.k == O_REG ? od.reg : RAX;
        int ts = tsize(to);
        bool wide = ts == 8 || (ts == 4 && !is_sg(to));
        char b[200];
        xsrc_str(c, in->src1, 0, b, sizeof b);
        EM("cvtt%s2si %s, %s\n", tsize(from) == 4 ? "ss" : "sd", wide ? GP64[r] : GP32[r], b);
        if (ts < 4) normalize(c, r, to);
        if (od.k != O_REG) store_gp(c, in->dest, r);
    }
}

static void emit_load(CodegenContext* c, IrInstruction* in) {
    IrType t = in->dest.type;
    int sz = tsize(t);
    Mem m = addr_mem(c, in->src1, in->aux, R11);
    Opnd od = get_opnd(c, in->dest);
    char b[200];

    if (is_fp(t)) {
        int x = od.k == O_XREG ? od.reg : 0;
        mfmt(b, sizeof b, &m, sz, 0);
        EM("%s xmm%d, %s\n", sz == 8 ? "movsd" : "movss", x, b);
        store_xmm(c, in->dest, x);
    } else if (sz > 8) {
        if (od.k == O_MEM) copy_mem(c, &od.mem, &m, sz);
        else { EM("; load > 8 bytes sem destino em memoria\n"); c->unsupported++; }
    } else {
        int r = od.k == O_REG ? od.reg : RAX;
        load_mem(c, r, &m, t);
        store_gp(c, in->dest, r);
    }
}

static void emit_store(CodegenContext* c, IrInstruction* in) {
    IrType t = in->src2.type;
    int sz = tsize(t);
    Mem m = addr_mem(c, in->src1, in->aux, R11);
    Opnd o = get_opnd(c, in->src2);
    char b[200];

    if (is_fp(t)) {
        int x = xreg_of(c, in->src2, 0);
        mfmt(b, sizeof b, &m, sz, 0);
        EM("%s %s, xmm%d\n", sz == 8 ? "movsd" : "movss", b, x);
    } else if (sz > 8) {
        if (o.k == O_MEM) copy_mem(c, &m, &o.mem, sz);
        else { EM("; store > 8 bytes sem origem em memoria\n"); c->unsupported++; }
    } else if (o.k == O_IMM && (sz < 8 || fits32(canon_imm(o.imm, t)))) {
        mfmt(b, sizeof b, &m, sz, 0);
        EM("mov %s, %lld\n", b, canon_imm(o.imm, t));
    } else {
        int r = o.k == O_REG ? o.reg : RAX;
        if (o.k != O_REG) load_gp(c, in->src2, RAX);
        mfmt(b, sizeof b, &m, sz, 0);
        EM("mov %s, %s\n", b, gp(r, sz));
    }
}

static void emit_atomic(CodegenContext* c, IrInstruction* in) {
    char b[200];
    switch (in->op) {
    case IR_ATOMIC_STORE: {
        int sz = tsize(in->src2.type);
        Mem m = addr_mem(c, in->src1, in->aux, R11);
        load_gp(c, in->src2, RAX);
        mfmt(b, sizeof b, &m, sz, 0);
        EM("xchg %s, %s\n", b, gp(RAX, sz));
        break;
    }
    case IR_ATOMIC_XCHG: {
        int sz = tsize(in->dest.type);
        Mem m = addr_mem(c, in->src1, in->aux, R11);
        load_gp(c, in->src2, RAX);
        mfmt(b, sizeof b, &m, sz, 0);
        EM("xchg %s, %s\n", b, gp(RAX, sz));
        normalize(c, RAX, in->dest.type);
        store_gp(c, in->dest, RAX);
        break;
    }
    case IR_ATOMIC_CAS: {
        int sz = tsize(in->dest.type);
        Mem m = addr_mem(c, in->src1, in->aux, R11);
        load_gp(c, in->src2, RAX);
        load_gp(c, in->src3, RCX);
        mfmt(b, sizeof b, &m, sz, 0);
        EM("lock cmpxchg %s, %s\n", b, gp(RCX, sz));
        normalize(c, RAX, in->dest.type);
        store_gp(c, in->dest, RAX);
        break;
    }
    case IR_ATOMIC_ADD:
    case IR_ATOMIC_AND:
    case IR_ATOMIC_OR:
    case IR_ATOMIC_XOR: {
        int sz = tsize(in->src2.type);
        Mem m = addr_mem(c, in->src1, in->aux, R11);
        bool unused = (in->flags & IRF_RESULT_UNUSED) || in->dest.kind == IR_VAL_NONE;
        mfmt(b, sizeof b, &m, sz, 0);
        if (in->op == IR_ATOMIC_ADD && !unused) {
            load_gp(c, in->src2, RAX);
            EM("lock xadd %s, %s\n", b, gp(RAX, sz));
            normalize(c, RAX, in->dest.type);
            store_gp(c, in->dest, RAX);
        } else {
            const char* mn = in->op == IR_ATOMIC_ADD ? "add" : in->op == IR_ATOMIC_AND ? "and" : in->op == IR_ATOMIC_OR ? "or" : "xor";
            Opnd o = get_opnd(c, in->src2);
            if (o.k == O_IMM && (sz < 8 || fits32(canon_imm(o.imm, in->src2.type)))) {
                EM("lock %s %s, %lld\n", mn, b, canon_imm(o.imm, in->src2.type));
            } else {
                load_gp(c, in->src2, RAX);
                EM("lock %s %s, %s\n", mn, b, gp(RAX, sz));
            }
        }
        break;
    }
    default: break;
    }
}

static void emit_memzero(CodegenContext* c, IrInstruction* in) {
    Mem m = addr_mem(c, in->src1, 0, R11);
    Opnd on = get_opnd(c, in->src2);
    char b[200];

    if (on.k == O_IMM && on.imm <= 64) {
        long long n = on.imm, off = 0;
        if (n >= 16) EM("pxor xmm0, xmm0\n");
        while (n - off >= 16) { mfmt(b, sizeof b, &m, 16, off); EM("movups %s, xmm0\n", b); off += 16; }
        static const int chunks[] = { 8, 4, 2, 1 };
        for (int k = 0; k < 4; ++k)
            while (n - off >= chunks[k]) { mfmt(b, sizeof b, &m, chunks[k], off); EM("mov %s, 0\n", b); off += chunks[k]; }
        return;
    }

    EM("push rdi\n");
    if (on.k == O_IMM && on.imm % 8 == 0) { EM("mov ecx, %lld\n", on.imm / 8); mfmt(b, sizeof b, &m, 0, 0); EM("lea rdi, %s\n", b); EM("xor eax, eax\n"); EM("rep stosq\n"); }
    else {
        load_gp(c, in->src2, RCX);
        mfmt(b, sizeof b, &m, 0, 0);
        EM("lea rdi, %s\n", b); EM("xor eax, eax\n"); EM("rep stosb\n");
    }
    EM("pop rdi\n");
}

static void emit_alloca(CodegenContext* c, IrInstruction* in) {
    int id = c->local_label++;
    load_gp(c, in->src1, RAX);
    ext64(c, RAX, in->src1.type);
    EM("add rax, 15\n");
    EM("and rax, -16\n");
    EM("mov rcx, rax\n");
    EM("mov r11, rsp\n");
    fprintf(c->out, ".Lp%d:\n", id);
    EM("cmp rcx, 4096\n");
    EM("jbe .Ld%d\n", id);
    EM("sub r11, 4096\n");
    EM("test [r11], al\n");
    EM("sub rcx, 4096\n");
    EM("jmp .Lp%d\n", id);
    fprintf(c->out, ".Ld%d:\n", id);
    EM("sub rsp, rax\n");
    EM("lea rax, [rsp+%d]\n", c->out_area);
    store_gp(c, in->dest, RAX);
}

static void emit_arg(CodegenContext* c, IrInstruction* in) {
    int idx = in->aux;
    if (idx < 0 || idx >= 4) { EM("; IR_ARG com indice %d fora de rcx/rdx/r8/r9\n", idx); c->unsupported++; return; }
    if (is_fp(in->src1.type)) {
        load_xmm(c, in->src1, idx);
        EM("movq %s, xmm%d\n", GP64[ARG_GP[idx]], idx);   /* chamadas variadicas exigem copia no reg inteiro */
    } else {
        load_gp(c, in->src1, ARG_GP[idx]);
    }
}

static void emit_arg_stack(CodegenContext* c, IrInstruction* in) {
    int total = in->aux;
    int slot = total - 1 - c->stack_arg_seen++;
    long long disp = 32 + 8LL * slot;
    char b[200];
    Mem m; memset(&m, 0, sizeof m); m.base = RSP; m.disp = disp;

    if (is_fp(in->src1.type)) {
        int x = xreg_of(c, in->src1, 0);
        mfmt(b, sizeof b, &m, tsize(in->src1.type), 0);
        EM("%s %s, xmm%d\n", tsize(in->src1.type) == 8 ? "movsd" : "movss", b, x);
        return;
    }
    Opnd o = get_opnd(c, in->src1);
    mfmt(b, sizeof b, &m, 8, 0);
    if (o.k == O_IMM && fits32(canon_imm(o.imm, in->src1.type))) { EM("mov %s, %lld\n", b, canon_imm(o.imm, in->src1.type)); return; }
    int r = o.k == O_REG ? o.reg : RAX;
    if (o.k != O_REG) load_gp(c, in->src1, RAX);
    EM("mov %s, %s\n", b, GP64[r]);
}

static void emit_call(CodegenContext* c, IrInstruction* in) {
    char b[200];
    if (in->src1.kind == IR_VAL_FUNC || in->src1.kind == IR_VAL_BUILTIN) {
        func_sym(c, in->src1.as.func_id, in->src1.kind == IR_VAL_BUILTIN, b, sizeof b);
        EM("call %s\n", b);
    } else {
        Opnd o = get_opnd(c, in->src1);
        if (o.k == O_REG) EM("call %s\n", GP64[o.reg]);
        else { load_gp(c, in->src1, RAX); EM("call rax\n"); }
    }
    c->stack_arg_seen = 0;

    if (in->dest.kind != IR_VAL_NONE && !(in->flags & IRF_RESULT_UNUSED)) {
        if (is_fp(in->dest.type)) store_xmm(c, in->dest, 0);
        else { normalize(c, RAX, in->dest.type); store_gp(c, in->dest, RAX); }
    }
}

static void emit_intrin(CodegenContext* c, IrInstruction* in) {
    switch (in->aux) {
    case INTR_VDIFF16:
        load_gp(c, in->src1, RAX);
        load_gp(c, in->src2, RDX);
        EM("movdqu xmm0, [rax]\n");
        EM("movdqu xmm1, [rdx]\n");
        EM("pcmpeqb xmm0, xmm1\n");
        EM("pmovmskb eax, xmm0\n");
        EM("xor eax, 0xFFFF\n");
        store_gp(c, in->dest, RAX);
        break;
    
    case INTR_UMULH:
        load_gp(c, in->src1, RAX);
        load_gp(c, in->src2, RDX);
        EM("mul rdx\n");
        store_gp(c, in->dest, RDX);
        break;

    case INTR_FPBITS:
        load_gp(c, in->src1, RAX);
        if (tsize(in->src1.type) == 4) EM("mov eax, eax\n");
        store_gp(c, in->dest, RAX);
        break;

    case INTR_BITSD:
        load_gp(c, in->src1, RAX);
        EM("movq xmm0, rax\n");
        store_xmm(c, in->dest, 0);
        break;

    case INTR_BITSF:
        load_gp(c, in->src1, RAX);
        EM("movd xmm0, eax\n");
        store_xmm(c, in->dest, 0);
        break;

    case INTR_VEQ64:
        load_gp(c, in->src1, RAX);
        load_gp(c, in->src2, RDX);
        EM("movdqu xmm0, [rax]\n");
        EM("movdqu xmm1, [rax+16]\n");
        EM("movdqu xmm2, [rdx]\n");
        EM("movdqu xmm3, [rdx+16]\n");
        EM("pcmpeqb xmm0, xmm2\n");
        EM("pcmpeqb xmm1, xmm3\n");
        EM("pand xmm0, xmm1\n");
        EM("movdqu xmm1, [rax+32]\n");
        EM("movdqu xmm2, [rdx+32]\n");
        EM("movdqu xmm3, [rax+48]\n");
        EM("pcmpeqb xmm1, xmm2\n");
        EM("movdqu xmm2, [rdx+48]\n");
        EM("pcmpeqb xmm3, xmm2\n");
        EM("pand xmm1, xmm3\n");
        EM("pand xmm0, xmm1\n");
        EM("pmovmskb eax, xmm0\n");
        EM("cmp eax, 0xFFFF\n");
        EM("sete al\n");
        EM("movzx eax, al\n");
        store_gp(c, in->dest, RAX);
        break;

    case INTR_CTZ:
        load_gp(c, in->src1, RAX);
        EM("bsf rax, rax\n");
        store_gp(c, in->dest, RAX);
        break;

    case INTR_VCOPY16X2:
        load_gp(c, in->src1, RAX);
        load_gp(c, in->src2, RDX);
        load_gp(c, in->src3, RCX);
        EM("movdqu xmm0, [rdx]\n");
        EM("movdqu xmm1, [rdx+rcx-16]\n");
        EM("movdqu [rax], xmm0\n");
        EM("movdqu [rax+rcx-16], xmm1\n");
        break;

    case INTR_VCOPY32X2:
        load_gp(c, in->src1, RAX);
        load_gp(c, in->src2, RDX);
        load_gp(c, in->src3, RCX);
        EM("movdqu xmm0, [rdx]\n");
        EM("movdqu xmm1, [rdx+16]\n");
        EM("movdqu xmm2, [rdx+rcx-32]\n");
        EM("movdqu xmm3, [rdx+rcx-16]\n");
        EM("movdqu [rax], xmm0\n");
        EM("movdqu [rax+16], xmm1\n");
        EM("movdqu [rax+rcx-32], xmm2\n");
        EM("movdqu [rax+rcx-16], xmm3\n");
        break;

    case INTR_VCOPY64:
        load_gp(c, in->src1, RAX);
        load_gp(c, in->src2, RDX);
        EM("movdqu xmm0, [rdx]\n");
        EM("movdqu xmm1, [rdx+16]\n");
        EM("movdqu xmm2, [rdx+32]\n");
        EM("movdqu xmm3, [rdx+48]\n");
        EM("movdqu [rax], xmm0\n");
        EM("movdqu [rax+16], xmm1\n");
        EM("movdqu [rax+32], xmm2\n");
        EM("movdqu [rax+48], xmm3\n");
        break;

    case INTR_REPMOVSB:
        load_gp(c, in->src1, RAX);
        load_gp(c, in->src2, RDX);
        load_gp(c, in->src3, RCX);
        EM("push rsi\n"); EM("push rdi\n");
        EM("mov rdi, rax\n"); EM("mov rsi, rdx\n");
        EM("rep movsb\n");
        EM("pop rdi\n"); EM("pop rsi\n");
        break;

    case INTR_VNULL16:
        load_gp(c, in->src1, RAX);
        EM("movdqu xmm0, [rax]\n");
        EM("pxor xmm1, xmm1\n");
        EM("pcmpeqb xmm0, xmm1\n");
        EM("pmovmskb eax, xmm0\n");
        store_gp(c, in->dest, RAX);
        break;

    case INTR_VNULL64:
        load_gp(c, in->src1, RAX);
        EM("movdqu xmm0, [rax]\n");
        EM("movdqu xmm1, [rax+16]\n");
        EM("movdqu xmm2, [rax+32]\n");
        EM("movdqu xmm3, [rax+48]\n");
        EM("pminub xmm0, xmm1\n");
        EM("pminub xmm2, xmm3\n");
        EM("pminub xmm0, xmm2\n");
        EM("pxor xmm1, xmm1\n");
        EM("pcmpeqb xmm0, xmm1\n");
        EM("pmovmskb eax, xmm0\n");
        store_gp(c, in->dest, RAX);
        break;

    case INTR_VFILL16X2:
        load_gp(c, in->src1, RAX);
        load_gp(c, in->src2, RDX);
        load_gp(c, in->src3, RCX);
        EM("movq xmm0, rdx\n");
        EM("punpcklqdq xmm0, xmm0\n");
        EM("movdqu [rax], xmm0\n");
        EM("movdqu [rax+rcx-16], xmm0\n");
        break;

    case INTR_VFILL32X2:
        load_gp(c, in->src1, RAX);
        load_gp(c, in->src2, RDX);
        load_gp(c, in->src3, RCX);
        EM("movq xmm0, rdx\n");
        EM("punpcklqdq xmm0, xmm0\n");
        EM("movdqu [rax], xmm0\n");
        EM("movdqu [rax+16], xmm0\n");
        EM("movdqu [rax+rcx-32], xmm0\n");
        EM("movdqu [rax+rcx-16], xmm0\n");
        break;

    case INTR_VFILL64:
        load_gp(c, in->src1, RAX);
        load_gp(c, in->src2, RDX);
        EM("movq xmm0, rdx\n");
        EM("punpcklqdq xmm0, xmm0\n");
        EM("movdqu [rax], xmm0\n");
        EM("movdqu [rax+16], xmm0\n");
        EM("movdqu [rax+32], xmm0\n");
        EM("movdqu [rax+48], xmm0\n");
        break;

    case INTR_REPSTOSB:
        load_gp(c, in->src1, RAX);
        load_gp(c, in->src2, RDX);
        load_gp(c, in->src3, RCX);
        EM("push rdi\n");
        EM("mov rdi, rax\n");
        EM("mov eax, edx\n");
        EM("rep stosb\n");
        EM("pop rdi\n");
        break;
    
    case INTR_VFIND16:
        load_gp(c, in->src1, RAX);
        load_gp(c, in->src2, RDX);
        EM("movdqu xmm0, [rax]\n");
        EM("movq xmm1, rdx\n");
        EM("punpcklqdq xmm1, xmm1\n");
        EM("pcmpeqb xmm0, xmm1\n");
        EM("pmovmskb eax, xmm0\n");
        store_gp(c, in->dest, RAX);
        break;

    case INTR_VFIND64:
        load_gp(c, in->src1, RAX);
        load_gp(c, in->src2, RDX);
        EM("movq xmm3, rdx\n");
        EM("punpcklqdq xmm3, xmm3\n");
        EM("movdqu xmm0, [rax]\n");
        EM("movdqu xmm1, [rax+16]\n");
        EM("movdqu xmm2, [rax+32]\n");
        EM("pcmpeqb xmm0, xmm3\n");
        EM("pcmpeqb xmm1, xmm3\n");
        EM("pcmpeqb xmm2, xmm3\n");
        EM("pmovmskb ecx, xmm0\n");
        EM("pmovmskb edx, xmm1\n");
        EM("pmovmskb r11d, xmm2\n");
        EM("movdqu xmm0, [rax+48]\n");
        EM("pcmpeqb xmm0, xmm3\n");
        EM("pmovmskb eax, xmm0\n");
        EM("shl rdx, 16\n");
        EM("shl r11, 32\n");
        EM("shl rax, 48\n");
        EM("or rax, rcx\n");
        EM("or rax, rdx\n");
        EM("or rax, r11\n");
        store_gp(c, in->dest, RAX);
        break;

    case INTR_CLZ: {
        bool w32 = tsize(in->src1.type) == 4;
        load_gp(c, in->src1, RAX);
        if (w32) {
            EM("mov ecx, 63\n");
            EM("bsr eax, eax\n");
            EM("cmovz eax, ecx\n");
            EM("xor eax, 31\n");
        } else {
            EM("mov ecx, 127\n");
            EM("bsr rax, rax\n");
            EM("cmovz rax, rcx\n");
            EM("xor eax, 63\n");
        }
        store_gp(c, in->dest, RAX);
        break;
    }

    case INTR_MEM_BARRIER:
        EM("; __mem_barrier\n");
        break;

    case INTR_VFILL64X2:
        load_gp(c, in->src1, RAX);
        load_gp(c, in->src2, RDX);
        load_gp(c, in->src3, RCX);
        EM("movq xmm0, rdx\n");
        EM("punpcklqdq xmm0, xmm0\n");
        EM("movdqu [rax], xmm0\n");
        EM("movdqu [rax+16], xmm0\n");
        EM("movdqu [rax+32], xmm0\n");
        EM("movdqu [rax+48], xmm0\n");
        EM("movdqu [rax+rcx-64], xmm0\n");
        EM("movdqu [rax+rcx-48], xmm0\n");
        EM("movdqu [rax+rcx-32], xmm0\n");
        EM("movdqu [rax+rcx-16], xmm0\n");
        break;

    case INTR_VFINDW16:
        load_gp(c, in->src1, RAX);
        load_gp(c, in->src2, RDX);
        EM("movdqu xmm0, [rax]\n");
        EM("movd xmm1, edx\n");
        EM("pshuflw xmm1, xmm1, 0\n");
        EM("pshufd xmm1, xmm1, 0\n");
        EM("pcmpeqw xmm0, xmm1\n");
        EM("pmovmskb eax, xmm0\n");
        store_gp(c, in->dest, RAX);
        break;

    case INTR_VCMPZ16:
        load_gp(c, in->src1, RAX);
        load_gp(c, in->src2, RDX);
        EM("movdqu xmm0, [rax]\n");
        EM("movdqu xmm1, [rdx]\n");
        EM("pxor xmm2, xmm2\n");
        EM("pcmpeqb xmm2, xmm0\n");
        EM("pcmpeqb xmm0, xmm1\n");
        EM("pandn xmm2, xmm0\n");
        EM("pmovmskb eax, xmm2\n");
        EM("xor eax, 0xFFFF\n");
        store_gp(c, in->dest, RAX);
        break;

    case INTR_VFINDZ16:
        load_gp(c, in->src1, RAX);
        load_gp(c, in->src2, RDX);
        EM("movdqu xmm0, [rax]\n");
        EM("movq xmm1, rdx\n");
        EM("punpcklqdq xmm1, xmm1\n");
        EM("movdqa xmm2, xmm0\n");
        EM("pxor xmm2, xmm1\n");
        EM("pminub xmm2, xmm0\n");
        EM("pxor xmm3, xmm3\n");
        EM("pcmpeqb xmm2, xmm3\n");
        EM("pmovmskb eax, xmm2\n");
        store_gp(c, in->dest, RAX);
        break;

    case INTR_VANYZ64:
        load_gp(c, in->src1, RAX);
        load_gp(c, in->src2, RDX);
        EM("movq xmm3, rdx\n");
        EM("punpcklqdq xmm3, xmm3\n");

        EM("movdqu xmm0, [rax]\n");
        EM("movdqa xmm1, xmm0\n");
        EM("pxor xmm1, xmm3\n");
        EM("pminub xmm0, xmm1\n");

        for (int off = 16; off < 64; off += 16) {
            EM("movdqu xmm1, [rax+%d]\n", off);
            EM("movdqa xmm2, xmm1\n");
            EM("pxor xmm2, xmm3\n");
            EM("pminub xmm1, xmm2\n");
            EM("pminub xmm0, xmm1\n");
        }

        EM("pxor xmm1, xmm1\n");
        EM("pcmpeqb xmm0, xmm1\n");
        EM("pmovmskb eax, xmm0\n");
        store_gp(c, in->dest, RAX);
        break;
    }
}

static void emit_memcopy(CodegenContext* c, IrInstruction* in) {
    Mem d = addr_mem(c, in->src1, in->aux, R11);
    Mem s = addr_mem(c, in->src2, in->aux2, RDX);
    long long n = in->src3.as.const_i;
    char b[200];

    if (n <= 128) { copy_mem(c, &d, &s, (int)n); return; }

    mfmt(b, sizeof b, &d, 0, 0); EM("lea rax, %s\n", b);
    mfmt(b, sizeof b, &s, 0, 0); EM("lea rdx, %s\n", b);
    EM("mov ecx, %lld\n", n);
    EM("push rsi\n"); EM("push rdi\n");
    EM("mov rdi, rax\n"); EM("mov rsi, rdx\n");
    EM("rep movsb\n");
    EM("pop rdi\n"); EM("pop rsi\n");
}

static void emit_instr(CodegenContext* c, IrInstruction* in, size_t idx) {
    IrFunctionRange* fn = c->fn;
    char b[200];

    switch (in->op) {
    case IR_ADD: if (is_fp(in->dest.type)) emit_falu(c, in, "add"); else emit_alu(c, in, "add"); break;
    case IR_SUB: if (is_fp(in->dest.type)) emit_falu(c, in, "sub"); else emit_alu(c, in, "sub"); break;
    case IR_MUL: if (is_fp(in->dest.type)) emit_falu(c, in, "mul"); else emit_alu(c, in, "imul"); break;
    case IR_DIV:
        if (is_fp(in->dest.type)) emit_falu(c, in, "div"); else emit_divmod(c, in, false);
        break;
    case IR_MOD:
        if (is_fp(in->dest.type)) { EM("; mod em ponto flutuante nao suportado\n"); c->unsupported++; }
        else emit_divmod(c, in, true);
        break;
    case IR_BAND: emit_alu(c, in, "and"); break;
    case IR_BOR:  emit_alu(c, in, "or"); break;
    case IR_BXOR: emit_alu(c, in, "xor"); break;
    case IR_SHL:  emit_shift(c, in, true); break;
    case IR_SHR:  emit_shift(c, in, false); break;
    case IR_NEG:
    case IR_NOT:  emit_unary(c, in); break;
    case IR_LNOT: emit_lnot(c, in); break;
    case IR_INTRIN: emit_intrin(c, in); break;
    case IR_MEMCOPY: emit_memcopy(c, in); break;

    case IR_VA_START: {
        Opnd od = get_opnd(c, in->dest);
        int r = od.k == O_REG ? od.reg : RAX;
        EM("lea %s, [rbp%+d]\n", GP64[r], 16 + 8 * fn->param_count);
        store_gp(c, in->dest, r);
        break;
    }

    case IR_CMP_LT: case IR_CMP_LE: case IR_CMP_GT:
    case IR_CMP_GE: case IR_CMP_EQ: case IR_CMP_NE:
        if (is_fp(in->src1.type)) emit_fcmp(c, in->op, in->dest, in->src1, in->src2);
        else {
            emit_cmp(c, in->src1, in->src2);
            emit_setcc(c, in->dest, cc_name(in->op - IR_CMP_LT, is_sg(in->src1.type)));
        }
        break;

    case IR_CAST: emit_cast(c, in); break;

    case IR_ASSIGN: {
        IrType t = in->dest.type;
        Opnd od = get_opnd(c, in->dest);
        Opnd os = get_opnd(c, in->src1);
        int sz = tsize(t);
        if (is_fp(t)) {
            int x = od.k == O_XREG ? od.reg : 0;
            load_xmm(c, in->src1, x);
            store_xmm(c, in->dest, x);
        } else if (sz > 8) {
            if (od.k == O_MEM && os.k == O_MEM) copy_mem(c, &od.mem, &os.mem, sz);
            else { EM("; assign > 8 bytes sem origem/destino em memoria\n"); c->unsupported++; }
        } else if (od.k == O_MEM && os.k == O_IMM && (sz < 8 || fits32(canon_imm(os.imm, t))) && in->dest.kind != IR_VAL_TEMP) {
            mfmt(b, sizeof b, &od.mem, sz, 0);
            EM("mov %s, %lld\n", b, canon_imm(os.imm, t));
        } else {
            int w = od.k == O_REG ? od.reg : RAX;
            load_gp(c, in->src1, w);
            convert_reg(c, w, in->src1.type, t);
            store_gp(c, in->dest, w);
        }
        break;
    }

    case IR_LABEL:
        fprintf(c->out, ".L%d:\n", in->dest.as.label_id);
        break;

    case IR_JMP: {
        if (idx + 1 < fn->end) {
            IrInstruction* nx = &c->ir->instructions.items[idx + 1];
            if (nx->op == IR_LABEL && nx->dest.as.label_id == in->dest.as.label_id) break;
        }
        EM("jmp .L%d\n", in->dest.as.label_id);
        break;
    }

    case IR_JMP_IF_ZERO:
    case IR_JMP_IF_NOT_ZERO: {
        bool jz = in->op == IR_JMP_IF_ZERO;
        Opnd o = get_opnd(c, in->src1);
        if (o.k == O_IMM) {
            bool zero = canon_imm(o.imm, in->src1.type) == 0;
            if (zero == jz) EM("jmp .L%d\n", in->dest.as.label_id);
            break;
        }
        if (is_fp(in->src1.type)) {
            int xr = xreg_of(c, in->src1, 0);
            int id = c->local_label++;
            EM("xorps xmm1, xmm1\n");
            EM("ucomis%c xmm%d, xmm1\n", tsize(in->src1.type) == 8 ? 'd' : 's', xr);
            if (jz) { EM("jp .Lx%d\n", id); EM("je .L%d\n", in->dest.as.label_id); fprintf(c->out, ".Lx%d:\n", id); }
            else    { EM("jp .L%d\n", in->dest.as.label_id); EM("jne .L%d\n", in->dest.as.label_id); }
            break;
        }
        emit_test(c, in->src1);
        EM("%s .L%d\n", jz ? "je" : "jne", in->dest.as.label_id);
        break;
    }

    case IR_JMP_LT: case IR_JMP_LE: case IR_JMP_GT:
    case IR_JMP_GE: case IR_JMP_EQ: case IR_JMP_NE:
        emit_cmp(c, in->src1, in->src2);
        EM("j%s .L%d\n", cc_name(in->op - IR_JMP_LT, is_sg(in->src1.type)), in->dest.as.label_id);
        break;

    case IR_RETURN:
        if (in->src1.kind != IR_VAL_NONE) {
            if (is_fp(in->src1.type)) load_xmm(c, in->src1, 0);
            else load_gp(c, in->src1, RAX);
        }
        if (idx + 1 != fn->end) EM("jmp .Lret\n");
        break;

    case IR_ARG:       emit_arg(c, in); break;
    case IR_ARG_STACK: emit_arg_stack(c, in); break;
    case IR_CALL:      emit_call(c, in); break;

    case IR_LOAD_INDIRECT:
    case IR_ATOMIC_LOAD:  emit_load(c, in); break;
    case IR_STORE_INDIRECT: emit_store(c, in); break;

    case IR_ALLOCA:     emit_alloca(c, in); break;
    case IR_STACK_SAVE:
        EM("mov rax, rsp\n");
        store_gp(c, in->dest, RAX);
        break;
    case IR_STACK_RESTORE: {
        Opnd o = get_opnd(c, in->src1);
        if (o.k == O_REG) EM("mov rsp, %s\n", GP64[o.reg]);
        else { load_gp(c, in->src1, RAX); EM("mov rsp, rax\n"); }
        break;
    }
    case IR_MEMZERO: emit_memzero(c, in); break;

    case IR_FENCE: EM("mfence\n"); break;
    case IR_ATOMIC_STORE: case IR_ATOMIC_XCHG: case IR_ATOMIC_CAS:
    case IR_ATOMIC_ADD: case IR_ATOMIC_AND: case IR_ATOMIC_OR: case IR_ATOMIC_XOR:
        emit_atomic(c, in);
        break;
    }
}

typedef struct {
    int start, end, weight;
    uint8_t used, xmm, nopromo;
} Iv;

typedef struct {
    int kind;
    int id;
    int start, end, weight;
    bool xmm, crosses;
    int reg;
} Cand;

static void touch(Iv* iv, int i, int w, bool xmm) {
    if (!iv->used) { iv->used = 1; iv->start = iv->end = i; }
    else { if (i < iv->start) iv->start = i; if (i > iv->end) iv->end = i; }
    iv->weight += w;
    iv->xmm = xmm;
}

static int cand_cmp(const void* a, const void* b) {
    const Cand* x = a; const Cand* y = b;
    if (x->start != y->start) return x->start - y->start;
    return y->weight - x->weight;
}

static int align_up(int v, int a) { return (v + a - 1) / a * a; }

static void alloc_function(CodegenContext* c, IrFunctionRange* fn) {
    IrInstruction* ins = c->ir->instructions.items + fn->start;
    int n = (int)(fn->end - fn->start);
    int nt = fn->temp_count, ns = fn->slot_count;
    int nlab = c->ir->next_label + 1;

    c->temp_loc = calloc((size_t)nt + 1, sizeof(CgLoc));
    c->slot_loc = calloc((size_t)ns + 1, sizeof(CgLoc));
    c->slot_off = calloc((size_t)ns + 1, sizeof(int32_t));

    Iv* tv = calloc((size_t)nt + 1, sizeof(Iv));
    Iv* sv = calloc((size_t)ns + 1, sizeof(Iv));
    int* lpos = malloc((size_t)nlab * sizeof(int));
    int* depth = calloc((size_t)n + 2, sizeof(int));
    int* cp = calloc((size_t)n + 2, sizeof(int));
    bool* bcold = calloc((size_t)n + 1, sizeof(bool));
    int* bj = malloc(((size_t)n + 1) * sizeof(int));
    int* bi = malloc(((size_t)n + 1) * sizeof(int));
    int nb = 0, max_stack = 0;
    bool has_call = false;

    for (int i = 0; i < nlab; ++i) lpos[i] = -1;

    for (int i = 0; i < n; ++i) {
        IrInstruction* in = &ins[i];
        cp[i + 1] = cp[i] + (in->op == IR_CALL);
        if (in->op == IR_CALL) { has_call = true; if (in->aux > max_stack) max_stack = in->aux; }
        if (in->op == IR_ARG_STACK && in->aux > max_stack) max_stack = in->aux;
        if (in->op == IR_LABEL && in->dest.as.label_id >= 0 && in->dest.as.label_id < nlab) lpos[in->dest.as.label_id] = i;
    }

    for (int i = 0; i < n; ++i) {
        IrOperators op = ins[i].op;
        bool jump = op == IR_JMP || op == IR_JMP_IF_ZERO || op == IR_JMP_IF_NOT_ZERO || (op >= IR_JMP_LT && op <= IR_JMP_NE);
        if (!jump) continue;
        int lb = ins[i].dest.as.label_id;
        if (lb < 0 || lb >= nlab) continue;
        int j = lpos[lb];
        if (j >= 0 && j <= i) {
            bj[nb] = j;
            bi[nb] = i;
            bcold[nb] = (ins[i].flags & IRF_COLD_JMP) != 0;
            nb++;
        }
    }

    for (int k = 0; k < nb; ++k)
        if (!bcold[k])
            for (int i = bj[k]; i <= bi[k]; ++i) depth[i]++;

    for (int i = 0; i < n; ++i) {
        IrInstruction* in = &ins[i];
        IrValue* vs[4] = { &in->dest, &in->src1, &in->src2, &in->src3 };
        int w = 1;
        for (int d = 0; d < depth[i] && w < 4096; ++d) w *= 8;
        for (int k = 0; k < 4; ++k) {
            IrValue* v = vs[k];
            if (v->kind == IR_VAL_TEMP && v->as.temp_id >= 0 && v->as.temp_id < nt) {
                touch(&tv[v->as.temp_id], i, w, is_fp(v->type));
            } else if ((v->kind == IR_VAL_SLOT || v->kind == IR_VAL_SLOT_ADDR) && v->as.slot_id >= 0 && v->as.slot_id < ns) {
                int s = v->as.slot_id;
                touch(&sv[s], i, w, is_fp(fn->slots[s].type));
                if (v->kind == IR_VAL_SLOT_ADDR || v->field_offset != 0 ||
                    tsize(v->type) != (int)fn->slots[s].size || v->type.is_atomic)
                    sv[s].nopromo = 1;
            }
        }
    }
    for (int s = 0; s < fn->param_count && s < ns; ++s) {
        if (!sv[s].used) { sv[s].used = 1; sv[s].end = 0; }
        sv[s].start = 0;
        sv[s].xmm = is_fp(fn->slots[s].type);
    }

    bool ch;
    int guard = 0;
    do {
        ch = false;
        for (int k = 0; k < nb; ++k) {
            int j = bj[k], i = bi[k];
            for (int t = 0; t < nt; ++t)
                if (tv[t].used && tv[t].start < j && tv[t].end >= j && tv[t].end < i) { tv[t].end = i; ch = true; }
            for (int s = 0; s < ns; ++s) {
                if (!sv[s].used) continue;
                if (sv[s].start <= i && sv[s].end >= j && (sv[s].start > j || sv[s].end < i)) {
                    if (j < sv[s].start) sv[s].start = j;
                    if (i > sv[s].end) sv[s].end = i;
                    ch = true;
                }
            }
        }
    } while (ch && ++guard < 16);

    Cand* cs = malloc(((size_t)nt + (size_t)ns + 1) * sizeof(Cand));
    int nc = 0;
    for (int t = 0; t < nt; ++t) {
        if (!tv[t].used) continue;
        cs[nc++] = (Cand){ 0, t, tv[t].start, tv[t].end, tv[t].weight, tv[t].xmm, cp[tv[t].end] - cp[tv[t].start + 1] > 0, -1 };
    }
    for (int s = 0; s < ns; ++s) {
        IrSlotInfo* si = &fn->slots[s];
        int sz = (int)si->size;
        if (!sv[s].used || sv[s].nopromo || si->is_array || si->type.is_atomic) continue;
        if (!(sz == 1 || sz == 2 || sz == 4 || sz == 8)) continue;
        if (is_fp(si->type) && sz < 4) continue;
        cs[nc++] = (Cand){ 1, s, sv[s].start, sv[s].end, sv[s].weight, is_fp(si->type), cp[sv[s].end] - cp[sv[s].start + 1] > 0, -1 };
    }
    qsort(cs, (size_t)nc, sizeof(Cand), cand_cmp);

    int gown[16], xown[16];
    for (int i = 0; i < 16; ++i) gown[i] = xown[i] = -1;

    for (int k = 0; k < nc; ++k) {
        for (int r = 0; r < 16; ++r) {
            if (gown[r] >= 0 && cs[gown[r]].end < cs[k].start) gown[r] = -1;
            if (xown[r] >= 0 && cs[xown[r]].end < cs[k].start) xown[r] = -1;
        }
        int* own = cs[k].xmm ? xown : gown;
        const int* vol = cs[k].xmm ? POOL_XMM_VOL : POOL_GP_VOL;
        int nvol = cs[k].xmm ? COUNT(POOL_XMM_VOL) : COUNT(POOL_GP_VOL);
        const int* cal = cs[k].xmm ? POOL_XMM_CALLEE : POOL_GP_CALLEE;
        int ncal = cs[k].xmm ? COUNT(POOL_XMM_CALLEE) : COUNT(POOL_GP_CALLEE);
        int got = -1;

        if (!cs[k].crosses) for (int i = 0; i < nvol && got < 0; ++i) if (own[vol[i]] < 0) got = vol[i];
        for (int i = 0; i < ncal && got < 0; ++i) if (own[cal[i]] < 0) got = cal[i];

        if (got < 0) {
            int best = -1;
            for (int pass = 0; pass < 2; ++pass) {
                const int* pool = pass == 0 ? cal : vol;
                int np = pass == 0 ? ncal : nvol;
                if (pass == 1 && cs[k].crosses) break;
                for (int i = 0; i < np; ++i) {
                    int o = own[pool[i]];
                    if (o >= 0 && cs[o].weight < cs[k].weight && (best < 0 || cs[o].weight < cs[own[best]].weight)) best = pool[i];
                }
            }
            if (best >= 0) { cs[own[best]].reg = -1; got = best; }
        }
        if (got >= 0) { own[got] = k; cs[k].reg = got; }
    }

    c->saved_gp = c->saved_xmm = 0;
    for (int k = 0; k < nc; ++k) {
        if (cs[k].reg < 0) continue;
        CgLoc l = { CG_LOC_REG, (uint8_t)cs[k].xmm, (uint8_t)cs[k].reg, 0 };
        if (cs[k].kind == 0) c->temp_loc[cs[k].id] = l; else c->slot_loc[cs[k].id] = l;
        if (cs[k].xmm) { if (cs[k].reg >= 6) c->saved_xmm |= 1u << cs[k].reg; }
        else if (cs[k].reg != R10) c->saved_gp |= 1u << cs[k].reg;
    }

    /* layout do frame */
    int off = 0;
    for (int r = 0; r < 16; ++r) if (c->saved_gp & (1u << r))  { off += 8;  c->saved_gp_off[r] = -off; }
    for (int r = 0; r < 16; ++r) if (c->saved_xmm & (1u << r)) { off += 16; c->saved_xmm_off[r] = -off; }

    for (int s = 0; s < ns; ++s) {
        if (c->slot_loc[s].kind == CG_LOC_REG) continue;
        c->slot_loc[s].kind = CG_LOC_MEM;
        if (s < fn->param_count) { c->slot_off[s] = 16 + 8 * s; continue; }
        IrSlotInfo* si = &fn->slots[s];
        int al = si->align ? (int)si->align : 8;
        int sz = si->size ? (int)si->size : 1;
        if (al > 16) al = 16;
        off = align_up(off + sz, al);
        c->slot_off[s] = -off;
    }
    for (int t = 0; t < nt; ++t) {
        if (!tv[t].used || c->temp_loc[t].kind == CG_LOC_REG) continue;
        off = align_up(off + 8, 8);
        c->temp_loc[t].kind = CG_LOC_MEM;
        c->temp_loc[t].xmm = tv[t].xmm;
        c->temp_loc[t].off = -off;
    }

    c->out_area = (has_call || fn->has_calls) ? 32 + 8 * max_stack : 0;
    c->frame_size = align_up(off + c->out_area, 16);

     free(tv); free(sv); free(lpos); free(depth); free(cp); free(bj); free(bi); free(bcold); free(cs);
}

static void emit_prologue(CodegenContext* c, IrFunctionRange* fn) {
    char b[200];
    EM("push rbp\n");
    EM("mov rbp, rsp\n");

    IrFuncInfo* finfo = find_func(c, fn->func_id);
    if (finfo && finfo->variadic)
        for (int i = fn->param_count; i < 4; ++i)
            EM("mov [rbp+%d], %s\n", 16 + 8 * i, GP64[ARG_GP[i]]);

    if (c->frame_size) {
        EM("sub rsp, %d\n", c->frame_size);
        for (int off = 4096; off < c->frame_size; off += 4096) EM("test [rsp+%d], al\n", c->frame_size - off);
        if (c->frame_size >= 4096) EM("test [rsp], al\n");
    }

    for (int r = 0; r < 16; ++r) if (c->saved_gp & (1u << r))  EM("mov [rbp%+d], %s\n", c->saved_gp_off[r], GP64[r]);
    for (int r = 0; r < 16; ++r) if (c->saved_xmm & (1u << r)) EM("movups [rbp%+d], xmm%d\n", c->saved_xmm_off[r], r);

    for (int i = 0; i < fn->param_count && i < fn->slot_count; ++i) {
        CgLoc l = c->slot_loc[i];
        IrType t = fn->slots[i].type;
        int sz = tsize(t);
        bool fp = is_fp(t);
        Mem home; memset(&home, 0, sizeof home); home.base = RBP; home.disp = 16 + 8 * i;

        if (l.kind == CG_LOC_REG) {
            if (i < 4) {
                if (fp) { EM("movaps xmm%d, xmm%d\n", l.reg, i); }
                else if (sz == 1) EM("%s %s, %s\n", is_sg(t) ? "movsx" : "movzx", GP32[l.reg], GP8[ARG_GP[i]]);
                else if (sz == 2) EM("%s %s, %s\n", is_sg(t) ? "movsx" : "movzx", GP32[l.reg], GP16[ARG_GP[i]]);
                else if (sz == 4) EM("mov %s, %s\n", GP32[l.reg], GP32[ARG_GP[i]]);
                else EM("mov %s, %s\n", GP64[l.reg], GP64[ARG_GP[i]]);
            } else if (fp) {
                mfmt(b, sizeof b, &home, sz, 0);
                EM("%s xmm%d, %s\n", sz == 8 ? "movsd" : "movss", l.reg, b);
            } else {
                load_mem(c, l.reg, &home, t);
            }
        } else if (i < 4) {
            mfmt(b, sizeof b, &home, sz, 0);
            if (fp) EM("%s %s, xmm%d\n", sz == 8 ? "movsd" : "movss", b, i);
            else    EM("mov %s, %s\n", b, gp(ARG_GP[i], sz));
        }
    }
}

static void emit_function(CodegenContext* c, IrFunctionRange* fn) {
    IrFuncInfo* info = find_func(c, fn->func_id);
    char sym[200];
    if (info) snprintf(sym, sizeof sym, "$%.*s", (int)info->name.len, info->name.start);
    else snprintf(sym, sizeof sym, "$__func_%d", fn->func_id);

    bool is_entry = false;
    if (info && c->entry && c->entry->kind == NODE_FUNC_DECL)
        is_entry = view_eq(info->name, c->entry->ast.func_decl.name);

    c->fn = fn;
    c->stack_arg_seen = 0;
    alloc_function(c, fn);

    fprintf(c->out, "\nalign 16\n");
    fprintf(c->out, "%s:\n", sym);
    emit_prologue(c, fn);

    for (size_t i = fn->start; i < fn->end; ++i) {
        c->cur = i;
        emit_instr(c, &c->ir->instructions.items[i], i);
    }

    if (is_entry && (fn->end == fn->start || c->ir->instructions.items[fn->end - 1].op != IR_RETURN)) EM("xor eax, eax\n");
    fprintf(c->out, ".Lret:\n");
    for (int r = 0; r < 16; ++r) if (c->saved_xmm & (1u << r)) EM("movups xmm%d, [rbp%+d]\n", r, c->saved_xmm_off[r]);
    for (int r = 0; r < 16; ++r) if (c->saved_gp & (1u << r))  EM("mov %s, [rbp%+d]\n", GP64[r], c->saved_gp_off[r]);
    EM("leave\n");
    EM("ret\n");

    free(c->temp_loc); free(c->slot_loc); free(c->slot_off);
    c->temp_loc = NULL; c->slot_loc = NULL; c->slot_off = NULL;
}

static void emit_string(CodegenContext* c, IrStringEntry* e) {
    /* o IR ja entrega os bytes finais (sem aspas, sem escapes) */
    const unsigned char* p = (const unsigned char*)e->text.start;
    size_t len = e->text.len;

    fprintf(c->out, "__str%d: db ", e->id);
    int col = 0;
    for (size_t i = 0; i < len; ++i) {
        fprintf(c->out, "%d,", p[i]);
        if (++col % 24 == 0) fprintf(c->out, " \\\n    ");
    }
    fprintf(c->out, "0\n");
}

static const char* dsz(int sz) {
    return sz == 1 ? "db" : sz == 2 ? "dw" : sz == 4 ? "dd" : "dq";
}

static void emit_zero_pad(CodegenContext* c, int n) {
    if (n > 0) fprintf(c->out, "    times %d db 0\n", n);
}

static bool emit_data_value(CodegenContext* c, IrValue v, int sz) {
    switch (v.kind) {
    case IR_VAL_CONST_INT:
    case IR_VAL_CONST_UINT: {
        if (sz != 1 && sz != 2 && sz != 4 && sz != 8) return false;
        long long k = v.kind == IR_VAL_CONST_INT ? v.as.const_i : (long long)v.as.const_ui;
        unsigned long long mask = sz == 8 ? ~0ULL : ((1ULL << (sz * 8)) - 1);
        fprintf(c->out, "    %s 0x%llx\n", dsz(sz), (unsigned long long)k & mask);
        return true;
    }
    case IR_VAL_CONST_FLOAT: {
        double val; bool single;
        if (!float_info(c, v.as.float_id, &val, &single)) return false;
        if (sz == 4) { float f = (float)val; uint32_t u; memcpy(&u, &f, 4); fprintf(c->out, "    dd 0x%x\n", u); return true; }
        if (sz == 8) { uint64_t u; memcpy(&u, &val, 8); fprintf(c->out, "    dq 0x%llx\n", (unsigned long long)u); return true; }
        return false;
    }
    case IR_VAL_CONST_STRING:
        if (sz != 8) return false;
        fprintf(c->out, "    dq __str%d\n", v.as.string_id);
        return true;
    case IR_VAL_GLOBAL_ADDR: {
        IrGlobal* t = find_global(c, v.as.global_id);
        if (!t || sz != 8) return false;
        fprintf(c->out, "    dq $%.*s%+d\n", (int)t->name.len, t->name.start, v.field_offset);
        return true;
    }
    case IR_VAL_FUNC: {
        if (sz != 8) return false;
        char b[200]; func_sym(c, v.as.func_id, false, b, sizeof b);
        fprintf(c->out, "    dq %s\n", b);
        return true;
    }
    default: return false;
    }
}

static void emit_db_bytes(CodegenContext* c, const unsigned char* p, size_t len) {
    for (size_t i = 0; i < len; i += 32) {
        size_t chunk = len - i < 32 ? len - i : 32;
        fprintf(c->out, "    db ");
        for (size_t j = 0; j < chunk; ++j) {
            if (j) fprintf(c->out, ",%d", p[i + j]);
            else   fprintf(c->out, "%d", p[i + j]);
        }
        fprintf(c->out, "\n");
    }
}

static void emit_global_items(CodegenContext* c, IrGlobal* g, int total) {
    int pos = 0;

    for (size_t i = 0; i < g->init_count; ++i) {
        const IrInitItem* it = &g->init_items[i];
        int off = (int)it->offset, size = (int)it->size, done = 0;

        emit_zero_pad(c, off - pos);

        if (it->is_bytes) {
            done = it->bytes.len < (size_t)size ? (int)it->bytes.len : size;
            emit_db_bytes(c, (const unsigned char*)it->bytes.start, (size_t)done);
        } else if (emit_data_value(c, it->value, size)) {
            done = size;
        } else {
            c->unsupported++;
        }

        emit_zero_pad(c, size - done);
        pos = off + size;
    }

    emit_zero_pad(c, total - pos);
}

static void emit_global_init(CodegenContext* c, IrGlobal* g) {
    int sz = g->size ? (int)g->size : tsize(g->type);

    if (g->init_count) { emit_global_items(c, g, sz); return; }

    int done = emit_data_value(c, g->init, sz) ? sz : 0;
    if (done == 0) c->unsupported++;
    emit_zero_pad(c, sz - done);
}

static void emit_data(CodegenContext* c) {
    IrGenContext* ir = c->ir;

    fprintf(c->out, "\nsection .rdata align=16\n");
    fprintf(c->out, "align 16\n__fsgn32: dd 0x80000000, 0, 0, 0\n");
    fprintf(c->out, "align 16\n__fsgn64: dq 0x8000000000000000, 0\n");
    for (size_t i = 0; i < ir->floats.count; ++i) {
        IrFloatEntry* f = &ir->floats.items[i];
        if (f->_single) { float x = (float)f->value; uint32_t u; memcpy(&u, &x, 4); fprintf(c->out, "align 4\n__flt%d: dd 0x%x\n", f->id, u); }
        else { uint64_t u; memcpy(&u, &f->value, 8); fprintf(c->out, "align 8\n__flt%d: dq 0x%llx\n", f->id, (unsigned long long)u); }
    }
    for (size_t i = 0; i < ir->strings.count; ++i) emit_string(c, &ir->strings.items[i]);

    for (int pass = 0; pass < 3; ++pass) {
        bool header = false;
        for (size_t i = 0; i < ir->globals.count; ++i) {
            IrGlobal* g = ir->globals.items[i];
            if (!g || g->_extern) continue;
            int p = g->has_init ? (g->_const ? 0 : 1) : 2;
            if (p != pass) continue;
            if (!header) {
                fprintf(c->out, "\nsection %s align=16\n", pass == 0 ? ".rdata" : pass == 1 ? ".data" : ".bss");
                header = true;
            }
            int sz = g->size ? (int)g->size : tsize(g->type);
            int al = g->align ? (int)g->align : 8;
            if (al > 16) al = 16;
            fprintf(c->out, "%s %d\n$%.*s:", pass == 2 ? "alignb" : "align", al, (int)g->name.len, g->name.start);
            if (pass == 2) fprintf(c->out, " resb %d\n", sz ? sz : 1);
            else { fprintf(c->out, "\n"); emit_global_init(c, g); }
        }
    }
}

CodegenContext create_codegen(FILE* out, Arena *arena, Node *entry, CheckContext *check_ctx) {
    CodegenContext c;
    memset(&c, 0, sizeof c);
    c.out = out;
    c.arena = arena;
    c.entry = entry;
    c.check_ctx = check_ctx;
    return c;
}

static void mark_use(bool* used, int n, const IrValue* v, IrValueKind k1, IrValueKind k2) {
    if ((v->kind == k1 || v->kind == k2) && v->as.func_id >= 0 && v->as.func_id < n)
        used[v->as.func_id] = true;
}

static int max_id_funcs(IrGenContext* ir) {
    int n = 0;
    for (size_t i = 0; i < ir->funcs.count; ++i)
        if (ir->funcs.items[i] && ir->funcs.items[i]->func_id + 1 > n) n = ir->funcs.items[i]->func_id + 1;
    return n;
}

static int max_id_globals(IrGenContext* ir) {
    int n = 0;
    for (size_t i = 0; i < ir->globals.count; ++i)
        if (ir->globals.items[i] && ir->globals.items[i]->id + 1 > n) n = ir->globals.items[i]->id + 1;
    return n;
}

/* Ponto de entrada real do processo (win64, sem CRT).
 * O Windows chama o entry sem argc/argv, entao montamos o argv a partir de
 * GetCommandLineA() e chamamos a funcao `start` do usuario como start(argc, argv).
 * O valor de retorno de start vira o exit code via ExitProcess.
 * So usa registradores volateis (rax, rcx, rdx, r8-r11). */
static void emit_entry_stub(CodegenContext* c) {
    if (!c->entry || c->entry->kind != NODE_FUNC_DECL) return;
    View n = c->entry->ast.func_decl.name;

    fprintf(c->out,
        "\nextern GetCommandLineA\nextern ExitProcess\nglobal __luegiu_entry\n"
        "section .text\nalign 16\n__luegiu_entry:\n"
        "    push rbp\n    mov rbp, rsp\n    sub rsp, 32\n"
        "    call GetCommandLineA\n"
        "    mov r11, rax\n"
        "    lea r10, [__luegiu_argbuf]\n"
        "    lea r8, [__luegiu_argv]\n"
        "    xor r9d, r9d\n"
        ".next:\n"
        "    movzx eax, byte [r11]\n"
        "    test al, al\n    jz .done\n"
        "    cmp al, ' '\n    je .ws\n"
        "    cmp al, 9\n    je .ws\n"
        "    cmp r9, 255\n    jae .done\n"
        "    mov [r8 + r9*8], r10\n"
        "    inc r9\n"
        "    xor ecx, ecx\n"
        ".tok:\n"
        "    movzx eax, byte [r11]\n"
        "    test al, al\n    jz .endtok\n"
        "    cmp al, '\"'\n    jne .notq\n"
        "    xor ecx, 1\n    inc r11\n    jmp .tok\n"
        ".notq:\n"
        "    test ecx, ecx\n    jnz .copy\n"
        "    cmp al, ' '\n    je .endtok\n"
        "    cmp al, 9\n    je .endtok\n"
        ".copy:\n"
        "    mov [r10], al\n    inc r10\n    inc r11\n    jmp .tok\n"
        ".endtok:\n"
        "    mov byte [r10], 0\n    inc r10\n    jmp .next\n"
        ".ws:\n"
        "    inc r11\n    jmp .next\n"
        ".done:\n"
        "    mov qword [r8 + r9*8], 0\n"
        "    mov rcx, r9\n"
        "    mov rdx, r8\n");
    fprintf(c->out, "    call $%.*s\n", (int)n.len, n.start);
    fprintf(c->out,
        "    mov ecx, eax\n"
        "    call ExitProcess\n"
        "    ud2\n"
        "\nsection .bss align=16\n"
        "__luegiu_argbuf: resb 65536\n"
        "__luegiu_argv: resq 256\n");
}

void emit_program(CodegenContext *c, IrGenContext *ir) {
    c->ir = ir;
    fprintf(c->out, "; gerado pelo codegen luegiu (win64 / nasm)\nbits 64\ndefault rel\n\n");

    int nf = max_id_funcs(ir), ng = max_id_globals(ir);
    bool* uf = calloc((size_t)nf + 1, sizeof(bool));
    bool* ug = calloc((size_t)ng + 1, sizeof(bool));
    if (!uf || !ug) { fprintf(stderr, "Codegen: falha ao alocar tabela de uso\n"); exit(ALLOCATE_ERROR); }

    for (size_t i = 0; i < ir->instructions.count; ++i) {
        IrValue* vs[4] = { &ir->instructions.items[i].dest, &ir->instructions.items[i].src1,
                           &ir->instructions.items[i].src2, &ir->instructions.items[i].src3 };
        for (int k = 0; k < 4; ++k) {
            mark_use(uf, nf, vs[k], IR_VAL_FUNC, IR_VAL_BUILTIN);
            mark_use(ug, ng, vs[k], IR_VAL_GLOBAL, IR_VAL_GLOBAL_ADDR);
        }
    }

    for (size_t i = 0; i < ir->globals.count; ++i) {
        IrGlobal* g = ir->globals.items[i];
        if (!g || !g->has_init) continue;
        mark_use(uf, nf, &g->init, IR_VAL_FUNC, IR_VAL_BUILTIN);
        mark_use(ug, ng, &g->init, IR_VAL_GLOBAL, IR_VAL_GLOBAL_ADDR);
        for (size_t k = 0; k < g->init_count; ++k) {
            mark_use(uf, nf, &g->init_items[k].value, IR_VAL_FUNC, IR_VAL_BUILTIN);
            mark_use(ug, ng, &g->init_items[k].value, IR_VAL_GLOBAL, IR_VAL_GLOBAL_ADDR);
        }
    }

    for (size_t i = 0; i < ir->funcs.count; ++i) {
        IrFuncInfo* f = ir->funcs.items[i];
        if (!f) continue;
        if (f->defined) {
            if (!f->_static) fprintf(c->out, "global $%.*s\n", (int)f->name.len, f->name.start);
        } else if (uf[f->func_id]) {
            fprintf(c->out, "extern $%.*s\n", (int)f->name.len, f->name.start);
        }
    }

    for (size_t i = 0; i < ir->globals.count; ++i) {
        IrGlobal* g = ir->globals.items[i];
        if (!g) continue;
        if (g->_extern) {
            if (ug[g->id]) fprintf(c->out, "extern $%.*s\n", (int)g->name.len, g->name.start);
        } else if (!g->_static) {
            fprintf(c->out, "global $%.*s\n", (int)g->name.len, g->name.start);
        }
    }

    free(uf);
    free(ug);

    fprintf(c->out, "\nsection .text\n");
    for (size_t i = 0; i < ir->functions.count; ++i) emit_function(c, &ir->functions.items[i]);

    emit_data(c);
    emit_entry_stub(c);

    if (c->unsupported)
        fprintf(c->out, "\n; AVISO: %zu construcao(oes) nao suportada(s) pelo codegen\n", c->unsupported);
}