%include "common.inc"
; Non-canonical (for both 48- and 57-bit linear addresses) raises #GP, not
; #PF; it must still be contained.
    mov rax, 0x0100000000000000
    mov al, [rax]
    EXIT 5
