#pragma once

#include "common.h"

bool svcmp(View str1, View str2);
bool viewcmpstr(View view, const char* string, size_t string_len);
bool is_alpha(char c);
bool is_number(char c);
bool is_alphanumeric(char c);
bool is_hexa(char c);
bool is_primary_identifier(char c);
bool is_identifier(char c);
const char* token_to_string(TokenType token);
const char* node_to_str(NodeKinds node);
const char* literal_to_str(LiteralKind literal);
