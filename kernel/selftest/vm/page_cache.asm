%include "common.inc"
; Read an immutable file-backed ELF byte for page-cache reuse tests.
    mov rbx, DATA
    cmp byte [rbx], 0x5A
    jne .fail
    EXIT 0
.fail:
    EXIT 1
