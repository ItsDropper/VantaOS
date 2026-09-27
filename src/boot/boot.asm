bits 32

section .multiboot
align 4
    dd 0x1BADB002
    dd 0x00000000
    dd -(0x1BADB002 + 0x00000000)

section .bss
align 16
stack_bottom:
    resb 65536
stack_top:

section .text.start
align 16
global _start
global stack_bottom
global stack_top
extern kernel_main

_start:
    mov esp, stack_top
    push ebx
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
