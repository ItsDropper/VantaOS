%include "common.inc"
; Verify both sides of an unaligned read-only file extent are zero-filled.
    mov rbx, DATA
    cmp byte [rbx], 0
    jne .fail
    cmp byte [rbx+4], 0x5A
    jne .fail
    cmp byte [rbx+20], 0
    jne .fail
    EXIT 0
.fail:
    EXIT 1
