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

    ; Build the IA-32e page hierarchy while still in protected mode.
    xor eax, eax
    mov ecx, 512
    mov edi, pml4_table
    rep stosq

    mov eax, pdpt_table
    or eax, 0x003
    mov [pml4_table], eax

    mov eax, pd_table
    or eax, 0x003
    mov [pdpt_table], eax

    xor ecx, ecx
.fill_pd:
    mov eax, ecx
    shl eax, 21
    or eax, 0x083
    mov [pd_table + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jb .fill_pd

    ; Enable PAE, point CR3 at the PML4, enable EFER.LME, then paging.
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov eax, pml4_table
    mov cr3, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    lgdt [bootstrap_gdt_pointer]
    jmp 0x08:long_mode_entry

bits 64
long_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    mov rsp, stack_top

    ; Multiboot passes the info structure in EBX. SysV x86-64 uses RDI.
    mov edi, ebx
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

section .rodata
align 8
bootstrap_gdt:
    dq 0x0000000000000000
    dq 0x00AF9A000000FFFF
    dq 0x00CF92000000FFFF

bootstrap_gdt_pointer:
    dw bootstrap_gdt_end - bootstrap_gdt - 1
    dq bootstrap_gdt
bootstrap_gdt_end:

section .bss
align 4096
pml4_table:
    resq 512
pdpt_table:
    resq 512
pd_table:
    resq 512

align 16
stack_bottom:
    resb 65536
stack_top:

section .note.GNU-stack noalloc noexec nowrite progbits
