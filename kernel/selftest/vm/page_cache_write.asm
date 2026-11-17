%include "common.inc"
; Populate the shared read-only page, then prove its file byte is not writable.
    mov rbx, DATA + 4
    cmp byte [rbx], 0x5A
    jne .fail
    mov byte [rbx], 0x77
    EXIT 0
.fail:
    EXIT 1
