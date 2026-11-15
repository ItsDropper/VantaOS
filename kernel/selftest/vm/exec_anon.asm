%include "common.inc"
; Instruction fetch from demand-zero memory must be refused, never mapped.
    mov rax, ANON
    jmp rax
