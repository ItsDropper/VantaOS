; Copyright (c) 2026 ItsDropper
;
; This file is part of VantaOS and is licensed under the MIT License.
; See the LICENSE file in the project root for the full license text.

bits 64

section .text.start
align 16
global uefi_kernel_entry
extern kernel_main_uefi

; The loader calls this custom kernel entry with the SysV x86-64 ABI.
; Establish an identity map for the first 4 GiB before entering kernel C code.
uefi_kernel_entry:
    cli
    mov r12, rdi

    cld
    lea rdi, [rel uefi_pml4]
    xor eax, eax
    mov ecx, 6 * 512
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

    lea rax, [rel uefi_pml4]
    mov cr3, rax

    lea rsp, [rel uefi_stack_top]
    and rsp, -16
    mov rdi, r12
    call kernel_main_uefi

.hang:
    cli
    hlt
    jmp .hang

section .bss
align 4096
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
