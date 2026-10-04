bits 64

section .text

global irq0_stub
global irq1_stub
global irq12_stub
global process_entry_trampoline

global exception0_stub
global exception1_stub
global exception2_stub
global exception3_stub
global exception4_stub
global exception5_stub
global exception6_stub
global exception7_stub
global exception8_stub
global exception9_stub
global exception10_stub
global exception11_stub
global exception12_stub
global exception13_stub
global exception14_stub
global exception15_stub
global exception16_stub
global exception17_stub
global exception18_stub
global exception19_stub
global exception20_stub
global exception21_stub
global exception22_stub
global exception23_stub
global exception24_stub
global exception25_stub
global exception26_stub
global exception27_stub
global exception28_stub
global exception29_stub
global exception30_stub
global exception31_stub

extern exception_handler
extern process_entry_dispatch
extern timer_handle_interrupt
extern keyboard_handle_interrupt
extern mouse_handle_interrupt
extern pic_send_eoi
extern scheduler_tick

process_entry_trampoline:
    cld
    call process_entry_dispatch
.halt:
    cli
    hlt
    jmp .halt

%macro SAVE_ALL 0
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rsi
    push rdi
    push rbp
    push rdx
    push rcx
    push rbx
    push rax
%endmacro

%macro RESTORE_ALL 0
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rbp
    pop rdi
    pop rsi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
%endmacro

/*
 * Long-mode interrupt delivery aligns RSP to 16 bytes before building
 * the CPU return frame. SAVE_ALL pushes 15 qwords, leaving RSP misaligned
 * for a normal SysV C call. Keep one padding qword with the saved context
 * so every C call made from a hardware IRQ has the ABI-required alignment.
 *
 * The saved context layout becomes:
 *   +0   padding
 *   +8   rax
 *   ...
 *   +112 r15
 *   +120 RIP
 *   +128 CS
 *   +136 RFLAGS
 *   +144 RSP
 *   +152 SS
 */
%macro SAVE_IRQ_ALL 0
    SAVE_ALL
    push qword 0
%endmacro

%macro RESTORE_IRQ_ALL 0
    pop rax
    /* discard the IRQ alignment padding */
    RESTORE_ALL
%endmacro

irq0_stub:
    cld
    SAVE_IRQ_ALL
    call timer_handle_interrupt
    mov edi, 0
    call pic_send_eoi
    mov rdi, rsp
    call scheduler_tick
    mov rsp, rax
    pop rax
    RESTORE_ALL
    iretq

irq1_stub:
    cld
    SAVE_IRQ_ALL
    call keyboard_handle_interrupt
    mov edi, 1
    call pic_send_eoi
    RESTORE_IRQ_ALL
    iretq

irq12_stub:
    cld
    SAVE_IRQ_ALL
    call mouse_handle_interrupt
    mov edi, 12
    call pic_send_eoi
    RESTORE_IRQ_ALL
    iretq

%macro EXCEPTION_NO_ERROR 1
exception%1_stub:
    cld
    push qword 0
    SAVE_ALL
    mov rsi, rsp
    mov edi, %1
    call exception_handler
    RESTORE_ALL
    add rsp, 8
    iretq
%endmacro

%macro EXCEPTION_ERROR 1
exception%1_stub:
    cld
    SAVE_ALL
    mov rsi, rsp
    mov edi, %1
    call exception_handler
    RESTORE_ALL
    add rsp, 8
    iretq
%endmacro

EXCEPTION_NO_ERROR 0
EXCEPTION_NO_ERROR 1
EXCEPTION_NO_ERROR 2
EXCEPTION_NO_ERROR 3
EXCEPTION_NO_ERROR 4
EXCEPTION_NO_ERROR 5
EXCEPTION_NO_ERROR 6
EXCEPTION_NO_ERROR 7
EXCEPTION_ERROR 8
EXCEPTION_NO_ERROR 9
EXCEPTION_ERROR 10
EXCEPTION_ERROR 11
EXCEPTION_ERROR 12
EXCEPTION_ERROR 13
EXCEPTION_ERROR 14
EXCEPTION_NO_ERROR 15
EXCEPTION_NO_ERROR 16
EXCEPTION_ERROR 17
EXCEPTION_NO_ERROR 18
EXCEPTION_NO_ERROR 19
EXCEPTION_NO_ERROR 20
EXCEPTION_NO_ERROR 21
EXCEPTION_NO_ERROR 22
EXCEPTION_NO_ERROR 23
EXCEPTION_NO_ERROR 24
EXCEPTION_NO_ERROR 25
EXCEPTION_NO_ERROR 26
EXCEPTION_NO_ERROR 27
EXCEPTION_NO_ERROR 28
EXCEPTION_NO_ERROR 29
EXCEPTION_NO_ERROR 30
EXCEPTION_NO_ERROR 31

section .note.GNU-stack noalloc noexec nowrite progbits
