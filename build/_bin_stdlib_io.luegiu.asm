; gerado pelo codegen luegiu (win64 / nasm)
bits 64
default rel

extern $SetFilePointerEx
extern $GetStdHandle
extern $WriteFile
extern $ReadFile
extern $CreateFileA
extern $CloseHandle
extern $strlen
global $write
global $fwrite
global $fwritec
global $open
global $close
global $tell
global $seek
global $read
global $vfwrite
global $vwrite
global $vsnwrite
global $snwrite

section .text

align 16
$write_flush:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov r10, rcx
    cmp dword [rel $mem_mode+0], 0
    jne .L0
    cmp dword [rel $write_len+0], 0
    jbe .L0
    mov qword [rbp-24], 0
    mov ebx, dword [rel $write_len+0]
    mov ebx, ebx
    mov rsi, rbx
    mov qword [rsp+32], 0
    mov rcx, r10
    lea rdx, [rel $write_buf+0]
    mov r8d, esi
    lea r9, [rbp-24]
    call $WriteFile
    mov dword [rel $write_len+0], 0
.L0:
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    leave
    ret

align 16
$write_put:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov r10, rcx
    movzx ebx, dl
    cmp dword [rel $mem_mode+0], 0
    je .L1
    cmp dword [rel $mem_cap+0], 0
    jbe .L2
    mov esi, dword [rel $write_total+0]
    add esi, 1
    cmp esi, dword [rel $mem_cap+0]
    jae .L2
    mov esi, dword [rel $write_total+0]
    movsxd rsi, esi
    mov rdi, qword [rel $mem_dst+0]
    add rdi, rsi
    mov byte [rdi+0], bl
.L2:
    mov eax, dword [rel $write_total+0]
    add eax, 1
    mov dword [rel $write_total+0], eax
    jmp .L3
.L1:
    cmp dword [rel $write_len+0], 512
    jb .L4
    mov rcx, r10
    call $write_flush
.L4:
    mov r10d, dword [rel $write_len+0]
    mov r10d, r10d
    lea rsi, [rel $write_buf+0]
    add rsi, r10
    mov byte [rsi+0], bl
    mov eax, dword [rel $write_len+0]
    add eax, 1
    mov dword [rel $write_len+0], eax
    mov eax, dword [rel $write_total+0]
    add eax, 1
    mov dword [rel $write_total+0], eax
.L3:
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    leave
    ret

align 16
$write_repeat:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov rdi, rcx
    movzx esi, dl
    mov ebx, r8d
    jmp .L6
.L5:
    mov rcx, rdi
    mov edx, esi
    call $write_put
    add ebx, -1
.L6:
    cmp ebx, 0
    jg .L5
.L7:
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    leave
    ret

align 16
$write_bytes:
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
    mov esi, r8d
    mov ebx, 0
    jmp .L10
.L8:
    mov r10d, ebx
    movsxd r10, r10d
    mov r13, r12
    add r13, r10
    movzx r10d, byte [r13+0]
    mov rcx, rdi
    mov edx, r10d
    call $write_put
.L9:
    add ebx, 1
.L10:
    cmp ebx, esi
    jl .L8
.L11:
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    mov r13, [rbp-40]
    leave
    ret

align 16
$write_field:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov rdi, rcx
    mov r12, rdx
    mov esi, r8d
    mov ebx, 0
    cmp dword [rel $f_width+0], esi
    jle .L12
    mov r10d, dword [rel $f_width+0]
    sub r10d, esi
    mov ebx, r10d
.L12:
    mov r10d, dword [rel $f_flags+0]
    and r10d, 1
    test r10d, r10d
    jne .L13
    mov rcx, rdi
    mov edx, 32
    mov r8d, ebx
    call $write_repeat
.L13:
    mov rcx, rdi
    mov rdx, r12
    mov r8d, esi
    call $write_bytes
    mov r10d, dword [rel $f_flags+0]
    and r10d, 1
    test r10d, r10d
    je .L14
    mov rcx, rdi
    mov edx, 32
    mov r8d, ebx
    call $write_repeat
.L14:
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    leave
    ret

align 16
$digits_of:
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
    mov rsi, rcx
    mov rbx, rdx
    mov rdi, r8
    mov dword [rbp+40], r9d
    mov r10d, 72
    cmp rbx, 0
    jne .L15
    add r10d, -1
    mov r13d, r10d
    movsxd r13, r13d
    mov r14, rsi
    add r14, r13
    mov byte [r14+0], 48
.L15:
    jmp .L17
.L16:
    mov rax, rbx
    xor edx, edx
    div rdi
    mov r14, rdx
    mov r13, r14
    mov rax, rbx
    xor edx, edx
    div rdi
    mov r14, rax
    mov rbx, r14
    add r10d, -1
    cmp r13, 10
    jae .L19
    mov r14d, r10d
    movsxd r14, r14d
    mov r15, rsi
    add r15, r14
    mov r14d, 48
    add r14, r13
    mov r12, r14
    movzx r12d, r12b
    mov byte [r15+0], r12b
    jmp .L20
.L19:
    cmp dword [rbp+40], 0
    je .L21
    mov r12d, r10d
    movsxd r12, r12d
    mov r14, rsi
    add r14, r12
    mov r12d, 65
    add r12, r13
    mov r15, r12
    sub r15, 10
    mov r12, r15
    movzx r12d, r12b
    mov byte [r14+0], r12b
    jmp .L22
.L21:
    mov r12d, r10d
    movsxd r12, r12d
    mov r14, rsi
    add r14, r12
    mov r12d, 97
    add r12, r13
    mov r15, r12
    sub r15, 10
    mov r12, r15
    movzx r12d, r12b
    mov byte [r14+0], r12b
.L22:
.L20:
.L17:
    cmp rbx, 0
    jne .L16
.L18:
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
$narrow_signed:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov rbx, rcx
    mov r10, rbx
    cmp dword [rel $f_bits+0], 8
    jne .L23
    mov rsi, rbx
    movsx esi, sil
    mov edi, esi
    movsxd rdi, edi
    mov r10, rdi
    jmp .L24
.L23:
    cmp dword [rel $f_bits+0], 16
    jne .L25
    mov rsi, rbx
    movsx esi, si
    mov edi, esi
    movsxd rdi, edi
    mov r10, rdi
    jmp .L26
.L25:
    cmp dword [rel $f_bits+0], 32
    jne .L27
    mov rsi, rbx
    mov ebx, esi
    movsxd rbx, ebx
    mov r10, rbx
.L27:
.L26:
.L24:
    mov rax, r10
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    leave
    ret

align 16
$narrow_unsigned:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov rbx, rcx
    mov r10, rbx
    cmp dword [rel $f_bits+0], 8
    jne .L28
    mov rsi, rbx
    movzx esi, sil
    mov edi, esi
    mov edi, edi
    mov r10, rdi
    jmp .L29
.L28:
    cmp dword [rel $f_bits+0], 16
    jne .L30
    mov rsi, rbx
    movzx esi, si
    mov edi, esi
    mov edi, edi
    mov r10, rdi
    jmp .L31
.L30:
    cmp dword [rel $f_bits+0], 32
    jne .L32
    mov rsi, rbx
    mov ebx, esi
    mov ebx, ebx
    mov r10, rbx
.L32:
.L31:
.L29:
    mov rax, r10
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    leave
    ret

align 16
$write_number:
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
    mov rbx, rcx
    mov qword [rbp+24], rdx
    mov esi, r8d
    mov rdi, r9
    mov rcx, rdi
    call $strlen
    mov r10, rax
    mov r13, r10
    mov r14d, r13d
    mov r13d, 0
    cmp dword [rel $f_prec+0], esi
    jle .L33
    mov r10d, dword [rel $f_prec+0]
    sub r10d, esi
    mov r13d, r10d
.L33:
    mov r10d, r14d
    add r10d, r13d
    mov r15d, r10d
    add r15d, esi
    mov r10d, r15d
    mov r15d, 0
    cmp dword [rel $f_width+0], r10d
    jle .L34
    mov r12d, dword [rel $f_width+0]
    sub r12d, r10d
    mov r15d, r12d
.L34:
    mov r10d, dword [rel $f_flags+0]
    and r10d, 1
    mov r12d, r10d
    test r12d, r12d
    jne .L35
    mov r10d, dword [rel $f_flags+0]
    and r10d, 2
    test r10d, r10d
    je .L35
    cmp dword [rel $f_prec+0], 0
    jge .L35
    mov r10d, r13d
    add r10d, r15d
    mov r13d, r10d
    mov r15d, 0
.L35:
    test r12d, r12d
    jne .L36
    mov rcx, rbx
    mov edx, 32
    mov r8d, r15d
    call $write_repeat
.L36:
    mov rcx, rbx
    mov rdx, rdi
    mov r8d, r14d
    call $write_bytes
    mov rcx, rbx
    mov edx, 48
    mov r8d, r13d
    call $write_repeat
    mov rcx, rbx
    mov rdx, qword [rbp+24]
    mov r8d, esi
    call $write_bytes
    test r12d, r12d
    je .L37
    mov rcx, rbx
    mov edx, 32
    mov r8d, r15d
    call $write_repeat
.L37:
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
$write_double:
    push rbp
    mov rbp, rsp
    sub rsp, 240
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov [rbp-40], r13
    mov [rbp-48], r14
    mov [rbp-56], r15
    movups [rbp-72], xmm6
    mov rsi, rcx
    movaps xmm6, xmm1
    mov ebx, dword [rel $f_prec+0]
    cmp ebx, 0
    jge .L38
    mov ebx, 6
.L38:
    cmp ebx, 17
    jle .L39
    mov ebx, 17
.L39:
    mov edi, 0
    xorps xmm0, xmm0
    ucomisd xmm0, xmm6
    seta r10b
    movzx r10d, r10b
    test r10d, r10d
    je .L40
    mov edi, 45
    movaps xmm4, xmm6
    xorps xmm4, oword [rel __fsgn64]
    movaps xmm6, xmm4
    jmp .L41
.L40:
    mov r10d, dword [rel $f_flags+0]
    and r10d, 4
    test r10d, r10d
    je .L42
    mov edi, 43
    jmp .L43
.L42:
    mov r10d, dword [rel $f_flags+0]
    and r10d, 8
    test r10d, r10d
    je .L44
    mov edi, 32
.L44:
.L43:
.L41:
    mov byte [rbp-194], dil
    mov byte [rbp-193], 0
    ucomisd xmm6, xmm6
    setne r10b
    setp cl
    or r10b, cl
    movzx r10d, r10b
    test r10d, r10d
    je .L45
    mov rcx, rsi
    lea rdx, [rel __str0+0]
    mov r8d, 3
    call $write_field
    jmp .L46
.L45:
    movaps xmm4, xmm6
    subsd xmm4, xmm6
    ucomisd xmm4, qword [rel __flt0+0]
    setne r10b
    setp cl
    or r10b, cl
    movzx r10d, r10b
    test r10d, r10d
    je .L47
    cmp edi, 45
    jne .L48
    mov rcx, rsi
    lea rdx, [rel __str1+0]
    mov r8d, 4
    call $write_field
    jmp .L49
.L48:
    mov rcx, rsi
    lea rdx, [rel __str2+0]
    mov r8d, 3
    call $write_field
.L49:
    jmp .L50
.L47:
    ucomisd xmm6, qword [rel __flt1+0]
    setae r10b
    movzx r10d, r10b
    test r10d, r10d
    je .L51
    mov rcx, rsi
    lea rdx, [rel __str3+0]
    mov r8d, 10
    call $write_field
    jmp .L52
.L51:
    movsd xmm4, qword [rel __flt2+0]
    mov r10d, 0
    jmp .L55
.L53:
    movaps xmm5, xmm4
    divsd xmm5, qword [rel __flt3+0]
    movaps xmm4, xmm5
.L54:
    add r10d, 1
.L55:
    cmp r10d, ebx
    jl .L53
.L56:
    movaps xmm5, xmm6
    addsd xmm5, xmm4
    movaps xmm6, xmm5
    cvttsd2si r10, xmm6
    mov rdi, r10
    mov rax, rdi
    pxor xmm4, xmm4
    test rax, rax
    js .Lx0
    cvtsi2sd xmm4, rax
    jmp .Ly0
.Lx0:
    mov rcx, rax
    shr rcx, 1
    and eax, 1
    or rcx, rax
    cvtsi2sd xmm4, rcx
    addsd xmm4, xmm4
.Ly0:
    movaps xmm5, xmm6
    subsd xmm5, xmm4
    movaps xmm6, xmm5
    lea rcx, [rbp-144]
    mov rdx, rdi
    mov r8d, 10
    mov r9d, 0
    call $digits_of
    mov r10d, eax
    mov edi, r10d
    mov r10d, 0
    mov r12d, edi
    jmp .L59
.L57:
    mov edi, r10d
    movsxd rdi, edi
    lea r13, [rbp-192]
    add r13, rdi
    mov edi, r12d
    movsxd rdi, edi
    lea r14, [rbp-144]
    add r14, rdi
    movzx edi, byte [r14+0]
    mov byte [r13+0], dil
    add r10d, 1
.L58:
    add r12d, 1
.L59:
    cmp r12d, 72
    jl .L57
.L60:
    cmp ebx, 0
    jg .L62
    mov edi, dword [rel $f_flags+0]
    and edi, 16
    test edi, edi
    je .L61
.L62:
    mov edi, r10d
    movsxd rdi, edi
    lea r12, [rbp-192]
    add r12, rdi
    mov byte [r12+0], 46
    add r10d, 1
.L61:
    mov edi, 0
    jmp .L65
.L63:
    movaps xmm4, xmm6
    mulsd xmm4, qword [rel __flt3+0]
    movaps xmm6, xmm4
    cvttsd2si r13d, xmm6
    mov r12d, r13d
    mov r13d, r10d
    movsxd r13, r13d
    lea r14, [rbp-192]
    add r14, r13
    mov r13d, 48
    add r13d, r12d
    mov r15d, r13d
    movzx r15d, r15b
    mov byte [r14+0], r15b
    add r10d, 1
    mov eax, r12d
    pxor xmm4, xmm4
    cvtsi2sd xmm4, eax
    movaps xmm5, xmm6
    subsd xmm5, xmm4
    movaps xmm6, xmm5
.L64:
    add edi, 1
.L65:
    cmp edi, ebx
    jl .L63
.L66:
    mov dword [rel $f_prec+0], -1
    mov rcx, rsi
    lea rdx, [rbp-192]
    mov r8d, r10d
    lea r9, [rbp-194]
    call $write_number
.L52:
.L50:
.L46:
.Lret:
    movups xmm6, [rbp-72]
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
$format_core:
    push rbp
    mov rbp, rsp
    sub rsp, 272
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov [rbp-40], r13
    mov [rbp-48], r14
    mov [rbp-56], r15
    mov qword [rbp+16], rcx
    mov rbx, rdx
    mov rsi, r8
    mov dword [rel $write_len+0], 0
    mov dword [rel $write_total+0], 0
    jmp .L68
.L67:
    movzx r10d, byte [rbx+0]
    mov eax, r10d
    mov byte [rbp-133], al
    add rbx, 1
    movzx eax, byte [rbp-133]
    cmp eax, 37
    je .L70
    mov rcx, qword [rbp+16]
    movzx edx, byte [rbp-133]
    call $write_put
    jmp .L68
.L70:
    mov dword [rel $f_flags+0], 0
    mov dword [rel $f_width+0], 0
    mov dword [rel $f_prec+0], -1
    mov dword [rel $f_bits+0], 32
    mov r12d, 1
    jmp .L72
.L71:
    movzx r10d, byte [rbx+0]
    mov r14d, r10d
    cmp r14d, 45
    jne .L74
    mov eax, dword [rel $f_flags+0]
    or eax, 1
    mov dword [rel $f_flags+0], eax
    jmp .L75
.L74:
    cmp r14d, 48
    jne .L76
    mov eax, dword [rel $f_flags+0]
    or eax, 2
    mov dword [rel $f_flags+0], eax
    jmp .L77
.L76:
    cmp r14d, 43
    jne .L78
    mov eax, dword [rel $f_flags+0]
    or eax, 4
    mov dword [rel $f_flags+0], eax
    jmp .L79
.L78:
    cmp r14d, 32
    jne .L80
    mov eax, dword [rel $f_flags+0]
    or eax, 8
    mov dword [rel $f_flags+0], eax
    jmp .L81
.L80:
    cmp r14d, 35
    jne .L82
    mov eax, dword [rel $f_flags+0]
    or eax, 16
    mov dword [rel $f_flags+0], eax
    jmp .L83
.L82:
    mov r12d, 0
.L83:
.L81:
.L79:
.L77:
.L75:
    test r12d, r12d
    je .L84
    add rbx, 1
.L84:
.L72:
    test r12d, r12d
    jne .L71
.L73:
    movzx r10d, byte [rbx+0]
    cmp r10d, 42
    jne .L85
    mov r10, rsi
    add rsi, 8
    mov rax, qword [r10+0]
    mov qword [rbp-208], rax
    mov r10, qword [rbp-208]
    mov eax, r10d
    mov dword [rel $f_width+0], eax
    add rbx, 1
    cmp dword [rel $f_width+0], 0
    jge .L86
    mov eax, dword [rel $f_flags+0]
    or eax, 1
    mov dword [rel $f_flags+0], eax
    mov r10d, dword [rel $f_width+0]
    neg r10d
    mov eax, r10d
    mov dword [rel $f_width+0], eax
.L86:
    jmp .L87
.L85:
    jmp .L89
.L88:
    mov r10d, dword [rel $f_width+0]
    imul r10d, r10d, 10
    movzx edi, byte [rbx+0]
    mov eax, edi
    sub eax, 48
    movzx eax, al
    mov qword [rbp-216], rax
    movzx edi, byte [rbp-216]
    mov eax, r10d
    add eax, edi
    mov qword [rbp-224], rax
    mov eax, dword [rbp-224]
    mov dword [rel $f_width+0], eax
    add rbx, 1
.L89:
    movzx r10d, byte [rbx+0]
    cmp r10d, 48
    jb .L91
    movzx r10d, byte [rbx+0]
    cmp r10d, 57
    jbe .L88
.L91:
.L90:
.L87:
    movzx r10d, byte [rbx+0]
    cmp r10d, 46
    jne .L92
    add rbx, 1
    mov dword [rel $f_prec+0], 0
    movzx r10d, byte [rbx+0]
    cmp r10d, 42
    jne .L93
    mov r10, rsi
    add rsi, 8
    mov rdi, qword [r10+0]
    mov r10, rdi
    mov eax, r10d
    mov dword [rel $f_prec+0], eax
    add rbx, 1
    jmp .L94
.L93:
    jmp .L96
.L95:
    mov r10d, dword [rel $f_prec+0]
    imul r10d, r10d, 10
    movzx edi, byte [rbx+0]
    mov eax, edi
    sub eax, 48
    movzx eax, al
    mov qword [rbp-232], rax
    movzx edi, byte [rbp-232]
    mov eax, r10d
    add eax, edi
    mov qword [rbp-240], rax
    mov eax, dword [rbp-240]
    mov dword [rel $f_prec+0], eax
    add rbx, 1
.L96:
    movzx r10d, byte [rbx+0]
    cmp r10d, 48
    jb .L98
    movzx r10d, byte [rbx+0]
    cmp r10d, 57
    jbe .L95
.L98:
.L97:
.L94:
.L92:
    mov r12d, 1
    jmp .L100
.L99:
    movzx r10d, byte [rbx+0]
    mov r13d, r10d
    cmp r13d, 104
    jne .L102
    cmp dword [rel $f_bits+0], 16
    jne .L103
    mov dword [rel $f_bits+0], 8
    jmp .L104
.L103:
    mov dword [rel $f_bits+0], 16
.L104:
    jmp .L105
.L102:
    cmp r13d, 108
    je .L107
    cmp r13d, 122
    je .L107
    cmp r13d, 106
    je .L107
    cmp r13d, 116
    jne .L106
.L107:
    mov dword [rel $f_bits+0], 64
    jmp .L108
.L106:
    mov r12d, 0
.L108:
.L105:
    test r12d, r12d
    je .L109
    add rbx, 1
.L109:
.L100:
    test r12d, r12d
    jne .L99
.L101:
    movzx r10d, byte [rbx+0]
    mov r15d, r10d
    mov r10d, r15d
    cmp r10d, 0
    jne .L110
    jmp .L69
.L110:
    add rbx, 1
    cmp r15d, 37
    jne .L111
    mov rcx, qword [rbp+16]
    mov edx, 37
    call $write_put
    jmp .L112
.L111:
    cmp r15d, 99
    jne .L113
    mov r10, rsi
    add rsi, 8
    mov rdi, qword [r10+0]
    mov r10, rdi
    movzx r10d, r10b
    mov eax, r10d
    mov byte [rbp-134], al
    mov rcx, qword [rbp+16]
    lea rdx, [rbp-134]
    mov r8d, 1
    call $write_field
    jmp .L114
.L113:
    cmp r15d, 115
    jne .L115
    mov r10, rsi
    add rsi, 8
    mov rdi, qword [r10+0]
    mov rax, rdi
    mov qword [rbp-144], rax
    cmp qword [rbp-144], 0
    jne .L116
    lea rax, [rel __str4+0]
    mov qword [rbp-144], rax
.L116:
    mov rcx, qword [rbp-144]
    call $strlen
    mov r10, rax
    mov rdi, r10
    mov eax, edi
    mov dword [rbp-148], eax
    cmp dword [rel $f_prec+0], 0
    jl .L117
    mov ecx, dword [rbp-148]
    cmp dword [rel $f_prec+0], ecx
    jge .L117
    mov eax, dword [rel $f_prec+0]
    mov dword [rbp-148], eax
.L117:
    mov rcx, qword [rbp+16]
    mov rdx, qword [rbp-144]
    mov r8d, dword [rbp-148]
    call $write_field
    jmp .L118
.L115:
    cmp r15d, 100
    je .L120
    cmp r15d, 105
    jne .L119
.L120:
    mov r10, rsi
    add rsi, 8
    mov rdi, qword [r10+0]
    mov rcx, rdi
    call $narrow_signed
    mov r10, rax
    mov rax, r10
    mov qword [rbp-160], rax
    mov rax, qword [rbp-160]
    mov qword [rbp-168], rax
    mov byte [rbp-132], 0
    cmp qword [rbp-160], 0
    jge .L121
    mov r10, qword [rbp-160]
    neg r10
    mov rax, r10
    mov qword [rbp-168], rax
    mov byte [rbp-132], 45
    jmp .L122
.L121:
    mov r10d, dword [rel $f_flags+0]
    and r10d, 4
    test r10d, r10d
    je .L123
    mov byte [rbp-132], 43
    jmp .L124
.L123:
    mov r10d, dword [rel $f_flags+0]
    and r10d, 8
    test r10d, r10d
    je .L125
    mov byte [rbp-132], 32
.L125:
.L124:
.L122:
    mov byte [rbp-131], 0
    lea rcx, [rbp-128]
    mov rdx, qword [rbp-168]
    mov r8d, 10
    mov r9d, 0
    call $digits_of
    mov r10d, eax
    mov eax, r10d
    mov dword [rbp-172], eax
    mov r10d, dword [rbp-172]
    movsxd r10, r10d
    lea rdi, [rbp-128]
    add rdi, r10
    mov r10d, 72
    sub r10d, dword [rbp-172]
    mov rcx, qword [rbp+16]
    mov rdx, rdi
    mov r8d, r10d
    lea r9, [rbp-132]
    call $write_number
    jmp .L126
.L119:
    cmp r15d, 117
    je .L128
    cmp r15d, 120
    je .L128
    cmp r15d, 88
    je .L128
    cmp r15d, 111
    je .L128
    cmp r15d, 98
    je .L128
    cmp r15d, 112
    jne .L127
.L128:
    mov r10, rsi
    add rsi, 8
    mov rdi, qword [r10+0]
    mov rax, rdi
    mov qword [rbp-184], rax
    cmp r15d, 112
    je .L129
    mov rcx, qword [rbp-184]
    call $narrow_unsigned
    mov r10, rax
    mov rax, r10
    mov qword [rbp-184], rax
.L129:
    mov qword [rbp-192], 10
    mov dword [rbp-196], 0
    cmp r15d, 120
    je .L131
    cmp r15d, 112
    jne .L130
.L131:
    mov qword [rbp-192], 16
    jmp .L132
.L130:
    cmp r15d, 88
    jne .L133
    mov qword [rbp-192], 16
    mov dword [rbp-196], 1
    jmp .L134
.L133:
    cmp r15d, 111
    jne .L135
    mov qword [rbp-192], 8
    jmp .L136
.L135:
    cmp r15d, 98
    jne .L137
    mov qword [rbp-192], 2
.L137:
.L136:
.L134:
.L132:
    mov byte [rbp-132], 0
    mov byte [rbp-131], 0
    mov byte [rbp-130], 0
    cmp r15d, 112
    je .L139
    cmp r15d, 120
    je .L140
    cmp r15d, 88
    jne .L138
.L140:
    mov r10d, dword [rel $f_flags+0]
    and r10d, 16
    test r10d, r10d
    je .L138
    cmp qword [rbp-184], 0
    je .L138
.L139:
    mov byte [rbp-132], 48
    cmp dword [rbp-196], 0
    je .L141
    mov byte [rbp-131], 88
    jmp .L142
.L141:
    mov byte [rbp-131], 120
.L142:
.L138:
    lea rcx, [rbp-128]
    mov rdx, qword [rbp-184]
    mov r8, qword [rbp-192]
    mov r9d, dword [rbp-196]
    call $digits_of
    mov r10d, eax
    mov eax, r10d
    mov dword [rbp-200], eax
    mov r10d, dword [rbp-200]
    movsxd r10, r10d
    lea rdi, [rbp-128]
    add rdi, r10
    mov r10d, 72
    sub r10d, dword [rbp-200]
    mov rcx, qword [rbp+16]
    mov rdx, rdi
    mov r8d, r10d
    lea r9, [rbp-132]
    call $write_number
    jmp .L143
.L127:
    cmp r15d, 102
    je .L145
    cmp r15d, 70
    jne .L144
.L145:
    mov r10, rsi
    add rsi, 8
    movsd xmm4, qword [r10+0]
    mov rcx, qword [rbp+16]
    movaps xmm1, xmm4
    movq rdx, xmm1
    call $write_double
    jmp .L146
.L144:
    mov rcx, qword [rbp+16]
    mov edx, 37
    call $write_put
    mov rcx, qword [rbp+16]
    mov edx, r15d
    call $write_put
.L146:
.L143:
.L126:
.L118:
.L114:
.L112:
.L68:
    movzx r10d, byte [rbx+0]
    mov edi, r10d
    cmp edi, 0
    jne .L67
.L69:
    mov rcx, qword [rbp+16]
    call $write_flush
    mov eax, dword [rel $write_total+0]
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
$vfwrite:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov r10, rcx
    mov rbx, rdx
    mov rsi, r8
    mov dword [rel $mem_mode+0], 0
    mov rcx, r10
    mov rdx, rbx
    mov r8, rsi
    call $format_core
    mov r10d, eax
    mov eax, r10d
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    leave
    ret

align 16
$vwrite:
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
    call $vfwrite
    mov r10d, eax
    mov eax, r10d
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    leave
    ret

align 16
$fwrite:
    push rbp
    mov rbp, rsp
    mov [rbp+32], r8
    mov [rbp+40], r9
    sub rsp, 64
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov rbx, rcx
    mov rsi, rdx
    lea r10, [rbp+32]
    mov rdi, r10
    mov rcx, rbx
    mov rdx, rsi
    mov r8, rdi
    call $vwrite
    mov r10d, eax
    mov ebx, r10d
    mov edi, 0
    mov eax, ebx
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    leave
    ret

align 16
$fwritec:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov [rbp-8], rbx
    movzx r10d, cl
    mov rbx, rdx
    mov rcx, rbx
    lea rdx, [rel __str5+0]
    mov r8d, r10d
    call $fwrite
    mov r10d, eax
    mov eax, r10d
.Lret:
    mov rbx, [rbp-8]
    leave
    ret

align 16
$write:
    push rbp
    mov rbp, rsp
    mov [rbp+24], rdx
    mov [rbp+32], r8
    mov [rbp+40], r9
    sub rsp, 48
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov rbx, rcx
    lea r10, [rbp+24]
    mov rsi, r10
    mov ecx, 4294967285
    call $GetStdHandle
    mov r10, rax
    mov rcx, r10
    mov rdx, rbx
    mov r8, rsi
    call $vwrite
    mov r10d, eax
    mov ebx, r10d
    mov esi, 0
    mov eax, ebx
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    leave
    ret

align 16
$vsnwrite:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov rdi, rcx
    mov r10, rdx
    mov rbx, r8
    mov rsi, r9
    mov dword [rel $mem_mode+0], 1
    mov rax, rdi
    mov qword [rel $mem_dst+0], rax
    cmp r10, 2147483647
    jbe .L147
    mov dword [rel $mem_cap+0], 2147483647
    jmp .L148
.L147:
    mov rdi, r10
    mov eax, edi
    mov dword [rel $mem_cap+0], eax
.L148:
    mov ecx, 0
    mov rdx, rbx
    mov r8, rsi
    call $format_core
    mov r10d, eax
    mov ebx, r10d
    cmp dword [rel $mem_cap+0], 0
    jbe .L149
    mov r10d, ebx
    cmp r10d, dword [rel $mem_cap+0]
    jb .L150
    mov esi, dword [rel $mem_cap+0]
    sub esi, 1
    mov r10d, esi
.L150:
    mov esi, r10d
    movsxd rsi, esi
    mov r10, qword [rel $mem_dst+0]
    add r10, rsi
    mov byte [r10+0], 0
.L149:
    mov dword [rel $mem_mode+0], 0
    mov eax, ebx
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    leave
    ret

align 16
$snwrite:
    push rbp
    mov rbp, rsp
    mov [rbp+40], r9
    sub rsp, 64
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov rbx, rcx
    mov rsi, rdx
    mov rdi, r8
    lea r10, [rbp+40]
    mov r12, r10
    mov rcx, rbx
    mov rdx, rsi
    mov r8, rdi
    mov r9, r12
    call $vsnwrite
    mov r10d, eax
    mov ebx, r10d
    mov r12d, 0
    mov eax, ebx
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    leave
    ret

align 16
$open:
    push rbp
    mov rbp, rsp
    sub rsp, 96
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov rsi, rcx
    mov ebx, edx
    mov r10d, 0
    mov edi, 0
    mov r12d, 0
    cmp ebx, 0
    je .L152
    cmp ebx, 1
    jne .L151
.L152:
    mov ecx, 2147483648
    or r10, rcx
    or rdi, 1
    add r12d, 3
    jmp .L153
.L151:
    cmp ebx, 2
    je .L155
    cmp ebx, 3
    jne .L154
.L155:
    mov ecx, 2147483648
    or r10, rcx
    or r10, 1073741824
    or rdi, 1
    add r12d, 3
    jmp .L156
.L154:
    cmp ebx, 4
    je .L158
    cmp ebx, 5
    jne .L157
.L158:
    or r10, 1073741824
    add r12d, 2
    jmp .L159
.L157:
    cmp ebx, 7
    je .L161
    cmp ebx, 7
    jne .L160
.L161:
    or r10, 1073741824
    mov ecx, 2147483648
    or r10, rcx
    add r12d, 2
    jmp .L162
.L160:
    cmp ebx, 9
    je .L164
    cmp ebx, 8
    jne .L163
.L164:
    or r10, 1073741824
    or r10, 4
    add r12d, 4
    jmp .L165
.L163:
    cmp ebx, 11
    je .L167
    cmp ebx, 10
    jne .L166
.L167:
    or r10, 1073741824
    mov ecx, 2147483648
    or r10, rcx
    or r10, 4
    add r12d, 4
.L166:
.L165:
.L162:
.L159:
.L156:
.L153:
    mov qword [rsp+48], 0
    mov qword [rsp+40], 128
    mov qword [rsp+32], r12
    mov rcx, rsi
    mov rdx, r10
    mov r8, rdi
    mov r9d, 0
    call $CreateFileA
    mov r10, rax
    mov rbx, r10
    cmp rbx, -1
    jne .L168
    mov eax, 0
    jmp .Lret
.L168:
    mov rax, rbx
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    mov rdi, [rbp-24]
    mov r12, [rbp-32]
    leave
    ret

align 16
$seek:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov rbx, rcx
    mov rsi, rdx
    mov r10d, r8d
    mov qword [rbp-24], 0
    mov rcx, rbx
    mov rdx, rsi
    lea r8, [rbp-24]
    mov r9d, r10d
    call $SetFilePointerEx
    movzx eax, al
    mov r10d, eax
    mov ebx, r10d
    cmp ebx, 0
    jne .L169
    mov eax, -1
    jmp .Lret
.L169:
    mov eax, 0
.Lret:
    mov rbx, [rbp-8]
    mov rsi, [rbp-16]
    leave
    ret

align 16
$tell:
    push rbp
    mov rbp, rsp
    sub rsp, 48
    mov [rbp-8], rbx
    mov r10, rcx
    mov qword [rbp-16], 0
    mov rcx, r10
    mov edx, 0
    lea r8, [rbp-16]
    mov r9d, 1
    call $SetFilePointerEx
    movzx eax, al
    mov r10d, eax
    mov ebx, r10d
    cmp ebx, 0
    jne .L170
    mov rax, -1
    jmp .Lret
.L170:
    mov rax, qword [rbp-16]
.Lret:
    mov rbx, [rbp-8]
    leave
    ret

align 16
$read:
    push rbp
    mov rbp, rsp
    sub rsp, 112
    mov [rbp-8], rbx
    mov [rbp-16], rsi
    mov [rbp-24], rdi
    mov [rbp-32], r12
    mov [rbp-40], r13
    mov [rbp-48], r14
    mov [rbp-56], r15
    mov rdi, rcx
    mov qword [rbp+24], rdx
    mov r10, r8
    mov rbx, r9
    cmp qword [rbp+24], 0
    je .L172
    cmp r10, 0
    jne .L171
.L172:
    mov eax, 0
    jmp .Lret
.L171:
    mov r12, qword [rbp+24]
    imul r12, r10
    mov r13, r12
    mov r12d, 0
    mov r14, rdi
    jmp .L174
.L173:
    mov edi, 1073741824
    mov r10, r13
    sub r10, r12
    cmp r10, 1073741824
    jae .L176
    mov r10, r13
    sub r10, r12
    mov rdi, r10
.L176:
    mov qword [rbp-64], 0
    mov r10, r14
    add r10, r12
    mov rsi, rdi
    mov qword [rsp+32], 0
    mov rcx, rbx
    mov rdx, r10
    mov r8d, esi
    lea r9, [rbp-64]
    call $ReadFile
    movzx eax, al
    mov r10d, eax
    mov r15d, r10d
    mov r10d, r15d
    cmp r10d, 0
    je .L178
    cmp qword [rbp-64], 0
    jne .L177
.L178:
    jmp .L175
.L177:
    mov r10, r12
    add r10, qword [rbp-64]
    mov r12, r10
.L174:
    cmp r12, r13
    jb .L173
.L175:
    mov rax, r12
    xor edx, edx
    div qword [rbp+24]
    mov r10, rax
    mov rax, r10
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
$close:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov r10, rcx
    mov rcx, r10
    call $CloseHandle
    movzx eax, al
    mov r10d, eax
    mov eax, r10d
.Lret:
    leave
    ret

section .rdata align=16
align 16
__fsgn32: dd 0x80000000, 0, 0, 0
align 16
__fsgn64: dq 0x8000000000000000, 0
align 8
__flt0: dq 0x0
align 8
__flt1: dq 0x43df399b1438a100
align 8
__flt2: dq 0x3fe0000000000000
align 8
__flt3: dq 0x4024000000000000
__str0: db 110,97,110,0
__str1: db 45,105,110,102,0
__str2: db 105,110,102,0
__str3: db 40,111,118,101,114,102,108,111,119,41,0
__str4: db 40,110,117,108,108,41,0
__str5: db 37,99,0

section .bss align=16
alignb 8
$mem_dst: resb 8
alignb 4
$mem_cap: resb 4
alignb 4
$mem_mode: resb 4
alignb 1
$write_buf: resb 512
alignb 4
$write_len: resb 4
alignb 4
$write_total: resb 4
alignb 4
$f_flags: resb 4
alignb 4
$f_width: resb 4
alignb 4
$f_prec: resb 4
alignb 4
$f_bits: resb 4
