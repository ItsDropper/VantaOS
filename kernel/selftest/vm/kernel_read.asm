%include "common.inc"
    mov rax, 0x200000            ; kernel identity-mapped memory
    mov al, [rax]
    EXIT 5
