%include "common.inc"
; ELF program: grow the stack by `count` pages (patched, offset 1).
    mov ecx, 0
.loop:
    sub rsp, 4096
    mov byte [rsp], 1
    dec ecx
    jnz .loop
    EXIT 0
