bits 32

section .multiboot
align 4
global multiboot_header

multiboot_header:
    dd 0x1BADB002
    dd 0x00000000
    dd -(0x1BADB002 + 0x00000000)

section .text.start
align 16
global _start
global stack_bottom
global stack_top
extern kernel_main

_start:
    cli
    mov esp, stack_top
    push ebx
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 65536
stack_top:

section .note.GNU-stack noalloc noexec nowrite progbits
