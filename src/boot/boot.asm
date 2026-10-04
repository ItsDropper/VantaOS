bits 32

section .multiboot
align 4
global multiboot_header

multiboot_header:
    dd 0x1BADB002
    dd 0
    dd -(0x1BADB002)

section .text.start
align 16
global _start
global stack_bottom
global stack_top
extern kernel_main

_start:
    cli
    mov esp, stack_top

    ; GRUB Multiboot gives the information structure in EBX.
    ; Keep it unchanged through the long-mode transition.
    mov eax, ebx

    ; Build a minimal identity map covering the first 1 GiB.
    mov eax, pml4_table
    mov dword [pml4_table], eax
    or dword [pml4_table], 0x003

    mov eax, pdpt_table
    mov dword [pml4_table], eax
    or dword [pml4_table], 0x003

    mov eax, page_directory
    mov dword [pdpt_table], eax
    or dword [pdpt_table], 0x003

    xor ecx, ecx
.map_pd:
    mov eax, ecx
    shl eax, 21
    or eax, 0x083
    mov [page_directory + ecx * 8], eax
    mov dword [page_directory + ecx * 8 + 4], 0
    inc ecx
    cmp ecx, 512
    jne .map_pd

    ; Enable PAE.
    mov eax, cr4
    or eax, 0x20
    mov cr4, eax

    ; CR3 points at the PML4.
    mov eax, pml4_table
    mov cr3, eax

    ; Enable IA32_EFER.LME.
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100
    wrmsr

    ; Enable paging. CPU enters IA-32e mode after this.
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    ; Preserve the Multiboot pointer in RBX after entering long mode.
    mov ebx, eax
    mov ebx, [esp]

    lgdt [gdt_pointer]

    jmp 0x08:long_mode_entry

bits 64
long_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor eax, eax
    mov fs, ax
    mov gs, ax

    mov rsp, stack_top
    and rsp, -16

    mov edi, ebx
    mov rdi, rbx
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

section .rodata
align 8

gdt:
    dq 0x0000000000000000
    dq 0x00209A0000000000
    dq 0x0000920000000000

gdt_pointer:
    dw gdt_end - gdt - 1
    dd gdt
gdt_end:

section .bss
align 4096
pml4_table:
    resb 4096
pdpt_table:
    resb 4096
page_directory:
    resb 4096

align 16
stack_bottom:
    resb 65536
stack_top:

section .note.GNU-stack noalloc noexec nowrite progbits
