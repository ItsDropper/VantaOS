%include "common.inc"
; Address inside the user window but not reserved by any region.
    mov rax, GAP
    mov al, [rax]
    EXIT 5
