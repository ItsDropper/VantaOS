bits 32

section .multiboot
align 4
    ; Request a linear framebuffer from GRUB.
    ; mode_type = 0, width = 1024, height = 768, depth = 32.
    dd 0x1BADB002
    dd 0x00000004
    dd -(0x1BADB002 + 0x00000004)
    dd 0
    dd 1024
    dd 768
    dd 32

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

section .text
global _start
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
