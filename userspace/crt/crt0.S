/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

/* Minimal System V AMD64 process entry for static VantaOS ELF programs. */
.section .text
.global _start
.type _start, @function
.extern main

_start:
    /* The kernel supplies argc, argv[], envp[] and the AT_NULL terminator. */
    xor %ebp, %ebp
    mov (%rsp), %rdi
    lea 8(%rsp), %rsi

    /* Align RSP before CALL so main enters with RSP % 16 == 8. */
    and $-16, %rsp
    call main

    /* main's return value is the process exit status. */
    mov %eax, %edi
    xor %eax, %eax
    int $0x80

    /* Exit is non-returning; do not execute arbitrary bytes if it returns. */
1:
    pause
    jmp 1b

.size _start, .-_start
.section .note.GNU-stack,"",@progbits
