; gerado pelo codegen luegiu (win64 / nasm)
bits 64
default rel

global $fileno
global $getenv
extern $GetEnvironmentStringsW
extern $GetFullPathNameA
extern $GetConsoleMode
extern $GetFileType
extern $GetStdHandle
global $_fullpath
global $__isatty

section .text

align 16
$__env_load:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    cmp qword [rel $__env_block+0], 0
    jne .L0
    call $GetEnvironmentStringsW
    mov r10, rax
    mov rax, r10
    mov qword [rel $__env_block+0], rax
.L0:
.Lret:
    leave
    ret

align 16
$__env_lower:
    push rbp
    mov rbp, rsp
    sub rsp, 16
    mov [rbp-8], rbx
    movzx r10d, cx
    cmp r10d, 65
    jb .L1
    cmp r10d, 90
    ja .L1
    mov ebx, r10d
    add ebx, 32
    movzx ebx, bx
    mov eax, ebx
    jmp .Lret
.L1:
    mov eax, r10d
.Lret:
    mov rbx, [rbp-8]
    leave
    ret

align 16
$getenv:
    push rbp
    mov rbp, rsp
    sub rsp, 80
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov [rbp-40], r13
    mov [rbp-48], r14
    mov rbx, rcx
    mov r10, rbx
    add r10, 0
    movzx esi, word [r10+0]
    mov r10d, esi
    cmp r10d, 0
    jne .L2
    mov eax, 0
    jmp .Lret
.L2:
    call $__env_load
    mov rsi, qword [rel $__env_block+0]
    cmp rsi, 0
    jne .L3
    mov eax, 0
    jmp .Lret
.L3:
    jmp .L5
.L4:
    mov rdi, rsi
    mov r12, rbx
    jmp .L8
.L7:
    add rdi, 2
    add r12, 2
.L8:
    mov r10, r12
    add r10, 0
    movzx r13d, word [r10+0]
    mov r10d, r13d
    cmp r10d, 0
    je .L10
    mov r10, rdi
    add r10, 0
    movzx r13d, word [r10+0]
    mov r10d, r13d
    cmp r10d, 0
    je .L10
    mov r10, rdi
    add r10, 0
    movzx r13d, word [r10+0]
    mov ecx, r13d
    call $__env_lower
    movzx eax, ax
    mov r13d, eax
    mov r10, r12
    add r10, 0
    movzx r14d, word [r10+0]
    mov ecx, r14d
    call $__env_lower
    movzx eax, ax
    mov r10d, eax
    cmp r13d, r10d
    je .L7
.L10:
.L9:
    mov r10, r12
    add r10, 0
    movzx r13d, word [r10+0]
    mov r10d, r13d
    cmp r10d, 0
    jne .L11
    mov r10, rdi
    add r10, 0
    movzx r13d, word [r10+0]
    cmp r13d, 61
    jne .L11
    mov r10, rdi
    add r10, 2
    mov rax, r10
    jmp .Lret
.L11:
    jmp .L13
.L12:
    add rsi, 2
.L13:
    mov r10, rsi
    add r10, 0
    movzx r13d, word [r10+0]
    mov r10d, r13d
    cmp r10d, 0
    jne .L12
.L14:
    add rsi, 2
.L5:
    mov r10, rsi
    add r10, 0
    movzx r13d, word [r10+0]
    mov r10d, r13d
    cmp r10d, 0
    jne .L4
.L6:
    mov eax, 0
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    mov r13, [rbp-40]
    mov r14, [rbp-48]
    leave
    ret

align 16
$__isatty:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov [rbp-8], rbx
    mov rbx, rcx
    mov dword [rbp-12], 0
    mov rcx, rbx
    call $GetFileType
    mov r10d, eax
    cmp r10d, 2
    je .L15
    mov eax, 0
    jmp .Lret
.L15:
    mov rcx, rbx
    lea rdx, [rbp-12]
    call $GetConsoleMode
    movzx eax, al
    mov r10d, eax
    mov ebx, r10d
    cmp ebx, 0
    setne r10b
    movzx r10d, r10b
    mov ebx, r10d
    mov eax, ebx
.Lret:
    mov rbx, [rbp-8]
    leave
    ret

align 16
$fileno:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov [rbp-8], rbx
    mov r10d, ecx
    cmp r10d, 0
    jl .L17
    cmp r10d, 2
    jle .L16
.L17:
    mov eax, 0
    jmp .Lret
.L16:
    mov ebx, -10
    sub ebx, r10d
    mov ecx, ebx
    call $GetStdHandle
    mov r10, rax
    mov rax, r10
.Lret:
    mov rbx, [rbp-8]
    leave
    ret

align 16
$_fullpath:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov rbx, rcx
    mov rdi, rdx
    mov rsi, r8
    mov r10, rsi
    mov r12d, r10d
    mov r12d, r12d
    mov rcx, rdi
    mov rdx, r12
    mov r8, rbx
    mov r9d, 0
    call $GetFullPathNameA
    mov r10d, eax
    mov edi, r10d
    cmp edi, 0
    je .L19
    mov r10, rsi
    cmp edi, r10d
    jb .L18
.L19:
    mov eax, 0
    jmp .Lret
.L18:
    mov rax, rbx
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    leave
    ret

section .rdata align=16
align 16
__fsgn32: dd 0x80000000, 0, 0, 0
align 16
__fsgn64: dq 0x8000000000000000, 0

section .bss align=16
alignb 8
$__env_block: resb 8
