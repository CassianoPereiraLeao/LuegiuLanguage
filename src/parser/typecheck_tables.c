#include "../include/typecheck.h"

static const View ENTRY_START = { "start", sizeof("start") - 1 };

typedef struct { 
    FuncEntry** list; 
    size_t n; 
} FuncCollect;

static bool same_file(const char* a, const char* b) {
    return a == b || (a && b && strcmp(a, b) == 0);
}

uint64_t view_hash(View view) {
    uint64_t hash = 14695981039346656037ULL;

    for(size_t i = 0; i < view.len; ++i) {
        hash ^= (unsigned char)view.start[i];
        hash *= 1099511628211ULL;
    }

    return hash;
}

#define DEFINE_HASH_TABLE(TableType, EntryType, ALIAS, INIT_SIZE) \
    typedef char ALIAS##_init_size_must_be_pow2[(((INIT_SIZE) > 0) && (((INIT_SIZE) & ((INIT_SIZE) - 1)) == 0)) ? 1 : -1]; \
    \
    static inline void ALIAS##_init(Arena *arena, TableType* table) { \
        table->capacity = (INIT_SIZE); \
        table->count = 0; \
        table->items = (EntryType**)arena_alloc(arena, sizeof(EntryType*) * (INIT_SIZE)); \
        memset(table->items, 0, sizeof(EntryType*) * (INIT_SIZE)); \
    } \
    \
    static void ALIAS##_rehash(Arena *arena, TableType* table) { \
        size_t new_cap = table->capacity * 2; \
        EntryType** new_items = (EntryType**)arena_alloc(arena, sizeof(EntryType*) * new_cap); \
        memset(new_items, 0, sizeof(EntryType*) * new_cap); \
        \
        for(size_t i = 0; i < table->capacity; ++i) { \
            EntryType* entry = table->items[i]; \
            while(entry) { \
                EntryType* next = entry->next; \
                uint64_t index = view_hash(entry->name) & (new_cap - 1); \
                entry->next = new_items[index]; \
                new_items[index] = entry; \
                entry = next; \
            } \
        } \
        table->items = new_items; \
        table->capacity = new_cap; \
    } \
    \
    static inline EntryType* ALIAS##_lookup_h(TableType *table, View name, uint64_t hash) { \
        if(table->capacity == 0) return NULL; \
        EntryType* entry = table->items[hash & (table->capacity - 1)]; \
        while(entry) { \
            if(svcmp(entry->name, name)) return entry; \
            entry = entry->next; \
        } \
        return NULL; \
    } \
    \
    static inline EntryType* ALIAS##_lookup(TableType *table, View name) { \
        if(table->capacity == 0) return NULL; \
        return ALIAS##_lookup_h(table, name, view_hash(name)); \
    } \
    \
    static inline EntryType* ALIAS##_insert_h(Arena *arena, TableType *table, View name, uint64_t hash) { \
        if(table->capacity == 0) { \
            ALIAS##_init(arena, table); \
        } else if((double)(table->count + 1) / (double)table->capacity >= TABLE_LOAD_FACTOR) { \
            ALIAS##_rehash(arena, table); \
        } \
        EntryType* entry = (EntryType*)arena_alloc(arena, sizeof(EntryType)); \
        memset(entry, 0, sizeof(EntryType)); \
        entry->name = name; \
        uint64_t index = hash & (table->capacity - 1); \
        entry->next = table->items[index]; \
        table->items[index] = entry; \
        table->count++; \
        return entry; \
    } \
    \
    static inline EntryType* ALIAS##_insert(Arena *arena, TableType *table, View name) { \
        return ALIAS##_insert_h(arena, table, name, view_hash(name)); \
    } \
    \
    static inline EntryType* ALIAS##_declare(Arena *arena, TableType *table, View name) { \
        uint64_t hash = view_hash(name); \
        if(ALIAS##_lookup_h(table, name, hash) != NULL) return NULL; \
        return ALIAS##_insert_h(arena, table, name, hash); \
    }

DEFINE_HASH_TABLE(SymbolTable, SymbolEntry, symtab, SCOPE_TABLE_INIT_SIZE)
DEFINE_HASH_TABLE(FuncTable, FuncEntry, functab, DEFAULT_INIT_SIZE)
DEFINE_HASH_TABLE(TypeTable, TypeEntry, typetab, DEFAULT_INIT_SIZE)
DEFINE_HASH_TABLE(GlobalTable, GlobalEntry, globaltab, DEFAULT_INIT_SIZE)
DEFINE_HASH_TABLE(LabelTable, LabelEntry, labeltab, DEFAULT_INIT_SIZE)
DEFINE_HASH_TABLE(NewtypeTable, TypeEntry, newtypetab, DEFAULT_INIT_SIZE)
DEFINE_HASH_TABLE(EnumConstTable, EnumConstEntry, enumconsttab, DEFAULT_INIT_SIZE)

#undef DEFINE_HASH_TABLE

static Arena* typecheck_arena = NULL;
static FuncTable g_functions = { 0 };
static TypeTable g_types = { 0 };
static GlobalTable g_globals = { 0 };
static LabelTable g_labels = { 0 };
static NewtypeTable g_newtypes = { 0 };
static EnumConstTable g_enum_consts = { 0 };
static const char* g_current_file = NULL;

void typecheck_set_file(const char* file) { g_current_file = file; }

void typecheck_globals_init(Arena *arena) {
    typecheck_arena = arena;

    memset(&g_functions, 0, sizeof(g_functions));
    memset(&g_types, 0, sizeof(g_types));
    memset(&g_globals, 0, sizeof(g_globals));
    memset(&g_labels, 0, sizeof(g_labels));
    memset(&g_newtypes, 0, sizeof(g_newtypes));
    memset(&g_enum_consts, 0, sizeof(g_enum_consts));

    functab_init(arena, &g_functions);
    typetab_init(arena, &g_types);
    globaltab_init(arena, &g_globals);
    labeltab_init(arena, &g_labels);
    newtypetab_init(arena, &g_newtypes);
    enumconsttab_init(arena, &g_enum_consts);
}

Scope* scope_push(Arena *arena, Scope *parent) {
    Scope* scope = (Scope*)arena_alloc(arena, sizeof(Scope));
    memset(scope, 0, sizeof(*scope));
    scope->next = parent;
    return scope;
}

SymbolEntry* scope_insert(Arena *arena, Scope *scope, View name, Typecheck type) {
    SymbolEntry* entry = symtab_declare(arena, &scope->symbols, name);
    if(!entry) return NULL;
    entry->type = type;
    return entry;
}

static void collect_func(FuncEntry *e, void *ud) { 
    FuncCollect* c = ud; 
    c->list[c->n++] = e; 
}

static int cmp_func(const void *a, const void *b) {
    const FuncEntry* x = *(FuncEntry* const*)a;
    const FuncEntry* y = *(FuncEntry* const*)b;
    size_t m = x->name.len < y->name.len ? x->name.len : y->name.len;
    int c = memcmp(x->name.start, y->name.start, m);
    if(c) return c;
    return (x->name.len > y->name.len) - (x->name.len < y->name.len);
}

void typecheck_dump_functions(FILE *out) {
    size_t total = func_count();
    FuncCollect col = { malloc(sizeof(FuncEntry*) * (total ? total : 1)), 0 };
    if(!col.list) return;

    function_table_foreach(collect_func, &col);
    qsort(col.list, col.n, sizeof(FuncEntry*), cmp_func);

    fprintf(out, "Tabela global de funcoes (%zu):\n", col.n);
    for(size_t i = 0; i < col.n; ++i) {
        FuncEntry* e = col.list[i];
        char sig[768];
        format_signature(sig, sizeof sig, e->name, &e->signature);

        const char* status = e->_builtin ? "builtin"
           : e->has_body ? "definida"
           : e->_extern ? "extern (resolvido pelo linker)" : "prototipo (sem definicao)";

        fprintf(out, "  %-52s [%s%s] chamadas=%zu", sig, status, e->_static ? ", static" : "", e->call_count);
        if(e->decl_node) fprintf(out, " | %s:%zu:%zu", e->decl_node->filepath, e->decl_node->line, e->decl_node->col);
        if(e->first_call) fprintf(out, " | 1a chamada %s:%zu:%zu", e->first_call->filepath, e->first_call->line, e->first_call->col);
        fprintf(out, "\n");
    }
    free(col.list);
}

SymbolEntry* scope_lookup(Scope *scope, View name) {
    return symtab_lookup(&scope->symbols, name);
}

SymbolEntry* scope_lookup_chain(Scope *scope, View name) {
    uint64_t hash = view_hash(name);

    for(; scope; scope = scope->next) {
        SymbolEntry* found = symtab_lookup_h(&scope->symbols, name, hash);
        if(found) return found;
    }

    return NULL;
}

FuncEntry* function_table_insert(View name) {
    FuncEntry* entry = functab_insert(typecheck_arena, &g_functions, name);
    entry->file = g_current_file;
    return entry;
}

FuncEntry* function_table_lookup(View name) {
    if(g_functions.capacity == 0) return NULL;

    FuncEntry* external = NULL;
    uint64_t hash = view_hash(name);

    for(FuncEntry* e = g_functions.items[hash& (g_functions.capacity - 1)]; e; e = e->next) {
        if(!svcmp(e->name, name)) continue;

        if(e->_static) {
            if(same_file(e->file, g_current_file)) return e;
        } else {
            external = e;
        }
    }

    return external;
}

void function_table_foreach(FuncTableVisitor visitor, void *userdata) {
    for(size_t i = 0; i < g_functions.capacity; ++i) {
        for(FuncEntry* entry = g_functions.items[i]; entry != NULL; entry = entry->next) {
            visitor(entry, userdata);
        }
    }
}

TypeEntry* type_table_insert(View name) {
    return typetab_declare(typecheck_arena, &g_types, name);
}

TypeEntry* type_table_lookup(View name) {
    return typetab_lookup(&g_types, name);
}

TypeEntry* newtype_table_insert(View name) {
    return newtypetab_declare(typecheck_arena, &g_newtypes, name);
}

TypeEntry* newtype_table_lookup(View name) {
    return newtypetab_lookup(&g_newtypes, name);
}

EnumConstEntry* enum_const_declare(View name) {
    return enumconsttab_declare(typecheck_arena, &g_enum_consts, name);
}

EnumConstEntry* enum_const_lookup(View name) {
    return enumconsttab_lookup(&g_enum_consts, name);
}

GlobalEntry* global_var_decl(View name, Typecheck type, Node *decl_node, bool _static,
                             bool _extern, bool has_initializer, GlobalDeclStatus *status) {
    GlobalEntry* entry = globaltab_lookup(&g_globals, name);

    if(!entry) {
        entry = globaltab_declare(typecheck_arena, &g_globals, name);
        entry->type = type;
        entry->decl_node = decl_node;
        entry->_static = _static;
        entry->_extern = _extern;
        entry->has_init = has_initializer;
        *status = GLOBAL_NEW;
        return entry;
    }

    if(!type_equals(entry->type, type) || entry->type._array != type._array) {
        *status = GLOBAL_TYPE_MISMATCH;
        return entry;
    }

    if(!entry->_extern && !_extern) { *status = GLOBAL_REDEFINED; return entry; }

    if(!_extern) {
        entry->_extern = false;
        entry->type = type;
        entry->decl_node = decl_node;
        entry->has_init = has_initializer;
        entry->_static = _static;
    }

    *status = GLOBAL_MERGED;
    return entry;
}

GlobalEntry* global_var_lookup(View name) {
    return globaltab_lookup(&g_globals, name);
}

LabelEntry* label_declare(View name, Node *declare) {
    LabelEntry* exist = labeltab_lookup(&g_labels, name);

    if(exist) {
        if(exist->_defined) return NULL;

        exist->_defined = true;
        exist->decl_node = declare;
        return exist;
    }

    LabelEntry* entry = labeltab_insert(typecheck_arena, &g_labels, name);
    entry->_defined = true;
    entry->decl_node = declare;
    entry->first_jump = NULL;
    return entry;
}

void label_table_reset(void) {
    memset(g_labels.items, 0, sizeof(LabelEntry*) * g_labels.capacity);
    g_labels.count = 0;
}

LabelEntry* label_reference(View name, Node *use) {
    LabelEntry* entry = labeltab_lookup(&g_labels, name);

    if(!entry) {
        entry = labeltab_insert(typecheck_arena, &g_labels, name);
        entry->_defined = false;
        entry->decl_node = NULL;
        entry->first_jump = use;
    }

    return entry;
}

void label_table_foreach(LabelTableVisitor visitor, void* userdata) {
    for(size_t i = 0; i < g_labels.capacity; i++) {
        for(LabelEntry* entry = g_labels.items[i]; entry != NULL; entry = entry->next) {
            visitor(entry, userdata);
        }
    }
}

bool resolve_variable(Scope *scope, View name, Typecheck *out) {
    SymbolEntry* local = scope_lookup_chain(scope, name);
    if(local) {
        *out = local->type;
        return true;
    }

    GlobalEntry* global = global_var_lookup(name);
    if(global) {
        *out = global->type;
        return true;
    }

    return false;
}

size_t func_count(void) {
    return g_functions.count;
}

FuncEntry* func_item(size_t index) {
    size_t seen = 0;

    for(size_t i = 0; i < g_functions.capacity; ++i) {
        for(FuncEntry* entry = g_functions.items[i]; entry != NULL; entry = entry->next) {
            if(seen++ == index) return entry;
        }
    }

    return NULL;
}

void typecheck_session_init(Arena *arena) { 
    typecheck_globals_init(arena); 
}

static void report_function_link(FuncEntry *entry, void *userdata) {
    CheckContext* ctx = (CheckContext*)userdata;

    /* extern = resolvido pelo linker (dll ou .obj): o compilador nao cobra definicao */
    if(entry->_builtin || entry->_extern || entry->has_body) return;

    char sig[768];
    format_signature(sig, sizeof sig, entry->name, &entry->signature);

    if(entry->call_count > 0) {
        Node* use = entry->first_call;
        SEMA_ERRC(use, "L001", "Referencia indefinida: '%s' e chamada %zu vez(es), mas nenhuma unidade "
                  "compilada a define", sig, entry->call_count);
        if(entry->decl_node)
            SEMA_NOTE(entry->decl_node, "declarada aqui como prototipo, sem corpo");
        SEMA_NOTE(use, "primeira chamada aqui; inclua no build o arquivo que define '%.*s' ou implemente-a",
                  VIEW_ARG(entry->name));
    } else if(entry->_pending && entry->decl_node) {
        SEMA_ERRC(entry->decl_node, "L002", "A funcao '%s' foi declarada mas nunca definida", sig);
    }
}

void typecheck_finalize(CheckContext *ctx, bool require_entry) {
    function_table_foreach(report_function_link, ctx);

    if(require_entry) {
        FuncEntry* start = function_table_lookup(ENTRY_START);
        if(!start || !start->has_body)
            diag_error_code(ctx->ctx, NULL, 0, 0, "L003",
                "Referencia indefinida para o ponto de entrada 'start': nenhuma unidade o define");
    }
}

CheckContext create_checkctx(Arena *arena, DiagnosticContext *ctx) {
    CheckContext check = { 0 };
    check.arena = arena;
    check.current_scope = scope_push(arena, NULL);
    check.ctx = ctx;
    return check;
}
