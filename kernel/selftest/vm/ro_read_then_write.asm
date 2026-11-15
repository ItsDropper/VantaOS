%include "common.inc"
; Reading faults in a read-only zero page; the following write then hits a
; present read-only PTE and must be treated as a protection violation.
    mov rax, RO
    cmp byte [rax], 0
    jne .bad
    mov byte [rax], 1
.bad:
    EXIT 5
