%include "common.inc"
; Touching a page after munmap must be a contained, unmapped-address fault.
    mov eax, 42
    mov edi, 4096
    mov esi, 3
    xor edx, edx
    int 0x80
    test rax, rax
    jle .fail
    mov rbx, rax
    mov qword [rbx], 0x5A5A      ; one demand fault
    mov eax, 43
    mov rdi, rbx
    mov esi, 4096
    int 0x80
    test rax, rax
    jnz .fail
    mov rax, [rbx]               ; must fault: no region
    EXIT 4                       ; reached only if the unmap did not take
.fail:
    EXIT 1
