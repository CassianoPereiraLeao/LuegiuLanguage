#include "../include/string_helper.h"

bool svcmp(View str1, View str2) {
    if(str1.len == 0 || str2.len == 0) return false;
    if(str1.len != str2.len) return false;

    return memcmp(str1.start, str2.start, str1.len) == 0;
}

bool viewcmpstr(View view, const char* string, size_t string_len) {
    if(view.len == 0 || string_len == 0) return false;
    if(view.len != string_len) return false;

    return memcmp(view.start, string, string_len) == 0;
}

bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool is_number(char c) {
    return (c >= '0' && c <= '9');
}

bool is_alphanumeric(char c) {
    return is_alpha(c) || is_number(c);
}

bool is_hexa(char c) {
    return is_number(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

bool is_primary_identifier(char c) {
    return is_alpha(c) || c == '_';
}

bool is_identifier(char c) {
    return is_alphanumeric(c) || c == '_';
}

bool is_terminator(char c) {
    return (c == '\n' || c == '\r' || c == '\t' || c == ' ');
}

const char* token_to_string(TokenType token) {
    switch (token)
    {
    case OPGT: return "GREATER THAN"; case OPGE: return "GREATER OR EQUALS";
    case OPLT: return "LESS THAN"; case OPLE: return "LESS OR EQUALS";
    case OPAND: return "AND"; case OPANDE: return "AND EQUALS";
    case OPOR: return "OR"; case OPORE: return "OR EQUALS";
    case OPXOR: return "XOR"; case OPXORE: return "XOR EQUALS";
    case LOGAND: return "LOGICAL AND"; case LOGOR: return "LOGICAL OR";
    case ASSIGN: return "ASSIGN"; case OPEQ: return "EQUALS";
    case OPMINUS: return "MINUS"; case OPMINUSE: return "MINUS EQUALS";
    case OPMINUSMINUS: return "MINUS MINUS"; case OPPLUS: return "PLUS";
    case OPPLUSE: return "PLUS EQUALS"; case OPPLUSPLUS: return "PLUS PLUS";
    case ARROW: return "ARROW"; case OPSLASH: return "SLASH";
    case OPSLASHE: return "SLASH EQUALS"; case OPMOD: return "MOD";
    case OPMODE: return "MOD EQUALS"; case OPSTAR: return "STAR";
    case OPSTARE: return "STAR EQUALS"; case KUINT8: return "UINT8";
    case IDENTIFIER: return "IDENTIFIER"; case STRING_LIT: return "STRING LITERAL";
    case CHAR_LIT: return "CHAR LITERAL"; case HEXA_LIT: return "HEXA LITERAL";
    case INT_LIT: return "INT LITERAL"; case FLOAT_LIT: return "FLOAT LITERAL";
    case DOUBLE_LIT: return "DOUBLE LITERAL"; case UNSIGNED_LIT: return "UNSIGNED LITERAL";
    case BIG_LIT: return "BIG LITERAL"; case OPEN_PAREN: return "OPEN PAREN";
    case CLOSE_PAREN: return "CLOSE PAREN"; case OPEN_BRACE: return "OPEN BRACE";
    case CLOSE_BRACE: return "CLOSE BRACE"; case OPEN_BRACKET: return "OPEN BRACKET";
    case CLOSE_BRACKET: return "CLOSE BRACKET"; case COLON: return "COLON";
    case SEMICOLON: return "SEMICOLON"; case ELLIPSES: return "ELLIPSES";
    case COMMA: return "COMMA"; case DOT: return "DOT";
    case OPSHL: return "LEFT SHIFT"; case OPSHR: return "RIGHT SHIFT";
    case OPBANG: return "BANG"; case DESC: return "DESCONSTRUCTOR";
    case OPSHLE: return "LEFT SHIFT EQUALS"; case OPSHRE: return "RIGHT SHIFT EQUALS";
    case OPBANGE: return "NOT EQUAL"; case OPTERN: return "TERNARY";
    case KDATA: return "DATA"; case KENUM: return "ENUM";
    case KCOPERATE: return "COPERATE"; case KNEWTYPE: return "NEW TYPE";
    case KWHILE: return "WHILE"; case KFOR: return "FOR";
    case KDO: return "DO"; case KIF: return "IF";
    case KELSE: return "ELSE"; case KCONST: return "CONST";
    case KSTATIC: return "STATIC"; case KATOMIC: return "ATOMIC";
    case KLOAD: return "LOAD"; case KCALL: return "CALL";
    case KBREAK: return "BREAK"; case KCONTINUE: return "CONTINUE";
    case KJUMP: return "JUMP"; case KEXTERN: return "EXTERN";
    case KBYTES: return "BYTES"; case KVOID: return "VOID";
    case KSMALL: return "SMALL"; case KLINK: return "LINK";
    case KHEXA: return "HEXA"; case KCHAR: return "CHAR";
    case KUTFCHAR: return "UTFCHAR"; case KFLOAT: return "FLOAT";
    case KDOUBLE: return "DOUBLE"; case KBIG: return "BIG";
    case KUINT16: return "UINT16"; case KUINT32: return "UINT32";
    case KUINT64: return "UINT64"; case KINT8: return "INT8";
    case KINT16: return "INT16"; case KINT32: return "INT32";
    case KINT64: return "INT64"; case UNDEFINED: return "?";
    case EOFF: return "END OF FILE";
    }

    return "?";
}

const char* node_to_str(NodeKinds node) {
    switch (node)
    {
    case NODE_ARRAY: return "NODE_ARRAY"; case NODE_BINARY_OP: return "NODE_BINARY_OP";
    case NODE_BLOCK_STMT: return "NODE_BLOCK_STMT"; case NODE_BREAK_STMT: return "NODE_BREAK_STMT";
    case NODE_CALL_STMT: return "NODE_CALL_STMT"; case NODE_CAST: return "NODE_CAST";
    case NODE_CONTINUE_STMT: return "NODE_CONTINUE_STMT"; case NODE_COPERATE: return "NODE_COPERATE";
    case NODE_DATA: return "NODE_DATA"; case NODE_DO_WHILE_LOOP: return "NODE_DO_WHILE_LOOP";
    case NODE_ENUM: return "NODE_ENUM"; case NODE_ENUM_CALL: return "NODE_ENUM_CALL";
    case NODE_ENUM_MEMBER: return "NODE_ENUM_MEMBER"; case NODE_FIELD_CALL: return "NODE_FIELD_CALL";
    case NODE_FOR_LOOP: return "NODE_FOR_LOOP"; case NODE_FUNC_CALL: return "NODE_FUNC_CALL";
    case NODE_FUNC_DECL: return "NODE_FUNC_DECL"; case NODE_IF_STMT: return "NODE_IF_STMT";
    case NODE_LITERAL: return "NODE_LITERAL"; case NODE_NEWTYPE: return "NODE_NEWTYPE";
    case NODE_POSTFIX_OP: return "NODE_POSTIFIX_OP"; case NODE_PROGRAM: return "NODE_PROGRAM";
    case NODE_UNARY_OP: return "NODE_UNARY_OP"; case NODE_VAR_CALL: return "NODE_VAR_CALL";
    case NODE_VAR_DECL: return "NODE_VAR_DECL"; case NODE_WHILE_LOOP: return "NODE_WHILE_LOOP";
    case NODE_TERNARY: return "NODE_TERNARY"; case NODE_UNDEFINED: return "NODE_UNDEFINED";
    case NODE_JUMP_LABEL: return "NODE_JMP_LABEL"; case NODE_LOAD_LABEL: return "NODE_LOAD_LABEL";
    case NODE_BYTES: return "NODE_BYTES";
    }

    return "?";
}

const char* literal_to_str(LiteralKind literal) {
    switch (literal)
    {
    case INT: return "INT";
    case UNSIGNED: return "UNSGINED";
    case STRING: return "STRING";
    case CHAR: return "CHAR";
    case FLOAT: return "FLOAT";
    case BIG: return "BIG";
    case HEXA: return "HEXA";
    case DOUBLE: return "DOUBLE";
    }

    return "?";
}
