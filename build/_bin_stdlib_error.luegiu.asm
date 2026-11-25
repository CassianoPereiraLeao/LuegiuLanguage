; gerado pelo codegen luegiu (win64 / nasm)
bits 64
default rel

extern $ExitProcess
extern $GetStdHandle
extern $fwrite
global $exit
global $end

section .text

align 16
$exit:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov [rbp-8], rbx
    mov ebx, ecx
    mov ecx, 4294967284
    call $GetStdHandle
    mov r10, rax
    mov rcx, r10
    lea rdx, [rel __str0+0]
    mov r8d, ebx
    call $fwrite
    mov ecx, ebx
    call $ExitProcess
.Lret:
    mov rbx, [rbp-8]
    leave
    ret

align 16
$end:
    push rbp
    mov rbp, rsp
    sub rsp, 16
    mov [rbp-8], rbx
    mov ebx, ecx
    mov r10d, ebx
    movsxd r10, r10d
    mov rax, r10
.Lret:
    mov rbx, [rbp-8]
    leave
    ret

section .rdata align=16
align 16
__fsgn32: dd 0x80000000, 0, 0, 0
align 16
__fsgn64: dq 0x8000000000000000, 0
__str0: db 91,108,117,101,103,105,117,93,32,101,120,105,116,32,99,111,109,32,111,32,99,111,100,105, \
    103,111,32,100,101,32,101,114,114,111,58,32,37,100,0
