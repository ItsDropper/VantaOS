bits 32

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

.process_exit_halt:
    hlt
    jmp .process_exit_halt

irq0_stub:
    cld
    pusha
    call timer_handle_interrupt

    /* Acknowledge the PIT before scheduling. */
    push dword 0
    call pic_send_eoi
    add esp, 4

    /*
     * scheduler_tick() receives the address of the complete PUSHA
     * frame and returns the frame that should be restored.
     */
    push esp
    call scheduler_tick
    add esp, 4
    mov esp, eax

    popa
    iretd

irq1_stub:
    cld
    pusha
    call keyboard_handle_interrupt
    push dword 1
    call pic_send_eoi
    add esp, 4
    popa
    iretd

irq12_stub:
    cld
    pusha
    call mouse_handle_interrupt
    push dword 12
    call pic_send_eoi
    add esp, 4
    popa
    iretd

%macro EXCEPTION_NO_ERROR 1
exception%1_stub:
    cld
    push dword 0
    pusha
    mov eax, esp
    push eax
    push dword %1
    call exception_handler
    add esp, 8
    popa
    add esp, 4
    iretd
%endmacro

%macro EXCEPTION_ERROR 1
exception%1_stub:
    cld
    pusha
    mov eax, esp
    push eax
    push dword %1
    call exception_handler
    add esp, 8
    popa
    add esp, 4
    iretd
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
EXCEPTION_NO_ERROR 17
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
