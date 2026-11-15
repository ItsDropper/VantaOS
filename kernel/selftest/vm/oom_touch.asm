%include "common.inc"
; The harness arms an allocation failure for this window.
    mov rax, OOMWIN
    mov byte [rax], 1
    EXIT 5
