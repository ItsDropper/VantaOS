bits 32

section .multiboot
align 4
    ; Multiboot v1 graphics request.
    ; The five address fields occupy offsets 12-28 even when
    ; flag 16 is not set; graphics fields begin at offset 32.
    dd 0x1BADB002
    dd 0x00000004
    dd -(0x1BADB002 + 0x00000004)

    ; Multiboot address fields (unused for ELF, but reserved
    ; in the fixed header layout).
    dd 0
    dd 0
    dd 0
    dd 0
    dd 0

    ; Request 1024x768x32 linear graphics.
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
