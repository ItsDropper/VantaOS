.section .text
.global _start
.type _start, @function

_start:
    cmpq $2, (%rsp)
    jne child_fail

    /* Verify the second argument survived the user-to-kernel-to-user copy. */
    mov 16(%rsp), %rdi
    lea expected_argument(%rip), %rsi
    mov $14, %ecx
compare_argument:
    movb (%rdi), %al
    cmpb (%rsi), %al
    jne child_fail
    inc %rdi
    inc %rsi
    dec %ecx
    jnz compare_argument

    mov $1, %eax
    lea child_message(%rip), %rdi
    mov $(child_message_end - child_message), %esi
    int $0x80
    jmp child_exit

child_fail:
    mov $1, %eax
    lea child_failure(%rip), %rdi
    mov $(child_failure_end - child_failure), %esi
    int $0x80

child_exit:
    xor %edi, %edi
    xor %eax, %eax
    int $0x80
    ud2

.section .rodata
expected_argument:
    .asciz "spawned-child"
child_message:
    .ascii "VANTAOS_USER_CHILD_PROGRAM_OK\n"
child_message_end:
child_failure:
    .ascii "VANTAOS_USER_CHILD_PROGRAM_FAIL\n"
child_failure_end:

.section .note.GNU-stack,"",@progbits
