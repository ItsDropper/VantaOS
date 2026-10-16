.section .text
.global _start
.type _start, @function

_start:
    mov $3, %eax
    lea file_path(%rip), %rdi
    mov $1, %esi
    int $0x80
    cmp $3, %rax
    jl file_fail
    mov %rax, %r12

    /* Invalid user destination must fail before consuming file bytes. */
    mov $4, %eax
    mov %r12, %rdi
    mov $0x7ffffffff000, %rsi
    mov $(fixture_end - fixture), %edx
    int $0x80
    cmp $-14, %rax
    jne file_fail

    mov $4, %eax
    mov %r12, %rdi
    lea file_buffer(%rip), %rsi
    mov $(fixture_end - fixture), %edx
    int $0x80
    cmp $(fixture_end - fixture), %rax
    jne file_fail

    lea file_buffer(%rip), %rsi
    lea fixture(%rip), %rdi
    mov $(fixture_end - fixture), %ecx
compare_fixture:
    test %ecx, %ecx
    jz fixture_matches
    movb (%rsi), %al
    cmpb (%rdi), %al
    jne file_fail
    inc %rsi
    inc %rdi
    dec %ecx
    jmp compare_fixture

fixture_matches:
    mov $5, %eax
    mov %r12, %rdi
    int $0x80
    test %rax, %rax
    jne file_fail

    /* Invalid descriptors are rejected deterministically. */
    mov $5, %eax
    mov $99, %edi
    int $0x80
    cmp $-9, %rax
    jne file_fail

    mov $3, %eax
    lea missing_path(%rip), %rdi
    mov $1, %esi
    int $0x80
    cmp $-2, %rax
    jne file_fail

    mov $1, %eax
    lea file_success(%rip), %rdi
    mov $(file_success_end - file_success), %esi
    int $0x80
    xor %eax, %eax
    int $0x80
    ud2

file_fail:
    mov $1, %eax
    lea file_failure(%rip), %rdi
    mov $(file_failure_end - file_failure), %esi
    int $0x80
    xor %eax, %eax
    int $0x80
    ud2

.section .rodata
file_path:
    .asciz "/USERDATA.TXT"
missing_path:
    .asciz "/NO-SUCH-FILE.TXT"
fixture:
    .ascii "VANTAOS_USER_FILE_FIXTURE\n"
fixture_end:
file_success:
    .ascii "VANTAOS_USER_FILE_IO_OK\n"
file_success_end:
file_failure:
    .ascii "VANTAOS_USER_FILE_IO_FAIL\n"
file_failure_end:

.section .bss
.align 16
file_buffer:
    .skip fixture_end - fixture

.section .note.GNU-stack,"",@progbits
