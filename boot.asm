bits 32

section .multiboot
align 4
    ; Do not request a Multiboot v1 graphics mode here.
    ; GRUB selects the graphical mode through gfxmode/gfxpayload
    ; and supplies the resulting framebuffer in multiboot_info.
    dd 0x1BADB002
    dd 0x00000000
    dd -(0x1BADB002 + 0x00000000)

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
