%include "common.inc"
; mmap 8 pages, touch each, then punch a hole (pages 2-4), repeat the same
; unmap (must fail), and trim both ends. Surviving pages keep their data.
; Expected: 8 demand faults, 5 frames released by munmap, 3 resident at exit.
    mov eax, 42                  ; mmap(length, prot=RW, flags=0)
    mov edi, 8 * 4096
    mov esi, 3
    xor edx, edx
    int 0x80
    test rax, rax
    jle .fail_map
    test eax, 0xFFF
    jnz .fail_map
    mov rbx, rax
    mov rcx, MMAPLOW
    cmp rbx, rcx
    jb .fail_map
    xor ecx, ecx
.touch:
    mov rdx, rcx
    shl rdx, 12
    lea rax, [rcx + 0x11]
    mov [rbx + rdx + 8], rax
    inc ecx
    cmp ecx, 8
    jb .touch

    mov eax, 43                  ; munmap pages 2..4
    lea rdi, [rbx + 2 * 4096]
    mov esi, 3 * 4096
    int 0x80
    test rax, rax
    jnz .fail_unmap
    mov eax, 43                  ; same range again: nothing mapped there
    lea rdi, [rbx + 2 * 4096]
    mov esi, 3 * 4096
    int 0x80
    cmp rax, -22
    jne .fail_unmap
    mov eax, 43                  ; misaligned start
    lea rdi, [rbx + 1]
    mov esi, 4096
    int 0x80
    cmp rax, -22
    jne .fail_unmap
    mov eax, 43                  ; trim the front page
    mov rdi, rbx
    mov esi, 4096
    int 0x80
    test rax, rax
    jnz .fail_unmap
    mov eax, 43                  ; trim the back page
    lea rdi, [rbx + 7 * 4096]
    mov esi, 4096
    int 0x80
    test rax, rax
    jnz .fail_unmap

    mov rax, [rbx + 1 * 4096 + 8]    ; survivors: pages 1, 5, 6
    cmp rax, 0x12
    jne .fail_data
    mov rax, [rbx + 5 * 4096 + 8]
    cmp rax, 0x16
    jne .fail_data
    mov rax, [rbx + 6 * 4096 + 8]
    cmp rax, 0x17
    jne .fail_data
    EXIT 0
.fail_map:
    EXIT 1
.fail_unmap:
    EXIT 2
.fail_data:
    EXIT 3
