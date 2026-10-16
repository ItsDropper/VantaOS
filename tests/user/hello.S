.section .text
.global _start
.type _start, @function

_start:
    mov (%rsp), %rax
    cmp $3, %rax
    jne argv_fail

    mov 16(%rsp), %rdi
    cmpb $'a', 0(%rdi)
    jne argv_fail
    cmpb $'a', 4(%rdi)
    jne argv_fail
    cmpb $0, 5(%rdi)
    jne argv_fail

    mov 24(%rsp), %rdi
    cmpb $'t', 0(%rdi)
    jne argv_fail
    cmpb $' ', 3(%rdi)
    jne argv_fail
    cmpb $'w', 4(%rdi)
    jne argv_fail
    cmpb $0, 9(%rdi)
    jne argv_fail

    mov $1, %eax
    lea argv_message(%rip), %rdi
    mov $(argv_message_end - argv_message), %esi
    int $0x80

    mov $1, %eax
    lea serial_message(%rip), %rdi
    mov $(serial_message_end - serial_message), %esi
    int $0x80

    mov $2, %eax
    lea terminal_message(%rip), %rdi
    mov $(terminal_message_end - terminal_message), %esi
    int $0x80

    xor %eax, %eax
    int $0x80
    ud2

argv_fail:
    mov $1, %eax
    lea argv_failure(%rip), %rdi
    mov $(argv_failure_end - argv_failure), %esi
    int $0x80
    xor %eax, %eax
    int $0x80
    ud2

.section .rodata
argv_message:
    .ascii "VANTAOS_ARGV_STACK_OK\n"
argv_message_end:

argv_failure:
    .ascii "VANTAOS_ARGV_STACK_FAIL\n"
argv_failure_end:

serial_message:
    .ascii "VANTAOS_FILESYSTEM_EXEC_OK\n"
serial_message_end:

terminal_message:
    .ascii "VantaOS userspace received argv and is running.\n"
terminal_message_end:

.section .note.GNU-stack,"",@progbits
