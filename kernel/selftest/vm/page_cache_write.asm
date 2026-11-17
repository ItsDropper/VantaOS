%include "common.inc"
; A shared read-only file page must never be writable.
    mov rbx, DATA
    mov byte [rbx], 0x77
    EXIT 0
