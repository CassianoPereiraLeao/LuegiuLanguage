#pragma once

#include "common.h"

#define KB 1024
#define ARENA_ALIGN 8

typedef struct ArenaChunk ArenaChunk;

struct ArenaChunk {
    struct ArenaChunk* next;
    size_t count;
    size_t capacity;
    uint8_t data[];
};

typedef struct {
    ArenaChunk* first;
    ArenaChunk* last;
    size_t size;
} Arena;

void init_arena(Arena *arena, size_t size);
void* arena_alloc(Arena *arena, size_t size);
void arena_free(Arena *arena);
size_t align_size(size_t size, size_t alignment);

static inline void* arena_memdup(Arena *arena, const void* src, size_t n) {
    void* p = arena_alloc(arena, n);
    if(n) memcpy(p, src, n);
    return p;
}

static inline char* arena_strndup(Arena *arena, const char* s, size_t n) {
    char* p = (char*)arena_alloc(arena, n + 1);
    if(n) memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static inline char* arena_strdup(Arena *arena, const char* s) {
    if(!s) return NULL;
    return arena_strndup(arena, s, strlen(s));
}
