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

; Save the complete general-purpose register set.
; The resulting memory layout matches struct exception_frame:
;
;   +000 r15
;   +008 r14
;   +016 r13
;   +024 r12
;   +032 r11
;   +040 r10
;   +048 r9
;   +056 r8
;   +064 rdi
;   +072 rsi
;   +080 rbp
;   +088 rdx
;   +096 rcx
;   +104 rbx
;   +112 rax
;
%macro PUSH_REGS 0
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro POP_REGS 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro

process_entry_trampoline:
    cld
    call process_entry_dispatch

.process_exit_halt:
    cli
    hlt
    jmp .process_exit_halt

irq0_stub:
    cld
    PUSH_REGS
    call timer_handle_interrupt
    mov edi, 0
    call pic_send_eoi

    mov rdi, rsp
    call scheduler_tick
    mov rsp, rax

    POP_REGS
    iretq

irq1_stub:
    cld
    PUSH_REGS
    call keyboard_handle_interrupt
    mov edi, 1
    call pic_send_eoi
    POP_REGS
    iretq

irq12_stub:
    cld
    PUSH_REGS
    call mouse_handle_interrupt
    mov edi, 12
    call pic_send_eoi
    POP_REGS
    iretq

%macro EXCEPTION_NO_ERROR 1
exception%1_stub:
    cld
    push qword 0
    PUSH_REGS
    mov rsi, rsp
    mov edi, %1
    call exception_handler
    POP_REGS
    add rsp, 8
    iretq
%endmacro

%macro EXCEPTION_ERROR 1
exception%1_stub:
    cld
    PUSH_REGS
    mov rsi, rsp
    mov edi, %1
    call exception_handler
    POP_REGS
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
