%include "common.inc"
; ELF program: file-backed byte present, bss tail lazily zero-filled.
    mov rbx, DATA
    cmp byte [rbx], 0x5A
    jne .fail
    mov rax, DATA + 15*4096 + 8
    cmp qword [rax], 0
    jne .fail
    mov qword [rax], 0x7777
    cmp qword [rax], 0x7777
    jne .fail
    EXIT 0
.fail:
    EXIT 1
