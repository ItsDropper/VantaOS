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

extern interrupt_handler
extern exception_handler
extern process_entry_dispatch

process_entry_trampoline:
    cld
    call process_entry_dispatch

.process_exit_halt:
    hlt
    jmp .process_exit_halt

%macro IRQ_STUB 2
%1:
    cld
    pusha

    ; * Keep the CPU-created return frame at a fixed location. The IRQ
    ; * handler may inspect it, but the normal IRQ path must never replace
    ; * ESP with an arbitrary C return value. Only a real scheduler switch
    ; * is allowed to provide a different frame.
    ;
    mov eax, esp
    push eax
    push dword %2
    call interrupt_handler
    add esp, 8

    ; * For ordinary IRQ handling, return through the frame that was
    ; * created by this interrupt. This is especially important for the
    ; * boot context: its stack is not a scheduler-owned task frame.
    ;
    popa
    iretd
%endmacro

IRQ_STUB irq0_stub, 32
IRQ_STUB irq1_stub, 33
IRQ_STUB irq12_stub, 44

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
EXCEPTION_ERROR    8
EXCEPTION_NO_ERROR 9
EXCEPTION_ERROR    10
EXCEPTION_ERROR    11
EXCEPTION_ERROR    12
EXCEPTION_ERROR    13
EXCEPTION_ERROR    14
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
