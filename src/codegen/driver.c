#include "../include/driver.h"

#ifdef _WIN32
  #include <direct.h>
  #define MKDIR(p) _mkdir(p)
#else
  #include <sys/stat.h>
  #define MKDIR(p) mkdir((p), 0755)
#endif

BuildOptions driver_default_options(void) {
    BuildOptions o;
    memset(&o, 0, sizeof o);
    o.nasm = getenv("LUEGIU_NASM") ? getenv("LUEGIU_NASM") : "nasm";
    o.linker = getenv("LUEGIU_LINKER") ? getenv("LUEGIU_LINKER") : "gcc";
    o.exe_path = "./out.exe";
    o.build_dir = "./build";
    return o;
}

void driver_ensure_dir(const char* dir) {
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s", dir);
    for (char* p = tmp + 1; *p; ++p) {
        if (*p == '/' || *p == '\\') { char ch = *p; *p = '\0'; MKDIR(tmp); *p = ch; }
    }
    MKDIR(tmp);
}

void driver_stem(const char* src, char* out, size_t out_size) {
    size_t i = 0, j = 0;
    while (src[i] && j + 1 < out_size) {
        if (src[i] == '.' && (src[i + 1] == '/' || src[i + 1] == '\\')) { ++i; continue; }
        char ch = src[i++];
        out[j++] = (ch == '/' || ch == '\\' || ch == ':') ? '_' : ch;
    }
    out[j] = '\0';
}

static int run_cmd(const char* cmd) {
    char full[4096];
#ifdef _WIN32
    snprintf(full, sizeof full, "\"%s\"", cmd);
#else
    snprintf(full, sizeof full, "%s", cmd);
#endif
    fprintf(stderr, "[luegiu] %s\n", cmd);
    return system(full);
}

bool driver_assemble(const BuildOptions* o, const char* asm_path, const char* obj_path) {
    char cmd[2048];
    snprintf(cmd, sizeof cmd, "\"%s\" -f win64 -o \"%s\" \"%s\"", o->nasm, obj_path, asm_path);
    if (run_cmd(cmd) != 0) {
        fprintf(stderr, "Erro: o nasm falhou (ele esta no PATH? use LUEGIU_NASM ou 'flag-nasm <caminho>').\n");
        return false;
    }
    return true;
}

static bool is_msvc_style(const char* linker) {
    return strstr(linker, "link") != NULL;
}
static bool append_arg(char* cmd, size_t size, size_t* n, const char* fmt, const char* a) {
    int w = snprintf(cmd + *n, size - *n, fmt, a);
    if(w < 0 || (size_t)w >= size - *n) {
        fprintf(stderr, "Erro: linha de comando do linker grande demais.\n");
        return false;
    }
    *n += (size_t)w;
    return true;
}

bool driver_link(const BuildOptions* o, const char** objs, size_t count, const LinkItem* links) {
    char cmd[8192];
    size_t n;
    bool msvc = is_msvc_style(o->linker);

    if (msvc) n = (size_t)snprintf(cmd, sizeof cmd,
        "\"%s\" /nologo /subsystem:console /entry:__luegiu_entry /nodefaultlib /out:\"%s\"", o->linker, o->exe_path);
    else      n = (size_t)snprintf(cmd, sizeof cmd,
        "\"%s\" -nostdlib -nostartfiles -Wl,-e,__luegiu_entry -o \"%s\"", o->linker, o->exe_path);

    /* 1. objetos gerados pelo compilador */
    for (size_t i = 0; i < count; ++i)
        if (!append_arg(cmd, sizeof cmd, &n, " \"%s\"", objs[i])) return false;

    /* 2. .obj da stdlib pedidos pelos headers */
    for (const LinkItem* it = links; it; it = it->next)
        if (it->kind == LINK_OBJECT && !append_arg(cmd, sizeof cmd, &n, " \"%s\"", it->path)) return false;

    /* 3. DLLs pedidas com extract -> [dll] */
    for (const LinkItem* it = links; it; it = it->next)
        if (it->kind == LINK_DLL &&
            !append_arg(cmd, sizeof cmd, &n, msvc ? " %s.lib" : " -l%s", it->path)) return false;

    /* o stub de entrada usa GetCommandLineA/ExitProcess */
    if (msvc && !append_arg(cmd, sizeof cmd, &n, " %s", "kernel32.lib")) return false;

    if (run_cmd(cmd) != 0) {
        fprintf(stderr, "Erro: o linker falhou (precisa de MinGW-w64 gcc, ou 'link'/'lld-link' com as libs do Windows SDK).\n");
        return false;
    }
    return true;
}

int driver_run(const char* exe_path) {
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "\"%s\"", exe_path);
    fprintf(stderr, "[luegiu] executando %s\n", exe_path);
    return run_cmd(cmd);
}