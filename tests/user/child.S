/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

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

    mov $23, %edi
    xor %eax, %eax
    int $0x80
    ud2

child_fail:
    mov $1, %eax
    lea child_failure(%rip), %rdi
    mov $(child_failure_end - child_failure), %esi
    int $0x80

    mov $1, %edi
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
