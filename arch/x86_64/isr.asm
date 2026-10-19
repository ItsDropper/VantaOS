; Copyright (c) 2026 ItsDropper
;
; This file is part of VantaOS and is licensed under the MIT License.
; See the LICENSE file in the project root for the full license text.

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
global user_mode_syscall_stub

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
extern user_mode_syscall_dispatch
extern scheduler_reschedule
extern user_mode_test_saved_rsp
extern user_mode_test_saved_rflags
extern user_mode_test_exit_requested

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
    cli
    push qword 0x1B
    push rdx
    push qword 0x202
    push qword 0x23
    push rax
    swapgs
    iretq

user_mode_test_return_trampoline:
    mov rsp, [rel user_mode_test_saved_rsp]
    mov rax, [rel user_mode_test_saved_rflags]
    push rax
    popfq
    ret

%macro SWAPGS_IF_USER 2
    test byte [%1 + %2], 3
    jz %%done
    swapgs
%%done:
%endmacro

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

; Convert a ring-3 exception/syscall frame into the scheduler's IRQ-frame
; layout, then switch away. The saved GPRs stay intact; the fake error-code
; slot is replaced by RIP and a padding word is inserted below the frame.
%macro SCHEDULE_USER_FRAME 0
    mov rsp, r12
    mov rax, [rsp + 128]
    mov rbx, [rsp + 136]
    mov rcx, [rsp + 144]
    mov rdx, [rsp + 152]
    mov rbp, [rsp + 160]
    mov [rsp + 120], rax
    mov [rsp + 128], rbx
    mov [rsp + 136], rcx
    mov [rsp + 144], rdx
    mov [rsp + 152], rbp
    sub rsp, 8
    mov qword [rsp], 0
    mov rdi, rsp
    and rsp, -16
    call scheduler_reschedule
    mov rsp, rax
    call scheduler_finish_context_switch
    SWAPGS_IF_USER rsp, 136
    pop rax
    RESTORE_ALL
    iretq
%endmacro

; int 0x80 is the minimal ring-3 syscall ABI in every build.
user_mode_syscall_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 0
    SAVE_ALL
    mov r12, rsp
    and rsp, -16
    mov rdi, r12
    call user_mode_syscall_dispatch
    cmp dword [rel user_mode_test_exit_requested], 0
    jne .user_test_exit
    test eax, eax
    jnz .schedule_user
    SWAPGS_IF_USER r12, 136
    mov rsp, r12
    RESTORE_ALL
    add rsp, 8
    iretq
.schedule_user:
    SCHEDULE_USER_FRAME
.user_test_exit:
    mov dword [rel user_mode_test_exit_requested], 0
    jmp user_mode_test_return_trampoline

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
    SWAPGS_IF_USER rsp, 8
    cld
    SAVE_IRQ_ALL
    mov rdi, rsp
    call lapic_timer_handle_interrupt
    mov rsp, rax
    call scheduler_finish_context_switch
    SWAPGS_IF_USER rsp, 136
    pop rax
    RESTORE_ALL
    iretq

reschedule_ipi_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    SAVE_IRQ_ALL
    mov rdi, rsp
    call reschedule_ipi_handle_interrupt
    mov rsp, rax
    call scheduler_finish_context_switch
    SWAPGS_IF_USER rsp, 136
    pop rax
    RESTORE_ALL
    iretq

tlb_shootdown_ipi_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    SAVE_IRQ_ALL
    call paging_tlb_shootdown_handle_interrupt
    SWAPGS_IF_USER rsp, 136
    RESTORE_IRQ_ALL
    iretq

smp_wakeup_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    SAVE_IRQ_ALL
    call apic_send_eoi
    SWAPGS_IF_USER rsp, 136
    RESTORE_IRQ_ALL
    iretq

irq0_stub:
    SWAPGS_IF_USER rsp, 8
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
    SWAPGS_IF_USER rsp, 136
    pop rax
    RESTORE_ALL
    iretq

irq1_stub:
    SWAPGS_IF_USER rsp, 8
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
    SWAPGS_IF_USER rsp, 136
    RESTORE_IRQ_ALL
    iretq

irq12_stub:
    SWAPGS_IF_USER rsp, 8
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
    SWAPGS_IF_USER rsp, 136
    RESTORE_IRQ_ALL
    iretq


; Generic APIC/MSI device vectors. The device must be quiesced before
; unregistering its handler; each entry supplies its own vector number.
device_irq_64_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 64
    jmp device_irq_common_stub

device_irq_65_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 65
    jmp device_irq_common_stub

device_irq_66_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 66
    jmp device_irq_common_stub

device_irq_67_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 67
    jmp device_irq_common_stub

device_irq_68_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 68
    jmp device_irq_common_stub

device_irq_69_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 69
    jmp device_irq_common_stub

device_irq_70_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 70
    jmp device_irq_common_stub

device_irq_71_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 71
    jmp device_irq_common_stub

device_irq_72_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 72
    jmp device_irq_common_stub

device_irq_73_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 73
    jmp device_irq_common_stub

device_irq_74_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 74
    jmp device_irq_common_stub

device_irq_75_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 75
    jmp device_irq_common_stub

device_irq_76_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 76
    jmp device_irq_common_stub

device_irq_77_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 77
    jmp device_irq_common_stub

device_irq_78_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 78
    jmp device_irq_common_stub

device_irq_79_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 79
    jmp device_irq_common_stub

device_irq_80_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 80
    jmp device_irq_common_stub

device_irq_81_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 81
    jmp device_irq_common_stub

device_irq_82_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 82
    jmp device_irq_common_stub

device_irq_83_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 83
    jmp device_irq_common_stub

device_irq_84_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 84
    jmp device_irq_common_stub

device_irq_85_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 85
    jmp device_irq_common_stub

device_irq_86_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 86
    jmp device_irq_common_stub

device_irq_87_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 87
    jmp device_irq_common_stub

device_irq_88_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 88
    jmp device_irq_common_stub

device_irq_89_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 89
    jmp device_irq_common_stub

device_irq_90_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 90
    jmp device_irq_common_stub

device_irq_91_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 91
    jmp device_irq_common_stub

device_irq_92_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 92
    jmp device_irq_common_stub

device_irq_93_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 93
    jmp device_irq_common_stub

device_irq_94_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 94
    jmp device_irq_common_stub

device_irq_95_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 95
    jmp device_irq_common_stub

device_irq_96_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 96
    jmp device_irq_common_stub

device_irq_97_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 97
    jmp device_irq_common_stub

device_irq_98_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 98
    jmp device_irq_common_stub

device_irq_99_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 99
    jmp device_irq_common_stub

device_irq_100_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 100
    jmp device_irq_common_stub

device_irq_101_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 101
    jmp device_irq_common_stub

device_irq_102_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 102
    jmp device_irq_common_stub

device_irq_103_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 103
    jmp device_irq_common_stub

device_irq_104_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 104
    jmp device_irq_common_stub

device_irq_105_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 105
    jmp device_irq_common_stub

device_irq_106_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 106
    jmp device_irq_common_stub

device_irq_107_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 107
    jmp device_irq_common_stub

device_irq_108_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 108
    jmp device_irq_common_stub

device_irq_109_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 109
    jmp device_irq_common_stub

device_irq_110_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 110
    jmp device_irq_common_stub

device_irq_111_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 111
    jmp device_irq_common_stub

device_irq_112_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 112
    jmp device_irq_common_stub

device_irq_113_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 113
    jmp device_irq_common_stub

device_irq_114_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 114
    jmp device_irq_common_stub

device_irq_115_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 115
    jmp device_irq_common_stub

device_irq_116_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 116
    jmp device_irq_common_stub

device_irq_117_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 117
    jmp device_irq_common_stub

device_irq_118_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 118
    jmp device_irq_common_stub

device_irq_119_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 119
    jmp device_irq_common_stub

device_irq_120_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 120
    jmp device_irq_common_stub

device_irq_121_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 121
    jmp device_irq_common_stub

device_irq_122_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 122
    jmp device_irq_common_stub

device_irq_123_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 123
    jmp device_irq_common_stub

device_irq_124_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 124
    jmp device_irq_common_stub

device_irq_125_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 125
    jmp device_irq_common_stub

device_irq_126_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 126
    jmp device_irq_common_stub

device_irq_127_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 127
    jmp device_irq_common_stub

device_irq_128_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 128
    jmp device_irq_common_stub

device_irq_129_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 129
    jmp device_irq_common_stub

device_irq_130_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 130
    jmp device_irq_common_stub

device_irq_131_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 131
    jmp device_irq_common_stub

device_irq_132_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 132
    jmp device_irq_common_stub

device_irq_133_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 133
    jmp device_irq_common_stub

device_irq_134_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 134
    jmp device_irq_common_stub

device_irq_135_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 135
    jmp device_irq_common_stub

device_irq_136_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 136
    jmp device_irq_common_stub

device_irq_137_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 137
    jmp device_irq_common_stub

device_irq_138_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 138
    jmp device_irq_common_stub

device_irq_139_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 139
    jmp device_irq_common_stub

device_irq_140_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 140
    jmp device_irq_common_stub

device_irq_141_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 141
    jmp device_irq_common_stub

device_irq_142_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 142
    jmp device_irq_common_stub

device_irq_143_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 143
    jmp device_irq_common_stub

device_irq_144_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 144
    jmp device_irq_common_stub

device_irq_145_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 145
    jmp device_irq_common_stub

device_irq_146_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 146
    jmp device_irq_common_stub

device_irq_147_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 147
    jmp device_irq_common_stub

device_irq_148_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 148
    jmp device_irq_common_stub

device_irq_149_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 149
    jmp device_irq_common_stub

device_irq_150_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 150
    jmp device_irq_common_stub

device_irq_151_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 151
    jmp device_irq_common_stub

device_irq_152_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 152
    jmp device_irq_common_stub

device_irq_153_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 153
    jmp device_irq_common_stub

device_irq_154_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 154
    jmp device_irq_common_stub

device_irq_155_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 155
    jmp device_irq_common_stub

device_irq_156_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 156
    jmp device_irq_common_stub

device_irq_157_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 157
    jmp device_irq_common_stub

device_irq_158_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 158
    jmp device_irq_common_stub

device_irq_159_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 159
    jmp device_irq_common_stub

device_irq_160_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 160
    jmp device_irq_common_stub

device_irq_161_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 161
    jmp device_irq_common_stub

device_irq_162_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 162
    jmp device_irq_common_stub

device_irq_163_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 163
    jmp device_irq_common_stub

device_irq_164_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 164
    jmp device_irq_common_stub

device_irq_165_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 165
    jmp device_irq_common_stub

device_irq_166_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 166
    jmp device_irq_common_stub

device_irq_167_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 167
    jmp device_irq_common_stub

device_irq_168_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 168
    jmp device_irq_common_stub

device_irq_169_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 169
    jmp device_irq_common_stub

device_irq_170_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 170
    jmp device_irq_common_stub

device_irq_171_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 171
    jmp device_irq_common_stub

device_irq_172_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 172
    jmp device_irq_common_stub

device_irq_173_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 173
    jmp device_irq_common_stub

device_irq_174_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 174
    jmp device_irq_common_stub

device_irq_175_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 175
    jmp device_irq_common_stub

device_irq_176_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 176
    jmp device_irq_common_stub

device_irq_177_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 177
    jmp device_irq_common_stub

device_irq_178_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 178
    jmp device_irq_common_stub

device_irq_179_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 179
    jmp device_irq_common_stub

device_irq_180_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 180
    jmp device_irq_common_stub

device_irq_181_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 181
    jmp device_irq_common_stub

device_irq_182_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 182
    jmp device_irq_common_stub

device_irq_183_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 183
    jmp device_irq_common_stub

device_irq_184_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 184
    jmp device_irq_common_stub

device_irq_185_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 185
    jmp device_irq_common_stub

device_irq_186_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 186
    jmp device_irq_common_stub

device_irq_187_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 187
    jmp device_irq_common_stub

device_irq_188_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 188
    jmp device_irq_common_stub

device_irq_189_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 189
    jmp device_irq_common_stub

device_irq_190_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 190
    jmp device_irq_common_stub

device_irq_191_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 191
    jmp device_irq_common_stub

device_irq_192_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 192
    jmp device_irq_common_stub

device_irq_193_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 193
    jmp device_irq_common_stub

device_irq_194_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 194
    jmp device_irq_common_stub

device_irq_195_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 195
    jmp device_irq_common_stub

device_irq_196_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 196
    jmp device_irq_common_stub

device_irq_197_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 197
    jmp device_irq_common_stub

device_irq_198_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 198
    jmp device_irq_common_stub

device_irq_199_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 199
    jmp device_irq_common_stub

device_irq_200_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 200
    jmp device_irq_common_stub

device_irq_201_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 201
    jmp device_irq_common_stub

device_irq_202_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 202
    jmp device_irq_common_stub

device_irq_203_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 203
    jmp device_irq_common_stub

device_irq_204_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 204
    jmp device_irq_common_stub

device_irq_205_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 205
    jmp device_irq_common_stub

device_irq_206_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 206
    jmp device_irq_common_stub

device_irq_207_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 207
    jmp device_irq_common_stub

device_irq_208_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 208
    jmp device_irq_common_stub

device_irq_209_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 209
    jmp device_irq_common_stub

device_irq_210_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 210
    jmp device_irq_common_stub

device_irq_211_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 211
    jmp device_irq_common_stub

device_irq_212_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 212
    jmp device_irq_common_stub

device_irq_213_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 213
    jmp device_irq_common_stub

device_irq_214_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 214
    jmp device_irq_common_stub

device_irq_215_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 215
    jmp device_irq_common_stub

device_irq_216_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 216
    jmp device_irq_common_stub

device_irq_217_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 217
    jmp device_irq_common_stub

device_irq_218_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 218
    jmp device_irq_common_stub

device_irq_219_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 219
    jmp device_irq_common_stub

device_irq_220_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 220
    jmp device_irq_common_stub

device_irq_221_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 221
    jmp device_irq_common_stub

device_irq_222_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 222
    jmp device_irq_common_stub

device_irq_223_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 223
    jmp device_irq_common_stub

device_irq_common_stub:
    SAVE_IRQ_ALL
    mov rdi, [rsp + 128]
    call device_irq_dispatch
    SWAPGS_IF_USER rsp, 144
    RESTORE_IRQ_ALL
    add rsp, 8
    iretq

%macro EXCEPTION_NO_ERROR 1
exception%1_stub:
    SWAPGS_IF_USER rsp, 8
    cld
    push qword 0
    SAVE_ALL
    mov r12, rsp
    and rsp, -16
    mov rsi, r12
    mov edi, %1
    call exception_handler
    cmp dword [rel user_mode_test_exit_requested], 0
    jne %%user_test_exit
    test byte [r12 + 136], 3
    jnz %%schedule_user
    SWAPGS_IF_USER r12, 136
    mov rsp, r12
    RESTORE_ALL
    add rsp, 8
    iretq
%%schedule_user:
    SCHEDULE_USER_FRAME
%%user_test_exit:
    mov dword [rel user_mode_test_exit_requested], 0
    jmp user_mode_test_return_trampoline
%endmacro

%macro EXCEPTION_ERROR 1
exception%1_stub:
    SWAPGS_IF_USER rsp, 16
    cld
    SAVE_ALL
    mov r12, rsp
    and rsp, -16
    mov rsi, r12
    mov edi, %1
    call exception_handler
    cmp dword [rel user_mode_test_exit_requested], 0
    jne %%user_test_exit
    test byte [r12 + 136], 3
    jnz %%schedule_user
    SWAPGS_IF_USER r12, 136
    mov rsp, r12
    RESTORE_ALL
    add rsp, 8
    iretq
%%schedule_user:
    SCHEDULE_USER_FRAME
%%user_test_exit:
    mov dword [rel user_mode_test_exit_requested], 0
    jmp user_mode_test_return_trampoline
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
global device_irq_stub_table
device_irq_stub_table:
    dq device_irq_64_stub
    dq device_irq_65_stub
    dq device_irq_66_stub
    dq device_irq_67_stub
    dq device_irq_68_stub
    dq device_irq_69_stub
    dq device_irq_70_stub
    dq device_irq_71_stub
    dq device_irq_72_stub
    dq device_irq_73_stub
    dq device_irq_74_stub
    dq device_irq_75_stub
    dq device_irq_76_stub
    dq device_irq_77_stub
    dq device_irq_78_stub
    dq device_irq_79_stub
    dq device_irq_80_stub
    dq device_irq_81_stub
    dq device_irq_82_stub
    dq device_irq_83_stub
    dq device_irq_84_stub
    dq device_irq_85_stub
    dq device_irq_86_stub
    dq device_irq_87_stub
    dq device_irq_88_stub
    dq device_irq_89_stub
    dq device_irq_90_stub
    dq device_irq_91_stub
    dq device_irq_92_stub
    dq device_irq_93_stub
    dq device_irq_94_stub
    dq device_irq_95_stub
    dq device_irq_96_stub
    dq device_irq_97_stub
    dq device_irq_98_stub
    dq device_irq_99_stub
    dq device_irq_100_stub
    dq device_irq_101_stub
    dq device_irq_102_stub
    dq device_irq_103_stub
    dq device_irq_104_stub
    dq device_irq_105_stub
    dq device_irq_106_stub
    dq device_irq_107_stub
    dq device_irq_108_stub
    dq device_irq_109_stub
    dq device_irq_110_stub
    dq device_irq_111_stub
    dq device_irq_112_stub
    dq device_irq_113_stub
    dq device_irq_114_stub
    dq device_irq_115_stub
    dq device_irq_116_stub
    dq device_irq_117_stub
    dq device_irq_118_stub
    dq device_irq_119_stub
    dq device_irq_120_stub
    dq device_irq_121_stub
    dq device_irq_122_stub
    dq device_irq_123_stub
    dq device_irq_124_stub
    dq device_irq_125_stub
    dq device_irq_126_stub
    dq device_irq_127_stub
    dq device_irq_128_stub
    dq device_irq_129_stub
    dq device_irq_130_stub
    dq device_irq_131_stub
    dq device_irq_132_stub
    dq device_irq_133_stub
    dq device_irq_134_stub
    dq device_irq_135_stub
    dq device_irq_136_stub
    dq device_irq_137_stub
    dq device_irq_138_stub
    dq device_irq_139_stub
    dq device_irq_140_stub
    dq device_irq_141_stub
    dq device_irq_142_stub
    dq device_irq_143_stub
    dq device_irq_144_stub
    dq device_irq_145_stub
    dq device_irq_146_stub
    dq device_irq_147_stub
    dq device_irq_148_stub
    dq device_irq_149_stub
    dq device_irq_150_stub
    dq device_irq_151_stub
    dq device_irq_152_stub
    dq device_irq_153_stub
    dq device_irq_154_stub
    dq device_irq_155_stub
    dq device_irq_156_stub
    dq device_irq_157_stub
    dq device_irq_158_stub
    dq device_irq_159_stub
    dq device_irq_160_stub
    dq device_irq_161_stub
    dq device_irq_162_stub
    dq device_irq_163_stub
    dq device_irq_164_stub
    dq device_irq_165_stub
    dq device_irq_166_stub
    dq device_irq_167_stub
    dq device_irq_168_stub
    dq device_irq_169_stub
    dq device_irq_170_stub
    dq device_irq_171_stub
    dq device_irq_172_stub
    dq device_irq_173_stub
    dq device_irq_174_stub
    dq device_irq_175_stub
    dq device_irq_176_stub
    dq device_irq_177_stub
    dq device_irq_178_stub
    dq device_irq_179_stub
    dq device_irq_180_stub
    dq device_irq_181_stub
    dq device_irq_182_stub
    dq device_irq_183_stub
    dq device_irq_184_stub
    dq device_irq_185_stub
    dq device_irq_186_stub
    dq device_irq_187_stub
    dq device_irq_188_stub
    dq device_irq_189_stub
    dq device_irq_190_stub
    dq device_irq_191_stub
    dq device_irq_192_stub
    dq device_irq_193_stub
    dq device_irq_194_stub
    dq device_irq_195_stub
    dq device_irq_196_stub
    dq device_irq_197_stub
    dq device_irq_198_stub
    dq device_irq_199_stub
    dq device_irq_200_stub
    dq device_irq_201_stub
    dq device_irq_202_stub
    dq device_irq_203_stub
    dq device_irq_204_stub
    dq device_irq_205_stub
    dq device_irq_206_stub
    dq device_irq_207_stub
    dq device_irq_208_stub
    dq device_irq_209_stub
    dq device_irq_210_stub
    dq device_irq_211_stub
    dq device_irq_212_stub
    dq device_irq_213_stub
    dq device_irq_214_stub
    dq device_irq_215_stub
    dq device_irq_216_stub
    dq device_irq_217_stub
    dq device_irq_218_stub
    dq device_irq_219_stub
    dq device_irq_220_stub
    dq device_irq_221_stub
    dq device_irq_222_stub
    dq device_irq_223_stub

global ap_trampoline_blob_start
global ap_trampoline_blob_end
ap_trampoline_blob_start:
    incbin "build/obj/arch/x86_64/ap_trampoline.bin"
ap_trampoline_blob_end:

section .note.GNU-stack noalloc noexec nowrite progbits
