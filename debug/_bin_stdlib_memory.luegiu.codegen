; gerado pelo codegen luegiu (win64 / nasm)
bits 64
default rel

extern $VirtualAlloc
extern $GetProcessHeap
extern $HeapAlloc
extern $HeapFree
global $malloc
global $free
global $calloc
global $realloc
global $memmove
global $memcpy
global $memcmp
global $diff_at
global $memset
global $memchr
global $bcopy
global $bzero
global $bcmp
global $memmem
global $mempcpy
global $memccpy
global $memrchr
global $wmemcmp
global $wmemchr
global $memset_explicit
global $explicit_bzero
global $memcpy_s
global $memmove_s
global $memset_s
global $_memicmp
global $memicmp
global $wmemcpy
global $wmemmove
global $wmemset

section .text

align 16
$small_refill:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov rbx, rcx
    mov r10, rbx
    shl r10, 4
    mov rsi, r10
    cmp rbx, 0
    jne .L0
    mov esi, 16
.L0:
    mov r10, rsi
    add r10, 16
    mov rdi, r10
    mov r10, qword [rel $g_end+0]
    sub r10, qword [rel $g_cur+0]
    cmp r10, rdi
    jae .L1
    mov ecx, 0
    mov edx, 262144
    mov r8d, 12288
    mov r9d, 4
    call $VirtualAlloc
    mov r10, rax
    mov rax, r10
    mov qword [rel $g_cur+0], rax
    cmp qword [rel $g_cur+0], 0
    jne .L2
    mov qword [rel $g_end+0], 0
    mov eax, 0
    jmp .Lret
.L2:
    mov r10, qword [rel $g_cur+0]
    add r10, 262144
    mov rax, r10
    mov qword [rel $g_end+0], rax
.L1:
    mov r10, qword [rel $g_cur+0]
    mov r12, qword [rel $g_cur+0]
    add r12, rdi
    mov rax, r12
    mov qword [rel $g_cur+0], rax
    mov qword [r10+0], rbx
    mov rbx, r10
    add rbx, 8
    mov qword [rbx+0], rsi
    mov rbx, r10
    add rbx, 16
    mov rax, rbx
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    leave
    ret

align 16
$sys_alloc:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov r10, rcx
    mov rcx, 0x7fffffffffffff00
    cmp r10, rcx
    jbe .L3
    mov eax, 0
    jmp .Lret
.L3:
    mov rbx, r10
    add rbx, 15
    mov r10d, 15
    not r10
    mov rsi, rbx
    and rsi, r10
    mov rbx, rsi
    call $GetProcessHeap
    mov r10, rax
    mov rsi, rbx
    add rsi, 16
    mov rcx, r10
    mov edx, 0
    mov r8, rsi
    call $HeapAlloc
    mov r10, rax
    mov rsi, r10
    cmp rsi, 0
    jne .L4
    mov eax, 0
    jmp .Lret
.L4:
    mov qword [rsi+0], 65
    mov r10, rsi
    add r10, 8
    mov qword [r10+0], rbx
    mov r10, rsi
    add r10, 16
    mov rax, r10
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    leave
    ret

align 16
$malloc:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov rbx, rcx
    cmp rbx, 1024
    ja .L5
    mov r10, rbx
    add r10, 15
    mov rsi, r10
    shr rsi, 4
    mov r10, rsi
    mov rsi, r10
    imul rsi, rsi, 8
    lea rdi, [rel $g_free+0]
    add rdi, rsi
    mov rsi, qword [rdi+0]
    mov rdi, rsi
    cmp rdi, 0
    je .L6
    mov rsi, r10
    imul rsi, rsi, 8
    lea r12, [rel $g_free+0]
    add r12, rsi
    mov rsi, qword [rdi+0]
    mov qword [r12+0], rsi
    mov rax, rdi
    jmp .Lret
.L6:
    mov rcx, r10
    call $small_refill
    mov r10, rax
    mov rax, r10
    jmp .Lret
.L5:
    mov rcx, rbx
    call $sys_alloc
    mov r10, rax
    mov rax, r10
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    leave
    ret

align 16
$free:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov r10, rcx
    cmp r10, 0
    jne .L7
    jmp .Lret
.L7:
    mov rbx, r10
    mov r10, rbx
    sub r10, 16
    mov rsi, qword [r10+0]
    mov r10, rsi
    cmp r10, 65
    jae .L8
    mov rsi, r10
    imul rsi, rsi, 8
    lea rdi, [rel $g_free+0]
    add rdi, rsi
    mov rsi, qword [rdi+0]
    mov qword [rbx+0], rsi
    mov rsi, r10
    imul rsi, rsi, 8
    lea r10, [rel $g_free+0]
    add r10, rsi
    mov qword [r10+0], rbx
    jmp .Lret
.L8:
    call $GetProcessHeap
    mov r10, rax
    mov rsi, rbx
    sub rsi, 16
    mov rcx, r10
    mov edx, 0
    mov r8, rsi
    call $HeapFree
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    leave
    ret

align 16
$calloc:
    push rbp
    mov rbp, rsp
    sub rsp, 80
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov [rbp-40], r13
    mov r10, rcx
    mov rbx, rdx
    mov rsi, r10
    or rsi, rbx
    mov rdi, rsi
    shr rdi, 32
    cmp rdi, 0
    je .L9
    cmp r10, 0
    je .L10
    mov esi, 0
    not rsi
    mov rax, rsi
    xor edx, edx
    div r10
    mov rdi, rax
    cmp rbx, rdi
    jbe .L10
    mov eax, 0
    jmp .Lret
.L10:
.L9:
    mov rsi, r10
    imul rsi, rbx
    mov rbx, rsi
    mov rcx, rbx
    call $malloc
    mov r10, rax
    mov rsi, r10
    cmp rsi, 0
    jne .L11
    mov eax, 0
    jmp .Lret
.L11:
    mov r10, rsi
    mov rdi, rbx
    add rdi, 7
    mov rbx, rdi
    shr rbx, 3
    mov rdi, rbx
    mov ebx, 0
    jmp .L14
.L12:
    mov r12, rbx
    imul r12, r12, 8
    mov r13, r10
    add r13, r12
    mov qword [r13+0], 0
.L13:
    add rbx, 1
.L14:
    cmp rbx, rdi
    jb .L12
.L15:
    mov rax, rsi
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    mov r13, [rbp-40]
    leave
    ret

align 16
$realloc:
    push rbp
    mov rbp, rsp
    sub rsp, 96
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov [rbp-40], r13
    mov [rbp-48], r14
    mov [rbp-56], r15
    mov qword [rbp+16], rcx
    mov rsi, rdx
    cmp qword [rbp+16], 0
    jne .L16
    mov rcx, rsi
    call $malloc
    mov r10, rax
    mov rax, r10
    jmp .Lret
.L16:
    mov rdi, qword [rbp+16]
    mov r10, rdi
    sub r10, 8
    mov r12, qword [r10+0]
    mov r13, r12
    cmp rsi, r13
    ja .L17
    mov rax, qword [rbp+16]
    jmp .Lret
.L17:
    mov rcx, rsi
    call $malloc
    mov r10, rax
    mov rsi, r10
    cmp rsi, 0
    jne .L18
    mov eax, 0
    jmp .Lret
.L18:
    mov r10, rdi
    mov rdi, rsi
    mov r12, r13
    shr r12, 3
    mov r13, r12
    mov r12d, 0
    jmp .L21
.L19:
    mov r14, r12
    imul r14, r14, 8
    mov r15, rdi
    add r15, r14
    mov r14, r12
    imul r14, r14, 8
    mov rbx, r10
    add rbx, r14
    mov r14, qword [rbx+0]
    mov qword [r15+0], r14
.L20:
    add r12, 1
.L21:
    cmp r12, r13
    jb .L19
.L22:
    mov rcx, qword [rbp+16]
    call $free
    mov rax, rsi
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    mov r13, [rbp-40]
    mov r14, [rbp-48]
    mov r15, [rbp-56]
    leave
    ret

align 16
$diff_at:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov rdi, rcx
    mov rsi, rdx
    mov rbx, r8
    mov r10, rdi
    add r10, rbx
    movzx edi, byte [r10+0]
    mov r10d, edi
    mov rdi, rsi
    add rdi, rbx
    movzx ebx, byte [rdi+0]
    mov esi, ebx
    mov ebx, r10d
    sub ebx, esi
    mov eax, ebx
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    leave
    ret

align 16
$resolve64:
    push rbp
    mov rbp, rsp
    sub rsp, 80
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov [rbp-40], r13
    mov [rbp-48], r14
    mov rdi, rcx
    mov rsi, rdx
    mov ebx, 0
    jmp .L25
.L23:
    mov r10, rdi
    add r10, rbx
    mov r13, rsi
    add r13, rbx
    mov rax, r10
    mov rdx, r13
    movdqu xmm0, [rax]
    movdqu xmm1, [rdx]
    pcmpeqb xmm0, xmm1
    pmovmskb eax, xmm0
    xor eax, 0xFFFF
    mov r14d, eax
    mov r12d, r14d
    cmp r12d, 0
    je .L27
    mov r10d, r12d
    mov r10d, r10d
    mov rax, r10
    bsf rax, rax
    mov r13d, eax
    mov r10d, r13d
    mov r10d, r10d
    mov r13, rbx
    add r13, r10
    mov rcx, rdi
    mov rdx, rsi
    mov r8, r13
    call $diff_at
    mov r10d, eax
    mov eax, r10d
    jmp .Lret
.L27:
.L24:
    mov r10, rbx
    add r10, 16
    mov rbx, r10
.L25:
    cmp rbx, 64
    jb .L23
.L26:
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
$memcmp:
    push rbp
    mov rbp, rsp
    sub rsp, 96
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov [rbp-40], r13
    mov [rbp-48], r14
    mov [rbp-56], r15
    mov rdi, rcx
    mov r10, rdx
    mov rsi, r8
    mov rbx, rdi
    mov rdi, r10
    cmp rsi, 64
    jb .L28
    mov r10, rsi
    sub r10, 64
    mov r12, rbx
    add r12, r10
    mov r13, r12
    mov r10, rsi
    sub r10, 64
    mov r12, rdi
    add r12, r10
    mov r14, r12
    jmp .L30
.L29:
    mov rax, rbx
    mov rdx, rdi
    movdqu xmm0, [rax]
    movdqu xmm1, [rax+16]
    movdqu xmm2, [rdx]
    movdqu xmm3, [rdx+16]
    pcmpeqb xmm0, xmm2
    pcmpeqb xmm1, xmm3
    pand xmm0, xmm1
    movdqu xmm1, [rax+32]
    movdqu xmm2, [rdx+32]
    movdqu xmm3, [rax+48]
    pcmpeqb xmm1, xmm2
    movdqu xmm2, [rdx+48]
    pcmpeqb xmm3, xmm2
    pand xmm1, xmm3
    pand xmm0, xmm1
    pmovmskb eax, xmm0
    cmp eax, 0xFFFF
    sete al
    movzx eax, al
    mov r10d, eax
    cmp r10d, 0
    jne .L32
    mov rcx, rbx
    mov rdx, rdi
    call $resolve64
    mov r10d, eax
    mov eax, r10d
    jmp .Lret
.L32:
    mov r10, rbx
    add r10, 64
    mov rbx, r10
    mov r10, rdi
    add r10, 64
    mov rdi, r10
.L30:
    cmp rbx, r13
    jb .L29
.L31:
    mov rax, r13
    mov rdx, r14
    movdqu xmm0, [rax]
    movdqu xmm1, [rax+16]
    movdqu xmm2, [rdx]
    movdqu xmm3, [rdx+16]
    pcmpeqb xmm0, xmm2
    pcmpeqb xmm1, xmm3
    pand xmm0, xmm1
    movdqu xmm1, [rax+32]
    movdqu xmm2, [rdx+32]
    movdqu xmm3, [rax+48]
    pcmpeqb xmm1, xmm2
    movdqu xmm2, [rdx+48]
    pcmpeqb xmm3, xmm2
    pand xmm1, xmm3
    pand xmm0, xmm1
    pmovmskb eax, xmm0
    cmp eax, 0xFFFF
    sete al
    movzx eax, al
    mov r10d, eax
    cmp r10d, 0
    jne .L33
    mov rcx, r13
    mov rdx, r14
    call $resolve64
    mov r10d, eax
    mov eax, r10d
    jmp .Lret
.L33:
    mov eax, 0
    jmp .Lret
.L28:
    cmp rsi, 16
    jb .L34
    mov r10, rsi
    sub r10, 16
    mov r12, rbx
    add r12, r10
    mov r13, r12
    mov r10, rsi
    sub r10, 16
    mov r12, rdi
    add r12, r10
    mov r14, r12
    jmp .L36
.L35:
    mov rax, rbx
    mov rdx, rdi
    movdqu xmm0, [rax]
    movdqu xmm1, [rdx]
    pcmpeqb xmm0, xmm1
    pmovmskb eax, xmm0
    xor eax, 0xFFFF
    mov r10d, eax
    mov r12d, r10d
    cmp r12d, 0
    je .L38
    mov r10d, r12d
    mov r10d, r10d
    mov rax, r10
    bsf rax, rax
    mov r15d, eax
    mov r10d, r15d
    mov r10d, r10d
    mov rcx, rbx
    mov rdx, rdi
    mov r8, r10
    call $diff_at
    mov r10d, eax
    mov eax, r10d
    jmp .Lret
.L38:
    mov r10, rbx
    add r10, 16
    mov rbx, r10
    mov r10, rdi
    add r10, 16
    mov rdi, r10
.L36:
    cmp rbx, r13
    jb .L35
.L37:
    mov rax, r13
    mov rdx, r14
    movdqu xmm0, [rax]
    movdqu xmm1, [rdx]
    pcmpeqb xmm0, xmm1
    pmovmskb eax, xmm0
    xor eax, 0xFFFF
    mov r10d, eax
    mov r12d, r10d
    cmp r12d, 0
    je .L39
    mov r10d, r12d
    mov r10d, r10d
    mov rax, r10
    bsf rax, rax
    mov r12d, eax
    mov r10d, r12d
    mov r10d, r10d
    mov rcx, r13
    mov rdx, r14
    mov r8, r10
    call $diff_at
    mov r10d, eax
    mov eax, r10d
    jmp .Lret
.L39:
    mov eax, 0
    jmp .Lret
.L34:
    cmp rsi, 8
    jb .L40
    mov r10, qword [rbx+0]
    mov r12, qword [rdi+0]
    mov r13, r10
    xor r13, r12
    mov r10, r13
    cmp r10, 0
    je .L41
    mov rax, r10
    bsf rax, rax
    mov r12d, eax
    mov r10d, r12d
    shr r10d, 3
    mov r12d, r10d
    mov r12d, r12d
    mov rcx, rbx
    mov rdx, rdi
    mov r8, r12
    call $diff_at
    mov r10d, eax
    mov eax, r10d
    jmp .Lret
.L41:
    mov r10, rsi
    sub r10, 8
    mov r12, r10
    mov r10, rbx
    add r10, r12
    mov r13, qword [r10+0]
    mov r10, rdi
    add r10, r12
    mov r14, qword [r10+0]
    mov r10, r13
    xor r10, r14
    mov r13, r10
    cmp r13, 0
    je .L42
    mov rax, r13
    bsf rax, rax
    mov r10d, eax
    mov r13d, r10d
    shr r13d, 3
    mov r10d, r13d
    mov r10d, r10d
    mov r13, r12
    add r13, r10
    mov rcx, rbx
    mov rdx, rdi
    mov r8, r13
    call $diff_at
    mov r10d, eax
    mov eax, r10d
    jmp .Lret
.L42:
    mov eax, 0
    jmp .Lret
.L40:
    cmp rsi, 4
    jb .L43
    mov r10d, dword [rbx+0]
    mov r12d, dword [rdi+0]
    mov r13d, r10d
    xor r13d, r12d
    mov r10d, r13d
    mov r10d, r10d
    mov r12, r10
    cmp r12, 0
    je .L44
    mov rax, r12
    bsf rax, rax
    mov r10d, eax
    mov r12d, r10d
    shr r12d, 3
    mov r10d, r12d
    mov r10d, r10d
    mov rcx, rbx
    mov rdx, rdi
    mov r8, r10
    call $diff_at
    mov r10d, eax
    mov eax, r10d
    jmp .Lret
.L44:
    mov r10, rsi
    sub r10, 4
    mov r12, r10
    mov r10, rbx
    add r10, r12
    mov r13d, dword [r10+0]
    mov r10, rdi
    add r10, r12
    mov r14d, dword [r10+0]
    mov r10d, r13d
    xor r10d, r14d
    mov r13d, r10d
    mov r13d, r13d
    mov r10, r13
    cmp r10, 0
    je .L45
    mov rax, r10
    bsf rax, rax
    mov r13d, eax
    mov r10d, r13d
    shr r10d, 3
    mov r13d, r10d
    mov r13d, r13d
    mov r10, r12
    add r10, r13
    mov rcx, rbx
    mov rdx, rdi
    mov r8, r10
    call $diff_at
    mov r10d, eax
    mov eax, r10d
    jmp .Lret
.L45:
    mov eax, 0
    jmp .Lret
.L43:
    cmp rsi, 0
    jne .L46
    mov eax, 0
    jmp .Lret
.L46:
    movzx r10d, byte [rbx+0]
    movzx r12d, byte [rdi+0]
    cmp r10d, r12d
    je .L47
    mov rcx, rbx
    mov rdx, rdi
    mov r8d, 0
    call $diff_at
    mov r10d, eax
    mov eax, r10d
    jmp .Lret
.L47:
    mov r10, rsi
    shr r10, 1
    mov r12, r10
    mov r10, rbx
    add r10, r12
    movzx r13d, byte [r10+0]
    mov r10, rdi
    add r10, r12
    movzx r14d, byte [r10+0]
    cmp r13d, r14d
    je .L48
    mov rcx, rbx
    mov rdx, rdi
    mov r8, r12
    call $diff_at
    mov r10d, eax
    mov eax, r10d
    jmp .Lret
.L48:
    mov r10, rsi
    sub r10, 1
    mov rcx, rbx
    mov rdx, rdi
    mov r8, r10
    call $diff_at
    mov r10d, eax
    mov eax, r10d
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    mov r13, [rbp-40]
    mov r14, [rbp-48]
    mov r15, [rbp-56]
    leave
    ret

align 16
$memcpy:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov [rbp-40], r13
    mov [rbp-48], r14
    mov rsi, rcx
    mov rdi, rdx
    mov rbx, r8
    mov r10, rsi
    mov r12, rdi
    cmp rbx, 16
    ja .L49
    cmp rbx, 8
    jb .L50
    mov rdi, qword [r12+0]
    mov r13, rdi
    mov rdi, rbx
    sub rdi, 8
    mov r14, r12
    add r14, rdi
    mov rdi, qword [r14+0]
    mov r14, rdi
    mov qword [r10+0], r13
    mov rdi, rbx
    sub rdi, 8
    mov r13, r10
    add r13, rdi
    mov qword [r13+0], r14
    mov rax, rsi
    jmp .Lret
.L50:
    cmp rbx, 4
    jb .L51
    mov edi, dword [r12+0]
    mov r13d, edi
    mov rdi, rbx
    sub rdi, 4
    mov r14, r12
    add r14, rdi
    mov edi, dword [r14+0]
    mov r14d, edi
    mov dword [r10+0], r13d
    mov rdi, rbx
    sub rdi, 4
    mov r13, r10
    add r13, rdi
    mov dword [r13+0], r14d
    mov rax, rsi
    jmp .Lret
.L51:
    cmp rbx, 2
    jb .L52
    movzx edi, word [r12+0]
    mov r13d, edi
    mov rdi, rbx
    sub rdi, 2
    mov r14, r12
    add r14, rdi
    movzx edi, word [r14+0]
    mov r14d, edi
    mov word [r10+0], r13w
    mov rdi, rbx
    sub rdi, 2
    mov r13, r10
    add r13, rdi
    mov word [r13+0], r14w
    mov rax, rsi
    jmp .Lret
.L52:
    cmp rbx, 1
    jne .L53
    movzx edi, byte [r12+0]
    mov byte [r10+0], dil
.L53:
    mov rax, rsi
    jmp .Lret
.L49:
    cmp rbx, 32
    ja .L54
    mov rax, r10
    mov rdx, r12
    mov rcx, rbx
    movdqu xmm0, [rdx]
    movdqu xmm1, [rdx+rcx-16]
    movdqu [rax], xmm0
    movdqu [rax+rcx-16], xmm1
    mov rax, rsi
    jmp .Lret
.L54:
    cmp rbx, 64
    ja .L55
    mov rax, r10
    mov rdx, r12
    mov rcx, rbx
    movdqu xmm0, [rdx]
    movdqu xmm1, [rdx+16]
    movdqu xmm2, [rdx+rcx-32]
    movdqu xmm3, [rdx+rcx-16]
    movdqu [rax], xmm0
    movdqu [rax+16], xmm1
    movdqu [rax+rcx-32], xmm2
    movdqu [rax+rcx-16], xmm3
    mov rax, rsi
    jmp .Lret
.L55:
    cmp rbx, 2048
    jb .L56
    mov rax, r10
    mov rdx, r12
    mov rcx, rbx
    push rsi
    push rdi
    mov rdi, rax
    mov rsi, rdx
    rep movsb
    pop rdi
    pop rsi
    mov rax, rsi
    jmp .Lret
.L56:
    mov rdi, rbx
    sub rdi, 64
    mov r13, r10
    add r13, rdi
    mov rdi, r13
    mov r13, rbx
    sub r13, 64
    mov rbx, r12
    add rbx, r13
    mov r13, rbx
    jmp .L58
.L57:
    mov rax, r10
    mov rdx, r12
    movdqu xmm0, [rdx]
    movdqu xmm1, [rdx+16]
    movdqu xmm2, [rdx+32]
    movdqu xmm3, [rdx+48]
    movdqu [rax], xmm0
    movdqu [rax+16], xmm1
    movdqu [rax+32], xmm2
    movdqu [rax+48], xmm3
    mov rbx, r10
    add rbx, 64
    mov r10, rbx
    mov rbx, r12
    add rbx, 64
    mov r12, rbx
.L58:
    cmp r10, rdi
    jb .L57
.L59:
    mov rax, rdi
    mov rdx, r13
    movdqu xmm0, [rdx]
    movdqu xmm1, [rdx+16]
    movdqu xmm2, [rdx+32]
    movdqu xmm3, [rdx+48]
    movdqu [rax], xmm0
    movdqu [rax+16], xmm1
    movdqu [rax+32], xmm2
    movdqu [rax+48], xmm3
    mov rax, rsi
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
$memmove:
    push rbp
    mov rbp, rsp
    sub rsp, 80
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov [rbp-40], r13
    mov rdi, rcx
    mov r12, rdx
    mov rbx, r8
    mov rsi, rdi
    mov r13, r12
    cmp rbx, 64
    ja .L60
    mov rcx, rdi
    mov rdx, r12
    mov r8, rbx
    call $memcpy
    mov r10, rax
    mov rax, r10
    jmp .Lret
.L60:
    cmp rsi, r13
    jne .L61
    mov rax, rdi
    jmp .Lret
.L61:
    cmp rsi, r13
    jae .L62
    mov r10, r13
    sub r10, rsi
    cmp r10, rbx
    jb .L63
    mov rcx, rdi
    mov rdx, r12
    mov r8, rbx
    call $memcpy
    mov r10, rax
    mov rax, r10
    jmp .Lret
.L63:
    jmp .L65
.L64:
    mov rax, rsi
    mov rdx, r13
    movdqu xmm0, [rdx]
    movdqu xmm1, [rdx+16]
    movdqu xmm2, [rdx+32]
    movdqu xmm3, [rdx+48]
    movdqu [rax], xmm0
    movdqu [rax+16], xmm1
    movdqu [rax+32], xmm2
    movdqu [rax+48], xmm3
    mov r10, rsi
    add r10, 64
    mov rsi, r10
    mov r10, r13
    add r10, 64
    mov r13, r10
    mov r10, rbx
    sub r10, 64
    mov rbx, r10
.L65:
    cmp rbx, 64
    jae .L64
.L66:
    cmp rbx, 0
    je .L67
    mov rcx, rsi
    mov rdx, r13
    mov r8, rbx
    call $memcpy
.L67:
    mov rax, rdi
    jmp .Lret
.L62:
    mov r10, rsi
    sub r10, r13
    cmp r10, rbx
    jb .L68
    mov rcx, rdi
    mov rdx, r12
    mov r8, rbx
    call $memcpy
    mov r10, rax
    mov rax, r10
    jmp .Lret
.L68:
    jmp .L70
.L69:
    mov r10, rbx
    sub r10, 64
    mov rbx, r10
    mov r10, rsi
    add r10, rbx
    mov r12, r13
    add r12, rbx
    mov rax, r10
    mov rdx, r12
    movdqu xmm0, [rdx]
    movdqu xmm1, [rdx+16]
    movdqu xmm2, [rdx+32]
    movdqu xmm3, [rdx+48]
    movdqu [rax], xmm0
    movdqu [rax+16], xmm1
    movdqu [rax+32], xmm2
    movdqu [rax+48], xmm3
.L70:
    cmp rbx, 64
    jae .L69
.L71:
    cmp rbx, 0
    je .L72
    mov rcx, rsi
    mov rdx, r13
    mov r8, rbx
    call $memcpy
.L72:
    mov rax, rdi
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    mov r13, [rbp-40]
    leave
    ret

align 16
$memset:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov [rbp-40], r13
    mov [rbp-48], r14
    mov rsi, rcx
    mov edi, edx
    mov rbx, r8
    mov r10, rsi
    mov r12d, edi
    movzx r12d, r12b
    mov edi, r12d
    mov edi, edi
    mov r12, rdi
    mov rcx, 0x101010101010101
    imul r12, rcx
    mov rdi, r12
    cmp rbx, 16
    ja .L73
    cmp rbx, 8
    jb .L74
    mov qword [r10+0], rdi
    mov r12, rbx
    sub r12, 8
    mov r13, r10
    add r13, r12
    mov qword [r13+0], rdi
    mov rax, rsi
    jmp .Lret
.L74:
    cmp rbx, 4
    jb .L75
    mov r12, rdi
    mov r13d, r12d
    mov dword [r10+0], r13d
    mov r12, rbx
    sub r12, 4
    mov r14, r10
    add r14, r12
    mov dword [r14+0], r13d
    mov rax, rsi
    jmp .Lret
.L75:
    cmp rbx, 2
    jb .L76
    mov r12, rdi
    movzx r12d, r12w
    mov r13d, r12d
    mov word [r10+0], r13w
    mov r12, rbx
    sub r12, 2
    mov r14, r10
    add r14, r12
    mov word [r14+0], r13w
    mov rax, rsi
    jmp .Lret
.L76:
    cmp rbx, 1
    jne .L77
    mov r12, rdi
    movzx r12d, r12b
    mov byte [r10+0], r12b
.L77:
    mov rax, rsi
    jmp .Lret
.L73:
    cmp rbx, 32
    ja .L78
    mov rax, r10
    mov rdx, rdi
    mov rcx, rbx
    movq xmm0, rdx
    punpcklqdq xmm0, xmm0
    movdqu [rax], xmm0
    movdqu [rax+rcx-16], xmm0
    mov rax, rsi
    jmp .Lret
.L78:
    cmp rbx, 64
    ja .L79
    mov rax, r10
    mov rdx, rdi
    mov rcx, rbx
    movq xmm0, rdx
    punpcklqdq xmm0, xmm0
    movdqu [rax], xmm0
    movdqu [rax+16], xmm0
    movdqu [rax+rcx-32], xmm0
    movdqu [rax+rcx-16], xmm0
    mov rax, rsi
    jmp .Lret
.L79:
    cmp rbx, 2048
    jb .L80
    mov rax, r10
    mov rdx, rdi
    mov rcx, rbx
    push rdi
    mov rdi, rax
    mov eax, edx
    rep stosb
    pop rdi
    mov rax, rsi
    jmp .Lret
.L80:
    mov r12, rbx
    sub r12, 64
    mov rbx, r10
    add rbx, r12
    mov r12, rbx
    jmp .L82
.L81:
    mov rax, r10
    mov rdx, rdi
    movq xmm0, rdx
    punpcklqdq xmm0, xmm0
    movdqu [rax], xmm0
    movdqu [rax+16], xmm0
    movdqu [rax+32], xmm0
    movdqu [rax+48], xmm0
    mov rbx, r10
    add rbx, 64
    mov r10, rbx
.L82:
    cmp r10, r12
    jb .L81
.L83:
    mov rax, r12
    mov rdx, rdi
    movq xmm0, rdx
    punpcklqdq xmm0, xmm0
    movdqu [rax], xmm0
    movdqu [rax+16], xmm0
    movdqu [rax+32], xmm0
    movdqu [rax+48], xmm0
    mov rax, rsi
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
$zbytes64:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov rbx, rcx
    mov r10, 0x7f7f7f7f7f7f7f7f
    mov rsi, rbx
    and rsi, r10
    mov rdi, rsi
    add rdi, r10
    mov rsi, rdi
    or rsi, rbx
    mov rbx, rsi
    or rbx, r10
    mov r10, rbx
    not r10
    mov rax, r10
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    leave
    ret

align 16
$zbytes32:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov ebx, ecx
    mov r10d, 2139062143
    mov esi, ebx
    and esi, r10d
    mov edi, esi
    add edi, r10d
    mov esi, edi
    or esi, ebx
    mov ebx, esi
    or ebx, r10d
    mov r10d, ebx
    not r10d
    mov eax, r10d
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    leave
    ret

align 16
$memchr:
    push rbp
    mov rbp, rsp
    sub rsp, 96
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov [rbp-40], r13
    mov [rbp-48], r14
    mov [rbp-56], r15
    mov rdi, rcx
    mov r10d, edx
    mov rsi, r8
    mov rbx, rdi
    mov edi, r10d
    movzx edi, dil
    mov r12d, edi
    mov r10d, r12d
    mov r10d, r10d
    mov rdi, r10
    mov rcx, 0x101010101010101
    imul rdi, rcx
    mov r13, rdi
    cmp rsi, 64
    jb .L84
    mov r10, rsi
    sub r10, 64
    mov rdi, rbx
    add rdi, r10
    mov r10, rdi
    jmp .L86
.L85:
    mov rax, rbx
    mov rdx, r13
    movq xmm3, rdx
    punpcklqdq xmm3, xmm3
    movdqu xmm0, [rax]
    movdqu xmm1, [rax+16]
    movdqu xmm2, [rax+32]
    pcmpeqb xmm0, xmm3
    pcmpeqb xmm1, xmm3
    pcmpeqb xmm2, xmm3
    pmovmskb ecx, xmm0
    pmovmskb edx, xmm1
    pmovmskb r11d, xmm2
    movdqu xmm0, [rax+48]
    pcmpeqb xmm0, xmm3
    pmovmskb eax, xmm0
    shl rdx, 16
    shl r11, 32
    shl rax, 48
    or rax, rcx
    or rax, rdx
    or rax, r11
    mov r14, rax
    mov rdi, r14
    cmp rdi, 0
    je .L88
    mov rax, rdi
    bsf rax, rax
    mov r14d, eax
    mov r15d, r14d
    mov r15d, r15d
    mov r14, rbx
    add r14, r15
    mov rax, r14
    jmp .Lret
.L88:
    mov r14, rbx
    add r14, 64
    mov rbx, r14
.L86:
    cmp rbx, r10
    jb .L85
.L87:
    mov rax, r10
    mov rdx, r13
    movq xmm3, rdx
    punpcklqdq xmm3, xmm3
    movdqu xmm0, [rax]
    movdqu xmm1, [rax+16]
    movdqu xmm2, [rax+32]
    pcmpeqb xmm0, xmm3
    pcmpeqb xmm1, xmm3
    pcmpeqb xmm2, xmm3
    pmovmskb ecx, xmm0
    pmovmskb edx, xmm1
    pmovmskb r11d, xmm2
    movdqu xmm0, [rax+48]
    pcmpeqb xmm0, xmm3
    pmovmskb eax, xmm0
    shl rdx, 16
    shl r11, 32
    shl rax, 48
    or rax, rcx
    or rax, rdx
    or rax, r11
    mov rdi, rax
    mov r14, rdi
    cmp r14, 0
    je .L89
    mov rax, r14
    bsf rax, rax
    mov edi, eax
    mov r14d, edi
    mov r14d, r14d
    mov rdi, r10
    add rdi, r14
    mov rax, rdi
    jmp .Lret
.L89:
    mov eax, 0
    jmp .Lret
.L84:
    cmp rsi, 16
    jb .L90
    mov r10, rsi
    sub r10, 16
    mov rdi, rbx
    add rdi, r10
    mov r10, rdi
    jmp .L92
.L91:
    mov rax, rbx
    mov rdx, r13
    movdqu xmm0, [rax]
    movq xmm1, rdx
    punpcklqdq xmm1, xmm1
    pcmpeqb xmm0, xmm1
    pmovmskb eax, xmm0
    mov r14d, eax
    mov edi, r14d
    cmp edi, 0
    je .L94
    mov r14d, edi
    mov r14d, r14d
    mov rax, r14
    bsf rax, rax
    mov r15d, eax
    mov r14d, r15d
    mov r14d, r14d
    mov r15, rbx
    add r15, r14
    mov rax, r15
    jmp .Lret
.L94:
    mov r14, rbx
    add r14, 16
    mov rbx, r14
.L92:
    cmp rbx, r10
    jb .L91
.L93:
    mov rax, r10
    mov rdx, r13
    movdqu xmm0, [rax]
    movq xmm1, rdx
    punpcklqdq xmm1, xmm1
    pcmpeqb xmm0, xmm1
    pmovmskb eax, xmm0
    mov edi, eax
    mov r14d, edi
    cmp r14d, 0
    je .L95
    mov edi, r14d
    mov edi, edi
    mov rax, rdi
    bsf rax, rax
    mov r14d, eax
    mov edi, r14d
    mov edi, edi
    mov r14, r10
    add r14, rdi
    mov rax, r14
    jmp .Lret
.L95:
    mov eax, 0
    jmp .Lret
.L90:
    cmp rsi, 8
    jb .L96
    mov r10, qword [rbx+0]
    mov rdi, r10
    xor rdi, r13
    mov rcx, rdi
    call $zbytes64
    mov r10, rax
    mov rdi, r10
    cmp rdi, 0
    je .L97
    mov rax, rdi
    bsf rax, rax
    mov r10d, eax
    mov r14d, r10d
    shr r14d, 3
    mov r10d, r14d
    mov r10d, r10d
    mov r14, rbx
    add r14, r10
    mov rax, r14
    jmp .Lret
.L97:
    mov r10, rsi
    sub r10, 8
    mov r14, r10
    mov r10, rbx
    add r10, r14
    mov r15, qword [r10+0]
    mov r10, r15
    xor r10, r13
    mov rcx, r10
    call $zbytes64
    mov r10, rax
    mov rdi, r10
    cmp rdi, 0
    je .L98
    mov r10, rbx
    add r10, r14
    mov rax, rdi
    bsf rax, rax
    mov r14d, eax
    mov edi, r14d
    shr edi, 3
    mov r14d, edi
    mov r14d, r14d
    mov rdi, r10
    add rdi, r14
    mov rax, rdi
    jmp .Lret
.L98:
    mov eax, 0
    jmp .Lret
.L96:
    cmp rsi, 4
    jb .L99
    mov r10, r13
    mov edi, r10d
    mov r10d, dword [rbx+0]
    mov r13d, r10d
    xor r13d, edi
    mov ecx, r13d
    call $zbytes32
    mov r10d, eax
    mov r13d, r10d
    cmp r13d, 0
    je .L100
    mov r10d, r13d
    mov r10d, r10d
    mov rax, r10
    bsf rax, rax
    mov r14d, eax
    mov r10d, r14d
    shr r10d, 3
    mov r14d, r10d
    mov r14d, r14d
    mov r10, rbx
    add r10, r14
    mov rax, r10
    jmp .Lret
.L100:
    mov r10, rsi
    sub r10, 4
    mov r14, r10
    mov r10, rbx
    add r10, r14
    mov r15d, dword [r10+0]
    mov r10d, r15d
    xor r10d, edi
    mov ecx, r10d
    call $zbytes32
    mov r10d, eax
    mov r13d, r10d
    cmp r13d, 0
    je .L101
    mov r10, rbx
    add r10, r14
    mov edi, r13d
    mov edi, edi
    mov rax, rdi
    bsf rax, rax
    mov r13d, eax
    mov edi, r13d
    shr edi, 3
    mov r13d, edi
    mov r13d, r13d
    mov rdi, r10
    add rdi, r13
    mov rax, rdi
    jmp .Lret
.L101:
    mov eax, 0
    jmp .Lret
.L99:
    cmp rsi, 0
    jne .L102
    mov eax, 0
    jmp .Lret
.L102:
    mov r10, rbx
    add r10, 0
    movzx edi, byte [r10+0]
    cmp edi, r12d
    jne .L103
    mov rax, rbx
    jmp .Lret
.L103:
    cmp rsi, 1
    jne .L104
    mov eax, 0
    jmp .Lret
.L104:
    mov r10, rbx
    add r10, 1
    movzx edi, byte [r10+0]
    cmp edi, r12d
    jne .L105
    mov r10, rbx
    add r10, 1
    mov rax, r10
    jmp .Lret
.L105:
    cmp rsi, 2
    jne .L106
    mov eax, 0
    jmp .Lret
.L106:
    mov r10, rbx
    add r10, 2
    movzx esi, byte [r10+0]
    cmp esi, r12d
    jne .L107
    mov r10, rbx
    add r10, 2
    mov rax, r10
    jmp .Lret
.L107:
    mov eax, 0
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    mov r13, [rbp-40]
    mov r14, [rbp-48]
    mov r15, [rbp-56]
    leave
    ret

align 16
$memrchr:
    push rbp
    mov rbp, rsp
    sub rsp, 96
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov [rbp-40], r13
    mov [rbp-48], r14
    mov [rbp-56], r15
    mov rdi, rcx
    mov r10d, edx
    mov rsi, r8
    mov rbx, rdi
    mov edi, r10d
    movzx edi, dil
    mov eax, edi
    mov byte [rbp-57], al
    movzx r10d, byte [rbp-57]
    mov r10d, r10d
    mov rdi, r10
    mov rcx, 0x101010101010101
    imul rdi, rcx
    mov r13, rdi
    cmp rsi, 64
    jb .L108
    mov r10, rsi
    sub r10, 64
    mov rdi, r10
    jmp .L110
.L109:
    mov r14, rbx
    add r14, rdi
    mov rax, r14
    mov rdx, r13
    movq xmm3, rdx
    punpcklqdq xmm3, xmm3
    movdqu xmm0, [rax]
    movdqu xmm1, [rax+16]
    movdqu xmm2, [rax+32]
    pcmpeqb xmm0, xmm3
    pcmpeqb xmm1, xmm3
    pcmpeqb xmm2, xmm3
    pmovmskb ecx, xmm0
    pmovmskb edx, xmm1
    pmovmskb r11d, xmm2
    movdqu xmm0, [rax+48]
    pcmpeqb xmm0, xmm3
    pmovmskb eax, xmm0
    shl rdx, 16
    shl r11, 32
    shl rax, 48
    or rax, rcx
    or rax, rdx
    or rax, r11
    mov r15, rax
    mov r10, r15
    cmp r10, 0
    je .L112
    mov r14, rbx
    add r14, rdi
    mov rax, r10
    mov ecx, 127
    bsr rax, rax
    cmovz rax, rcx
    xor eax, 63
    mov r15d, eax
    mov r12d, 63
    sub r12d, r15d
    mov r15d, r12d
    mov r15d, r15d
    mov r12, r14
    add r12, r15
    mov rax, r12
    jmp .Lret
.L112:
    mov r12, rdi
    sub r12, 64
    mov rdi, r12
.L110:
    cmp rdi, 64
    jae .L109
.L111:
    mov r10, rbx
    add r10, rdi
    mov rax, r10
    mov rdx, r13
    movq xmm3, rdx
    punpcklqdq xmm3, xmm3
    movdqu xmm0, [rax]
    movdqu xmm1, [rax+16]
    movdqu xmm2, [rax+32]
    pcmpeqb xmm0, xmm3
    pcmpeqb xmm1, xmm3
    pcmpeqb xmm2, xmm3
    pmovmskb ecx, xmm0
    pmovmskb edx, xmm1
    pmovmskb r11d, xmm2
    movdqu xmm0, [rax+48]
    pcmpeqb xmm0, xmm3
    pmovmskb eax, xmm0
    shl rdx, 16
    shl r11, 32
    shl rax, 48
    or rax, rcx
    or rax, rdx
    or rax, r11
    mov r12, rax
    mov r10, r12
    cmp r10, 0
    je .L113
    mov r12, rbx
    add r12, rdi
    mov rax, r10
    mov ecx, 127
    bsr rax, rax
    cmovz rax, rcx
    xor eax, 63
    mov edi, eax
    mov r10d, 63
    sub r10d, edi
    mov edi, r10d
    mov edi, edi
    mov r10, r12
    add r10, rdi
    mov rax, r10
    jmp .Lret
.L113:
    mov rax, rbx
    mov rdx, r13
    movq xmm3, rdx
    punpcklqdq xmm3, xmm3
    movdqu xmm0, [rax]
    movdqu xmm1, [rax+16]
    movdqu xmm2, [rax+32]
    pcmpeqb xmm0, xmm3
    pcmpeqb xmm1, xmm3
    pcmpeqb xmm2, xmm3
    pmovmskb ecx, xmm0
    pmovmskb edx, xmm1
    pmovmskb r11d, xmm2
    movdqu xmm0, [rax+48]
    pcmpeqb xmm0, xmm3
    pmovmskb eax, xmm0
    shl rdx, 16
    shl r11, 32
    shl rax, 48
    or rax, rcx
    or rax, rdx
    or rax, r11
    mov r10, rax
    mov rdi, r10
    cmp rdi, 0
    je .L114
    mov rax, rdi
    mov ecx, 127
    bsr rax, rax
    cmovz rax, rcx
    xor eax, 63
    mov r10d, eax
    mov edi, 63
    sub edi, r10d
    mov r10d, edi
    mov r10d, r10d
    mov rdi, rbx
    add rdi, r10
    mov rax, rdi
    jmp .Lret
.L114:
    mov eax, 0
    jmp .Lret
.L108:
    cmp rsi, 16
    jb .L115
    mov r10, rsi
    sub r10, 16
    mov rdi, r10
    jmp .L117
.L116:
    mov r12, rbx
    add r12, rdi
    mov rax, r12
    mov rdx, r13
    movdqu xmm0, [rax]
    movq xmm1, rdx
    punpcklqdq xmm1, xmm1
    pcmpeqb xmm0, xmm1
    pmovmskb eax, xmm0
    mov r14d, eax
    mov r10d, r14d
    cmp r10d, 0
    je .L119
    mov r12, rbx
    add r12, rdi
    mov eax, r10d
    mov ecx, 63
    bsr eax, eax
    cmovz eax, ecx
    xor eax, 31
    mov r14d, eax
    mov r15d, 31
    sub r15d, r14d
    mov r14d, r15d
    mov r14d, r14d
    mov r15, r12
    add r15, r14
    mov rax, r15
    jmp .Lret
.L119:
    mov r12, rdi
    sub r12, 16
    mov rdi, r12
.L117:
    cmp rdi, 16
    jae .L116
.L118:
    mov r10, rbx
    add r10, rdi
    mov rax, r10
    mov rdx, r13
    movdqu xmm0, [rax]
    movq xmm1, rdx
    punpcklqdq xmm1, xmm1
    pcmpeqb xmm0, xmm1
    pmovmskb eax, xmm0
    mov r12d, eax
    mov r10d, r12d
    cmp r10d, 0
    je .L120
    mov r12, rbx
    add r12, rdi
    mov eax, r10d
    mov ecx, 63
    bsr eax, eax
    cmovz eax, ecx
    xor eax, 31
    mov edi, eax
    mov r10d, 31
    sub r10d, edi
    mov edi, r10d
    mov edi, edi
    mov r10, r12
    add r10, rdi
    mov rax, r10
    jmp .Lret
.L120:
    mov rax, rbx
    mov rdx, r13
    movdqu xmm0, [rax]
    movq xmm1, rdx
    punpcklqdq xmm1, xmm1
    pcmpeqb xmm0, xmm1
    pmovmskb eax, xmm0
    mov r10d, eax
    mov edi, r10d
    cmp edi, 0
    je .L121
    mov eax, edi
    mov ecx, 63
    bsr eax, eax
    cmovz eax, ecx
    xor eax, 31
    mov r10d, eax
    mov edi, 31
    sub edi, r10d
    mov r10d, edi
    mov r10d, r10d
    mov rdi, rbx
    add rdi, r10
    mov rax, rdi
    jmp .Lret
.L121:
    mov eax, 0
    jmp .Lret
.L115:
    cmp rsi, 8
    jb .L122
    mov r10, rsi
    sub r10, 8
    mov rdi, r10
    mov r10, rbx
    add r10, rdi
    mov r12, qword [r10+0]
    mov r10, r12
    xor r10, r13
    mov rcx, r10
    call $zbytes64
    mov r10, rax
    mov r12, r10
    cmp r12, 0
    je .L123
    mov r10, rbx
    add r10, rdi
    mov rax, r12
    mov ecx, 127
    bsr rax, rax
    cmovz rax, rcx
    xor eax, 63
    mov edi, eax
    mov r14d, 63
    sub r14d, edi
    mov edi, r14d
    shr edi, 3
    mov r14d, edi
    mov r14d, r14d
    mov rdi, r10
    add rdi, r14
    mov rax, rdi
    jmp .Lret
.L123:
    mov r10, qword [rbx+0]
    mov rdi, r10
    xor rdi, r13
    mov rcx, rdi
    call $zbytes64
    mov r10, rax
    mov r12, r10
    cmp r12, 0
    je .L124
    mov rax, r12
    mov ecx, 127
    bsr rax, rax
    cmovz rax, rcx
    xor eax, 63
    mov r10d, eax
    mov edi, 63
    sub edi, r10d
    mov r10d, edi
    shr r10d, 3
    mov edi, r10d
    mov edi, edi
    mov r10, rbx
    add r10, rdi
    mov rax, r10
    jmp .Lret
.L124:
    mov eax, 0
    jmp .Lret
.L122:
    cmp rsi, 4
    jb .L125
    mov r10, r13
    mov edi, r10d
    mov r10, rsi
    sub r10, 4
    mov r12, r10
    mov r10, rbx
    add r10, r12
    mov r13d, dword [r10+0]
    mov r10d, r13d
    xor r10d, edi
    mov ecx, r10d
    call $zbytes32
    mov r10d, eax
    mov r13d, r10d
    cmp r13d, 0
    je .L126
    mov r10, rbx
    add r10, r12
    mov eax, r13d
    mov ecx, 63
    bsr eax, eax
    cmovz eax, ecx
    xor eax, 31
    mov r12d, eax
    mov r14d, 31
    sub r14d, r12d
    mov r12d, r14d
    shr r12d, 3
    mov r14d, r12d
    mov r14d, r14d
    mov r12, r10
    add r12, r14
    mov rax, r12
    jmp .Lret
.L126:
    mov r10d, dword [rbx+0]
    mov r12d, r10d
    xor r12d, edi
    mov ecx, r12d
    call $zbytes32
    mov r10d, eax
    mov r13d, r10d
    cmp r13d, 0
    je .L127
    mov eax, r13d
    mov ecx, 63
    bsr eax, eax
    cmovz eax, ecx
    xor eax, 31
    mov r10d, eax
    mov edi, 31
    sub edi, r10d
    mov r10d, edi
    shr r10d, 3
    mov edi, r10d
    mov edi, edi
    mov r10, rbx
    add r10, rdi
    mov rax, r10
    jmp .Lret
.L127:
    mov eax, 0
    jmp .Lret
.L125:
    cmp rsi, 0
    jne .L128
    mov eax, 0
    jmp .Lret
.L128:
    mov r10, rsi
    sub r10, 1
    mov rdi, rbx
    add rdi, r10
    movzx r10d, byte [rdi+0]
    movzx ecx, byte [rbp-57]
    cmp r10d, ecx
    jne .L129
    mov r10, rsi
    sub r10, 1
    mov rdi, rbx
    add rdi, r10
    mov rax, rdi
    jmp .Lret
.L129:
    cmp rsi, 1
    jne .L130
    mov eax, 0
    jmp .Lret
.L130:
    mov r10, rsi
    sub r10, 2
    mov rdi, rbx
    add rdi, r10
    movzx r10d, byte [rdi+0]
    movzx ecx, byte [rbp-57]
    cmp r10d, ecx
    jne .L131
    mov r10, rsi
    sub r10, 2
    mov rdi, rbx
    add rdi, r10
    mov rax, rdi
    jmp .Lret
.L131:
    cmp rsi, 2
    jne .L132
    mov eax, 0
    jmp .Lret
.L132:
    mov r10, rbx
    add r10, 0
    movzx esi, byte [r10+0]
    movzx ecx, byte [rbp-57]
    cmp esi, ecx
    jne .L133
    mov rax, rbx
    jmp .Lret
.L133:
    mov eax, 0
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    mov r13, [rbp-40]
    mov r14, [rbp-48]
    mov r15, [rbp-56]
    leave
    ret

align 16
$memccpy:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov rsi, rcx
    mov rbx, rdx
    mov r10d, r8d
    mov rdi, r9
    mov rcx, rbx
    mov edx, r10d
    mov r8, rdi
    call $memchr
    mov r10, rax
    mov r12, r10
    cmp r12, 0
    jne .L134
    mov rcx, rsi
    mov rdx, rbx
    mov r8, rdi
    call $memcpy
    mov eax, 0
    jmp .Lret
.L134:
    mov r10, r12
    sub r10, rbx
    mov rdi, r10
    add rdi, 1
    mov r12, rdi
    mov rcx, rsi
    mov rdx, rbx
    mov r8, r12
    call $memcpy
    mov r10, rsi
    add r10, r12
    mov rax, r10
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    leave
    ret

align 16
$mempcpy:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov rbx, rcx
    mov r10, rdx
    mov rsi, r8
    mov rcx, rbx
    mov rdx, r10
    mov r8, rsi
    call $memcpy
    mov r10, rbx
    add r10, rsi
    mov rax, r10
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    leave
    ret

align 16
$memmem:
    push rbp
    mov rbp, rsp
    sub rsp, 192
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov [rbp-40], r13
    mov [rbp-48], r14
    mov [rbp-56], r15
    mov r10, rcx
    mov qword [rbp+24], rdx
    mov r12, r8
    mov qword [rbp+40], r9
    mov rbx, r10
    mov r13, r12
    cmp qword [rbp+40], 0
    jne .L135
    mov rax, r10
    jmp .Lret
.L135:
    mov rcx, qword [rbp+40]
    cmp qword [rbp+24], rcx
    jae .L136
    mov eax, 0
    jmp .Lret
.L136:
    cmp qword [rbp+40], 1
    jne .L137
    mov r12, r13
    add r12, 0
    movzx r14d, byte [r12+0]
    mov r12d, r14d
    mov rcx, r10
    mov edx, r12d
    mov r8, qword [rbp+24]
    call $memchr
    mov r10, rax
    mov rax, r10
    jmp .Lret
.L137:
    mov r10, r13
    add r10, 0
    movzx r12d, byte [r10+0]
    mov eax, r12d
    mov byte [rbp-57], al
    mov r10, qword [rbp+40]
    sub r10, 1
    mov r12, r13
    add r12, r10
    movzx r10d, byte [r12+0]
    mov eax, r10d
    mov byte [rbp-58], al
    movzx r10d, byte [rbp-57]
    mov r10d, r10d
    mov rax, r10
    mov rcx, 0x101010101010101
    imul rax, rcx
    mov qword [rbp-112], rax
    mov rax, qword [rbp-112]
    mov qword [rbp-72], rax
    movzx r10d, byte [rbp-58]
    mov r10d, r10d
    mov rax, r10
    mov rcx, 0x101010101010101
    imul rax, rcx
    mov qword [rbp-120], rax
    mov rax, qword [rbp-120]
    mov qword [rbp-80], rax
    mov r10, qword [rbp+24]
    sub r10, qword [rbp+40]
    mov rax, r10
    add rax, 1
    mov qword [rbp-128], rax
    mov rax, qword [rbp-128]
    mov qword [rbp-88], rax
    mov r10, qword [rbp+40]
    sub r10, 2
    mov r15, r10
    cmp qword [rbp-88], 16
    jb .L138
    mov r10, qword [rbp-88]
    sub r10, 16
    mov rax, r10
    mov qword [rbp-96], rax
    mov r14d, 0
    mov qword [rbp-104], 1
    jmp .L140
.L139:
    mov r10, rbx
    add r10, r14
    mov rax, r10
    mov rdx, qword [rbp-72]
    movdqu xmm0, [rax]
    movq xmm1, rdx
    punpcklqdq xmm1, xmm1
    pcmpeqb xmm0, xmm1
    pmovmskb eax, xmm0
    mov qword [rbp-136], rax
    mov r10, rbx
    add r10, r14
    mov rax, qword [rbp+40]
    sub rax, 1
    mov qword [rbp-144], rax
    mov rax, r10
    add rax, qword [rbp-144]
    mov qword [rbp-152], rax
    mov rax, qword [rbp-152]
    mov rdx, qword [rbp-80]
    movdqu xmm0, [rax]
    movq xmm1, rdx
    punpcklqdq xmm1, xmm1
    pcmpeqb xmm0, xmm1
    pmovmskb eax, xmm0
    mov r10d, eax
    mov eax, dword [rbp-136]
    and eax, r10d
    mov qword [rbp-160], rax
    mov edi, dword [rbp-160]
    jmp .L143
.L142:
    mov r10d, edi
    mov r10d, r10d
    mov rax, r10
    bsf rax, rax
    mov r12d, eax
    mov r10d, r12d
    mov r10d, r10d
    mov r12, r14
    add r12, r10
    mov rsi, r12
    mov r10, rbx
    add r10, rsi
    mov r12, r10
    add r12, 1
    mov r10, r13
    add r10, 1
    mov rcx, r12
    mov rdx, r10
    mov r8, r15
    call $memcmp
    mov r10d, eax
    cmp r10d, 0
    jne .L145
    mov r10, rbx
    add r10, rsi
    mov rax, r10
    jmp .Lret
.L145:
    mov r10d, edi
    sub r10d, 1
    mov r12d, edi
    and r12d, r10d
    mov edi, r12d
.L143:
    cmp edi, 0
    jne .L142
.L144:
    cmp r14, qword [rbp-96]
    jne .L146
    mov qword [rbp-104], 0
    jmp .L147
.L146:
    mov r10, r14
    add r10, 16
    mov r14, r10
    cmp r14, qword [rbp-96]
    jbe .L148
    mov r14, qword [rbp-96]
.L148:
.L147:
.L140:
    cmp qword [rbp-104], 0
    jne .L139
.L141:
    mov eax, 0
    jmp .Lret
.L138:
    mov esi, 0
    jmp .L151
.L149:
    mov r10, rbx
    add r10, rsi
    movzx edi, byte [r10+0]
    movzx ecx, byte [rbp-57]
    cmp edi, ecx
    jne .L153
    mov r10, qword [rbp+40]
    sub r10, 1
    mov rdi, rsi
    add rdi, r10
    mov r10, rbx
    add r10, rdi
    movzx edi, byte [r10+0]
    movzx ecx, byte [rbp-58]
    cmp edi, ecx
    jne .L153
    mov r10, rbx
    add r10, rsi
    mov rdi, r10
    add rdi, 1
    mov r10, r13
    add r10, 1
    mov rcx, rdi
    mov rdx, r10
    mov r8, r15
    call $memcmp
    mov r10d, eax
    cmp r10d, 0
    jne .L154
    mov r10, rbx
    add r10, rsi
    mov rax, r10
    jmp .Lret
.L154:
.L153:
.L150:
    add rsi, 1
.L151:
    cmp rsi, qword [rbp-88]
    jb .L149
.L152:
    mov eax, 0
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    mov r13, [rbp-40]
    mov r14, [rbp-48]
    mov r15, [rbp-56]
    leave
    ret

align 16
$bcmp:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov [rbp-40], r13
    mov [rbp-48], r14
    mov rdi, rcx
    mov rsi, rdx
    mov rbx, r8
    mov r10, rdi
    mov rdi, rsi
    cmp rbx, 64
    jb .L155
    mov rsi, rbx
    sub rsi, 64
    mov r12, r10
    add r12, rsi
    mov rsi, r12
    mov r12, rbx
    sub r12, 64
    mov r13, rdi
    add r13, r12
    mov r12, r13
    jmp .L157
.L156:
    mov rax, r10
    mov rdx, rdi
    movdqu xmm0, [rax]
    movdqu xmm1, [rax+16]
    movdqu xmm2, [rdx]
    movdqu xmm3, [rdx+16]
    pcmpeqb xmm0, xmm2
    pcmpeqb xmm1, xmm3
    pand xmm0, xmm1
    movdqu xmm1, [rax+32]
    movdqu xmm2, [rdx+32]
    movdqu xmm3, [rax+48]
    pcmpeqb xmm1, xmm2
    movdqu xmm2, [rdx+48]
    pcmpeqb xmm3, xmm2
    pand xmm1, xmm3
    pand xmm0, xmm1
    pmovmskb eax, xmm0
    cmp eax, 0xFFFF
    sete al
    movzx eax, al
    mov r13d, eax
    cmp r13d, 0
    jne .L159
    mov eax, 1
    jmp .Lret
.L159:
    mov r13, r10
    add r13, 64
    mov r10, r13
    mov r13, rdi
    add r13, 64
    mov rdi, r13
.L157:
    cmp r10, rsi
    jb .L156
.L158:
    mov rax, rsi
    mov rdx, r12
    movdqu xmm0, [rax]
    movdqu xmm1, [rax+16]
    movdqu xmm2, [rdx]
    movdqu xmm3, [rdx+16]
    pcmpeqb xmm0, xmm2
    pcmpeqb xmm1, xmm3
    pand xmm0, xmm1
    movdqu xmm1, [rax+32]
    movdqu xmm2, [rdx+32]
    movdqu xmm3, [rax+48]
    pcmpeqb xmm1, xmm2
    movdqu xmm2, [rdx+48]
    pcmpeqb xmm3, xmm2
    pand xmm1, xmm3
    pand xmm0, xmm1
    pmovmskb eax, xmm0
    cmp eax, 0xFFFF
    sete al
    movzx eax, al
    mov r13d, eax
    cmp r13d, 0
    jne .L160
    mov eax, 1
    jmp .Lret
.L160:
    mov eax, 0
    jmp .Lret
.L155:
    cmp rbx, 16
    jb .L161
    mov rsi, rbx
    sub rsi, 16
    mov r12, r10
    add r12, rsi
    mov rsi, r12
    mov r12, rbx
    sub r12, 16
    mov r13, rdi
    add r13, r12
    mov r12, r13
    jmp .L163
.L162:
    mov rax, r10
    mov rdx, rdi
    movdqu xmm0, [rax]
    movdqu xmm1, [rdx]
    pcmpeqb xmm0, xmm1
    pmovmskb eax, xmm0
    xor eax, 0xFFFF
    mov r13d, eax
    cmp r13d, 0
    je .L165
    mov eax, 1
    jmp .Lret
.L165:
    mov r13, r10
    add r13, 16
    mov r10, r13
    mov r13, rdi
    add r13, 16
    mov rdi, r13
.L163:
    cmp r10, rsi
    jb .L162
.L164:
    mov rax, rsi
    mov rdx, r12
    movdqu xmm0, [rax]
    movdqu xmm1, [rdx]
    pcmpeqb xmm0, xmm1
    pmovmskb eax, xmm0
    xor eax, 0xFFFF
    mov r13d, eax
    cmp r13d, 0
    je .L166
    mov eax, 1
    jmp .Lret
.L166:
    mov eax, 0
    jmp .Lret
.L161:
    cmp rbx, 8
    jb .L167
    mov rsi, rbx
    sub rsi, 8
    mov r12, rsi
    mov rsi, qword [r10+0]
    mov r13, qword [rdi+0]
    mov r14, rsi
    xor r14, r13
    mov rsi, r10
    add rsi, r12
    mov r13, qword [rsi+0]
    mov rsi, rdi
    add rsi, r12
    mov r12, qword [rsi+0]
    mov rsi, r13
    xor rsi, r12
    mov r12, r14
    or r12, rsi
    mov rsi, r12
    cmp rsi, 0
    je .L168
    mov eax, 1
    jmp .Lret
.L168:
    mov eax, 0
    jmp .Lret
.L167:
    cmp rbx, 4
    jb .L169
    mov rsi, rbx
    sub rsi, 4
    mov r12, rsi
    mov esi, dword [r10+0]
    mov r13d, dword [rdi+0]
    mov r14d, esi
    xor r14d, r13d
    mov rsi, r10
    add rsi, r12
    mov r13d, dword [rsi+0]
    mov rsi, rdi
    add rsi, r12
    mov r12d, dword [rsi+0]
    mov esi, r13d
    xor esi, r12d
    mov r12d, r14d
    or r12d, esi
    mov esi, r12d
    cmp esi, 0
    je .L170
    mov eax, 1
    jmp .Lret
.L170:
    mov eax, 0
    jmp .Lret
.L169:
    cmp rbx, 0
    jne .L171
    mov eax, 0
    jmp .Lret
.L171:
    mov rsi, r10
    add rsi, 0
    movzx r12d, byte [rsi+0]
    mov rsi, rdi
    add rsi, 0
    movzx r13d, byte [rsi+0]
    mov esi, r12d
    xor esi, r13d
    movzx esi, sil
    mov r12d, esi
    mov rsi, rbx
    shr rsi, 1
    mov r13, r10
    add r13, rsi
    movzx esi, byte [r13+0]
    mov r13, rbx
    shr r13, 1
    mov r14, rdi
    add r14, r13
    movzx r13d, byte [r14+0]
    mov r14d, esi
    xor r14d, r13d
    movzx r14d, r14b
    mov esi, r14d
    mov r13d, r12d
    or r13d, esi
    mov rsi, rbx
    sub rsi, 1
    mov r12, r10
    add r12, rsi
    movzx r10d, byte [r12+0]
    mov rsi, rbx
    sub rsi, 1
    mov rbx, rdi
    add rbx, rsi
    movzx esi, byte [rbx+0]
    mov ebx, r10d
    xor ebx, esi
    movzx ebx, bl
    mov r10d, ebx
    mov ebx, r13d
    or ebx, r10d
    mov r10d, ebx
    cmp r10d, 0
    je .L172
    mov eax, 1
    jmp .Lret
.L172:
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
$bzero:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov [rbp-8], rbx
    mov rbx, rcx
    mov r10, rdx
    mov rcx, rbx
    mov edx, 0
    mov r8, r10
    call $memset
.Lret:
    mov rbx, [rbp-8]
    leave
    ret

align 16
$bcopy:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov rsi, rcx
    mov r10, rdx
    mov rbx, r8
    mov rcx, r10
    mov rdx, rsi
    mov r8, rbx
    call $memmove
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    leave
    ret

align 16
$memset_explicit:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov rbx, rcx
    mov esi, edx
    mov r10, r8
    mov rcx, rbx
    mov edx, esi
    mov r8, r10
    call $memset
    ; __mem_barrier
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    leave
    ret

align 16
$explicit_bzero:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov [rbp-8], rbx
    mov rbx, rcx
    mov r10, rdx
    mov rcx, rbx
    mov edx, 0
    mov r8, r10
    call $memset
    ; __mem_barrier
.Lret:
    mov rbx, [rbp-8]
    leave
    ret

align 16
$memcpy_s:
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
    mov rdi, rdx
    mov r12, r8
    mov rsi, r9
    cmp rsi, 0
    jne .L173
    mov eax, 0
    jmp .Lret
.L173:
    cmp rbx, 0
    jne .L174
    mov eax, 22
    jmp .Lret
.L174:
    cmp r12, 0
    jne .L175
    mov rcx, rbx
    mov edx, 0
    mov r8, rdi
    call $memset
    mov eax, 22
    jmp .Lret
.L175:
    cmp rdi, rsi
    jae .L176
    mov rcx, rbx
    mov edx, 0
    mov r8, rdi
    call $memset
    mov eax, 34
    jmp .Lret
.L176:
    mov r10, rbx
    mov r13, r12
    mov r14, r10
    sub r14, r13
    cmp r14, rsi
    jb .L178
    mov r14, r13
    sub r14, r10
    cmp r14, rsi
    jae .L177
.L178:
    mov rcx, rbx
    mov edx, 0
    mov r8, rdi
    call $memset
    mov eax, 22
    jmp .Lret
.L177:
    mov rcx, rbx
    mov rdx, r12
    mov r8, rsi
    call $memcpy
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
$memmove_s:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov rbx, rcx
    mov rdi, rdx
    mov r12, r8
    mov rsi, r9
    cmp rsi, 0
    jne .L179
    mov eax, 0
    jmp .Lret
.L179:
    cmp rbx, 0
    jne .L180
    mov eax, 22
    jmp .Lret
.L180:
    cmp r12, 0
    jne .L181
    mov rcx, rbx
    mov edx, 0
    mov r8, rdi
    call $memset
    mov eax, 22
    jmp .Lret
.L181:
    cmp rdi, rsi
    jae .L182
    mov rcx, rbx
    mov edx, 0
    mov r8, rdi
    call $memset
    mov eax, 34
    jmp .Lret
.L182:
    mov rcx, rbx
    mov rdx, r12
    mov r8, rsi
    call $memmove
    mov eax, 0
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    leave
    ret

align 16
$memset_s:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov rbx, rcx
    mov r10, rdx
    mov esi, r8d
    mov rdi, r9
    cmp rbx, 0
    jne .L183
    mov eax, 22
    jmp .Lret
.L183:
    cmp rdi, r10
    jbe .L184
    mov rcx, rbx
    mov edx, esi
    mov r8, r10
    call $memset_explicit
    mov eax, 34
    jmp .Lret
.L184:
    mov rcx, rbx
    mov edx, esi
    mov r8, rdi
    call $memset_explicit
    mov eax, 0
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    leave
    ret

align 16
$lower8:
    push rbp
    mov rbp, rsp
    sub rsp, 16
    mov [rbp-8], rbx
    mov r10d, ecx
    mov ebx, r10d
    sub ebx, 65
    cmp ebx, 26
    jae .L185
    mov ebx, r10d
    add ebx, 32
    mov eax, ebx
    jmp .Lret
.L185:
    mov eax, r10d
.Lret:
    mov rbx, [rbp-8]
    leave
    ret

align 16
$_memicmp:
    push rbp
    mov rbp, rsp
    sub rsp, 96
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov [rbp-40], r13
    mov [rbp-48], r14
    mov [rbp-56], r15
    mov rdi, rcx
    mov r10, rdx
    mov rsi, r8
    mov rbx, rdi
    mov rdi, r10
    mov r12d, 0
    jmp .L187
.L186:
    mov r10, rbx
    add r10, r12
    mov r13, qword [r10+0]
    mov r10, rdi
    add r10, r12
    mov r14, qword [r10+0]
    cmp r13, r14
    je .L189
    jmp .L188
.L189:
    mov r10, r12
    add r10, 8
    mov r12, r10
.L187:
    mov r10, r12
    add r10, 8
    cmp r10, rsi
    jbe .L186
.L188:
    jmp .L192
.L190:
    mov r10, rbx
    add r10, r12
    movzx r15d, byte [r10+0]
    mov r10d, r15d
    mov r14d, r10d
    mov r10, rdi
    add r10, r12
    movzx r15d, byte [r10+0]
    mov r10d, r15d
    mov r13d, r10d
    cmp r14d, r13d
    je .L194
    mov ecx, r14d
    call $lower8
    mov r10d, eax
    mov r14d, r10d
    mov ecx, r13d
    call $lower8
    mov r10d, eax
    mov r13d, r10d
    cmp r14d, r13d
    je .L195
    mov r10d, r14d
    sub r10d, r13d
    mov eax, r10d
    jmp .Lret
.L195:
.L194:
.L191:
    add r12, 1
.L192:
    cmp r12, rsi
    jb .L190
.L193:
    mov eax, 0
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    mov r13, [rbp-40]
    mov r14, [rbp-48]
    mov r15, [rbp-56]
    leave
    ret

align 16
$memicmp:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov r10, rcx
    mov rbx, rdx
    mov rsi, r8
    mov rcx, r10
    mov rdx, rbx
    mov r8, rsi
    call $_memicmp
    mov r10d, eax
    mov eax, r10d
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    leave
    ret

align 16
$wmemcpy:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov rbx, rcx
    mov rsi, rdx
    mov rdi, r8
    mov r10, rdi
    shl r10, 1
    mov rcx, rbx
    mov rdx, rsi
    mov r8, r10
    call $memcpy
    mov r10, rax
    mov rax, r10
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    leave
    ret

align 16
$wmemmove:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov rbx, rcx
    mov rsi, rdx
    mov rdi, r8
    mov r10, rdi
    shl r10, 1
    mov rcx, rbx
    mov rdx, rsi
    mov r8, r10
    call $memmove
    mov r10, rax
    mov rax, r10
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    leave
    ret

align 16
$wmemset:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov [rbp-40], r13
    mov [rbp-48], r14
    mov [rbp-56], r15
    mov rbx, rcx
    movzx esi, dx
    mov rdi, r8
    mov r10, rbx
    mov r12, rdi
    shl r12, 1
    mov rdi, r12
    mov r12d, esi
    mov r12d, r12d
    mov r13, r12
    mov rcx, 0x1000100010001
    imul r13, rcx
    mov r12, r13
    cmp rdi, 16
    ja .L196
    cmp rdi, 8
    jb .L197
    mov qword [r10+0], r12
    mov r13, rdi
    sub r13, 8
    mov r14, r10
    add r14, r13
    mov qword [r14+0], r12
    mov rax, rbx
    jmp .Lret
.L197:
    cmp rdi, 4
    jb .L198
    mov r13, r12
    mov r14d, r13d
    mov dword [r10+0], r14d
    mov r13, rdi
    sub r13, 4
    mov r15, r10
    add r15, r13
    mov dword [r15+0], r14d
    mov rax, rbx
    jmp .Lret
.L198:
    cmp rdi, 2
    jne .L199
    mov word [r10+0], si
.L199:
    mov rax, rbx
    jmp .Lret
.L196:
    cmp rdi, 32
    ja .L200
    mov rax, r10
    mov rdx, r12
    mov rcx, rdi
    movq xmm0, rdx
    punpcklqdq xmm0, xmm0
    movdqu [rax], xmm0
    movdqu [rax+rcx-16], xmm0
    mov rax, rbx
    jmp .Lret
.L200:
    cmp rdi, 64
    ja .L201
    mov rax, r10
    mov rdx, r12
    mov rcx, rdi
    movq xmm0, rdx
    punpcklqdq xmm0, xmm0
    movdqu [rax], xmm0
    movdqu [rax+16], xmm0
    movdqu [rax+rcx-32], xmm0
    movdqu [rax+rcx-16], xmm0
    mov rax, rbx
    jmp .Lret
.L201:
    cmp rdi, 128
    ja .L202
    mov rax, r10
    mov rdx, r12
    mov rcx, rdi
    movq xmm0, rdx
    punpcklqdq xmm0, xmm0
    movdqu [rax], xmm0
    movdqu [rax+16], xmm0
    movdqu [rax+32], xmm0
    movdqu [rax+48], xmm0
    movdqu [rax+rcx-64], xmm0
    movdqu [rax+rcx-48], xmm0
    movdqu [rax+rcx-32], xmm0
    movdqu [rax+rcx-16], xmm0
    mov rax, rbx
    jmp .Lret
.L202:
    mov rsi, rdi
    sub rsi, 64
    mov rdi, r10
    add rdi, rsi
    mov rsi, rdi
    mov rax, r10
    mov rdx, r12
    movq xmm0, rdx
    punpcklqdq xmm0, xmm0
    movdqu [rax], xmm0
    movdqu [rax+16], xmm0
    movdqu [rax+32], xmm0
    movdqu [rax+48], xmm0
    mov rdi, r10
    and rdi, 63
    mov r13d, 64
    sub r13, rdi
    mov rdi, r13
    mov r13, r10
    add r13, rdi
    mov r10, r13
    jmp .L204
.L203:
    mov rax, r10
    mov rdx, r12
    movq xmm0, rdx
    punpcklqdq xmm0, xmm0
    movdqu [rax], xmm0
    movdqu [rax+16], xmm0
    movdqu [rax+32], xmm0
    movdqu [rax+48], xmm0
    mov rdi, r10
    add rdi, 64
    mov r10, rdi
.L204:
    cmp r10, rsi
    jb .L203
.L205:
    mov rax, rsi
    mov rdx, r12
    movq xmm0, rdx
    punpcklqdq xmm0, xmm0
    movdqu [rax], xmm0
    movdqu [rax+16], xmm0
    movdqu [rax+32], xmm0
    movdqu [rax+48], xmm0
    mov rax, rbx
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    mov r13, [rbp-40]
    mov r14, [rbp-48]
    mov r15, [rbp-56]
    leave
    ret

align 16
$wdiff_at:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov rsi, rcx
    mov rdi, rdx
    mov rbx, r8
    mov r10, rbx
    imul r10, r10, 2
    mov r12, rsi
    add r12, r10
    movzx r10d, word [r12+0]
    mov esi, r10d
    mov r10, rbx
    imul r10, r10, 2
    mov rbx, rdi
    add rbx, r10
    movzx r10d, word [rbx+0]
    mov ebx, r10d
    mov r10d, esi
    sub r10d, ebx
    mov eax, r10d
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    leave
    ret

align 16
$wmemcmp:
    push rbp
    mov rbp, rsp
    sub rsp, 96
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov [rbp-40], r13
    mov [rbp-48], r14
    mov [rbp-56], r15
    mov qword [rbp+16], rcx
    mov qword [rbp+24], rdx
    mov rbx, r8
    mov r10, rbx
    shl r10, 1
    mov r12, r10
    cmp r12, 16
    jb .L206
    mov r13, qword [rbp+16]
    mov r14, qword [rbp+24]
    mov r10, r12
    sub r10, 16
    mov r15, r13
    add r15, r10
    mov rsi, r15
    mov r10, r12
    sub r10, 16
    mov r12, r14
    add r12, r10
    mov r15, r12
    jmp .L208
.L207:
    mov rax, r13
    mov rdx, r14
    movdqu xmm0, [rax]
    movdqu xmm1, [rdx]
    pcmpeqb xmm0, xmm1
    pmovmskb eax, xmm0
    xor eax, 0xFFFF
    mov r10d, eax
    mov r12d, r10d
    cmp r12d, 0
    je .L210
    mov r10d, r12d
    mov r10d, r10d
    mov rax, r10
    bsf rax, rax
    mov edi, eax
    mov r10d, edi
    shr r10d, 1
    mov edi, r10d
    mov edi, edi
    mov rcx, r13
    mov rdx, r14
    mov r8, rdi
    call $wdiff_at
    mov r10d, eax
    mov eax, r10d
    jmp .Lret
.L210:
    mov r10, r13
    add r10, 16
    mov r13, r10
    mov r10, r14
    add r10, 16
    mov r14, r10
.L208:
    cmp r13, rsi
    jb .L207
.L209:
    mov rax, rsi
    mov rdx, r15
    movdqu xmm0, [rax]
    movdqu xmm1, [rdx]
    pcmpeqb xmm0, xmm1
    pmovmskb eax, xmm0
    xor eax, 0xFFFF
    mov r10d, eax
    mov edi, r10d
    cmp edi, 0
    je .L211
    mov r10d, edi
    mov r10d, r10d
    mov rax, r10
    bsf rax, rax
    mov edi, eax
    mov r10d, edi
    shr r10d, 1
    mov edi, r10d
    mov edi, edi
    mov rcx, rsi
    mov rdx, r15
    mov r8, rdi
    call $wdiff_at
    mov r10d, eax
    mov eax, r10d
    jmp .Lret
.L211:
    mov eax, 0
    jmp .Lret
.L206:
    mov r10, qword [rbp+16]
    mov rsi, qword [rbp+24]
    mov edi, 0
    jmp .L214
.L212:
    mov r12, rdi
    imul r12, r12, 2
    mov r13, r10
    add r13, r12
    movzx r12d, word [r13+0]
    mov r13, rdi
    imul r13, r13, 2
    mov r14, rsi
    add r14, r13
    movzx r13d, word [r14+0]
    cmp r12d, r13d
    je .L216
    mov r12, rdi
    imul r12, r12, 2
    mov r13, r10
    add r13, r12
    movzx r12d, word [r13+0]
    mov r13d, r12d
    mov r12, rdi
    imul r12, r12, 2
    mov r14, rsi
    add r14, r12
    movzx r12d, word [r14+0]
    mov r14d, r12d
    mov r12d, r13d
    sub r12d, r14d
    mov eax, r12d
    jmp .Lret
.L216:
.L213:
    add rdi, 1
.L214:
    cmp rdi, rbx
    jb .L212
.L215:
    mov eax, 0
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    mov r13, [rbp-40]
    mov r14, [rbp-48]
    mov r15, [rbp-56]
    leave
    ret

align 16
$wmemchr:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov [rbp-40], r13
    mov [rbp-48], r14
    mov [rbp-56], r15
    mov qword [rbp+16], rcx
    movzx r10d, dx
    mov rbx, r8
    mov rdi, rbx
    shl rdi, 1
    mov r12, rdi
    cmp r12, 16
    jb .L217
    mov rdi, qword [rbp+16]
    mov r13d, r10d
    mov r13d, r13d
    mov r14, r13
    mov rcx, 0x1000100010001
    imul r14, rcx
    mov r13, r14
    mov r14, r12
    sub r14, 16
    mov r12, rdi
    add r12, r14
    mov r14, r12
    jmp .L219
.L218:
    mov rax, rdi
    mov rdx, r13
    movdqu xmm0, [rax]
    movd xmm1, edx
    pshuflw xmm1, xmm1, 0
    pshufd xmm1, xmm1, 0
    pcmpeqw xmm0, xmm1
    pmovmskb eax, xmm0
    mov r15d, eax
    mov r12d, r15d
    cmp r12d, 0
    je .L221
    mov r15d, r12d
    mov r15d, r15d
    mov rax, r15
    bsf rax, rax
    mov esi, eax
    mov r15d, esi
    mov r15d, r15d
    mov rsi, rdi
    add rsi, r15
    mov rax, rsi
    jmp .Lret
.L221:
    mov rsi, rdi
    add rsi, 16
    mov rdi, rsi
.L219:
    cmp rdi, r14
    jb .L218
.L220:
    mov rax, r14
    mov rdx, r13
    movdqu xmm0, [rax]
    movd xmm1, edx
    pshuflw xmm1, xmm1, 0
    pshufd xmm1, xmm1, 0
    pcmpeqw xmm0, xmm1
    pmovmskb eax, xmm0
    mov esi, eax
    mov edi, esi
    cmp edi, 0
    je .L222
    mov esi, edi
    mov esi, esi
    mov rax, rsi
    bsf rax, rax
    mov edi, eax
    mov esi, edi
    mov esi, esi
    mov rdi, r14
    add rdi, rsi
    mov rax, rdi
    jmp .Lret
.L222:
    mov eax, 0
    jmp .Lret
.L217:
    mov rsi, qword [rbp+16]
    mov edi, 0
    jmp .L225
.L223:
    mov r12, rdi
    imul r12, r12, 2
    mov r13, rsi
    add r13, r12
    movzx r12d, word [r13+0]
    cmp r12d, r10d
    jne .L227
    mov r12, rdi
    imul r12, r12, 2
    mov r13, rsi
    add r13, r12
    mov rax, r13
    jmp .Lret
.L227:
.L224:
    add rdi, 1
.L225:
    cmp rdi, rbx
    jb .L223
.L226:
    mov eax, 0
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    mov r13, [rbp-40]
    mov r14, [rbp-48]
    mov r15, [rbp-56]
    leave
    ret

section .rdata align=16
align 16
__fsgn32: dd 0x80000000, 0, 0, 0
align 16
__fsgn64: dq 0x8000000000000000, 0

section .bss align=16
alignb 8
$g_free: resb 520
alignb 8
$g_cur: resb 8
alignb 8
$g_end: resb 8
