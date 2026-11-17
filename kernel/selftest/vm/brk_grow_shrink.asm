%include "common.inc"
; brk: query, grow 3 pages, touch, shrink to 1, regrow (fresh page reads zero),
; and an oversized request must leave the break unchanged.
; Expected: 4 demand faults, 2 released by shrink, 2 resident at exit.
    mov eax, 44
    xor edi, edi                 ; brk(0) reports the current break
    int 0x80
    test rax, rax
    jle .fail_brk
    test eax, 0xFFF
    jnz .fail_brk
    mov rbx, rax
    mov eax, 44
    lea rdi, [rbx + 3 * 4096]
    int 0x80
    lea rcx, [rbx + 3 * 4096]
    cmp rax, rcx
    jne .fail_brk
    mov qword [rbx + 8], 0x71
    mov qword [rbx + 4096 + 8], 0x72
    mov qword [rbx + 2 * 4096 + 8], 0x73
    mov eax, 44                  ; shrink to one page
    lea rdi, [rbx + 4096]
    int 0x80
    lea rcx, [rbx + 4096]
    cmp rax, rcx
    jne .fail_brk
    cmp qword [rbx + 8], 0x71
    jne .fail_data
    mov eax, 44                  ; regrow over the released pages
    lea rdi, [rbx + 3 * 4096]
    int 0x80
    lea rcx, [rbx + 3 * 4096]
    cmp rax, rcx
    jne .fail_brk
    cmp qword [rbx + 4096 + 8], 0   ; must be a fresh zero page
    jne .fail_data
    mov eax, 44                  ; far beyond the heap limit: refused
    mov rdi, rbx
    add rdi, 0x10000000
    int 0x80
    lea rcx, [rbx + 3 * 4096]
    cmp rax, rcx
    jne .fail_brk
    EXIT 0
.fail_brk:
    EXIT 1
.fail_data:
    EXIT 2
