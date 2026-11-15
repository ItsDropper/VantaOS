; Shared by the ring-3 demand-paging self-test programs. Layout constants are
; supplied by scripts/gen_vm_selftest_programs.py (-D flags) so the assembly
; and the C harness cannot drift apart.
bits 64
default rel

%macro EXIT 1
    mov edi, %1
    xor eax, eax
    int 0x80
    jmp $
%endmacro
