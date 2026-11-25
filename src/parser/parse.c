#include "../include/parser.h"

#include <float.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Aloca cada Node com o tamanho do seu proprio kind (em vez de sizeof(Node)).
 * Invariante: ninguem muda node->kind depois do parse para um kind com payload
 * maior, nem copia *node por valor. Se algum passo seu faz isso: -DPARSER_COMPACT_NODES=0 */
#ifndef PARSER_COMPACT_NODES
#define PARSER_COMPACT_NODES 1
#endif

#define PARSER_MAX_DEPTH 256
#define FOUND_BUF 64
#define PTR_LVL_MAX      32
#define SV_ARGS(v)       (int)(v).len, (v).start

typedef enum {
    DECL_GLOBAL,
    DECL_LOCAL,
    DECL_MEMBER
} DeclContext;

typedef struct {
    bool _static;
    bool _extern;
    bool inline__;
    bool _const;
    bool _atomic;
    Token start;
    Token inline_tok;
} DeclSpecs;

static inline size_t tok_len(Token token) {
    return token.value.len ? token.value.len : 1;
}

/* Token UNDEFINED ja foi reportado pelo lexer: nao duplica o erro, so entra em panic. */
#define PERR(p, tok, ...)                                                           \
    do {                                                                            \
        Token tok_ = (tok);                                                         \
        if(!(p)->panic && tok_.type != UNDEFINED)                                   \
            diag_error_span((p)->ctx, (p)->filename, tok_.line, tok_.col,           \
                            tok_len(tok_), __VA_ARGS__);                            \
        (p)->panic = true;                                                          \
    } while(0)

#define PSOFT(p, tok, ...)                                                          \
    do {                                                                            \
        Token tok_ = (tok);                                                         \
        if(!(p)->panic)                                                             \
            diag_error_span((p)->ctx, (p)->filename, tok_.line, tok_.col,           \
                            tok_len(tok_), __VA_ARGS__);                            \
    } while(0)

static Node* parse_expr(Parser *parser);
static Node* parse_assignment(Parser *parser);
static Node* parse_ternary(Parser *parser);
static Node* parse_unary(Parser *parser);
static Node* parse_unary_impl(Parser *parser);
static Node* parse_statement(Parser *parser);
static Node* parse_statement_impl(Parser *parser);
static Node* parse_block(Parser *parser, bool new_scope);
static Node* parse_declaration(Parser *parser, DeclContext ctx);
static Node* parse_member_declaration(Parser *parser);
static Node* parse_aggregate_spec(Parser *parser, bool allow_define);
static Node* parse_enum_spec(Parser *parser, bool allow_define);
static Node* parse_init_list(Parser *parser);
static TypeSpec parse_full_type(Parser *parser, bool allow_define);

/* ------------------------------------------------------------------ */
/* Nos                                                                 */
/* ------------------------------------------------------------------ */
#define AST_OFF   offsetof(Node, ast)
#define HDR_SZ    ((AST_OFF + 15u) & ~(size_t)15u)
#define AST_SZ(m) ((AST_OFF + sizeof(((Node*)0)->ast.m) + 15u) & ~(size_t)15u)
#define AST_SZ_EXPR(m) (AST_SZ(m) > AST_SZ(literal) ? AST_SZ(m) : AST_SZ(literal))

static const uint16_t k_node_size[] = {
    [NODE_UNDEFINED]     = HDR_SZ,
    [NODE_VAR_DECL]      = AST_SZ(var_decl),
    [NODE_FUNC_DECL]     = AST_SZ(func_decl),
    [NODE_VAR_CALL]      = AST_SZ_EXPR(var_call),
    [NODE_FUNC_CALL]     = AST_SZ_EXPR(func_call),
    [NODE_LITERAL]       = AST_SZ(literal),
    [NODE_BYTES]         = AST_SZ_EXPR(bytes),
    [NODE_IF_STMT]       = AST_SZ(if_stmt),
    [NODE_BLOCK_STMT]    = AST_SZ(block),
    [NODE_CALL_STMT]     = AST_SZ(call_statement),
    [NODE_BREAK_STMT]    = HDR_SZ,
    [NODE_CONTINUE_STMT] = HDR_SZ,
    [NODE_FOR_LOOP]      = AST_SZ(for_loop),
    [NODE_WHILE_LOOP]    = AST_SZ(while_loop),
    [NODE_DO_WHILE_LOOP] = AST_SZ(while_loop),
    [NODE_DATA]          = AST_SZ(aggregate),
    [NODE_ENUM]          = AST_SZ(enum_decl),
    [NODE_ENUM_CALL]     = AST_SZ_EXPR(enum_access),
    [NODE_ENUM_MEMBER]   = AST_SZ(enum_member),
    [NODE_COPERATE]      = AST_SZ(aggregate),
    [NODE_TERNARY]       = AST_SZ_EXPR(if_stmt),
    [NODE_NEWTYPE]       = AST_SZ(newtype),
    [NODE_FIELD_CALL]    = AST_SZ_EXPR(field_access),
    [NODE_CAST]          = AST_SZ_EXPR(cast),
    [NODE_PROGRAM]       = AST_SZ(program),
    [NODE_BINARY_OP]     = AST_SZ_EXPR(binary),
    [NODE_UNARY_OP]      = AST_SZ_EXPR(unary),
    [NODE_POSTFIX_OP]    = AST_SZ_EXPR(unary),
    [NODE_ARRAY]         = AST_SZ_EXPR(binary),
    [NODE_LOAD_LABEL]    = AST_SZ(jmp_label),
    [NODE_JUMP_LABEL]    = AST_SZ(jmp_label),
    [NODE_INIT_LIST]     = AST_SZ(init_list),
    [NODE_DESIGNATOR]    = AST_SZ(designator),
};

/* Se adicionar um NodeKinds novo no fim do enum, atualize a tabela acima. */
_Static_assert(sizeof(k_node_size) / sizeof(k_node_size[0]) == NODE_DESIGNATOR + 1,
               "k_node_size precisa cobrir todos os NodeKinds");

static Node* new_node(Parser *parser, NodeKinds kind, uint32_t line, uint32_t col) {
#if PARSER_COMPACT_NODES
    size_t size = k_node_size[kind];
#else
    size_t size = sizeof(Node);
#endif
    Node* node = (Node*)arena_alloc(parser->arena, size);
    memset(node, 0, size);   /* remova se o seu arena_alloc ja devolve memoria zerada */
    node->kind = kind;
    node->line = line;
    node->col = col;
    node->filepath = parser->filename;
    return node;
}

static inline Node* create_node_at(Parser *parser, NodeKinds kind, Token origin) {
    return new_node(parser, kind, origin.line, origin.col);
}

static inline Node* create_node(Parser *parser, NodeKinds kind) {
    return new_node(parser, kind, parser->current.line, parser->current.col);
}

/* NodeList e o vetor inicial numa unica alocacao */
static NodeList* list_create(Arena *arena) {
    NodeList* list = (NodeList*)arena_alloc(arena, sizeof(NodeList) + 4 * sizeof(Node*));
    list->capacity = 4;
    list->count = 0;
    list->items = (Node**)(list + 1);
    return list;
}

static void list_push(Arena *arena, NodeList *list, Node *item) {
    if(UNLIKELY(list->count >= list->capacity)) {
        size_t new_cap = (list->capacity > 0) ? list->capacity * 2 : 4;
        Node** new_items = (Node**)arena_alloc(arena, sizeof(Node*) * new_cap);

        memcpy(new_items, list->items, sizeof(Node*) * list->count);

        list->capacity = new_cap;
        list->items = new_items;
    }

    list->items[list->count++] = item;
}

static void list_push_chain(Arena *arena, NodeList *list, Node *node) {
    while(node != NULL) {
        Node* next = node->next;
        node->next = NULL;
        list_push(arena, list, node);
        node = next;
    }
}

static void chain_append(Node **head, Node **tail, Node *node) {
    if(node == NULL) return;
    if(*head == NULL) *head = node;
    else (*tail)->next = node;

    *tail = node;
}

/* ------------------------------------------------------------------ */
/* Cursor de tokens                                                    */
/* ------------------------------------------------------------------ */
static inline Token peek(Parser *parser) {
    return parser->current;
}

static inline Token peeknext(Parser *parser) {
    return parser->next;
}

static inline bool isend(Parser *parser) {
    return parser->current.type == EOFF;
}

/* Avanca sem devolver o token (evita copia quando o resultado nao e usado). */
static inline void consume(Parser *parser) {
    parser->prev = parser->current;
    parser->current = parser->next;
    parser->next = (parser->current.type == EOFF) ? parser->current : next_token(parser->lexer);
    parser->consumed++;
}

static inline Token advance(Parser *parser) {
    consume(parser);
    return parser->prev;
}

static inline bool match(Parser *parser, TokenType type) {
    if(parser->current.type != type) return false;

    consume(parser);
    return true;
}

static const char* describe_token(Token t, char *buf, size_t cap) {
    const char* kind;

    switch (t.type)
    {
    case EOFF: return "fim do arquivo";
    case IDENTIFIER: kind = "identificador"; break;
    case STRING_LIT: case WSTRING_LIT: case CHAR_LIT: case WCHAR_LIT:
    case INT_LIT: case BIG_LIT: case UNSIGNED_LIT: case HEXA_LIT:
    case FLOAT_LIT: case DOUBLE_LIT:
        kind = "literal";
        break;
    default:
        snprintf(buf, cap, "'%s'", token_to_string(t.type));
        return buf;
    }

    bool cut = t.value.len > 24;
    snprintf(buf, cap, "%s '%.*s'%s", kind, (int)(cut ? 24 : t.value.len), t.value.start,
             cut ? "..." : "");
    return buf;
}

static NOINLINE Token expect_fail(Parser *parser, TokenType type, const char* message) {
    Token got = parser->current;
    Token prev = parser->prev;
    bool line_end = (type == SEMICOLON || type == CLOSE_PAREN || type == CLOSE_BRACKET) &&
                    prev.line != 0 && got.line > prev.line;

    if(!parser->panic) {
        if(line_end) {
            /* aponta para logo depois do ultimo token valido, onde o simbolo deveria estar */
            diag_error_span(parser->ctx, parser->filename, prev.line, prev.col + (uint32_t)prev.value.len, 1,
                            "%s", message);
        } else if(got.type != UNDEFINED) {   /* erro lexico ja reportado pelo lexer */
            char buf[FOUND_BUF];
            diag_error_span(parser->ctx, parser->filename, got.line, got.col, tok_len(got),
                            "%s (encontrado %s)", message, describe_token(got, buf, sizeof buf));
        }
    }

    if(!line_end) parser->panic = true;

    Token missing = { 0 };
    missing.type = type;
    missing.line = got.line;
    missing.col = got.col;
    return missing;
}

/* Versao que devolve o token (value.len == 0 indica token ausente). */
static inline Token expected(Parser *parser, TokenType type, const char* message) {
    if(LIKELY(parser->current.type == type)) return advance(parser);
    return expect_fail(parser, type, message);
}

/* Versao que descarta o token. */
static inline void expect(Parser *parser, TokenType type, const char* message) {
    if(LIKELY(parser->current.type == type)) consume(parser);
    else expect_fail(parser, type, message);
}

static void expect_closing(Parser *parser, TokenType close, Token open, const char* message) {
    bool report = !parser->panic;
    bool missing = parser->current.type != close;

    expect(parser, close, message);

    if(report && missing && open.value.len)
        diag_note_span(parser->ctx, parser->filename, open.line, open.col, tok_len(open),
                       "'%s' aberto aqui nunca foi fechado", token_to_string(open.type));
}

static void expect_close_brace(Parser *parser, Token open, const char* message) {
    expect_closing(parser, CLOSE_BRACE, open, message);
}

/* ------------------------------------------------------------------ */
/* Limite de profundidade (sem chamada indireta)                       */
/* ------------------------------------------------------------------ */
static NOINLINE Node* depth_overflow(Parser *parser) {
    PERR(parser, peek(parser), "Aninhamento excede o limite do parser (%d)", PARSER_MAX_DEPTH);
    return create_node(parser, NODE_UNDEFINED);
}

#define DEFINE_GUARDED(name, impl)                          \
    static Node* name(Parser *parser) {                     \
        if(UNLIKELY(parser->depth >= PARSER_MAX_DEPTH))     \
            return depth_overflow(parser);                  \
        parser->depth++;                                    \
        Node* node = impl(parser);                          \
        parser->depth--;                                    \
        return node;                                        \
    }

DEFINE_GUARDED(parse_unary,              parse_unary_impl)
DEFINE_GUARDED(parse_statement,          parse_statement_impl)
DEFINE_GUARDED(parse_ternary_g,          parse_ternary)
DEFINE_GUARDED(parse_assignment_g,       parse_assignment)
DEFINE_GUARDED(parse_init_list_g,        parse_init_list)
DEFINE_GUARDED(parse_member_declaration_g, parse_member_declaration)

/* ------------------------------------------------------------------ */
/* Tabela de simbolos                                                  */
/* ------------------------------------------------------------------ */
static uint32_t view_hash(View v) {
    uint32_t h = 2166136261u;

    for(size_t i = 0; i < v.len; ++i) {
        h ^= (uint8_t)v.start[i];
        h *= 16777619u;
    }

    return h;
}

static const SymEntry* sym_find(Parser *parser, View name) {
    const SymTable *table = &parser->syms;
    if(table->count == 0) return NULL;            /* evita o hash quando nao ha simbolos */

    int32_t i = table->heads[view_hash(name) & (SYM_BUCKETS - 1)];

    while(i >= 0) {
        const SymEntry *entry = &table->items[i];
        if(svcmp(entry->name, name)) return entry;
        i = entry->prev;
    }

    return NULL;
}

static bool is_newtype_name(Parser *parser, View name) {
    const SymEntry *entry = sym_find(parser, name);
    return entry != NULL && entry->is_type;
}

static void sym_push(Parser *parser, Token name, bool is_type) {
    SymTable *table = &parser->syms;

    if(table->count == table->capacity) {
        size_t cap = (table->capacity > 0) ? table->capacity * 2 : 64;
        SymEntry* items = (SymEntry*)arena_alloc(parser->arena, cap * sizeof(SymEntry));

        if(table->count > 0) memcpy(items, table->items, table->count * sizeof(SymEntry));

        table->items = items;
        table->capacity = cap;
    }

    uint32_t bucket = view_hash(name.value) & (SYM_BUCKETS - 1);
    SymEntry *entry = &table->items[table->count];
    entry->name = name.value;
    entry->origin = name;
    entry->prev = table->heads[bucket];
    entry->bucket = bucket;
    entry->depth = table->depth;
    entry->is_type = is_type;

    table->heads[bucket] = (int32_t)table->count++;
}

static void scope_enter(Parser *parser) {
    parser->syms.depth++;
}

static void scope_leave(Parser *parser) {
    SymTable *table = &parser->syms;

    while(table->count > 0 && table->items[table->count - 1].depth == table->depth) {
        const SymEntry *entry = &table->items[--table->count];
        table->heads[entry->bucket] = entry->prev;
    }

    table->depth--;
}

static void report_redeclared(Parser *parser, Token name, const SymEntry *prev) {
    if(parser->panic) return;

    diag_error_span(parser->ctx, parser->filename, name.line, name.col, tok_len(name),
        "O nome '%.*s' ja foi declarado neste escopo", SV_ARGS(name.value));
    diag_note_span(parser->ctx, parser->filename, prev->origin.line, prev->origin.col,
        tok_len(prev->origin), "'%.*s' foi declarado aqui", SV_ARGS(prev->origin.value));
}

static void declare_newtype(Parser *parser, Token name) {
    const SymEntry *prev = sym_find(parser, name.value);

    if(prev != NULL && prev->depth == parser->syms.depth) {
        report_redeclared(parser, name, prev);
        return;
    }

    sym_push(parser, name, true);
}

static void declare_ordinary(Parser *parser, Token name) {
    const SymEntry *prev = sym_find(parser, name.value);
    if(prev == NULL) return;

    if(prev->depth == parser->syms.depth) {   /* tipo ou shadow ja declarado aqui */
        report_redeclared(parser, name, prev);
        return;
    }

    if(prev->is_type) sym_push(parser, name, false);   /* so registra se esconde um tipo */
}

static size_t edit_distance(View a, View b) {
    enum { MAXL = 32 };
    if(a.len >= MAXL || b.len >= MAXL) return (size_t)-1;

    size_t row[MAXL + 1];
    for(size_t j = 0; j <= b.len; ++j) row[j] = j;

    for(size_t i = 1; i <= a.len; ++i) {
        size_t diag = row[0];
        row[0] = i;

        for(size_t j = 1; j <= b.len; ++j) {
            size_t up = row[j];
            size_t best = diag + (a.start[i - 1] == b.start[j - 1] ? 0 : 1);

            if(up + 1 < best) best = up + 1;
            if(row[j - 1] + 1 < best) best = row[j - 1] + 1;

            diag = up;
            row[j] = best;
        }
    }

    return row[b.len];
}

/* So roda no caminho de erro. Devolve o newtype visivel mais parecido, ou NULL. */
static const SymEntry* suggest_type(Parser *parser, View name) {
    const SymTable *table = &parser->syms;
    const SymEntry *best = NULL;
    size_t best_d = (name.len <= 4 ? 1 : 2) + 1;

    for(size_t i = table->count; i-- > 0;) {
        const SymEntry *entry = &table->items[i];
        if(!entry->is_type) continue;

        size_t d = edit_distance(name, entry->name);
        if(d < best_d) { best_d = d; best = entry; }
    }

    return best;
}

/* ------------------------------------------------------------------ */
/* Classificacao de tokens e recuperacao                               */
/* ------------------------------------------------------------------ */
static inline bool istypebase(TokenType type) {
    return (type >= KVOID && type <= KINT64);
}

static bool starts_statement_token(TokenType type) {
    switch (type)
    {
    case KCALL: case KBREAK: case KCONTINUE:
    case KWHILE: case KFOR: case KDO:
    case KIF: case KJUMP: case KLOAD:
    case KNEWTYPE: case KDATA: case KCOPERATE:
    case KENUM: case KSTATIC: case KEXTERN: case KINLINE:
    case KCONST: case KATOMIC:
        return true;
    default: return istypebase(type);
    }
}

static void synchronize(Parser *parser, size_t start) {
    /* O ';' que fecha o comando ja foi consumido: ja estamos numa fronteira.
     * Sem isso, o proximo comando inteiro era descartado. */
    if(parser->consumed != start && parser->prev.type == SEMICOLON) {
        parser->panic = false;
        return;
    }

    while(!isend(parser)) {
        TokenType token = peek(parser).type;
        if(token == CLOSE_BRACE || starts_statement_token(token)) break;

        consume(parser);
        if(token == SEMICOLON) break;
    }

    parser->panic = false;
}

static void recover(Parser *parser, size_t consumed) {
    if(parser->panic) synchronize(parser, consumed);
    if(parser->consumed == consumed && !isend(parser)) consume(parser);
}

static void skip_bad_token(Parser *parser) {
    TokenType token = peek(parser).type;
    if(token == SEMICOLON || token == CLOSE_BRACE || token == CLOSE_PAREN || token == EOFF) return;
    consume(parser);
}

static bool starts_declaration(Parser *parser) {
    Token token = peek(parser);

    switch (token.type)
    {
    case KNEWTYPE: case KDATA: case KCOPERATE: case KENUM:
    case KSTATIC: case KEXTERN: case KINLINE: case KCONST: case KATOMIC:
        return true;
    case IDENTIFIER:
        /* So consulta a tabela de simbolos quando o proximo token permite
         * uma declaracao ("T *p", "T const x", "T;"); "x = 1;" e "f(x);" nao pagam o hash. */
        switch (peeknext(parser).type)
        {
        case IDENTIFIER: return true;
        case OPSTAR: case KCONST: case KATOMIC: case SEMICOLON:
            return is_newtype_name(parser, token.value);
        default: return false;
        }
    default:
        return istypebase(token.type);
    }
}

/* Recuperacao no escopo global: pula ate o fim da declaracao atual (';' ou '}' que
 * fecha o nivel 0) ou ate o inicio da proxima declaracao. Chaves aninhadas sao puladas
 * por inteiro, entao um "if (x) { ... }" solto gera um erro so, nao um por token. */
static void synchronize_global(Parser *parser, size_t start) {
    if(parser->consumed != start &&
       (parser->prev.type == SEMICOLON || parser->prev.type == CLOSE_BRACE)) {
        parser->panic = false;
        return;
    }

    int braces = 0;

    while(!isend(parser)) {
        TokenType t = peek(parser).type;

        if(braces == 0 && parser->consumed != start && starts_declaration(parser)) break;

        consume(parser);

        if(t == OPEN_BRACE) braces++;
        else if(t == CLOSE_BRACE) { if(braces > 0 && --braces == 0) break; }
        else if(t == SEMICOLON && braces == 0) break;
    }

    parser->panic = false;
}

static void recover_global(Parser *parser, size_t consumed) {
    if(parser->panic) synchronize_global(parser, consumed);
    if(parser->consumed == consumed && !isend(parser)) consume(parser);
}

static bool peeknext_starts_type(Parser *parser) {
    Token next = peeknext(parser);

    switch (next.type)
    {
    case KCONST: case KATOMIC: case KDATA: case KCOPERATE: case KENUM:
        return true;
    case IDENTIFIER:
        return is_newtype_name(parser, next.value);
    default:
        return istypebase(next.type);
    }
}

/* ------------------------------------------------------------------ */
/* Tipos                                                               */
/* ------------------------------------------------------------------ */
static void parse_qualifiers(Parser *parser, DeclSpecs *specs, bool allow_storage) {
    for(;;) {
        Token type = peek(parser);
        bool* flag = NULL;

        switch (type.type)
        {
        case KCONST: flag = &specs->_const; break;
        case KATOMIC: flag = &specs->_atomic; break;
        case KSTATIC: if(allow_storage) flag = &specs->_static; break;
        case KEXTERN: if(allow_storage) flag = &specs->_extern; break;
        case KINLINE:
            if(allow_storage) {
                flag = &specs->inline__;
                specs->inline_tok = type;
            }
            break;
        default: break;
        }

        if(flag == NULL) return;
        if(*flag) PSOFT(parser, type, "Qualificador repetido: %s", token_to_string(type.type));
        *flag = true;
        consume(parser);
    }
}

static void parse_pointer_chain(Parser *parser, TypeSpec *spec) {
    spec->ptr_lvl = 0;
    spec->ptr_const_mask = 0;
    spec->ptr_atomic_mask = 0;

    while(peek(parser).type == OPSTAR) {
        Token star = advance(parser);
        bool is_const = false, is_atomic = false;

        for(;;) {                                   /* qualificadores do ponteiro */
            Token q = peek(parser);
            bool* flag = (q.type == KCONST) ? &is_const : (q.type == KATOMIC) ? &is_atomic : NULL;
            if(flag == NULL) break;
            if(*flag) PSOFT(parser, q, "Qualificador repetido: %s", token_to_string(q.type));
            *flag = true;
            consume(parser);
        }

        if(spec->ptr_lvl >= PTR_LVL_MAX) {
            PSOFT(parser, star, "Niveis de ponteiros ultrapassa o limite. (Max: %d)", PTR_LVL_MAX);
            continue;
        }

        uint32_t bit = 1U << spec->ptr_lvl++;
        if(is_atomic) spec->ptr_atomic_mask |= bit;
        if(is_const) spec->ptr_const_mask |= bit;
    }
}

static uint8_t typespec_top_const(const TypeSpec *type) {
    if(type->ptr_lvl == 0) return type->base_const;
    return (type->ptr_const_mask >> (type->ptr_lvl - 1)) & 1U;
}

static uint8_t typespec_top_atomic(const TypeSpec *type) {
    if(type->ptr_lvl == 0) return type->base_atomic;
    return (type->ptr_atomic_mask >> (type->ptr_lvl - 1)) & 1U;
}

static void parse_array_dimensions(Parser *parser, TypeSpec *spec) {
    if(peek(parser).type != OPEN_BRACKET) return;

    spec->is_array = true;
    spec->array_dim = list_create(parser->arena);

    while(peek(parser).type == OPEN_BRACKET) {
        Token open = advance(parser);
        Node* dim;

        if(peek(parser).type == CLOSE_BRACKET) {
            if(spec->array_dim->count > 0)
                PSOFT(parser, open, "Somente a primeira dimensao do array pode ser omitida");
            dim = create_node(parser, NODE_UNDEFINED);
        } else {
            dim = parse_expr(parser);
        }

        expect(parser, CLOSE_BRACKET, "Esperado ']' apos a dimensao do array");
        list_push(parser->arena, spec->array_dim, dim);
    }
}

static TypeSpec parse_type_spec(Parser *parser, bool allow_define) {
    TypeSpec spec = { 0 };
    Token token = peek(parser);

    if(token.type == KDATA || token.type == KCOPERATE) {
        spec.base = token.type;
        spec.nested = parse_aggregate_spec(parser, allow_define);
        spec.alias = spec.nested->ast.aggregate.name;
        return spec;
    }

    if(token.type == KENUM) {
        spec.base = KENUM;
        spec.nested = parse_enum_spec(parser, allow_define);
        spec.alias = spec.nested->ast.enum_decl.name;
        return spec;
    }

    if(token.type == IDENTIFIER) {
        spec.base = IDENTIFIER;
        spec.alias = token.value;

        if(!is_newtype_name(parser, token.value))
            PSOFT(parser, token, "Tipo '%.*s' nao declarado (declare-o com newtype antes de usar)",
                  SV_ARGS(token.value));

        consume(parser);
        return spec;
    }

    if(istypebase(token.type)) {
        spec.base = token.type;
        consume(parser);
        return spec;
    }

    PERR(parser, token, "Esperado um tipo, mas encontrado: %s", token_to_string(token.type));
    skip_bad_token(parser);
    return spec;
}

static TypeSpec parse_full_type(Parser *parser, bool allow_define) {
    DeclSpecs specs = { 0 };
    parse_qualifiers(parser, &specs, false);
    TypeSpec spec = parse_type_spec(parser, allow_define);
    parse_qualifiers(parser, &specs, false);
    spec.base_const = specs._const;
    spec.base_atomic = specs._atomic;
    parse_pointer_chain(parser, &spec);
    return spec;
}

static Node* parse_member_declaration(Parser *parser) {
    return parse_declaration(parser, DECL_MEMBER);
}

static NodeList* parse_aggregate_members(Parser *parser) {
    NodeList* members = list_create(parser->arena);
    Token open = expected(parser, OPEN_BRACE, "Esperado '{' para abrir os membros");

    while(!isend(parser) && peek(parser).type != CLOSE_BRACE) {
        size_t before = parser->consumed;

        if(!starts_declaration(parser)) {
            Token bad = peek(parser);
            PERR(parser, bad, "Esperado a declaracao de um membro, mas encontrado: %s",
                 token_to_string(bad.type));
        } else {
            list_push_chain(parser->arena, members, parse_member_declaration_g(parser));
        }

        recover(parser, before);
    }

    expect_close_brace(parser, open, "Esperado '}' para fechar o bloco");
    return members;
}

static Node* parse_aggregate_spec(Parser *parser, bool allow_define) {
    Token kw = advance(parser);
    const char* what = (kw.type == KDATA) ? "data" : "coperate";

    Node* node = create_node_at(parser, (kw.type == KDATA) ? NODE_DATA : NODE_COPERATE, kw);

    if(peek(parser).type == IDENTIFIER) node->ast.aggregate.name = advance(parser).value;

    if(peek(parser).type == OPEN_BRACE) {
        if(!allow_define)
            PSOFT(parser, peek(parser), "Nao e permitido definir um %s neste contexto", what);

        node->ast.aggregate.members = parse_aggregate_members(parser);

        if(node->ast.aggregate.members->count == 0)
            PSOFT(parser, kw, "%s sem membros", what);
    } else if(node->ast.aggregate.name.len == 0) {
        PERR(parser, peek(parser), "Esperado nome ou '{' apos '%s'", what);
    }

    return node;
}

static NodeList* parse_enum_members(Parser *parser) {
    NodeList* members = list_create(parser->arena);
    Token open = expected(parser, OPEN_BRACE, "Esperado '{' para abrir os membros do enum");

    while(!isend(parser) && peek(parser).type != CLOSE_BRACE) {
        Token name = expected(parser, IDENTIFIER, "Esperado o nome do membro do enum");
        if(name.value.len == 0) break;

        /* O(n^2), mas so no n de membros de um enum */
        for(size_t i = 0; i < members->count; ++i) {
            const Node* prev = members->items[i];
            if(!svcmp(prev->ast.enum_member.name, name.value)) continue;

            if(!parser->panic) {
                diag_error_span(parser->ctx, parser->filename, name.line, name.col, tok_len(name),
                                "O membro '%.*s' ja foi declarado neste enum", SV_ARGS(name.value));
                diag_note_span(parser->ctx, parser->filename, (uint32_t)prev->line, (uint32_t)prev->col,
                               name.value.len ? name.value.len : 1,
                               "'%.*s' foi declarado aqui", SV_ARGS(name.value));
            }
            break;
        }

        Node* member = create_node_at(parser, NODE_ENUM_MEMBER, name);
        member->ast.enum_member.name = name.value;

        if(match(parser, ASSIGN)) member->ast.enum_member.value = parse_ternary(parser);

        list_push(parser->arena, members, member);

        if(!match(parser, COMMA)) break;
    }

    if(members->count == 0) PSOFT(parser, open, "enum sem membros");

    expect_closing(parser, CLOSE_BRACE, open, "Esperado '}' ao final do enum (faltou ',' entre os membros?)");
    return members;
}

static Node* parse_enum_spec(Parser *parser, bool allow_define) {
    Token kw = advance(parser);
    Node* node = create_node_at(parser, NODE_ENUM, kw);

    if(peek(parser).type == IDENTIFIER) node->ast.enum_decl.name = advance(parser).value;

    if(peek(parser).type == OPEN_BRACE) {
        if(!allow_define)
            PSOFT(parser, peek(parser), "Nao e permitido definir um enum neste contexto");

        node->ast.enum_decl.members = parse_enum_members(parser);
    } else if(node->ast.enum_decl.name.len == 0) {
        PERR(parser, peek(parser), "Esperado nome ou '{' apos 'enum'");
    }

    return node;
}

/* ------------------------------------------------------------------ */
/* Literais                                                            */
/* ------------------------------------------------------------------ */
static bool convert_integer(Parser *parser, Token token, unsigned base, size_t skip, unsigned long long *out) {
    View value = token.value;
    unsigned long long result = 0;

    const unsigned long long cutoff = ULLONG_MAX / base;
    const unsigned cutlim = (unsigned)(ULLONG_MAX % base);

    *out = 0;

    if(base == 16 && value.len <= skip) {
        PSOFT(parser, token, "Literal hexadecimal sem digitos apos '0x'");
        return false;
    }

    for(size_t i = skip; i < value.len; ++i) {
        unsigned char c = (unsigned char)value.start[i];
        unsigned digit;

        if(c >= '0' && c <= '9') digit = c - '0';
        else if(base == 16 && c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if(base == 16 && c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else if(base == 10 && (c == 'U' || c == 'B')) break;   /* sufixo */
        else {
            PSOFT(parser, token, "Caractere invalido '%c' (ASCII %d) no literal %s '%.*s'",
                  (c >= 32 && c < 127) ? c : '?', c,
                  base == 16 ? "hexadecimal" : "inteiro", SV_ARGS(value));
            return false;
        }

        if(UNLIKELY(result > cutoff || (result == cutoff && digit > cutlim))) {
            PSOFT(parser, token, "Literal inteiro '%.*s' fora do alcance de 64 bits (maximo 18446744073709551615)",
                  SV_ARGS(value));
            return false;
        }

        result = result * base + digit;
    }

    *out = result;
    return true;
}

static long double convert_float(Parser *parser, Token token, bool is_float) {
    View value = token.value;
    char buffer[128];
    size_t n = 0;

    for(size_t i = 0; i < value.len; ++i) {
        char c = value.start[i];

        if(is_number(c) || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
            if(n == sizeof(buffer) - 1) {
                PSOFT(parser, token, "Literal de ponto flutuante longo demais (max %d caracteres)",
                      (int)sizeof(buffer) - 1);
                return 0;
            }

            buffer[n++] = c;
            continue;
        }

        if(c == 'f' || c == 'F') break;

        PSOFT(parser, token, "Caractere invalido '%c' (ASCII %d) no literal de ponto flutuante '%.*s'",
              (c >= 32 && c < 127) ? c : '?', c, SV_ARGS(value));
        return 0;
    }

    buffer[n] = '\0';

    char* end = NULL;
    long double result = strtold(buffer, &end);

    /* strtold para no primeiro caractere invalido: "1e", "1.2.3", "+-" */
    if(n == 0 || end != buffer + n) {
        PSOFT(parser, token, "Literal de ponto flutuante malformado: '%.*s'", SV_ARGS(value));
        return 0;
    }

    long double limit = is_float ? (long double)FLT_MAX : (long double)DBL_MAX;

    if(result > limit || result < -limit) {
        PSOFT(parser, token, "Literal '%.*s' fora do alcance de um tipo '%s' (maximo %s)", SV_ARGS(value),
              is_float ? "float" : "double",
              is_float ? "3.4028235e+38" : "1.7976931348623157e+308");
        return 0;
    }

    return is_float ? (long double)(float)result : result;
}

/* open = tamanho do prefixo de abertura: 1 para " e ', 2 para W" e W' */
static View strip_quotes(View str, size_t open) {
    if(str.len < open + 1) {
        str.len = 0;
        return str;
    }

    str.start += open;
    str.len -= open + 1;
    return str;
}

/* ------------------------------------------------------------------ */
/* Expressoes                                                          */
/* ------------------------------------------------------------------ */

/* Chamada de funcao: o '(' ja foi consumido; 'paren' e o token '('. */
static Node* finish_call(Parser *parser, Token paren, View name) {
    Node* node = create_node_at(parser, NODE_FUNC_CALL, paren);
    node->ast.func_call.name = name;
    NodeList* args = list_create(parser->arena);
    node->ast.func_call.args = args;

    if(peek(parser).type != CLOSE_PAREN && !match(parser, KVOID)) {
        do {
            if(peek(parser).type == CLOSE_PAREN) {
                PSOFT(parser, peek(parser), "Argumento ausente: virgula sobrando antes de ')'");
                break;
            }
            list_push(parser->arena, args, parse_expr(parser));
        } while(match(parser, COMMA));
    }

    expect_closing(parser, CLOSE_PAREN, paren, "Esperado ')' apos os argumentos da funcao");
    return node;
}

static Node* parse_primary(Parser *parser) {
    Token tok = peek(parser);

    switch (tok.type)
    {
    case UNSIGNED_LIT:
    case HEXA_LIT: {
        consume(parser);
        Node* node = create_node_at(parser, NODE_LITERAL, tok);
        unsigned long long value = 0;

        if(tok.type == HEXA_LIT) {
            convert_integer(parser, tok, 16, 2, &value);
            node->ast.literal.kind = HEXA;
        } else {
            convert_integer(parser, tok, 10, 0, &value);
            node->ast.literal.kind = UNSIGNED;
        }

        node->ast.literal.u64 = value;
        return node;
    }

    case INT_LIT:
    case BIG_LIT: {
        consume(parser);
        Node* node = create_node_at(parser, NODE_LITERAL, tok);
        unsigned long long value = 0;

        convert_integer(parser, tok, 10, 0, &value);
        if(value > (unsigned long long)LLONG_MAX) {
            PSOFT(parser, tok, "Literal inteiro fora do alcance de um inteiro com sinal de 64 bits");
            value = 0;
        }

        node->ast.literal.kind = (tok.type == BIG_LIT) ? BIG : INT;
        node->ast.literal.i64 = (long long)value;
        return node;
    }

    case FLOAT_LIT:
    case DOUBLE_LIT: {
        consume(parser);
        Node* node = create_node_at(parser, NODE_LITERAL, tok);
        node->ast.literal.kind = (tok.type == FLOAT_LIT) ? FLOAT : DOUBLE;
        node->ast.literal.f64 = convert_float(parser, tok, tok.type == FLOAT_LIT);
        return node;
    }

    case CHAR_LIT:
    case WCHAR_LIT: {
        consume(parser);
        bool wide = (tok.type == WCHAR_LIT);
        Node* node = create_node_at(parser, NODE_LITERAL, tok);
        node->ast.literal.character = strip_quotes(tok.value, wide ? 2 : 1);
        node->ast.literal.kind = wide ? WIDE_CHAR : CHAR;
        return node;
    }

    case STRING_LIT:
    case WSTRING_LIT: {
        consume(parser);
        bool wide = (tok.type == WSTRING_LIT);
        Node* node = create_node_at(parser, NODE_LITERAL, tok);
        node->ast.literal.string = strip_quotes(tok.value, wide ? 2 : 1);
        node->ast.literal.kind = wide ? WIDE_STRING : STRING;
        return node;
    }

    case IDENTIFIER: {
        consume(parser);
        TokenType next = peek(parser).type;

        if(next == OPEN_PAREN) {
            /* chamada direta: nao aloca o VAR_CALL intermediario */
            Token paren = advance(parser);
            return finish_call(parser, paren, tok.value);
        }

        if(next == COLON && peeknext(parser).type == OPLT) {
            consume(parser);
            consume(parser);

            Token member = expected(parser, IDENTIFIER, "Esperado nome do membro do enum apos ':<'");

            Node* node = create_node_at(parser, NODE_ENUM_CALL, tok);
            node->ast.enum_access.alias = tok.value;
            node->ast.enum_access.member = member.value;
            return node;
        }

        Node* node = create_node_at(parser, NODE_VAR_CALL, tok);
        node->ast.var_call.name = tok.value;
        return node;
    }

    case OPEN_PAREN: {
        consume(parser);
        Node* node = parse_expr(parser);
        expect(parser, CLOSE_PAREN, "Esperado ')' apos a expressao");
        return node;
    }

    default: break;
    }

    PERR(parser, tok, "Esperado uma expressao, mas encontrado '%s'", token_to_string(tok.type));
    return create_node(parser, NODE_UNDEFINED);
}

static Node* parse_postfix(Parser *parser) {
    Node* expr = parse_primary(parser);

    for(;;) {
        Token tok = peek(parser);

        switch (tok.type)
        {
        case OPMINUSMINUS:
        case OPPLUSPLUS: {
            consume(parser);
            Node* node = create_node_at(parser, NODE_POSTFIX_OP, tok);
            node->ast.unary.op = tok.type;
            node->ast.unary.operand = expr;
            expr = node;
            break;
        }

        case OPEN_BRACKET: {
            consume(parser);
            Node* node = create_node_at(parser, NODE_ARRAY, tok);
            node->ast.binary.left = expr;
            node->ast.binary.right = parse_expr(parser);
            expect(parser, CLOSE_BRACKET, "Esperado ']' apos o indice");
            expr = node;
            break;
        }

        case DOT:
        case ARROW: {
            consume(parser);
            Token field = expected(parser, IDENTIFIER, "Esperado nome do campo apos o operador");

            Node* node = create_node_at(parser, NODE_FIELD_CALL, tok);
            node->ast.field_access.arrow = (tok.type == ARROW);
            node->ast.field_access.base = expr;
            node->ast.field_access.name = field.value;
            node->ast.field_access.offset = 0;
            expr = node;
            break;
        }

        case OPEN_PAREN: {
            consume(parser);

            if(expr->kind != NODE_VAR_CALL) {
                PSOFT(parser, tok, "Somente pode chamar funcoes pelo nome");

                int nest = 1;
                while(!isend(parser) && nest > 0) {
                    if(peek(parser).type == OPEN_PAREN) nest++;
                    else if(peek(parser).type == CLOSE_PAREN) nest--;
                    consume(parser);
                }

                break;
            }

            expr = finish_call(parser, tok, expr->ast.var_call.name);
            break;
        }

        default:
            return expr;
        }
    }
}

static Node* parse_unary_impl(Parser *parser) {
    Token tok = peek(parser);

    if(tok.type == OPEN_PAREN && peeknext_starts_type(parser)) {
        consume(parser);

        Node* node = create_node_at(parser, NODE_CAST, tok);
        node->ast.cast.spec = parse_full_type(parser, false);

        expect(parser, CLOSE_PAREN, "Esperado ')' apos o tipo do cast");

        node->ast.cast.operand = parse_unary(parser);
        return node;
    }

    switch (tok.type)
    {
    case KBYTES: {
        consume(parser);
        Node* node = create_node_at(parser, NODE_BYTES, tok);

        if(peek(parser).type == OPEN_PAREN && peeknext_starts_type(parser)) {
            consume(parser);
            node->ast.bytes.spec = parse_full_type(parser, false);
            parse_array_dimensions(parser, &node->ast.bytes.spec);
            expect(parser, CLOSE_PAREN, "Esperado ')' apos o tipo do bytes");
        } else {
            node->ast.bytes.is_expr = true;
            node->ast.bytes.operand = parse_unary(parser);
        }

        return node;
    }

    case OPMINUS: case OPPLUS: case OPMINUSMINUS: case OPPLUSPLUS:
    case OPAND: case OPBANG: case OPSTAR: case DESC: {
        consume(parser);
        Node* node = create_node_at(parser, NODE_UNARY_OP, tok);
        node->ast.unary.op = tok.type;
        node->ast.unary.operand = parse_unary(parser);
        return node;
    }

    default:
        return parse_postfix(parser);
    }
}

/* Precedencia por tabela (um load) em vez de switch. Indices fora da tabela => 0. */
static const uint8_t k_prec[] = {
    [OPSTAR] = 10, [OPSLASH] = 10, [OPMOD] = 10,
    [OPPLUS] = 9,  [OPMINUS] = 9,
    [OPSHL] = 8,   [OPSHR] = 8,
    [OPLT] = 7,    [OPLE] = 7,    [OPGT] = 7,   [OPGE] = 7,
    [OPEQ] = 6,    [OPBANGE] = 6,
    [OPAND] = 5,
    [OPXOR] = 4,
    [OPOR] = 3,
    [LOGAND] = 2,
    [LOGOR] = 1,
};

static inline int binary_prec(TokenType type) {
    return ((unsigned)type < sizeof(k_prec)) ? k_prec[type] : 0;
}

static Node* parse_binary(Parser *parser, int min_prec) {
    Node* left = parse_unary(parser);

    for(;;) {
        Token op = peek(parser);
        int prec = binary_prec(op.type);
        if(prec < min_prec) break;                 /* min_prec >= 1, entao cobre prec == 0 */

        consume(parser);

        Node* right = parse_binary(parser, prec + 1);
        Node* node = create_node_at(parser, NODE_BINARY_OP, op);
        node->ast.binary.op = op.type;
        node->ast.binary.left = left;
        node->ast.binary.right = right;
        left = node;
    }

    return left;
}

static Node* parse_ternary(Parser *parser) {
    Node* condition = parse_binary(parser, 1);
    if(peek(parser).type != OPTERN) return condition;

    Token op = advance(parser);
    Node* node = create_node_at(parser, NODE_TERNARY, op);
    node->ast.if_stmt.condition = condition;
    node->ast.if_stmt.then = parse_expr(parser);

    expect(parser, COLON, "Esperado ':' no operador ternario");

    node->ast.if_stmt.otherwise = parse_ternary_g(parser);
    return node;
}

static bool is_lvalue_node(const Node *node) {
    switch (node->kind)
    {
    case NODE_VAR_CALL: case NODE_FIELD_CALL: case NODE_ARRAY:
    case NODE_UNDEFINED:
        return true;
    case NODE_UNARY_OP:
        return node->ast.unary.op == OPSTAR;
    default:
        return false;
    }
}

static bool is_assign_op(TokenType type) {
    switch (type)
    {
    case ASSIGN: case OPPLUSE: case OPMINUSE: case OPANDE:
    case OPXORE: case OPORE: case OPMODE: case OPSLASHE:
    case OPSTARE: case OPSHRE: case OPSHLE:
        return true;
    default: return false;
    }
}

static Node* parse_assignment(Parser *parser) {
    Node* left = parse_ternary(parser);
    if(!is_assign_op(peek(parser).type)) return left;

    Token op = advance(parser);

    if(!is_lvalue_node(left))
        PSOFT(parser, op, "O lado esquerdo de '%s' nao e atribuivel "
                          "(so variaveis, campos, indices e '*ptr'; confira a precedencia dos operadores)",
              token_to_string(op.type));

    Node* node = create_node_at(parser, NODE_BINARY_OP, op);
    node->ast.binary.op = op.type;
    node->ast.binary.left = left;
    node->ast.binary.right = parse_assignment_g(parser);
    return node;
}

static Node* parse_designator_chain(Parser *parser) {
    Node *head = NULL, *tail = NULL;

    for(;;) {
        Token tok = peek(parser);
        Node* d;

        if(tok.type == OPEN_BRACKET) {
            consume(parser);
            d = create_node_at(parser, NODE_DESIGNATOR, tok);
            d->ast.designator.is_index = true;
            d->ast.designator.index = parse_ternary(parser);
            expect(parser, CLOSE_BRACKET, "Esperado ']' apos o indice do designador");
        } else if(tok.type == DOT) {
            consume(parser);
            Token field = expected(parser, IDENTIFIER, "Esperado nome do campo apos '.' no designador");
            d = create_node_at(parser, NODE_DESIGNATOR, tok);
            d->ast.designator.field = field.value;
        } else {
            break;
        }

        if(head == NULL) head = d;
        else tail->ast.designator.rest = d;
        tail = d;
    }

    return head;
}

static Node* parse_init_element(Parser *parser) {
    TokenType t = peek(parser).type;

    if(t == OPEN_BRACKET || t == DOT) {
        Node* head = parse_designator_chain(parser);
        expect(parser, ASSIGN, "Esperado '=' apos o designador");

        head->ast.designator.value = (peek(parser).type == OPEN_BRACE)
            ? parse_init_list_g(parser) : parse_expr(parser);
        return head;
    }

    return (t == OPEN_BRACE) ? parse_init_list_g(parser) : parse_expr(parser);
}

static Node* parse_expr(Parser *parser) {
    return parse_assignment(parser);
}

static Node* parse_expr_statement(Parser *parser) {
    Node* node = parse_expr(parser);
    expect(parser, SEMICOLON, "Esperado ';' apos a expressao");
    return node;
}

static Node* parse_init_list(Parser *parser) {
    Node* node = create_node(parser, NODE_INIT_LIST);
    NodeList* elements = list_create(parser->arena);
    node->ast.init_list.elements = elements;
    node->ast.init_list._zero = false;

    Token open = expected(parser, OPEN_BRACE, "Esperado '{' para inicializar a lista");

    while(!isend(parser) && peek(parser).type != CLOSE_BRACE) {
        list_push(parser->arena, elements, parse_init_element(parser));

        if(!match(parser, COMMA)) break;
    }

    expect_close_brace(parser, open, "Esperado '}' para fechar o bloco");

    if(elements->count == 1 && elements->items[0]->kind == NODE_LITERAL &&
        elements->items[0]->ast.literal.kind == INT && elements->items[0]->ast.literal.i64 == 0)
        node->ast.init_list._zero = true;

    return node;
}

/* ------------------------------------------------------------------ */
/* Declaracoes                                                         */
/* ------------------------------------------------------------------ */
static Node* parse_var_declarator(Parser *parser, const DeclSpecs *specs, TypeSpec spec,
                                  Token name, DeclContext ctx) {
    if(spec.base == KVOID && spec.ptr_lvl == 0)
        PSOFT(parser, name, "'%.*s' nao pode ter o tipo void", SV_ARGS(name.value));

    Node* node = create_node_at(parser, NODE_VAR_DECL, specs->start);
    node->ast.var_decl.name = name.value;
    node->ast.var_decl._static = specs->_static;
    node->ast.var_decl._extern = specs->_extern;
    node->ast.var_decl._atomic = typespec_top_atomic(&spec);
    node->ast.var_decl._const = typespec_top_const(&spec);

    parse_array_dimensions(parser, &spec);
    node->ast.var_decl.call_type = spec;

    if(ctx != DECL_MEMBER) declare_ordinary(parser, name);

    if(peek(parser).type == ASSIGN) {
        Token assign = advance(parser);

        if(ctx == DECL_MEMBER)
            PSOFT(parser, assign, "Membros de data/coperate nao podem ter inicializador");
        else if(specs->_extern)
            PSOFT(parser, assign, "Variavel extern nao pode ter inicializador");

        node->ast.var_decl.init = (peek(parser).type == OPEN_BRACE) ? parse_init_list(parser) : parse_expr(parser);
    }

    return node;
}

static Node* parse_parameter(Parser *parser, size_t count) {
    DeclSpecs specs = { 0 };
    specs.start = peek(parser);
    parse_qualifiers(parser, &specs, true);

    if(specs._static || specs._extern || specs.inline__)
        PSOFT(parser, specs.start, "A funcao nao pode ter parametros static, extern ou inline");

    TypeSpec type = parse_type_spec(parser, false);
    parse_qualifiers(parser, &specs, false);
    type.base_const = specs._const;
    type.base_atomic = specs._atomic;
    parse_pointer_chain(parser, &type);

    Token name = peek(parser);
    bool is_void = (type.base == KVOID && type.ptr_lvl == 0);

    if(is_void && name.type != IDENTIFIER) {
        if(count != 0) PSOFT(parser, specs.start, "'void' so pode aparecer sozinho na lista de parametros");
        return NULL;
    }

    Node* node = create_node_at(parser, NODE_VAR_DECL, specs.start);

    if(is_void) PSOFT(parser, name, "O parametro '%.*s' nao pode ter o tipo void", SV_ARGS(name.value));

    if(name.type == IDENTIFIER) {
        consume(parser);
        node->ast.var_decl.name = name.value;
        declare_ordinary(parser, name);
    }

    parse_array_dimensions(parser, &type);

    node->ast.var_decl.call_type = type;
    node->ast.var_decl._const = typespec_top_const(&type);
    node->ast.var_decl._atomic = typespec_top_atomic(&type);
    return node;
}

static Node* parse_func_declarator(Parser *parser, const DeclSpecs *specs, Token name, TypeSpec spec) {
    Node* node = create_node_at(parser, NODE_FUNC_DECL, specs->start);
    /* const/atomic aqui se referem ao tipo de retorno, nao a qualquer qualificador da declaracao */
    node->ast.func_decl._atomic = typespec_top_atomic(&spec);
    node->ast.func_decl._const = typespec_top_const(&spec);
    node->ast.func_decl._extern = specs->_extern;
    node->ast.func_decl._static = specs->_static;
    node->ast.func_decl.inline__ = specs->inline__;
    node->ast.func_decl.name = name.value;
    node->ast.func_decl.call_type = spec;

    NodeList* params = list_create(parser->arena);
    node->ast.func_decl.params = params;

    scope_enter(parser);

    Token open = expected(parser, OPEN_PAREN, "Esperado '(' apos o nome da funcao");

    if(peek(parser).type != CLOSE_PAREN) {
        do {
            if(peek(parser).type == ELLIPSES) {
                Token ellipsis = advance(parser);

                if(params->count == 0)
                    PSOFT(parser, ellipsis, "Funcao variadica precisa de ao menos um parametro nomeado antes de '...'");

                node->ast.func_decl._variadic = true;
                break;
            }

            Node* param = parse_parameter(parser, params->count);
            if(param == NULL) break;

            list_push(parser->arena, params, param);
        } while(match(parser, COMMA));
    }

    expect_closing(parser, CLOSE_PAREN, open, "Esperado ')' apos os parametros da funcao");

    if(peek(parser).type == OPEN_BRACE) {
        if(specs->_extern) PSOFT(parser, peek(parser), "Funcao extern nao pode ter corpo");
        node->ast.func_decl.body = parse_block(parser, false);
    } else {
        expect(parser, SEMICOLON, "Esperado '{' ou ';' apos a assinatura da funcao");
    }

    scope_leave(parser);
    return node;
}

static Node* parse_newtype_decl(Parser *parser) {
    Token kw = advance(parser);

    DeclSpecs specs = { 0 };
    specs.start = peek(parser);
    parse_qualifiers(parser, &specs, false);

    TypeSpec base = parse_type_spec(parser, true);
    parse_qualifiers(parser, &specs, false);
    base.base_const = specs._const;
    base.base_atomic = specs._atomic;

    Node *head = NULL, *tail = NULL;

    do {
        TypeSpec spec = base;
        parse_pointer_chain(parser, &spec);

        Token name = peek(parser);
        if(name.type != IDENTIFIER) {
            PERR(parser, name, "Esperado o nome do newtype, mas encontrado: %s", token_to_string(name.type));
            break;
        }

        consume(parser);
        parse_array_dimensions(parser, &spec);

        Node* node = create_node_at(parser, NODE_NEWTYPE, kw);
        node->ast.newtype.alias = name.value;
        node->ast.newtype.underlying = spec;

        declare_newtype(parser, name);
        chain_append(&head, &tail, node);
    } while(match(parser, COMMA));

    expect(parser, SEMICOLON, "Esperado ';' apos o newtype");
    return head;
}

static Node* parse_declaration(Parser *parser, DeclContext ctx) {
    if(peek(parser).type == KNEWTYPE) {
        if(ctx == DECL_MEMBER)
            PSOFT(parser, peek(parser), "newtype nao pode ser declarado dentro de data/coperate");

        return parse_newtype_decl(parser);
    }

    DeclSpecs specs = { 0 };
    specs.start = peek(parser);
    parse_qualifiers(parser, &specs, true);

    if(ctx == DECL_MEMBER && (specs._static || specs._extern || specs.inline__))
        PSOFT(parser, specs.start, "Membros de data/coperate nao podem ser static, extern ou inline");

    if(specs._static && specs._extern)
        PSOFT(parser, specs.start, "Uma declaracao nao pode ser static e extern ao mesmo tempo");

    /* extern nao tem corpo, entao nao ha o que inlinar */
    if(specs.inline__ && specs._extern)
        PSOFT(parser, specs.inline_tok, "Uma funcao nao pode ser inline e extern ao mesmo tempo");

    TypeSpec base = parse_type_spec(parser, true);
    parse_qualifiers(parser, &specs, false);
    base.base_const = specs._const;
    base.base_atomic = specs._atomic;

    if(match(parser, SEMICOLON)) {
        if(specs.inline__ && ctx != DECL_MEMBER)
            PSOFT(parser, specs.inline_tok, "'inline' so pode ser usado em declaracoes de funcao");

        if(base.nested != NULL) return base.nested;

        PSOFT(parser, specs.start, "Declaracao vazia: esperado um nome apos o tipo");
        return NULL;
    }

    Node *head = NULL, *tail = NULL;
    bool first = true;

    do {
        TypeSpec spec = base;
        parse_pointer_chain(parser, &spec);

        Token name = peek(parser);
        if(name.type != IDENTIFIER) {
            PERR(parser, name, "Esperado o nome da declaracao, mas encontrado: %s", token_to_string(name.type));
            break;
        }

        consume(parser);

        if(peek(parser).type == OPEN_PAREN) {
            if(!first) PSOFT(parser, name, "Uma funcao nao pode aparecer em uma lista de declaradores");
            if(ctx != DECL_GLOBAL) PSOFT(parser, name, "Funcoes so podem ser declaradas no escopo global");

            declare_ordinary(parser, name);
            chain_append(&head, &tail, parse_func_declarator(parser, &specs, name, spec));
            return head;
        }

        if(first && specs.inline__ && ctx != DECL_MEMBER)
            PSOFT(parser, specs.inline_tok, "'inline' so pode ser usado em funcoes ('%.*s' nao e uma funcao)",
                  SV_ARGS(name.value));

        chain_append(&head, &tail, parse_var_declarator(parser, &specs, spec, name, ctx));
        first = false;
    } while(match(parser, COMMA));

    expect(parser, SEMICOLON, "Esperado ';' apos a declaracao");
    return head;
}

/* ------------------------------------------------------------------ */
/* Comandos                                                            */
/* ------------------------------------------------------------------ */
static Node* parse_break_statement(Parser *parser) {
    Node* node = create_node(parser, NODE_BREAK_STMT);

    expect(parser, KBREAK, "Esperado break");
    expect(parser, SEMICOLON, "Esperado ';' apos o break");

    return node;
}

static Node* parse_continue_statement(Parser *parser) {
    Node* node = create_node(parser, NODE_CONTINUE_STMT);

    expect(parser, KCONTINUE, "Esperado continue");
    expect(parser, SEMICOLON, "Esperado ';' apos o continue");

    return node;
}

static Node* parse_call_statement(Parser *parser) {
    Node* node = create_node(parser, NODE_CALL_STMT);

    expect(parser, KCALL, "Esperado call");

    if(peek(parser).type != SEMICOLON) node->ast.call_statement.result = parse_expr(parser);

    expect(parser, SEMICOLON, "Esperado ';' apos a expressao");
    return node;
}

static Node* parse_label_statement(Parser *parser) {
    Token kw = advance(parser);
    bool is_jump = (kw.type == KJUMP);
    Node* node = create_node_at(parser, is_jump ? NODE_JUMP_LABEL : NODE_LOAD_LABEL, kw);

    Token name = expected(parser, IDENTIFIER,
        is_jump ? "Esperado o nome do rotulo apos 'jump'" : "Esperado o nome do rotulo apos 'load'");
    node->ast.jmp_label.name = name.value;

    expect(parser, SEMICOLON, is_jump ? "Esperado ';' apos o jump" : "Esperado ';' apos o load");
    return node;
}

static Node* parse_body(Parser *parser) {
    if(starts_declaration(parser))
        PSOFT(parser, peek(parser), "Uma declaracao nao pode ser o corpo direto da estrutura; use '{ }'");

    scope_enter(parser);
    Node* body = parse_statement(parser);
    scope_leave(parser);
    return body;
}

static Node* parse_condition(Parser *parser, const char* open_msg, const char* close_msg) {
    Token open = expected(parser, OPEN_PAREN, open_msg);
    Node* condition = parse_expr(parser);
    expect_closing(parser, CLOSE_PAREN, open, close_msg);
    return condition;
}

static Node* parse_if_statement(Parser *parser) {
    Token kw = advance(parser);
    Node* node = create_node_at(parser, NODE_IF_STMT, kw);

    node->ast.if_stmt.condition = parse_condition(parser, "Esperado '(' apos 'if'", "Esperado ')' apos a condicao do if");
    node->ast.if_stmt.then = parse_body(parser);

    if(match(parser, KELSE)) node->ast.if_stmt.otherwise = parse_body(parser);

    return node;
}

static Node* parse_while_statement(Parser *parser) {
    Token kw = advance(parser);
    Node* node = create_node_at(parser, NODE_WHILE_LOOP, kw);

    node->ast.while_loop.condition = parse_condition(parser, "Esperado '(' apos 'while'", "Esperado ')' apos a condicao do while");
    node->ast.while_loop.body = parse_body(parser);
    return node;
}

static Node* parse_do_statement(Parser *parser) {
    Token kw = advance(parser);
    Node* node = create_node_at(parser, NODE_DO_WHILE_LOOP, kw);

    node->ast.while_loop.body = parse_body(parser);

    expect(parser, KWHILE, "Esperado 'while' apos o corpo do do");
    node->ast.while_loop.condition = parse_condition(parser, "Esperado '(' apos 'while'", "Esperado ')' apos a condicao do do-while");
    expect(parser, SEMICOLON, "Esperado ';' apos o do-while");
    return node;
}

static Node* parse_for_statement(Parser *parser) {
    Token kw = advance(parser);
    Node* node = create_node_at(parser, NODE_FOR_LOOP, kw);

    scope_enter(parser);
    expect(parser, OPEN_PAREN, "Esperado '(' apos 'for'");

    if(match(parser, SEMICOLON)) node->ast.for_loop.init = NULL;
    else if(starts_declaration(parser)) node->ast.for_loop.init = parse_declaration(parser, DECL_LOCAL);
    else node->ast.for_loop.init = parse_expr_statement(parser);

    if(peek(parser).type != SEMICOLON) node->ast.for_loop.condition = parse_expr(parser);
    expect(parser, SEMICOLON, "Esperado ';' apos a condicao do for");

    if(peek(parser).type != CLOSE_PAREN) node->ast.for_loop.increment = parse_expr(parser);
    expect(parser, CLOSE_PAREN, "Esperado ')' apos o incremento do for");

    node->ast.for_loop.body = parse_body(parser);

    scope_leave(parser);
    return node;
}

static Node* parse_statement_impl(Parser *parser) {
    Token tok = peek(parser);

    switch (tok.type)
    {
    case OPEN_BRACE: return parse_block(parser, true);
    case KIF: return parse_if_statement(parser);
    case KWHILE: return parse_while_statement(parser);
    case KDO: return parse_do_statement(parser);
    case KFOR: return parse_for_statement(parser);
    case KBREAK: return parse_break_statement(parser);
    case KCONTINUE: return parse_continue_statement(parser);
    case KCALL: return parse_call_statement(parser);

    case SEMICOLON: {
        consume(parser);
        Node* node = create_node_at(parser, NODE_BLOCK_STMT, tok);
        node->ast.block.statements = list_create(parser->arena);
        return node;
    }

    case KLOAD:
    case KJUMP: return parse_label_statement(parser);

    default: break;
    }

    if(starts_declaration(parser)) return parse_declaration(parser, DECL_LOCAL);
    return parse_expr_statement(parser);
}

static Node* parse_block(Parser *parser, bool new_scope) {
    Node* node = create_node(parser, NODE_BLOCK_STMT);
    NodeList* statements = list_create(parser->arena);
    node->ast.block.statements = statements;

    if(new_scope) scope_enter(parser);

    Token open = expected(parser, OPEN_BRACE, "Esperado '{' para abrir o bloco");

    while(!isend(parser) && peek(parser).type != CLOSE_BRACE) {
        size_t before = parser->consumed;
        list_push_chain(parser->arena, statements, parse_statement(parser));
        recover(parser, before);
    }

    expect_close_brace(parser, open, "Esperado '}' para fechar o bloco");

    if(new_scope) scope_leave(parser);
    return node;
}

/* ------------------------------------------------------------------ */
/* Entrada                                                             */
/* ------------------------------------------------------------------ */
Parser create_parser(Lexer *lexer, DiagnosticContext *ctx, Arena *arena) {
    Parser parser = { 0 };
    parser.arena = arena;
    parser.lexer = lexer;
    parser.ctx = ctx;
    parser.filename = lexer->filename;       /* Token nao carrega mais o arquivo */
    memset(parser.syms.heads, 0xFF, sizeof(parser.syms.heads));

    parser.current = next_token(lexer);
    parser.next = (parser.current.type == EOFF) ? parser.current : next_token(lexer);
    return parser;
}

static Node* parse_global_item(Parser *parser) {
    Token tok = peek(parser);

    if(tok.type == SEMICOLON) {
        consume(parser);
        diag_warning(parser->ctx, parser->filename, tok.line, tok.col, "';' extra no escopo global foi ignorado");
        return NULL;
    }

    if(starts_declaration(parser)) return parse_declaration(parser, DECL_GLOBAL);

    PERR(parser, tok, "Esperada uma declaracao no escopo global, mas encontrado: %s", token_to_string(tok.type));
    return NULL;
}

Node* parse_program(Parser *parser) {
    Node* program = create_node(parser, NODE_PROGRAM);
    NodeList* statements = list_create(parser->arena);
    program->ast.program.statements = statements;

    while(!isend(parser)) {
        size_t before = parser->consumed;
        list_push_chain(parser->arena, statements, parse_global_item(parser));
        recover_global(parser, before);
    }

    return program;
}
