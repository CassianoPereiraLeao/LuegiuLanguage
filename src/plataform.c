#include "./include/platform.h"
#include <stdio.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#  include <stdint.h>
#else
#  include <unistd.h>
#  include <sys/types.h>
#endif

#define EXE_BUF 4096

bool stdlib_dir(char* out, size_t cap) {
    char exe[EXE_BUF];

#if defined(_WIN32)
    DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)sizeof exe);
    if(n == 0 || n >= sizeof exe) return false;
#elif defined(__APPLE__)
    uint32_t sz = (uint32_t)sizeof exe;
    if(_NSGetExecutablePath(exe, &sz) != 0) return false;
#else
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if(n <= 0) return false;
    exe[n] = '\0';
#endif

    char* sep = NULL;
    for(char* q = exe; *q; ++q)
        if(*q == '/' || *q == '\\') sep = q;
    if(!sep) return false;
    *sep = '\0';

    int w = snprintf(out, cap, "%s/stdlib", exe);
    return w > 0 && (size_t)w < cap;
}
