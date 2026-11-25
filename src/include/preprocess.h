#pragma once

#include "common.h"
#include "arena.h"
#include "string_helper.h"

#define STRING_BUILDER_INIT_CAPACITY (4U * KB)
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define MAX_CAP_PER_FILE (128U * MB)

#define MAX_INCLUDE_DEPTH    64
#define MAX_EXPANSION_DEPTH  200
#define MAX_MACRO_PARAMS     127

#define COMMENT_OPEN       "/comment"
#define COMMENT_OPEN_LEN   8
#define COMMENT_CLOSE      "endcomment/"
#define COMMENT_CLOSE_LEN  11

typedef enum {
    TARGET_SINGLE,
    TARGET_MULTIPLE
} IncludePolicy;

typedef enum {
    STANDARD_LIBRARY,
    USER_DIRECTIVE,
} ExtractType;

typedef enum { 
    LINK_OBJECT, LINK_DLL 
} LinkKind;

typedef struct LinkItem {
    const char* path;
    LinkKind kind;
    struct LinkItem* next;
} LinkItem;

typedef struct IncludeFile {
    const char* path;
    struct IncludeFile* next;
} IncludeFile;

typedef struct {
    const char* original_src;
    const char* process_src;
    size_t process_len;
    LinkItem* links;
    size_t link_count;
} Preprocess;

typedef enum {
    MACRO_OBJECT,
    MACRO_FUNCTION,
    MACRO_BUILTIN_LINE,
    MACRO_BUILTIN_FILE,
    MACRO_BUILTIN_COUNTER
} MacroKind;

typedef enum {
    PART_TEXT,
    PART_PARAM
} MacroPartKind;

typedef struct {
    const char* text;
    uint32_t len;
    int32_t param;
    uint8_t kind;
} MacroPart;

typedef struct Macro {
    const char* name;
    size_t name_len;
    const char* value;
    size_t value_len;
    struct Macro* next;

    uint32_t hash;
    MacroKind kind;
    int nparams;
    bool has_ident;
    bool disabled;

    MacroPart* parts;
    size_t nparts;
} Macro;

typedef struct {
    size_t out;
    size_t src;
    const char* filename;
} LineMapEntry;

typedef struct {
    size_t count;
    size_t cap;
    LineMapEntry* v;
} LineMap;

typedef struct {
    char* data;
    size_t len;
    size_t capacity;
    Arena* arena;
} StringBuilder;

void linemap_zero_init(LineMap *map);
void linemap_resolve(LineMap *map, size_t last_line, const char** file_out, int *line_out);

void preprocess_run(Preprocess *pp, const char* filepath, const char* lib_dir,
                    Arena *arena, LineMap *map);
