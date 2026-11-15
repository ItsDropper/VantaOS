%include "common.inc"
    mov rax, 0xFFFF800000000000  ; canonical upper half
    mov al, [rax]
    EXIT 5
