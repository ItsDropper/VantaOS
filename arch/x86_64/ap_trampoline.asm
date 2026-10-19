; Copyright (c) 2026 ItsDropper
;
; This file is part of VantaOS and is licensed under the MIT License.
; See the LICENSE file in the project root for the full license text.

; Position-dependent AP startup trampoline copied to physical 0x8000.
; Mailbox at physical 0x7000: CR3 (qword), entry (qword), stack top (qword).
bits 16
org 0x8000

ap_trampoline_start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0xFFF0

    lgdt [ap_gdt_pointer]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:ap_protected_entry

bits 32
ap_protected_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    mov eax, cr4
    or eax, (1 << 5)
    mov cr4, eax

    mov eax, [0x7000]
    mov cr3, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8)
    wrmsr

    ; Enable EFER.NXE on APs before they can enter an address space with
    ; non-executable user pages. Older CPUs without NX keep the fallback.
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .nx_done
    mov eax, 0x80000001
    cpuid
    bt edx, 20
    jnc .nx_done
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 11)
    wrmsr
.nx_done:
    mov eax, cr0
    or eax, (1 << 31)
    mov cr0, eax
    jmp 0x18:ap_long_entry

bits 64
ap_long_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    mov rsp, [abs 0x7010]
    and rsp, -16
    xor rbp, rbp
    push qword 0

    mov eax, 1
    cpuid
    shr ebx, 24
    mov edi, ebx
    mov rax, [abs 0x7008]
    jmp rax

align 8
ap_gdt:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
    dq 0x00AF9A000000FFFF
ap_gdt_end:

ap_gdt_pointer:
    dw ap_gdt_end - ap_gdt - 1
    dd ap_gdt

ap_trampoline_end:
