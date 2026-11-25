#include "./include/lexer.h"
#include "./include/parser.h"
#include "./include/string_helper.h"
#include "./include/diagnostics.h"
#include "./include/typecheck.h"
#include "./include/arena.h"
#include "./include/ir.h"
#include "./include/codegen.h"
#include "./include/driver.h"
#include "./include/platform.h"

#define MAX_PATHS_POSSIBLE 256
#define DEFAULT_OUT_PATH "./out.s"
#define NL fprintf(out, "\n")
#define NLSTDOUT fprintf(stdout, "\n");

typedef struct {
    const char* path;
    Arena arena;
    LineMap map;
    Preprocess pp;
    DiagnosticContext ctx;
    Node* program;
    Node* entry;
    IrGenContext ir;
} Unit;

typedef enum {
    DEFAULT_BEFORE_PREPROCESS,
    DEFAULT_PREPROCESS,
    DEFAULT_LEXER,
    DEFAULT_PARSER,
    DEFAULT_CHECK,
    DEFAULT_IR,
    DEFAULT_GEN,
    DEFAULT_OBJ
} DefaultPathNamePriority;

typedef struct {
    char* resolved[MAX_PATHS_POSSIBLE];
    uint16_t count;
    char* out_path;
} PathResolve;

typedef struct {
    bool before_preprocess;
    bool preprocess;
    bool lexer;
    bool parse;
    bool check;
    bool check_table;
    bool ir;
    bool codegen;
    bool obj;
    PathResolve paths;
    DefaultPathNamePriority priority;
    BuildOptions build;
} DebugOptionsConfiguration;

static DebugOptionsConfiguration shift(int argc, char** argv) {
    DebugOptionsConfiguration options = { 0 };
    options.build = driver_default_options();
    options.paths.out_path = DEFAULT_OUT_PATH;
    
    for(int i = 1; i < argc; ++i) {
        if(strcmp(argv[i], "flag-bfp") == 0) {
            options.before_preprocess = true;
            options.priority = (options.priority > DEFAULT_BEFORE_PREPROCESS) ? options.priority : DEFAULT_BEFORE_PREPROCESS;
        } else if(strcmp(argv[i], "flag-pp") == 0) {
            options.preprocess = true;
            options.priority = (options.priority > DEFAULT_PREPROCESS) ? options.priority : DEFAULT_PREPROCESS;
        } else if(strcmp(argv[i], "flag-lex") == 0) {
            options.lexer = true;
            options.priority = (options.priority > DEFAULT_LEXER) ? options.priority : DEFAULT_LEXER;
        } else if(strcmp(argv[i], "flag-parse") == 0) {
            options.parse = true;
            options.priority = (options.priority > DEFAULT_PARSER) ? options.priority : DEFAULT_PARSER; 
        } else if(strcmp(argv[i], "flag-check") == 0) {
            options.check = true;
            options.priority = (options.priority > DEFAULT_CHECK) ? options.priority : DEFAULT_CHECK; 
        } else if(strcmp(argv[i], "flag-ir") == 0) {
            options.ir = true;
            options.priority = (options.priority > DEFAULT_IR) ? options.priority : DEFAULT_IR;
        } else if(strcmp(argv[i], "flag-codegen") == 0) {
            options.build.asm_only = true;
            options.priority = (options.priority > DEFAULT_GEN) ? options.priority : DEFAULT_GEN;
        } else if(strcmp(argv[i], "flag-out") == 0) {
            if(i + 1 < argc)
                options.paths.out_path = argv[++i];
            else {
                fprintf(stderr, "Erro: a flag 'flag-out' requer um caminho de saída.\n");
                exit(WRONG_PATH);
            }
        } else if(strcmp(argv[i], "flag-obj") == 0) {
            options.build.obj_only = true;
            options.priority = (options.priority > DEFAULT_OBJ) ? options.priority : DEFAULT_OBJ;
        } else if(strcmp(argv[i], "-check-function-table") == 0) {
            options.check_table = true;
        } else {
            if(options.paths.count >= MAX_PATHS_POSSIBLE) {
                fprintf(stderr, "Erro: maximo de %d arquivos excedido.\n", MAX_PATHS_POSSIBLE);
                exit(OUT_OF_PATH);
            }
            options.paths.resolved[options.paths.count++] = argv[i];
        }
    }

    return options;
}

static const char* prio_to_str(DefaultPathNamePriority priority) {
    switch (priority)
    {
    case DEFAULT_BEFORE_PREPROCESS: return "before_preprocess";
    case DEFAULT_PREPROCESS: return "preprocess";
    case DEFAULT_LEXER: return "lexer";
    case DEFAULT_PARSER: return "parser";
    case DEFAULT_CHECK: return "typecheck";
    case DEFAULT_IR: return "ir";
    case DEFAULT_GEN: return "codegen";
    case DEFAULT_OBJ: return "obj";
    }

    return "bug_dump";
}

static char* readfile(const char* path) {
    FILE* file = fopen(path, "rb");
    if(!file) {
        fprintf(stderr, "O arquivo passado ao compilador está incorreto: %s\n", path);
        exit(WRONG_PATH);
    }

    fseek(file, 0L, SEEK_END);
    long tell = ftell(file);
    fseek(file, 0L, SEEK_SET);

    if(tell < 0) { 
        fprintf(stderr, "Nao foi possivel medir o arquivo: %s\n", path); 
        fclose(file); 
        exit(READFILE_ERROR); 
    }

    char* src = (char*)malloc(tell + 1);
    if(src == NULL) {
        fprintf(stderr, "Erro no malloc, tente novamente compilar o programa\n");
        fclose(file);
        exit(ALLOCATE_ERROR);
    }

    size_t bytes = fread(src, 1, tell, file);
    if(bytes < (size_t)tell) {
        fprintf(stderr, "Os bytes do arquivo são maiores que os bytes lidos\n");
        fclose(file);
        free(src);
        exit(READFILE_ERROR);
    }

    
    src[bytes] = '\0';

    fclose(file);
    return src;
}

static char* sanitize_path(const char* path, char* out, size_t out_size) {
    size_t i = 0, j = 0;

    while(path[i] != '\0' && j < out_size - 1) {
        if(path[i] == '.' && (path[i + 1] == '/' || path[i + 1] == '\\')) {
            ++i;
            continue;
        }

        char c = path[i++];
        out[j++] = (c == '/' || c == '\\') ? '_' : c;
    }

    out[j] = '\0';
    return out;
}

static void indent(FILE *out, int depth) {
    for(int i = 0; i < depth; ++i) fprintf(out, "  ");
}

static void print_view(FILE *out, View view) {
    fprintf(out, "%.*s", (int)view.len, view.start);
}

static void print_node(FILE *out, Node *node, int depth);

static void print_nodelist(FILE *out, NodeList *list, int depth) {
    if(!list) return;

    for(size_t i = 0; i < list->count; ++i) {
        print_node(out, list->items[i], depth);
    }
}

static void print_type_spec(FILE* out, TypeSpec spec) {
    if(spec.base == IDENTIFIER) {
        if(spec.nested)
            fprintf(out, "<inline aggregate>");
        else
            print_view(out, spec.alias);
    } else {
        fprintf(out, "%s", token_to_string(spec.base));
    }

    for(uint8_t i = 0; i < spec.ptr_lvl; ++i) fprintf(out, "*");
}

static const char* bool_to_str(bool istrue) {
    if(istrue) return "true";

    return "false";
}

static void print_type_extras(FILE *out, TypeSpec spec, int depth) {
    if(spec.base_const || spec.base_atomic) {
        indent(out, depth);
        fprintf(out, " base_const = %s, base_atomic = %s", bool_to_str(spec.base_const), bool_to_str(spec.base_atomic)); NL;
    }

    if(spec.ptr_lvl > 0) {
        indent(out, depth);
        fprintf(out, " ptr_const_mask = 0x%x, ptr_atomic_mask = 0x%x", spec.ptr_const_mask, spec.ptr_atomic_mask); NL;
    }

    if(spec.is_array) {
        indent(out, depth); fprintf(out, " array dims:"); NL;
        print_nodelist(out, spec.array_dim, depth + 1);
    }

    if(spec.base == IDENTIFIER && spec.nested) {
        indent(out, depth); fprintf(out, " nested:"); NL;
        print_node(out, spec.nested, depth + 1);
    }
}

static void print_node(FILE *out, Node *node, int depth) {
    if(!node) {
        indent(out, depth);
        fprintf(out, "<null>"); NL;
        return;
    }

    indent(out, depth);

    switch (node->kind)
    {
    case NODE_PROGRAM:
        fprintf(out, "PROGRAM [%s:%zu:%zu]", node->filepath, node->line, node->col); NL;
        print_nodelist(out, node->ast.program.statements, depth + 1);
        break;

    case NODE_BLOCK_STMT:
        fprintf(out, "BLOCK [%zu:%zu]", node->line, node->col); NL;
        print_nodelist(out, node->ast.block.statements, depth + 1);
        break;

    case NODE_VAR_DECL:
        fprintf(out, "VAR DECLARATION [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " name = "); print_view(out, node->ast.var_decl.name); NL;
        indent(out, depth); fprintf(out, " type = "); print_type_spec(out, node->ast.var_decl.call_type); NL;
        indent(out, depth); fprintf(out, " const = %s", bool_to_str(node->ast.var_decl._const)); NL;
        indent(out, depth); fprintf(out, " atomic = %s", bool_to_str(node->ast.var_decl._atomic)); NL;
        indent(out, depth); fprintf(out, " static = %s", bool_to_str(node->ast.var_decl._static)); NL;
        indent(out, depth); fprintf(out, " extern = %s", bool_to_str(node->ast.var_decl._extern)); NL;
        indent(out, depth); fprintf(out, " pointer_lvl = %d", node->ast.var_decl.call_type.ptr_lvl); NL;
        print_type_extras(out, node->ast.var_decl.call_type, depth);

        if(node->ast.var_decl.init) {
            indent(out, depth); fprintf(out, " init:"); NL;
            print_node(out, node->ast.var_decl.init, depth + 1);
        }
        NL;
        break;

    case NODE_VAR_CALL:
        fprintf(out, "VAR CALL [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " name = "); print_view(out, node->ast.var_call.name); NL;
        break;

    case NODE_CAST:
        fprintf(out, "CAST [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " cast for = "); print_type_spec(out, node->ast.cast.spec); NL;
        print_type_extras(out, node->ast.cast.spec, depth);
        indent(out, depth); fprintf(out, " operand:"); NL;
        print_node(out, node->ast.cast.operand, depth + 1);
        break;

    case NODE_BYTES:
        fprintf(out, "BYTES [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " is_expr = %s", bool_to_str(node->ast.bytes.is_expr)); NL;
        indent(out, depth); fprintf(out, " size = %zu", node->ast.bytes.size); NL;

        if(node->ast.bytes.is_expr) {
            indent(out, depth); fprintf(out, " operand:"); NL;
            print_node(out, node->ast.bytes.operand, depth + 1);
        } else {
            indent(out, depth); fprintf(out, " spec = "); print_type_spec(out, node->ast.bytes.spec); NL;
            print_type_extras(out, node->ast.bytes.spec, depth);
        }
        break;

    case NODE_UNARY_OP:
        fprintf(out, "UNARY OP [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " operator = %s", token_to_string(node->ast.unary.op)); NL;
        print_node(out, node->ast.unary.operand, depth + 1);
        break;

    case NODE_POSTFIX_OP:
        fprintf(out, "POSTFIX OP [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " operator = %s", token_to_string(node->ast.unary.op)); NL;
        print_node(out, node->ast.unary.operand, depth + 1);
        break;

    case NODE_BINARY_OP:
        fprintf(out, "BINARY OP [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " operator = %s", token_to_string(node->ast.binary.op)); NL;
        print_node(out, node->ast.binary.left, depth + 1);
        print_node(out, node->ast.binary.right, depth + 1);
        break;

    case NODE_TERNARY:
        fprintf(out, "TERNARY [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " condition:"); NL;
        print_node(out, node->ast.if_stmt.condition, depth + 1);
        indent(out, depth); fprintf(out, " then:"); NL;
        print_node(out, node->ast.if_stmt.then, depth + 1);
        indent(out, depth); fprintf(out, " otherwise:"); NL;
        print_node(out, node->ast.if_stmt.otherwise, depth + 1);
        NL;
        break;

    case NODE_BREAK_STMT:
        fprintf(out, "BREAK [%zu:%zu]", node->line, node->col); NL;
        break;

    case NODE_CONTINUE_STMT:
        fprintf(out, "CONTINUE [%zu:%zu]", node->line, node->col); NL;
        break;

    case NODE_LITERAL:
        fprintf(out, "LITERAL [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth);

        switch (node->ast.literal.kind)
        {
        case STRING: fprintf(out, " string = "); print_view(out, node->ast.literal.string); break;
        case CHAR: fprintf(out, " char = "); print_view(out, node->ast.literal.character); break;
        case HEXA: fprintf(out, " hexa = 0x%llx", node->ast.literal.u64); break;
        case UNSIGNED: fprintf(out, " uint = %llu", node->ast.literal.u64); break;
        case INT: fprintf(out, " int = %lld", node->ast.literal.i64); break;
        case BIG: fprintf(out, " big = %lld", node->ast.literal.i64); break;
        case FLOAT: fprintf(out, " float = %Lf", node->ast.literal.f64); break;
        case DOUBLE: fprintf(out, " double = %Lf", node->ast.literal.f64); break;
        case WIDE_CHAR: fprintf(out, " wchar = %c", node->ast.literal.character);
        }
        NL;
        break;

    case NODE_CALL_STMT:
        fprintf(out, "CALL [%zu:%zu]", node->line, node->col); NL;
        if(node->ast.call_statement.result != NULL) print_node(out, node->ast.call_statement.result, depth + 1);
        break;

    case NODE_FUNC_DECL:
        fprintf(out, "FUNC DECLARATION [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " name = "); print_view(out, node->ast.func_decl.name); NL;
        indent(out, depth); fprintf(out, " call type = "); print_type_spec(out, node->ast.func_decl.call_type); NL;
        indent(out, depth); fprintf(out, " atomic = %s", bool_to_str(node->ast.func_decl._atomic)); NL;
        indent(out, depth); fprintf(out, " static = %s", bool_to_str(node->ast.func_decl._static)); NL;
        indent(out, depth); fprintf(out, " variadic = %s", bool_to_str(node->ast.func_decl._variadic)); NL;
        indent(out, depth); fprintf(out, " const = %s", bool_to_str(node->ast.func_decl._const)); NL;
        indent(out, depth); fprintf(out, " extern = %s", bool_to_str(node->ast.func_decl._extern)); NL;
        print_type_extras(out, node->ast.func_decl.call_type, depth);
        indent(out, depth); fprintf(out, " params:"); NL;
        print_nodelist(out, node->ast.func_decl.params, depth + 1);

        if(node->ast.func_decl.body) {
            indent(out, depth);
            fprintf(out, " body:"); NL;
            print_node(out, node->ast.func_decl.body, depth + 1);
        }
        NL;
        break;

    case NODE_FUNC_CALL:
        fprintf(out, "FUNC CALL [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " name = "); print_view(out, node->ast.func_call.name); NL;
        if(node->ast.func_call.args) {
            indent(out, depth); fprintf(out, " args:"); NL;
            print_nodelist(out, node->ast.func_call.args, depth + 1);
        }
        NL;
        break;

    case NODE_WHILE_LOOP:
        fprintf(out, "WHILE [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " expr:"); NL;
        print_node(out, node->ast.while_loop.condition, depth + 1);

        if(node->ast.while_loop.body) {
            indent(out, depth);
            fprintf(out, " body:"); NL;
            print_node(out, node->ast.while_loop.body, depth + 1);
        }
        NL;
        break;

    case NODE_DO_WHILE_LOOP:
        fprintf(out, "DO WHILE [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " body:"); NL;
        print_node(out, node->ast.while_loop.body, depth + 1);
        indent(out, depth); fprintf(out, " expr:"); NL;
        print_node(out, node->ast.while_loop.condition, depth + 1);
        NL;
        break;

    case NODE_FOR_LOOP:
        fprintf(out, "FOR [%zu:%zu]", node->line, node->col); NL;

        if(node->ast.for_loop.init) {
            indent(out, depth); fprintf(out, " init:"); NL;
            print_node(out, node->ast.for_loop.init, depth + 1);
        }

        if(node->ast.for_loop.condition) {
            indent(out, depth); fprintf(out, " expr:"); NL;
            print_node(out, node->ast.for_loop.condition, depth + 1);
        }

        if(node->ast.for_loop.increment) {
            indent(out, depth); fprintf(out, " increment:"); NL;
            print_node(out, node->ast.for_loop.increment, depth + 1);
        }

        if(node->ast.for_loop.body) {
            indent(out, depth); fprintf(out, " body:"); NL;
            print_node(out, node->ast.for_loop.body, depth + 1);
        }
        NL;
        break;

    case NODE_DATA:
        fprintf(out, "DATA [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " name = "); print_view(out, node->ast.aggregate.name); NL;
        indent(out, depth); fprintf(out, " members:"); NL;
        print_nodelist(out, node->ast.aggregate.members, depth + 1);

        if(node->ast.aggregate.tailing) {
            indent(out, depth); fprintf(out, " tailing:"); NL;
            print_node(out, node->ast.aggregate.tailing, depth + 1);
        }
        NL;
        break;

    case NODE_COPERATE:
        fprintf(out, "COPERATE [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " name = "); print_view(out, node->ast.aggregate.name); NL;
        indent(out, depth); fprintf(out, " members:"); NL;
        print_nodelist(out, node->ast.aggregate.members, depth + 1);

        if(node->ast.aggregate.tailing) {
            indent(out, depth); fprintf(out, " tailing:"); NL;
            print_node(out, node->ast.aggregate.tailing, depth + 1);
        }
        NL;
        break;

    case NODE_ENUM:
        fprintf(out, "ENUM [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " name = "); print_view(out, node->ast.enum_decl.name); NL;
        indent(out, depth); fprintf(out, " members:"); NL;
        print_nodelist(out, node->ast.enum_decl.members, depth + 1);
        NL;
        break;

    case NODE_ENUM_MEMBER:
        fprintf(out, "ENUM MEMBER [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " name = "); print_view(out, node->ast.enum_member.name); NL;

        if(node->ast.enum_member.value) {
            indent(out, depth); fprintf(out, " value:"); NL;
            print_node(out, node->ast.enum_member.value, depth + 1);
        }
        break;

    case NODE_ENUM_CALL:
        fprintf(out, "ENUM CALL [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " enum name = "); print_view(out, node->ast.enum_access.alias); NL;
        indent(out, depth); fprintf(out, " enum member = "); print_view(out, node->ast.enum_access.member); NL;
        break;

    case NODE_NEWTYPE:
        fprintf(out, "NEWTYPE [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " name = "); print_view(out, node->ast.newtype.alias); NL;
        indent(out, depth); fprintf(out, " spec:"); NL;
        indent(out, depth + 1); print_type_spec(out, node->ast.newtype.underlying); NL;
        print_type_extras(out, node->ast.newtype.underlying, depth + 1);
        NL;
        break;

    case NODE_FIELD_CALL:
        fprintf(out, "FIELD CALL [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " name = "); print_view(out, node->ast.field_access.name); NL;
        indent(out, depth); fprintf(out, " arrow = %s", bool_to_str(node->ast.field_access.arrow)); NL;
        indent(out, depth); fprintf(out, " base:"); NL;
        print_node(out, node->ast.field_access.base, depth + 1);
        NL;
        break;

    case NODE_IF_STMT:
        fprintf(out, "IF STATEMENT [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " condition:"); NL;
        print_node(out, node->ast.if_stmt.condition, depth + 1);
        indent(out, depth); fprintf(out, " then:"); NL;
        print_node(out, node->ast.if_stmt.then, depth + 1);
        if(node->ast.if_stmt.otherwise) {
            indent(out, depth); fprintf(out, " otherwise:"); NL;
            print_node(out, node->ast.if_stmt.otherwise, depth + 1);
        }
        NL;
        break;

    case NODE_INIT_LIST:
        fprintf(out, "INIT LIST [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " zero = %s", bool_to_str(node->ast.init_list._zero)); NL;
        indent(out, depth); fprintf(out, " elements:"); NL;
        print_nodelist(out, node->ast.init_list.elements, depth + 1);
        NL;
        break;

    case NODE_JUMP_LABEL:
        fprintf(out, "JUMP [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " label_name = "); print_view(out, node->ast.jmp_label.name); NL;
        break;

    case NODE_LOAD_LABEL:
        fprintf(out, "LOAD [%zu:%zu]", node->line, node->col); NL;
        indent(out, depth); fprintf(out, " label_name = "); print_view(out, node->ast.jmp_label.name); NL;
        break;

    default:
        fprintf(stdout, "Ainda criando o caso para o debug, esperando manutencao: %s", node_to_str(node->kind)); NLSTDOUT;
        return;
    }
}

static void debug_before_preprocess(FILE *out, const char* filename, const char* src) {
    fprintf(out, "Codigo original do arquivo: %s\n", filename);
    fputs(src, out);
    NL;
}

static void debug_preprocess(FILE *out, const char* filename, const char* processed) {
    fprintf(out, "Codigo preprocessado do arquivo: %s\n", filename);
    fputs(processed, out);
    NL;
}

static void debug_lexer(FILE *out, Lexer *lexer) {
    fprintf(out, "Analise lexica do arquivo: \n");

    Token token = { 0 };
    while ((token = next_token(lexer)).type != EOFF) {
        fprintf(out, "Token: %-20s | Lexeme: %-10.*s | String Len: %-3zu | Line: %-4zu | Col: %-4zu\n",
            token_to_string(token.type), (int)token.value.len, token.value.start, token.value.len,
            (size_t)token.line, (size_t)token.col);
    }

    NL;
}

static void debug_parser(FILE *out, Node *program) {
    fprintf(out, "Analise sintatica (AST):\n");
    print_node(out, program, 0);
}

static bool unit_analyze(Unit *u, const char* path) {
    u->path = path;
    init_arena(&u->arena, MB);
    diag_init(&u->ctx, &u->arena);
 
    diag_set_phase(&u->ctx, PREPROCESS);
    char lib[PATH_MAX];
    const char* lib_dir = stdlib_dir(lib, sizeof lib) ? lib : NULL;
    preprocess_run(&u->pp, path, lib_dir, &u->arena, &u->map);
    diag_register_source(&u->ctx, path, u->pp.original_src);
    diag_set_linemap(&u->ctx, path, &u->map);
 
    diag_set_phase(&u->ctx, SYNTAX);
    Lexer lexer = create_lexer(path, u->pp.process_src, &u->ctx);
    Parser parser = create_parser(&lexer, &u->ctx, &u->arena);
    u->program = parse_program(&parser);
    if(has_errors(&u->ctx)) return false;
 
    diag_set_phase(&u->ctx, SEMANTICAL);
    CheckContext check = create_checkctx(&u->arena, &u->ctx);
    check_program(&check, u->program);

    u->entry = check.entry_function;
    if(has_errors(&u->ctx)) return false;

    u->ir = create_irgen_context(&u->arena);
    irgen_program(&u->ir, u->program);
    return true;
}

static const char* ir_op_to_str(IrOperators op) {
    switch (op) {
    case IR_ADD: return "add";
    case IR_SUB: return "sub";
    case IR_MUL: return "mul";
    case IR_DIV: return "div";
    case IR_MOD: return "mod";
    case IR_BAND: return "band";
    case IR_BOR: return "bor";
    case IR_BXOR: return "bxor";
    case IR_SHL: return "shl";
    case IR_SHR: return "shr";
    case IR_NEG: return "neg";
    case IR_NOT: return "not";
    case IR_LNOT: return "lnot";
    case IR_CMP_LT: return "cmp_lt";
    case IR_CMP_LE: return "cmp_le";
    case IR_CMP_GT: return "cmp_gt";
    case IR_CMP_GE: return "cmp_ge";
    case IR_CMP_EQ: return "cmp_eq";
    case IR_CMP_NE: return "cmp_ne";
    case IR_CAST: return "cast";
    case IR_ASSIGN: return "assign";
    case IR_LABEL: return "label";
    case IR_JMP: return "jmp";
    case IR_JMP_IF_ZERO: return "jz";
    case IR_JMP_IF_NOT_ZERO: return "jnz";
    case IR_JMP_LT: return "jlt";
    case IR_JMP_LE: return "jle";
    case IR_JMP_GT: return "jgt";
    case IR_JMP_GE: return "jge";
    case IR_JMP_EQ: return "jeq";
    case IR_JMP_NE: return "jne";
    case IR_RETURN: return "ret";
    case IR_ARG: return "arg";
    case IR_ARG_STACK: return "arg_stack";
    case IR_CALL: return "call";
    case IR_LOAD_INDIRECT: return "load";
    case IR_STORE_INDIRECT: return "store";
    case IR_ALLOCA: return "alloca";
    case IR_STACK_SAVE: return "stack_save";
    case IR_STACK_RESTORE: return "stack_restore";
    case IR_MEMZERO: return "memzero";
    case IR_FENCE: return "fence";
    case IR_ATOMIC_LOAD: return "atomic_load";
    case IR_ATOMIC_STORE: return "atomic_store";
    case IR_ATOMIC_XCHG: return "atomic_xchg";
    case IR_ATOMIC_CAS: return "atomic_cas";
    case IR_ATOMIC_ADD: return "atomic_add";
    case IR_ATOMIC_AND: return "atomic_and";
    case IR_ATOMIC_OR: return "atomic_or";
    case IR_ATOMIC_XOR: return "atomic_xor";
    case IR_VA_START: return "va_start";
    }
    return "<op_invalido>";
}
 
static void print_ir_type(FILE *out, IrType t) {
    if(t.size == 0 && t.ptr_lvl == 0) { fprintf(out, "void"); return; }
 
    if(t.ptr_lvl > 0) {
        /* ponteiro: mostra o tipo base pelo step quando possivel */
        fprintf(out, "ptr%u[step=%u]", (unsigned)t.ptr_lvl, t.step);
    } else if(t.is_float) {
        fprintf(out, "f%u", t.size * 8);
    } else {
        fprintf(out, "%c%u", t.is_signed ? 'i' : 'u', t.size * 8);
    }
 
    if(t.is_atomic) fprintf(out, " atomic");
}
 
static IrFuncInfo* ir_find_func(IrGenContext *ctx, int id) {
    for(size_t i = 0; i < ctx->funcs.count; ++i)
        if(ctx->funcs.items[i] && ctx->funcs.items[i]->func_id == id)
            return ctx->funcs.items[i];
    return NULL;
}
 
static IrGlobal* ir_find_global(IrGenContext *ctx, int id) {
    for(size_t i = 0; i < ctx->globals.count; ++i)
        if(ctx->globals.items[i] && ctx->globals.items[i]->id == id)
            return ctx->globals.items[i];
    return NULL;
}
 
static void print_ir_escaped(FILE *out, View v) {
    size_t max = v.len > 40 ? 40 : v.len;
    for(size_t i = 0; i < max; ++i) {
        unsigned char c = (unsigned char)v.start[i];
        if(c == '\n') fprintf(out, "\\n");
        else if(c == '\t') fprintf(out, "\\t");
        else if(c == '\r') fprintf(out, "\\r");
        else if(c == '"') fprintf(out, "\\\"");
        else if(c < 32 || c > 126) fprintf(out, "\\x%02x", c);
        else fputc(c, out);
    }
    if(v.len > max) fprintf(out, "...");
}
 
static void print_ir_value(FILE *out, IrGenContext *ctx, IrValue v, bool with_type) {
    switch (v.kind) {
    case IR_VAL_NONE:
        fprintf(out, "_");
        return;
    case IR_VAL_TEMP:
        fprintf(out, "t%d", v.as.temp_id);
        break;
    case IR_VAL_CONST_INT:
        fprintf(out, "%lld", v.as.const_i);
        break;
    case IR_VAL_CONST_UINT:
        fprintf(out, "%lluu", v.as.const_ui);
        break;
    case IR_VAL_CONST_FLOAT: {
        bool found = false;
        for(size_t i = 0; i < ctx->floats.count; ++i) {
            if(ctx->floats.items[i].id == v.as.float_id) {
                fprintf(out, "%g%s", ctx->floats.items[i].value,
                        ctx->floats.items[i]._single ? "f" : "");
                found = true;
                break;
            }
        }
        if(!found) fprintf(out, "float#%d", v.as.float_id);
        break;
    }
    case IR_VAL_CONST_STRING: {
        bool found = false;
        for(size_t i = 0; i < ctx->strings.count; ++i) {
            if(ctx->strings.items[i].id == v.as.string_id) {
                fprintf(out, "str#%d \"", v.as.string_id);
                print_ir_escaped(out, ctx->strings.items[i].text);
                fprintf(out, "\"");
                found = true;
                break;
            }
        }
        if(!found) fprintf(out, "str#%d", v.as.string_id);
        break;
    }
    case IR_VAL_SLOT:
        fprintf(out, "slot%d", v.as.slot_id);
        break;
    case IR_VAL_SLOT_ADDR:
        fprintf(out, "&slot%d", v.as.slot_id);
        break;
    case IR_VAL_GLOBAL:
    case IR_VAL_GLOBAL_ADDR: {
        IrGlobal *g = ir_find_global(ctx, v.as.global_id);
        fprintf(out, v.kind == IR_VAL_GLOBAL_ADDR ? "&@" : "@");
        if(g) print_view(out, g->name);
        else fprintf(out, "global#%d", v.as.global_id);
        break;
    }
    case IR_VAL_LABEL:
        fprintf(out, "L%d", v.as.label_id);
        return; /* label nao tem tipo util */
    case IR_VAL_FUNC:
    case IR_VAL_BUILTIN: {
        IrFuncInfo *f = ir_find_func(ctx, v.as.func_id);
        if(v.kind == IR_VAL_BUILTIN) fprintf(out, "builtin ");
        if(f) print_view(out, f->name);
        else fprintf(out, "func#%d", v.as.func_id);
        return;
    }
    }
 
    if(v.field_offset != 0) fprintf(out, "%+d", v.field_offset);
 
    if(with_type) {
        fprintf(out, ":");
        print_ir_type(out, v.type);
    }
}
 
static bool ir_is_cond_jump(IrOperators op) {
    return op >= IR_JMP_LT && op <= IR_JMP_NE;
}
 
static void print_ir_instruction(FILE *out, IrGenContext *ctx, IrInstruction *in) {
    switch (in->op) {
    case IR_LABEL:
        print_ir_value(out, ctx, in->dest, false);
        fprintf(out, ":");
        break;
 
    case IR_JMP:
        fprintf(out, "jmp ");
        print_ir_value(out, ctx, in->dest, false);
        break;
 
    case IR_JMP_IF_ZERO:
    case IR_JMP_IF_NOT_ZERO:
        fprintf(out, "%s ", ir_op_to_str(in->op));
        print_ir_value(out, ctx, in->src1, true);
        fprintf(out, ", ");
        print_ir_value(out, ctx, in->dest, false);
        break;
 
    case IR_RETURN:
        fprintf(out, "ret");
        if(in->src1.kind != IR_VAL_NONE) {
            fprintf(out, " ");
            print_ir_value(out, ctx, in->src1, true);
        }
        break;
 
    case IR_ARG:
        fprintf(out, "arg %s[%d] ", in->src1.type.is_float ? "xmm" : "reg", in->aux);
        print_ir_value(out, ctx, in->src1, true);
        break;
 
    case IR_ARG_STACK:
        fprintf(out, "arg_stack (total=%d) ", in->aux);
        print_ir_value(out, ctx, in->src1, true);
        break;
 
    case IR_CALL:
        if(in->dest.kind != IR_VAL_NONE) {
            print_ir_value(out, ctx, in->dest, true);
            fprintf(out, " = ");
        }
        fprintf(out, "call ");
        print_ir_value(out, ctx, in->src1, false);
        fprintf(out, " (stack_args=%d, xmm=%d)", in->aux, in->aux2);
        break;
 
    case IR_LOAD_INDIRECT:
    case IR_ATOMIC_LOAD:
        print_ir_value(out, ctx, in->dest, true);
        fprintf(out, " = %s [", ir_op_to_str(in->op));
        print_ir_value(out, ctx, in->src1, true);
        fprintf(out, " %+d]", in->aux);
        break;
 
    case IR_STORE_INDIRECT:
    case IR_ATOMIC_STORE:
        fprintf(out, "%s [", ir_op_to_str(in->op));
        print_ir_value(out, ctx, in->src1, true);
        fprintf(out, " %+d], ", in->aux);
        print_ir_value(out, ctx, in->src2, true);
        break;
 
    case IR_MEMZERO:
        fprintf(out, "memzero ");
        print_ir_value(out, ctx, in->src1, true);
        fprintf(out, ", bytes=");
        print_ir_value(out, ctx, in->src2, false);
        break;
 
    case IR_FENCE:
        fprintf(out, "fence");
        break;
 
    default: {
        if(ir_is_cond_jump(in->op)) {
            fprintf(out, "%s ", ir_op_to_str(in->op));
            print_ir_value(out, ctx, in->src1, true);
            fprintf(out, ", ");
            print_ir_value(out, ctx, in->src2, true);
            fprintf(out, ", ");
            print_ir_value(out, ctx, in->dest, false);
            break;
        }
 
        if(in->dest.kind != IR_VAL_NONE) {
            print_ir_value(out, ctx, in->dest, true);
            fprintf(out, " = ");
        }
 
        fprintf(out, "%s", ir_op_to_str(in->op));
 
        IrValue srcs[3] = { in->src1, in->src2, in->src3 };
        bool first = true;
        for(int i = 0; i < 3; ++i) {
            if(srcs[i].kind == IR_VAL_NONE) continue;
            fprintf(out, first ? " " : ", ");
            print_ir_value(out, ctx, srcs[i], true);
            first = false;
        }
 
        if(in->aux != 0 || in->aux2 != 0)
            fprintf(out, " (aux=%d, aux2=%d)", in->aux, in->aux2);
        break;
    }
    }
 
    if(in->flags & IRF_RESULT_UNUSED) fprintf(out, "   ; resultado nao usado");
}
 
static void print_ir_range(FILE *out, IrGenContext *ctx, size_t from, size_t to) {
    for(size_t i = from; i < to && i < ctx->instructions.count; ++i) {
        IrInstruction *in = &ctx->instructions.items[i];
        bool is_label = (in->op == IR_LABEL);
        fprintf(out, "  %04zu:%s", i, is_label ? " " : "   ");
        print_ir_instruction(out, ctx, in);
        NL;
    }
}
 
static void print_ir_function(FILE *out, IrGenContext *ctx, IrFunctionRange *fn) {
    IrFuncInfo *info = ir_find_func(ctx, fn->func_id);
 
    fprintf(out, "Funcao ");
    if(info) print_view(out, info->name);
    else fprintf(out, "<desconhecida>");
    fprintf(out, "  [id=%d] instrucoes [%zu, %zu)", fn->func_id, fn->start, fn->end); NL;
 
    if(info) {
        fprintf(out, "  static=%s extern=%s variadic=%s defined=%s",
            bool_to_str(info->_static), bool_to_str(info->_extern),
            bool_to_str(info->variadic), bool_to_str(info->defined)); NL;
    }
 
    fprintf(out, "  slots=%d temps=%d params=%d calls=%s vla=%s",
        fn->slot_count, fn->temp_count, fn->param_count,
        fn->has_calls ? "yes" : "no", fn->uses_vla ? "yes" : "no"); NL;
 
    for(int i = 0; i < fn->slot_count; ++i) {
        IrSlotInfo *s = &fn->slots[i];
        fprintf(out, "  slot%d: size=%u align=%u ", i, s->size, s->align);
        print_ir_type(out, s->type);
        if(s->is_param) fprintf(out, " (param)");
        if(s->is_array) fprintf(out, " (array)");
        NL;
    }
 
    NL;
    print_ir_range(out, ctx, fn->start, fn->end);
    NL;
}
 
static void print_ir_globals(FILE *out, IrGenContext *ctx) {
    fprintf(out, "Globais (%zu):\n", ctx->globals.count);
    for(size_t i = 0; i < ctx->globals.count; ++i) {
        IrGlobal *g = ctx->globals.items[i];
        if(!g) continue;
 
        fprintf(out, "  @");
        print_view(out, g->name);
        fprintf(out, " [id=%d] size=%u align=%u ", g->id, g->size, g->align);
        print_ir_type(out, g->type);
        fprintf(out, " static=%s extern=%s const=%s array=%s",
            bool_to_str(g->_static), bool_to_str(g->_extern),
            bool_to_str(g->_const), bool_to_str(g->is_array));
 
        if(g->has_init) {
            fprintf(out, " init=");
            print_ir_value(out, ctx, g->init, true);
        }
        NL;
    }
    NL;
}
 
static void debug_ir(FILE *out, IrGenContext *ctx) {
    fprintf(out, "Analise do IR:\n");
    fprintf(out, "instrucoes=%zu funcoes=%zu globais=%zu strings=%zu floats=%zu temps=%d labels=%d unsupported=%zu\n\n",
        ctx->instructions.count, ctx->functions.count, ctx->globals.count,
        ctx->strings.count, ctx->floats.count, ctx->next_temp, ctx->next_label,
        ctx->unsupported);
 
    if(ctx->globals.count > 0) print_ir_globals(out, ctx);

    size_t cursor = 0;
    for(size_t i = 0; i < ctx->functions.count; ++i) {
        IrFunctionRange *fn = &ctx->functions.items[i];
 
        if(fn->start > cursor) {
            fprintf(out, "Fora de funcao [%zu, %zu)\n", cursor, fn->start);
            print_ir_range(out, ctx, cursor, fn->start);
            NL;
        }
 
        print_ir_function(out, ctx, fn);
        cursor = fn->end;
    }
 
    if(cursor < ctx->instructions.count) {
        fprintf(out, "Fora de funcao [%zu, %zu)\n", cursor, ctx->instructions.count);
        print_ir_range(out, ctx, cursor, ctx->instructions.count);
        NL;
    }
 
    if(ctx->unsupported > 0)
        fprintf(out, "AVISO: %zu construcao(oes) ainda nao suportada(s) pelo irgen\n", ctx->unsupported);
}

static size_t debug_program(const char* src, DebugOptionsConfiguration options, const char* filename, const char* out_path) {
    char sanitized[256];
    char path[512];

    if(strcmp(out_path, DEFAULT_OUT_PATH) == 0) {
        sanitize_path(filename, sanitized, sizeof(sanitized));
        snprintf(path, sizeof(path), "./debug/%s.%s", sanitized, prio_to_str(options.priority));
    } else {
        snprintf(path, sizeof(path), "%s", out_path);
    }

    FILE* out = fopen(path, "wb");
    if(!out) {
        fprintf(stderr, "Erro ao abrir o caminho do arquivo de saida: %s\n", path);
        exit(WRONG_PATH);
    }

    Arena arena;
    init_arena(&arena, MB);
    DiagnosticContext ctx;
    diag_init(&ctx, &arena);
    LineMap map;

    diag_set_phase(&ctx, PREPROCESS);
    Preprocess pp = { 0 };
    char lib[PATH_MAX];
    const char* lib_dir = stdlib_dir(lib, sizeof lib) ? lib : NULL;
    preprocess_run(&pp, filename, lib_dir, &arena, &map);

    diag_register_source(&ctx, filename, pp.original_src);
    diag_set_linemap(&ctx, filename, &map);

    if(options.before_preprocess) debug_before_preprocess(out, filename, src);
    if(options.preprocess) debug_preprocess(out, filename, pp.process_src);

    bool will_parse = options.parse || options.check || options.ir || options.codegen;

    if(options.lexer) {
        diag_set_phase(&ctx, LEXICAL);
        Lexer lexer = create_lexer(filename, pp.process_src, will_parse ? NULL : &ctx);
        debug_lexer(out, &lexer);
    }

    Node* program = NULL;

    if(will_parse) {
        diag_set_phase(&ctx, SYNTAX);
        Lexer parser_lexer = create_lexer(filename, pp.process_src, &ctx);
        Parser parser = create_parser(&parser_lexer, &ctx, &arena);
        program = parse_program(&parser);
    }

    if(options.parse) debug_parser(out, program);

    Node* entry = NULL;
    if((options.check || options.ir || options.codegen) && program && !has_errors(&ctx)){
        diag_set_phase(&ctx, SEMANTICAL);
        CheckContext check = create_checkctx(&arena, &ctx);
        check_program(&check, program);
        entry = check.entry_function;
    }

    if((options.ir || options.codegen) && program && !has_errors(&ctx)) {
        IrGenContext ir = create_irgen_context(&arena);
        irgen_program(&ir, program);
        if(options.ir) debug_ir(out, &ir);
        if(options.codegen) {
            CodegenContext cg = create_codegen(out, &arena, entry, NULL);
            emit_program(&cg, &ir);
        }
    }

    diag_report_all(&ctx);
    size_t errors = diag_error_count(&ctx);

    fclose(out);
    if(!(options.check || options.ir || options.codegen)) arena_free(&arena);
    return errors;
}

static bool has_any_flag(DebugOptionsConfiguration options) {
    return (options.before_preprocess || options.check || options.codegen || options.ir
        || options.lexer || options.parse || options.preprocess);
}

static void swap_ext(const char* path, const char* ext, char* out, size_t size) {
    snprintf(out, size, "%s", path);

    char* dot = strrchr(out, '.');
    char* sep = strrchr(out, '/');
    char* bs  = strrchr(out, '\\');
    if(bs && (!sep || bs > sep)) sep = bs;

    if(dot && (!sep || dot > sep)) *dot = '\0';

    size_t n = strlen(out);
    snprintf(out + n, size - n, "%s", ext);
}

static bool link_same(const LinkItem* a, const LinkItem* b) {
    if(a->kind != b->kind) return false;
#ifdef _WIN32
    return _stricmp(a->path, b->path) == 0;
#else
    return strcmp(a->path, b->path) == 0;
#endif
}

static LinkItem* merge_links(const Unit* units, size_t count) {
    LinkItem* head = NULL;
    LinkItem* tail = NULL;

    for(size_t i = 0; i < count; ++i) {
        for(const LinkItem* it = units[i].pp.links; it; it = it->next) {
            bool dup = false;
            for(const LinkItem* g = head; g; g = g->next) {
                if(link_same(g, it)) { dup = true; break; }
            }
            if(dup) continue;

            LinkItem* n = (LinkItem*)calloc(1, sizeof *n);
            if(!n) {
                fprintf(stderr, "Erro no calloc\n");
                exit(ALLOCATE_ERROR);
            }
            n->path = it->path;
            n->kind = it->kind;
            n->next = NULL;

            if(tail) tail->next = n; else head = n;
            tail = n;
        }
    }

    return head;
}

static void free_links(LinkItem* list) {
    while(list) {
        LinkItem* next = list->next;
        free(list);
        list = next;
    }
}

static int build_executable(DebugOptionsConfiguration* options, Unit* units, const char* out_path) {
    BuildOptions* b = &options->build;
    bool custom_out = strcmp(out_path, DEFAULT_OUT_PATH) != 0;
    if(custom_out) b->exe_path = out_path;

    if(b->obj_only && custom_out && options->paths.count > 1) {
        fprintf(stderr, "Erro: 'flag-out' com 'flag-obj' so aceita um unico arquivo.\n");
        return 1;
    }

    driver_ensure_dir(b->build_dir);

    static char asm_paths[MAX_PATHS_POSSIBLE][512];
    static char obj_paths[MAX_PATHS_POSSIBLE][512];
    const char* objs[MAX_PATHS_POSSIBLE];
    size_t nobj = 0;

    for(uint16_t i = 0; i < options->paths.count; ++i) {
        char stem[256];
        driver_stem(units[i].path, stem, sizeof(stem));
        snprintf(asm_paths[i], sizeof(asm_paths[i]), "%s/%s.asm", b->build_dir, stem);

        if(b->obj_only) {
            if(custom_out) snprintf(obj_paths[i], sizeof(obj_paths[i]), "%s", out_path);
            else swap_ext(units[i].path, ".obj", obj_paths[i], sizeof(obj_paths[i]));
        } else {
            snprintf(obj_paths[i], sizeof(obj_paths[i]), "%s/%s.obj", b->build_dir, stem);
        }

        FILE* f = fopen(asm_paths[i], "wb");
        if(!f) { fprintf(stderr, "Erro ao criar %s\n", asm_paths[i]); return 1; }

        CodegenContext cg = create_codegen(f, &units[i].arena, units[i].entry, NULL);
        emit_program(&cg, &units[i].ir);
        fclose(f);

        if(units[i].ir.unsupported || cg.unsupported)
            fprintf(stderr, "Aviso: %s tem %zu construcao(oes) nao suportada(s) no irgen e %zu no codegen\n",
                    units[i].path, units[i].ir.unsupported, cg.unsupported);

        if(b->asm_only) continue;
        if(!driver_assemble(b, asm_paths[i], obj_paths[i])) return 1;
        objs[nobj++] = obj_paths[i];

        if(b->obj_only) fprintf(stderr, "[luegiu] gerado %s\n", obj_paths[i]);
    }

    if(b->asm_only) { fprintf(stderr, "[luegiu] assembly em %s/\n", b->build_dir); return 0; }

    if(b->obj_only) {
        if(!b->keep)
            for(uint16_t i = 0; i < options->paths.count; ++i) remove(asm_paths[i]);
        return 0;
    }

    LinkItem* all_links = merge_links(units, options->paths.count);
    bool linked = driver_link(b, objs, nobj, all_links);
    free_links(all_links);
    if(!linked) return 1;

    fprintf(stderr, "[luegiu] gerado %s\n", b->exe_path);

    if(!b->keep) {
        for(uint16_t i = 0; i < options->paths.count; ++i) { remove(asm_paths[i]); remove(obj_paths[i]); }
    }
    return b->run ? driver_run(b->exe_path) : 0;
}

int main(int argc, char** argv) {
    if(argc < 2) {
        fprintf(stderr, "Por favor, passe algum arquivo-recurso para o compilador\n");
        fprintf(stderr, "Exemplo: luegiu [FLAGS] <arquivo>\n");
        exit(OUT_OF_PATH);
    }

    DebugOptionsConfiguration options = shift(argc, argv);

    if(options.paths.count == 0) {
        fprintf(stderr, "Por favor, coloque algum arquivo-recurso para o compilador\n");
        exit(OUT_OF_SOURCES);
    }

    char* out_path = options.paths.out_path;

    Arena session_arena = { 0 };
    init_arena(&session_arena, MB);
    typecheck_session_init(&session_arena);

    Arena global_arena = { 0 };
    init_arena(&global_arena, MB);
    DiagnosticContext global_ctx = { 0 };
    diag_init(&global_ctx, &global_arena);

    size_t errors = 0;

    Unit* units = NULL;
    if(has_any_flag(options)) {
        for(uint16_t i = 0; i < options.paths.count; ++i) {
            char* source = readfile(options.paths.resolved[i]);
            errors += debug_program(source, options, options.paths.resolved[i], out_path);
            free(source);
        }
    } else {
        units = calloc(options.paths.count, sizeof(Unit));
        if(!units) { fprintf(stderr, "Erro no calloc\n"); exit(ALLOCATE_ERROR); }

        for(uint16_t i = 0; i < options.paths.count; ++i) {
            unit_analyze(&units[i], options.paths.resolved[i]);
            diag_register_source(&global_ctx, units[i].path, units[i].pp.original_src);
            diag_set_linemap(&global_ctx, units[i].path, &units[i].map);
            diag_report_all(&units[i].ctx);
            errors += diag_error_count(&units[i].ctx);
        }
    }

    if(errors == 0 && (options.check || !has_any_flag(options))) {
        diag_set_phase(&global_ctx, SEMANTICAL);
        CheckContext link = create_checkctx(&session_arena, &global_ctx);
        typecheck_finalize(&link, !has_any_flag(options) && !options.build.obj_only);
        diag_report_all(&global_ctx);
        errors += diag_error_count(&global_ctx);

        if(has_errors(&global_ctx) && options.check_table) typecheck_dump_functions(stderr);
    }

    if(options.check && strcmp(out_path, DEFAULT_OUT_PATH) == 0) {
        FILE* f = fopen("./debug/functions_global.typecheck", "wb");
        if(f) { typecheck_dump_functions(f); fclose(f); }
    }

    if(errors == 0 && !has_any_flag(options) && units) {
        int rc = build_executable(&options, units, out_path);
        if(rc != 0) return rc;
    }
    
    return errors ? COMPILE_ERROR : 0;
}
