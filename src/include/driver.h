#pragma once

#include "common.h"
#include "preprocess.h"

typedef struct {
    const char* nasm;
    const char* linker;
    const char* exe_path;
    const char* build_dir;
    bool keep;
    bool asm_only;
    bool obj_only;
    bool run;
} BuildOptions;

BuildOptions driver_default_options(void);
void driver_ensure_dir(const char* dir);
void driver_stem(const char* src, char* out, size_t out_size);
bool driver_assemble(const BuildOptions* o, const char* asm_path, const char* obj_path);
bool driver_link(const BuildOptions* o, const char** objs, size_t count, const LinkItem* links);
int  driver_run(const char* exe_path);