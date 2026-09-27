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

    ; Keep the kernel entry ABI 16-byte aligned.
    ; stack_top is 16-byte aligned, so reserve 8 bytes before
    ; pushing the single kernel_main argument. CALL then leaves
    ; ESP 16-byte aligned at kernel_main entry.
    sub esp, 8
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
