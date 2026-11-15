%include "common.inc"
; First touch of a read-only anonymous page is a write: access denied.
    mov rax, RO
    mov byte [rax], 1
    EXIT 5
