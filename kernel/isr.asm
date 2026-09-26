bits 32

section .text

global irq0_stub
global irq1_stub

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


irq0_stub:
    pusha

    push dword 32
    call interrupt_handler
    add esp, 4

    popa
    iretd


irq1_stub:
    pusha

    push dword 33
    call interrupt_handler
    add esp, 4

    popa
    iretd


%macro EXCEPTION_NO_ERROR 1
exception%1_stub:
    pusha

    ; Add a fake error code so every exception
    ; has the same stack layout.
    push dword 0

    ; Pass exception number and pointer to saved frame.
    push esp
    push dword %1
    call exception_handler
    add esp, 8

    ; Remove fake error code.
    add esp, 4

    popa
    iretd
%endmacro


%macro EXCEPTION_ERROR 1
exception%1_stub:
    pusha

    ; CPU already pushed the real error code.

    ; Pass exception number and pointer to saved frame.
    push esp
    push dword %1
    call exception_handler
    add esp, 8

    ; Remove CPU-provided error code.
    add esp, 4

    popa
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
EXCEPTION_ERROR    17
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