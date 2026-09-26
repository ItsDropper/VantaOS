bits 32

section .multiboot
align 4
    dd 0x1BADB002                   ; Multiboot magic number
    dd 0x00000000                   ; Flags
    dd -(0x1BADB002 + 0x00000000)   ; Checksum

section .bss
align 16
stack_bottom:
    resb 16384                      ; Reserve 16 KiB for the kernel stack
stack_top:

section .text
global _start
extern kernel_main

_start:
    ; Set up 16-byte stack alignment per System V ABI
    mov esp, stack_top

    ; Multiboot places a pointer to the multiboot_info_t structure in EBX
    push ebx                        ; Pass Multiboot info structure pointer as first argument to kernel_main

    call kernel_main

.hang:
    cli                             ; Disable interrupts
    hlt                             ; Halt CPU until next interrupt
    jmp .hang                       ; Infinite loop fallback

; Mark stack non-executable to prevent linker warnings
section .note.GNU-stack noalloc noexec nowrite progbits