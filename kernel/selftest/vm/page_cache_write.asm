%include "common.inc"
; Populate the shared read-only page, then prove a write is still denied.
    mov rbx, DATA
    cmp byte [rbx], 0x5A
    jne .fail
    mov byte [rbx], 0x77
    EXIT 0
.fail:
    EXIT 1
