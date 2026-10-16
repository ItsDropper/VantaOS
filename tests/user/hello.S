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
    mov %rax, %r14

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

    /* Invalid create pointers and access modes must be rejected. */
    mov $15, %eax
    mov $0x7ffffffff000, %rdi
    int $0x80
    cmp $-14, %rax
    jne file_fail

    mov $3, %eax
    lea config_path(%rip), %rdi
    xor %esi, %esi
    int $0x80
    cmp $-22, %rax
    jne file_fail

    /* Create a real FAT32-backed config file through ring-3 syscalls. */
    mov $15, %eax
    lea config_path(%rip), %rdi
    int $0x80
    test %rax, %rax
    jne file_fail

    /* Open read/write and truncate using explicit access flags. */
    mov $3, %eax
    lea config_path(%rip), %rdi
    mov $6, %esi
    int $0x80
    cmp $3, %rax
    jl file_fail
    mov %rax, %r12

    /* A write-only descriptor must not be usable for reads. */
    mov $4, %eax
    mov %r12, %rdi
    lea file_buffer(%rip), %rsi
    mov $1, %edx
    int $0x80
    cmp $-5, %rax
    jne file_fail

    /* File writes must validate pointers and reject lengths above 511 bytes. */
    mov $14, %eax
    mov %r12, %rdi
    mov $0x7ffffffff000, %rsi
    mov $1, %edx
    int $0x80
    cmp $-14, %rax
    jne file_fail

    mov $14, %eax
    mov %r12, %rdi
    lea config_data(%rip), %rsi
    mov $512, %edx
    int $0x80
    cmp $-22, %rax
    jne file_fail

    mov $14, %eax
    mov %r12, %rdi
    lea config_data(%rip), %rsi
    mov $(config_data_end - config_data), %edx
    int $0x80
    cmp $(config_data_end - config_data), %rax
    jne file_fail

    mov $5, %eax
    mov %r12, %rdi
    int $0x80
    test %rax, %rax
    jne file_fail

    /* Read it back from disk via a fresh read-only descriptor. */
    mov $3, %eax
    lea config_path(%rip), %rdi
    mov $1, %esi
    int $0x80
    cmp $3, %rax
    jl file_fail
    mov %rax, %r12

    mov $4, %eax
    mov %r12, %rdi
    lea file_buffer(%rip), %rsi
    mov $(config_data_end - config_data), %edx
    int $0x80
    cmp $(config_data_end - config_data), %rax
    jne file_fail

    lea file_buffer(%rip), %rsi
    lea config_data(%rip), %rdi
    mov $(config_data_end - config_data), %ecx
compare_config:
    test %ecx, %ecx
    jz config_matches
    movb (%rsi), %al
    cmpb (%rdi), %al
    jne file_fail
    inc %rsi
    inc %rdi
    dec %ecx
    jmp compare_config

config_matches:
    mov $5, %eax
    mov %r12, %rdi
    int $0x80
    test %rax, %rax
    jne file_fail

    mov $1, %eax
    lea write_success(%rip), %rdi
    mov $(write_success_end - write_success), %esi
    int $0x80

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


    /* waitpid must reject invalid status pointers without blocking. */
    mov $7, %eax
    mov %r14, %rdi
    mov $0x7ffffffff000, %rsi
    int $0x80
    cmp $-14, %rax
    jne file_fail

    mov $7, %eax
    xor %edi, %edi
    xor %esi, %esi
    int $0x80
    cmp $-22, %rax
    jne file_fail

    mov $1, %eax
    lea spawn_message(%rip), %rdi
    mov $(spawn_message_end - spawn_message), %esi
    int $0x80

    /* Block until the child exits and verify the kernel preserved its status. */
    mov $7, %eax
    mov %r14, %rdi
    lea child_status(%rip), %rsi
    int $0x80
    cmp %r14, %rax
    jne file_fail
    mov child_status(%rip), %eax
    cmp $23, %eax
    jne file_fail

    /* A collected child must no longer be waitable. */
    mov $7, %eax
    mov %r14, %rdi
    xor %esi, %esi
    int $0x80
    cmp $-10, %rax
    jne file_fail

    mov $1, %eax
    lea wait_success(%rip), %rdi
    mov $(wait_success_end - wait_success), %esi
    int $0x80
    

    xor %edi, %edi
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
wait_success:
    .ascii "VANTAOS_USER_WAITPID_OK\n"
wait_success_end:
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
config_path:
    .asciz "/VNTUSER.CFG"
config_data:
    .ascii "region=Europe/Helsinki\ntimezone=local\n"
config_data_end:
missing_path:
    .asciz "/NO-SUCH-FILE.TXT"
fixture:
    .ascii "VANTAOS_USER_FILE_FIXTURE\n"
fixture_end:
write_success:
    .ascii "VANTAOS_USER_FILE_WRITE_OK\n"
write_success_end:
file_success:
    .ascii "VANTAOS_USER_FILE_IO_OK\n"
file_success_end:
file_failure:
    .ascii "VANTAOS_USER_FILE_IO_FAIL\n"
file_failure_end:

.section .bss
.align 16
file_buffer:
    .skip config_data_end - config_data
.align 4
child_status:
    .skip 4

.section .note.GNU-stack,"",@progbits
