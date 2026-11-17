%include "common.inc"
; Exercise the real ring-3 mmap/munmap/brk syscalls against live page tables.
; This runs in the QEMU GRUB matrix and OVMF boot, not against host stubs.

    ; Four-page anonymous mapping. Every page must be zero on first touch.
    mov eax, 42
    mov edi, 16384
    mov esi, 3
    xor edx, edx
    int 0x80
    test rax, rax
    js .fail
    mov r12, rax

    xor ecx, ecx
.map_check:
    cmp ecx, 4
    jae .unmap_middle
    mov rbx, rcx
    shl rbx, 12
    add rbx, r12
    cmp qword [rbx], 0
    jne .fail
    mov eax, ecx
    add eax, 0x1357
    mov [rbx], rax
    cmp [rbx], rax
    jne .fail
    inc ecx
    jmp .map_check

    ; Middle punch forces a real PTE clear, TLB shootdown and frame release.
.unmap_middle:
    mov eax, 43
    lea rdi, [r12 + 4096]
    mov esi, 4096
    xor edx, edx
    int 0x80
    test rax, rax
    jnz .fail

    ; Remove the head, then the remaining tail of the split mapping.
    mov eax, 43
    mov rdi, r12
    mov esi, 4096
    xor edx, edx
    int 0x80
    test rax, rax
    jnz .fail

    mov eax, 43
    lea rdi, [r12 + 8192]
    mov esi, 8192
    xor edx, edx
    int 0x80
    test rax, rax
    jnz .fail

    ; Reuse an address range and prove fresh anonymous pages are zeroed.
    mov eax, 42
    mov edi, 4096
    mov esi, 3
    xor edx, edx
    int 0x80
    test rax, rax
    js .fail
    mov r13, rax
    cmp qword [r13], 0
    jne .fail
    mov eax, 0x5AA55AA5
    mov [r13], rax
    cmp [r13], rax
    jne .fail
    mov eax, 43
    mov rdi, r13
    mov esi, 4096
    xor edx, edx
    int 0x80
    test rax, rax
    jnz .fail

    ; Grow brk by two pages, touch both lazily, shrink and regrow.
    mov eax, 44
    xor edi, edi
    xor esi, esi
    int 0x80
    test rax, rax
    js .fail
    mov r14, rax
    lea r15, [r14 + 8192]
    mov eax, 44
    mov rdi, r15
    xor esi, esi
    int 0x80
    cmp rax, r15
    jne .fail

    cmp qword [r14], 0
    jne .fail
    cmp qword [r14 + 4096], 0
    jne .fail
    mov qword [r14], 0x11223344
    mov qword [r14 + 4096], 0x55667788

    mov eax, 44
    mov rdi, r14
    xor esi, esi
    int 0x80
    cmp rax, r14
    jne .fail

    mov eax, 44
    mov rdi, r15
    xor esi, esi
    int 0x80
    cmp rax, r15
    jne .fail
    cmp qword [r14], 0
    jne .fail
    cmp qword [r14 + 4096], 0
    jne .fail

    mov eax, 44
    mov rdi, r14
    xor esi, esi
    int 0x80
    cmp rax, r14
    jne .fail
    EXIT 0
.fail:
    EXIT 1
