; Copyright (c) 2026 ItsDropper
;
; This file is part of VantaOS and is licensed under the MIT License.
; See the LICENSE file in the project root for the full license text.

bits 64

%ifdef VANTAOS_DEBUG_PORT
%macro UEFI_DEBUG_CHAR 1
    mov dx, 0x00E9
    mov al, %1
    out dx, al
%endmacro
%else
%macro UEFI_DEBUG_CHAR 1
%endmacro
%endif

section .text.start
align 16
global uefi_kernel_entry
extern kernel_main_uefi

; The loader calls this custom kernel entry with the SysV x86-64 ABI.
; Build identity mappings for the first 4 GiB before entering kernel C code.
; Preserve the firmware's paging depth: OVMF may leave LA57 enabled.
uefi_kernel_entry:
    UEFI_DEBUG_CHAR 'A'
    cli
    mov r12, rdi

    cld
    lea rdi, [rel uefi_pml5]
    xor eax, eax
    mov ecx, 7 * 512
    rep stosq

    lea rax, [rel uefi_pdpt]
    or rax, 0x003
    mov [rel uefi_pml4], rax

    lea rax, [rel uefi_pd0]
    or rax, 0x003
    mov [rel uefi_pdpt], rax
    lea rax, [rel uefi_pd1]
    or rax, 0x003
    mov [rel uefi_pdpt + 8], rax
    lea rax, [rel uefi_pd2]
    or rax, 0x003
    mov [rel uefi_pdpt + 16], rax
    lea rax, [rel uefi_pd3]
    or rax, 0x003
    mov [rel uefi_pdpt + 24], rax

    lea rdi, [rel uefi_pd0]
    xor r8d, r8d
.map_2m_pages:
    mov rax, r8
    shl rax, 21
    or rax, 0x083
    mov [rdi], rax
    add rdi, 8
    inc r8
    cmp r8, 2048
    jb .map_2m_pages

    UEFI_DEBUG_CHAR 'B'

    ; Long mode requires PAE; explicitly enable PSE for the 2 MiB leaves.
    mov rax, cr4
    or rax, (1 << 4)
    mov cr4, rax

    ; If firmware uses five-level paging, make PML5[0] point at our PML4.
    ; Otherwise CR3 must point directly at the PML4.
    test rax, (1 << 12)
    jz .use_pml4
    lea rax, [rel uefi_pml4]
    or rax, 0x003
    mov [rel uefi_pml5], rax
    lea rax, [rel uefi_pml5]
    jmp .load_cr3
.use_pml4:
    lea rax, [rel uefi_pml4]
.load_cr3:
    mov cr3, rax
    UEFI_DEBUG_CHAR 'C'

    lea rsp, [rel uefi_stack_top]
    and rsp, -16
    mov rdi, r12
    UEFI_DEBUG_CHAR 'D'
    call kernel_main_uefi

.hang:
    cli
    hlt
    jmp .hang

section .bss
align 4096
uefi_pml5:
    resq 512
uefi_pml4:
    resq 512
uefi_pdpt:
    resq 512
uefi_pd0:
    resq 512
uefi_pd1:
    resq 512
uefi_pd2:
    resq 512
uefi_pd3:
    resq 512

align 16
uefi_stack_bottom:
    resb 65536
uefi_stack_top:

section .note.GNU-stack noalloc noexec nowrite progbits
