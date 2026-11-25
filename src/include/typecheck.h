#pragma once

#include "types.h"
#include "parser.h"
#include "common.h"
#include "diagnostics.h"
#include "string_helper.h"
#include <limits.h>

#define SCOPE_TABLE_INIT_SIZE 8
#define DEFAULT_INIT_SIZE 32
#define TABLE_LOAD_FACTOR 0.8

#define VIEW_ARG(v) (int)(v).len, (v).start
#define SEMA_ERR(n, ...)  diag_error(ctx->ctx, (n)->filepath, (n)->line, (n)->col, __VA_ARGS__)
#define SEMA_WARN(n, ...) diag_warning(ctx->ctx, (n)->filepath, (n)->line, (n)->col, __VA_ARGS__)
#define SEMA_ERRC(n, code, ...) diag_error_code(ctx->ctx, (n)->filepath, (n)->line, (n)->col, (code), __VA_ARGS__)
#define SEMA_NOTE(n, ...)       diag_note(ctx->ctx, (n)->filepath, (n)->line, (n)->col, __VA_ARGS__)

#define TABLE_DEF(T) \
    struct { \
        T items; \
        size_t count; \
        size_t capacity; \
    }

typedef struct AggregateDef AggregateDef;

typedef enum {
    COMPAT_OK,
    COMPAT_ERROR,
    COMPAT_WARNING
} CompatResult;

typedef enum {
    GLOBAL_NEW, GLOBAL_MERGED, 
    GLOBAL_REDEFINED, GLOBAL_TYPE_MISMATCH
} GlobalDeclStatus;

typedef enum {
    TYPE_ENTRY_ALIAS,
    TYPE_ENTRY_AGGREGATE,
    TYPE_ENTRY_ENUM,
} TypeEntryKind;

enum { 
    INTR_VDIFF16 = 1, INTR_VEQ64, INTR_CTZ,
    INTR_VCOPY16X2, INTR_VCOPY32X2, INTR_VCOPY64, 
    INTR_REPMOVSB, INTR_VNULL16, INTR_VNULL64,
    INTR_VFILL16X2, INTR_VFILL32X2, INTR_VFILL64, 
    INTR_REPSTOSB,
    INTR_VFIND16, INTR_VFIND64, INTR_CLZ,
    INTR_MEM_BARRIER, INTR_VFILL64X2, INTR_VFINDW16,
    INTR_VCMPZ16, INTR_VFINDZ16, INTR_VANYZ64,
    INTR_UMULH, INTR_FPBITS, INTR_BITSD, 
    INTR_BITSF
};

typedef struct {
    long long value;
    bool big;
} ConstInt;

typedef struct {
    bool known;
    LiteralKind lkind;   /* INT, UNSIGNED, FLOAT ou DOUBLE: como o valor vira literal */
    ConstInt i;
    long double f;
} ConstValue;

typedef struct {
    View name;
    Typecheck type;
    size_t offset;
    size_t size;
} FieldInfo;

typedef struct { 
    const char* name; 
    int id;
    int argc; 
    TokenType ret; 
} IntrinInfo;

struct AggregateDef {
    NodeKinds kind;
    FieldInfo* fields;
    size_t count;
    size_t size;
    size_t align;
    bool complete;
    bool flexible;
};

typedef struct EnumConstEntry {
    View name;
    struct EnumConstEntry* next;
    View enum_name;
    long long value;
    Node* decl_node;
} EnumConstEntry;

typedef struct { 
    EnumConstEntry** items; 
    size_t count; 
    size_t capacity; 
} EnumConstTable;

typedef struct {
    LiteralKind kind;
    View name;
    unsigned long long ivalue;
} EnumMemberInfo;

typedef struct {
    EnumMemberInfo* members;
    size_t member_count;
} EnumDef;

typedef struct TypeEntry {
    View name;
    TypeEntryKind kind;

    union {
        Typecheck alias;
        AggregateDef aggregate;
        EnumDef enumdef;
    };

    struct TypeEntry* next;
} TypeEntry;

typedef struct {
    Typecheck call_type;
    Typecheck* param_types;
    size_t param_count;
    bool variadic;
} FunctionSignature;

typedef struct FuncEntry {
    View name;
    FunctionSignature signature;
    bool _static;
    bool _extern;
    bool inline__;
    bool _pending;
    bool _builtin;
    bool _kernel;
    bool has_body;
    size_t call_count;
    Node* first_call;
    Node* decl_node;
    const char* file;
    struct FuncEntry* next;
} FuncEntry;

typedef struct SymbolEntry {
    View name;
    Typecheck type;
    ConstValue cval;
    struct SymbolEntry* next;
} SymbolEntry;

typedef struct GlobalEntry {
    View name;
    Typecheck type;
    bool _static;
    bool _extern;
    bool is_const;
    bool has_init;
    ConstValue cval;
    Node* decl_node;
    struct GlobalEntry* next;
} GlobalEntry;

typedef struct {
    InitItem* items;
    size_t count;
    size_t capacity;
    size_t payload;     /* soma de item.size */
    size_t high;        /* maior (offset + size) ja emitido: caminho rapido sem sobreposicao */
    bool unsorted;
} InitBuilder;

typedef struct LabelEntry {
    View name;
    Node* first_jump;
    bool _defined;
    Node* decl_node;
    struct LabelEntry* next;
} LabelEntry;

typedef struct {
    View name;
    Typecheck type;
} Symbol;

typedef TABLE_DEF(SymbolEntry**) SymbolTable;
typedef TABLE_DEF(FuncEntry**) FuncTable;
typedef TABLE_DEF(TypeEntry**) TypeTable;
typedef TABLE_DEF(GlobalEntry**) GlobalTable;
typedef TABLE_DEF(LabelEntry**) LabelTable;
typedef TABLE_DEF(TypeEntry**) NewtypeTable;

#undef TABLE_DEF

typedef struct Scope {
    SymbolTable symbols;
    struct Scope* next;
} Scope;

typedef struct {
    Arena* arena;
    DiagnosticContext *ctx;
    Scope* current_scope;
    Typecheck current_call_type;
    bool in_function;
    bool in_loop;
    bool in_variadic;
    bool no_fold;        /* operando de &, ++, -- ou lado esquerdo de atribuicao: nao vira literal */
    Node* entry_function;
    bool kernel_mode;
} CheckContext;

CheckContext create_checkctx(Arena *arena, DiagnosticContext *ctx);
void check_program(CheckContext *ctx, Node *program);
void typecheck_finalize(CheckContext *ctx, bool require_entry);
void typecheck_session_init(Arena *arena);

void typecheck_globals_init(Arena *arena);
void typecheck_dump_functions(FILE *out);
Scope* scope_push(Arena *arena, Scope *parent);

SymbolEntry* scope_insert(Arena *arena, Scope *scope, View name, Typecheck type);
SymbolEntry* scope_lookup(Scope *scope, View name);
SymbolEntry* scope_lookup_chain(Scope *scope, View name);

FuncEntry* function_table_insert(View name);
FuncEntry* function_table_lookup(View name);

typedef void (*FuncTableVisitor)(FuncEntry *entry, void *userdata);
void function_table_foreach(FuncTableVisitor visitor, void *userdata);

TypeEntry* type_table_insert(View name);
TypeEntry* type_table_lookup(View name);

TypeEntry* newtype_table_insert(View name);
TypeEntry* newtype_table_lookup(View name);

GlobalEntry* global_var_decl(View name, Typecheck type, Node *decl_node, bool _static,
                             bool _extern, bool has_initializer, GlobalDeclStatus *status);
GlobalEntry* global_var_lookup(View name);
void typecheck_set_file(const char* file);

typedef void (*LabelTableVisitor)(LabelEntry *entry, void *userdata);
LabelEntry* label_declare(View name, Node *declare);
LabelEntry* label_reference(View name, Node *use);
void label_table_foreach(LabelTableVisitor visitor, void *userdata);
void label_table_reset(void);

bool resolve_variable(Scope *scope, View name, Typecheck *out);
uint64_t view_hash(View view);

size_t func_count(void);
FuncEntry* func_item(size_t i);

bool isarith_compoundop(TokenType op);
bool isbitwise_compoundop(TokenType op);
EnumConstEntry* enum_const_declare(View name);
EnumConstEntry* enum_const_lookup(View name);

bool type_equals(Typecheck first, Typecheck second);
char* format_signature(char* buf, size_t size, View name, const FunctionSignature* sig);

bool decode_char_literal(View v, unsigned long long *out);
bool isintegerbase(TokenType type);
bool isunsignedbase(TokenType type);
bool is_numeric_constant(Node *node);
bool fold_const_binary(CheckContext *ctx, Node *node, Typecheck result);
bool const_eval(Node *node, ConstInt *out);
bool is_prim_scalar(Typecheck type);
int integerwidth(TokenType type);
const char* tname(Typecheck t);
void const_wrap(ConstInt *c, Typecheck type);
size_t type_size_scalar(Typecheck type);
size_t type_align(Typecheck type);
size_t unescaped_length(View raw);
