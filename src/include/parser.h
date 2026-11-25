#pragma once

#include "common.h"
#include "lexer.h"
#include "arena.h"
#include "diagnostics.h"
#include "types.h"

typedef struct NodeList NodeList;

typedef struct {
    TokenType base;
    View alias;

    struct Node* nested;

    uint8_t ptr_lvl;
    uint32_t ptr_const_mask;
    uint32_t ptr_atomic_mask;
    uint8_t base_const;
    uint8_t base_atomic;

    NodeList* array_dim;
    uint8_t is_array;
} TypeSpec;

/* Item achatado de uma lista de inicializacao (preenchido pelo typecheck). */
typedef struct {
    size_t offset;              /* bytes desde o inicio do objeto inicializado */
    size_t size;                /* bytes cobertos pelo item */
    const Typecheck* type;      /* tipo do slot (NULL quando is_string) */
    struct Node* value;         /* expressao escalar, ou literal string se is_string */
    uint8_t is_string;          /* char[N] <- string literal (copia de bytes) */
} InitItem;

typedef struct Node {
    NodeKinds kind;
    const char* filepath;
    size_t line;
    size_t col;

    struct Node* next;

    Typecheck resolved_type;
    size_t func_id;

    union {
        struct {
            View name;
        } var_call;

        struct {
            TypeSpec call_type;
            View name;

            bool _const;
            bool _atomic;
            bool _static;
            bool _extern;

            struct Node* init;
        } var_decl;

        struct {
            NodeList* elements;
            bool _zero;
            bool dense;             /* itens cobrem todos os campos/elementos (sem memzero previo) */
            size_t item_count;
            InitItem* items;        /* ordenados por offset, sem sobreposicao */
        } init_list;

        struct {
            View name;
            NodeList* members;
        } enum_decl;

        struct {
            View name;
            struct Node* value;
        } enum_member;

        struct {
            View alias;
            View member;
            long long iresolved_type;
        } enum_access;

        struct {
            TypeSpec call_type;
            View name;

            NodeList* params;

            bool _variadic;
            bool _static;
            bool _const;
            bool _atomic;
            bool _extern;
            bool inline__;

            struct Node* body;
        } func_decl;

        struct {
            View name;
            NodeList* args;
        } func_call;

        struct {
            struct Node* result;
        } call_statement;

        struct {
            View name;
            NodeList* members;
            struct Node* tailing;
        } aggregate;

        struct {
            struct Node* base;
            View name;
            bool arrow;
            size_t offset;
            Typecheck base_type;
        } field_access;

        struct {
            View alias;
            TypeSpec underlying;
        } newtype;

        struct {
            struct Node* operand;
            TokenType op;
        } unary;

        struct {
            TokenType op;
            struct Node* left;
            struct Node* right;
        } binary;

        struct {
            struct Node* condition;
            struct Node* body;
        } while_loop;

        struct {
            struct Node* init;
            struct Node* condition;
            struct Node* increment;
            struct Node* body;
        } for_loop;

        struct {
            struct Node* condition;
            struct Node* then;
            struct Node* otherwise;
        } if_stmt;

        struct {
            TypeSpec spec;
            struct Node* operand;
        } cast;

        struct {
            LiteralKind kind;
            union {
                long long i64;
                unsigned long long u64;
                long double f64;
                View character; 
                View string;
            };
        } literal;

        struct {
            View name;
        } jmp_label;

        struct {
            TypeSpec spec;
            struct Node* operand;
            bool is_expr;
            size_t size;
        } bytes;

        struct {
            bool is_index;
            struct Node* index;
            View field;
            struct Node* rest;
            struct Node* value;
        } designator;

        struct {
            NodeList* statements;
        } block;

        struct {
            NodeList* statements;
        } program;
    } ast;
} Node;

newtype_array_definition(Node*, NodeList);

#define SYM_BUCKETS 256u

typedef struct {
    View name;
    Token origin;
    int32_t prev;
    uint32_t bucket;
    int depth;
    bool is_type;
} SymEntry;

typedef struct {
    SymEntry* items;
    size_t count;
    size_t capacity;
    int32_t heads[SYM_BUCKETS];
    int depth;
} SymTable;

typedef struct {
    Lexer* lexer;
    Arena* arena;
    const char* filename;
    Token next;
    Token current;
    Token prev;

    bool panic;
    size_t consumed;
    int depth;

    SymTable syms;
    DiagnosticContext* ctx;
} Parser;

Parser create_parser(Lexer *lexer, DiagnosticContext *ctx, Arena *arena);
Node* parse_program(Parser *parser);