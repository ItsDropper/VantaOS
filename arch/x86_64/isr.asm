bits 64

section .text

global irq0_stub
global irq1_stub
global irq12_stub
global apic_spurious_stub
global smp_wakeup_stub
global lapic_timer_stub
global reschedule_ipi_stub
global tlb_shootdown_ipi_stub
global process_entry_trampoline
global user_mode_test_enter
global user_mode_test_return_trampoline
global user_mode_test_syscall_stub

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
extern scheduler_finish_context_switch
extern apic_send_eoi
extern apic_is_enabled
extern lapic_timer_handle_interrupt
extern reschedule_ipi_handle_interrupt
extern paging_tlb_shootdown_handle_interrupt
extern user_mode_selftest_syscall_dispatch
extern user_mode_test_saved_rsp
extern user_mode_test_saved_rflags

apic_spurious_stub:
    iretq

process_entry_trampoline:
    cld
    call process_entry_dispatch
.halt:
    hlt
    jmp .halt

; Test-only ring-3 entry. The caller's stack and flags are restored by the
; return trampoline after the test syscall or expected user-mode page fault.
user_mode_test_enter:
    mov [rel user_mode_test_saved_rsp], rsp
    pushfq
    pop rax
    mov [rel user_mode_test_saved_rflags], rax
    mov rax, rdi
    mov rdx, rsi
    push qword 0x1B
    push rdx
    push qword 0x202
    push qword 0x23
    push rax
    iretq

user_mode_test_return_trampoline:
    mov rsp, [rel user_mode_test_saved_rsp]
    mov rax, [rel user_mode_test_saved_rflags]
    push rax
    popfq
    ret

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

; int 0x80 enters only through a DPL-3 gate in self-test builds.
user_mode_test_syscall_stub:
    cld
    push qword 0
    SAVE_ALL
    mov r12, rsp
    and rsp, -16
    mov rdi, r12
    call user_mode_selftest_syscall_dispatch
    mov rsp, r12
    RESTORE_ALL
    add rsp, 8
    iretq

; Long-mode interrupt delivery aligns RSP to 16 bytes before building
; the CPU return frame. SAVE_ALL pushes 15 qwords, leaving RSP misaligned
; for a normal SysV C call. Keep one padding qword with the saved context
; so every C call made from a hardware IRQ has the ABI-required alignment.
;
; IRQ saved context layout (before the CPU return frame):
;   +0   padding
;   +8   r15
;   +16  r14
;   ...
;   +120 rax
;   +128 RIP
;   +136 CS
;   +144 RFLAGS
;   +152 RSP (only on privilege transition)
;   +160 SS  (only on privilege transition)
%macro SAVE_IRQ_ALL 0
    SAVE_ALL
    push qword 0
%endmacro

%macro RESTORE_IRQ_ALL 0
    pop rax
    ; Discard the IRQ alignment padding
    RESTORE_ALL
%endmacro

; Fixed IPI used to wake initialized APs from their idle HLT loop.
lapic_timer_stub:
    cld
    SAVE_IRQ_ALL
    mov rdi, rsp
    call lapic_timer_handle_interrupt
    mov rsp, rax
    call scheduler_finish_context_switch
    pop rax
    RESTORE_ALL
    iretq

reschedule_ipi_stub:
    cld
    SAVE_IRQ_ALL
    mov rdi, rsp
    call reschedule_ipi_handle_interrupt
    mov rsp, rax
    call scheduler_finish_context_switch
    pop rax
    RESTORE_ALL
    iretq

tlb_shootdown_ipi_stub:
    cld
    SAVE_IRQ_ALL
    call paging_tlb_shootdown_handle_interrupt
    RESTORE_IRQ_ALL
    iretq

smp_wakeup_stub:
    cld
    SAVE_IRQ_ALL
    call apic_send_eoi
    RESTORE_IRQ_ALL
    iretq

irq0_stub:
    cld
    SAVE_IRQ_ALL
    call timer_handle_interrupt
    call apic_is_enabled
    test eax, eax
    jz .irq0_pic_eoi
    call apic_send_eoi
    jmp .irq0_eoi_done
.irq0_pic_eoi:
    mov edi, 0
    call pic_send_eoi
.irq0_eoi_done:
    mov rdi, rsp
    call scheduler_tick
    mov rsp, rax
    call scheduler_finish_context_switch
    pop rax
    RESTORE_ALL
    iretq

irq1_stub:
    cld
    SAVE_IRQ_ALL
    call keyboard_handle_interrupt
    call apic_is_enabled
    test eax, eax
    jz .irq1_pic_eoi
    call apic_send_eoi
    jmp .irq1_eoi_done
.irq1_pic_eoi:
    mov edi, 1
    call pic_send_eoi
.irq1_eoi_done:
    RESTORE_IRQ_ALL
    iretq

irq12_stub:
    cld
    SAVE_IRQ_ALL
    call mouse_handle_interrupt
    call apic_is_enabled
    test eax, eax
    jz .irq12_pic_eoi
    call apic_send_eoi
    jmp .irq12_eoi_done
.irq12_pic_eoi:
    mov edi, 12
    call pic_send_eoi
.irq12_eoi_done:
    RESTORE_IRQ_ALL
    iretq

%macro EXCEPTION_NO_ERROR 1
exception%1_stub:
    cld
    push qword 0
    SAVE_ALL
    mov r12, rsp
    and rsp, -16
    mov rsi, r12
    mov edi, %1
    call exception_handler
    mov rsp, r12
    RESTORE_ALL
    add rsp, 8
    iretq
%endmacro

%macro EXCEPTION_ERROR 1
exception%1_stub:
    cld
    SAVE_ALL
    mov r12, rsp
    and rsp, -16
    mov rsi, r12
    mov edi, %1
    call exception_handler
    mov rsp, r12
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

section .rodata
align 16
global ap_trampoline_blob_start
global ap_trampoline_blob_end
ap_trampoline_blob_start:
    incbin "build/obj/arch/x86_64/ap_trampoline.bin"
ap_trampoline_blob_end:

section .note.GNU-stack noalloc noexec nowrite progbits
