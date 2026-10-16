.section .text
.global _start
.type _start, @function

_start:
    /* Validate argc and the two shell-parsed arguments. */
    mov (%rsp), %rax
    cmp $3, %rax
    jne file_fail
    mov 16(%rsp), %rdi
    cmpb $'a', 0(%rdi)
    jne file_fail
    cmpb $'a', 4(%rdi)
    jne file_fail
    cmpb $0, 5(%rdi)
    jne file_fail
    mov 24(%rsp), %rdi
    cmpb $'t', 0(%rdi)
    jne file_fail
    cmpb $' ', 3(%rdi)
    jne file_fail
    cmpb $'w', 4(%rdi)
    jne file_fail
    cmpb $0, 9(%rdi)
    jne file_fail

    mov $1, %eax
    lea argv_message(%rip), %rdi
    mov $(argv_message_end - argv_message), %esi
    int $0x80
    mov $1, %eax
    lea exec_message(%rip), %rdi
    mov $(exec_message_end - exec_message), %esi
    int $0x80

    /* spawn rejects invalid user pointers and malformed argument counts. */
    mov $6, %eax
    mov $0x7ffffffff000, %rdi
    lea child_argv(%rip), %rsi
    mov $1, %edx
    int $0x80
    cmp $-14, %rax
    jne file_fail

    mov $6, %eax
    lea child_path(%rip), %rdi
    mov $0x7ffffffff000, %rsi
    mov $1, %edx
    int $0x80
    cmp $-14, %rax
    jne file_fail

    mov $6, %eax
    lea child_path(%rip), %rdi
    lea child_argv(%rip), %rsi
    xor %edx, %edx
    int $0x80
    cmp $-22, %rax
    jne file_fail

    mov $6, %eax
    lea child_path(%rip), %rdi
    lea child_argv(%rip), %rsi
    mov $17, %edx
    int $0x80
    cmp $-22, %rax
    jne file_fail

    mov $6, %eax
    lea child_path(%rip), %rdi
    lea invalid_child_argv(%rip), %rsi
    mov $1, %edx
    int $0x80
    cmp $-14, %rax
    jne file_fail

    mov $6, %eax
    lea child_path(%rip), %rdi
    lea long_child_argv(%rip), %rsi
    mov $1, %edx
    int $0x80
    cmp $-22, %rax
    jne file_fail

    /* Launch a separate ELF from FAT32 with a kernel-owned argv copy. */
    mov $6, %eax
    lea child_path(%rip), %rdi
    lea child_argv(%rip), %rsi
    mov $2, %edx
    int $0x80
    test %rax, %rax
    jle file_fail

    mov $1, %eax
    lea spawn_message(%rip), %rdi
    mov $(spawn_message_end - spawn_message), %esi
    int $0x80

    /* Invalid output pointers and oversized writes must be rejected. */
    mov $1, %eax
    mov $0x7ffffffff000, %rdi
    mov $1, %esi
    int $0x80
    cmp $-14, %rax
    jne file_fail

    mov $1, %eax
    lea file_path(%rip), %rdi
    mov $4097, %esi
    int $0x80
    cmp $-22, %rax
    jne file_fail

    /* An invalid path pointer must not reach the VFS. */
    mov $3, %eax
    mov $0x7ffffffff000, %rdi
    mov $1, %esi
    int $0x80
    cmp $-14, %rax
    jne file_fail

    /* Open the known FAT32 fixture read-only. */
    mov $3, %eax
    lea file_path(%rip), %rdi
    mov $1, %esi
    int $0x80
    cmp $3, %rax
    jl file_fail
    mov %rax, %r12

    /* Requests above the syscall limit must fail without consuming data. */
    mov $4, %eax
    mov %r12, %rdi
    lea file_buffer(%rip), %rsi
    mov $4097, %edx
    int $0x80
    cmp $-22, %rax
    jne file_fail

    /* An invalid destination must not consume any file bytes. */
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

    /* Invalid descriptors and missing paths must return errors. */
    mov $5, %eax
    mov $99, %edi
    int $0x80
    cmp $-9, %rax
    jne file_fail

    /* High descriptor bits must not truncate into a valid descriptor. */
    mov $5, %eax
    movabs $0x100000003, %rdi
    int $0x80
    cmp $-9, %rax
    jne file_fail

    mov $3, %eax
    lea missing_path(%rip), %rdi
    mov $1, %esi
    int $0x80
    cmp $-2, %rax
    jne file_fail

    /* Fill all per-process slots, verify exhaustion, then exit without
       closing them so the reaper must release every owned VFS handle. */
    mov $8, %r13
open_until_full:
    mov $3, %eax
    lea file_path(%rip), %rdi
    mov $1, %esi
    int $0x80
    cmp $3, %rax
    jl file_fail
    dec %r13
    jnz open_until_full

    mov $3, %eax
    lea file_path(%rip), %rdi
    mov $1, %esi
    int $0x80
    cmp $-24, %rax
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
argv_message:
    .ascii "VANTAOS_ARGV_STACK_OK\n"
argv_message_end:
exec_message:
    .ascii "VANTAOS_FILESYSTEM_EXEC_OK\n"
exec_message_end:
spawn_message:
    .ascii "VANTAOS_USER_SPAWN_OK\n"
spawn_message_end:
child_path:
    .asciz "/CHILD.ELF"
.align 8
child_argv:
    .quad child_path
    .quad child_argument
child_argument:
    .asciz "spawned-child"
invalid_child_argv:
    .quad 0x7ffffffff000
long_child_argv:
    .quad long_child_argument
long_child_argument:
    .fill 513, 1, 0x61
    .byte 0
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
