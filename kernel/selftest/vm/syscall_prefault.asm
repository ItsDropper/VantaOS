%include "common.inc"
; A syscall buffer inside an untouched demand-zero page must be made resident
; by the kernel before validation instead of failing with -EFAULT.
    mov eax, 2                   ; console write
    mov rdi, SYSBUF
    mov esi, 1
    int 0x80
    cmp rax, -14
    je .fail
    mov rbx, SYSBUF
    cmp byte [rbx], 0            ; page is now resident and zero
    jne .fail
    EXIT 0
.fail:
    EXIT 3
