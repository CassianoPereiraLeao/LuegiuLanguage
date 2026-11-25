; gerado pelo codegen luegiu (win64 / nasm)
bits 64
default rel


section .text

align 16
$FNV1a_hash_name:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov [rbp-40], r13
    mov rbx, rcx
    mov rsi, rdx
    mov r10d, 2166136261
    mov edi, 0
    jmp .L2
.L0:
    mov r12, rbx
    add r12, rdi
    movzx r13d, byte [r12+0]
    mov r12d, r13d
    xor r10d, r12d
    imul r10d, r10d, 16777619
.L1:
    add rdi, 1
.L2:
    cmp rdi, rsi
    jb .L0
.L3:
    mov eax, r10d
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    mov r13, [rbp-40]
    leave
    ret

section .rdata align=16
align 16
__fsgn32: dd 0x80000000, 0, 0, 0
align 16
__fsgn64: dq 0x8000000000000000, 0

section .rdata align=16
align 8
$t1:
    dq 0x80
align 8
$t2:
    dq 0x1f
align 8
$t3:
    dq 0xfffffffffffffff0
align 8
$t4:
    dq 0x20
align 8
$t5:
    dq 0x18
