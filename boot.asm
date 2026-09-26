bits 32

section .multiboot
align 4
    dd 0x1BADB002
    dd 0x00000000
    dd -(0x1BADB002 + 0x00000000)

section .text
global _start
extern kernel_main

_start:
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang