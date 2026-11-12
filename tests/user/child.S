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
    xor %r13d, %r13d
    xor %r11d, %r11d
    mov (%rsp), %rax
    cmp $2, %rax
    je child_success
    cmp $3, %rax
    jne child_fail

    /*
     * The optional third argument is the parent's decimal IPC capability.
     * The launcher formats it as exactly 20 digits, including leading zeroes.
     */
    mov 24(%rsp), %rdi
    xor %eax, %eax
    mov $20, %ecx
    movabs $0x0CCCCCCCCCCCCCCC, %r8
parse_parent_token:
    movzbq (%rdi), %rdx
    cmp $'0', %dl
    jb child_fail
    cmp $'9', %dl
    ja child_fail
    sub $'0', %dl
    movzbq %dl, %rdx
    cmp %r8, %rax
    ja child_fail
    jb parse_parent_token_accumulate
    cmp $7, %rdx
    ja child_fail
parse_parent_token_accumulate:
    imul $10, %rax, %rax
    add %rdx, %rax
    inc %rdi
    loop parse_parent_token
    cmpb $0, (%rdi)
    jne child_fail
    test %rax, %rax
    jle child_fail
    mov %rax, %r12

    mov $1, %eax
    lea parent_token_ok(%rip), %rdi
    mov $(parent_token_ok_end - parent_token_ok), %esi
    int $0x80

    /* Create a child-owned endpoint and publish its capability to the parent. */
    mov $26, %eax
    int $0x80
    test %rax, %rax
    jle child_fail
    mov %rax, %r13
    mov $1, %r11d
    mov %r13, child_endpoint_token(%rip)

    mov $1, %eax
    lea child_endpoint_created(%rip), %rdi
    mov $(child_endpoint_created_end - child_endpoint_created), %esi
    int $0x80

    mov $27, %eax
    mov %r12, %rdi
    lea child_endpoint_token(%rip), %rsi
    mov $8, %edx
    int $0x80
    test %rax, %rax
    jne child_fail_close
    mov $2, %r11d

    mov $1, %eax
    lea child_endpoint_sent(%rip), %rdi
    mov $(child_endpoint_sent_end - child_endpoint_sent), %esi
    int $0x80

    /*
     * The parent replies only after receiving our endpoint token. Poll with a
     * finite yield budget so a broken IPC path cannot hang the QEMU smoke test.
     */
    mov $500, %r14d
child_receive_retry:
    mov $28, %eax
    mov %r13, %rdi
    lea child_reply(%rip), %rsi
    mov $3, %edx
    int $0x80
    cmp $-11, %rax
    jne child_receive_done
    mov $9, %eax
    int $0x80
    dec %r14d
    jnz child_receive_retry
    jmp child_fail_close

child_receive_done:
    cmp $3, %rax
    jne child_fail_close
    cmpb $'a', child_reply(%rip)
    jne child_fail_close
    cmpb $'c', child_reply+1(%rip)
    jne child_fail_close
    cmpb $'k', child_reply+2(%rip)
    jne child_fail_close
    mov $3, %r11d

    mov $1, %eax
    lea child_ack_received(%rip), %rdi
    mov $(child_ack_received_end - child_ack_received), %esi
    int $0x80

    /*
     * Deliberately leave the endpoint open. Process-exit cleanup must revoke
     * it; the parent verifies that a post-waitpid send returns -EBADF.
     */
child_success:
    mov $1, %eax
    lea child_message(%rip), %rdi
    mov $(child_message_end - child_message), %esi
    int $0x80

    mov $23, %edi
    xor %eax, %eax
    int $0x80
    ud2

child_fail_close:
    test %r13, %r13
    jz child_fail
    mov $29, %eax
    mov %r13, %rdi
    int $0x80
    xor %r13d, %r13d

child_fail:
    cmp $0, %r11d
    jne child_fail_after_token
    mov $1, %eax
    lea child_bad_parent_token(%rip), %rdi
    mov $(child_bad_parent_token_end - child_bad_parent_token), %esi
    int $0x80
    jmp child_fail_message
child_fail_after_token:
    cmp $1, %r11d
    jne child_fail_after_create
    mov $1, %eax
    lea child_endpoint_create_fail(%rip), %rdi
    mov $(child_endpoint_create_fail_end - child_endpoint_create_fail), %esi
    int $0x80
    jmp child_fail_message
child_fail_after_create:
    cmp $2, %r11d
    jne child_fail_after_send
    mov $1, %eax
    lea child_endpoint_send_fail(%rip), %rdi
    mov $(child_endpoint_send_fail_end - child_endpoint_send_fail), %esi
    int $0x80
    jmp child_fail_message
child_fail_after_send:
    mov $1, %eax
    lea child_endpoint_reply_fail(%rip), %rdi
    mov $(child_endpoint_reply_fail_end - child_endpoint_reply), %esi
    int $0x80

child_fail_message:
    mov $1, %eax
    lea child_failure(%rip), %rdi
    mov $(child_failure_end - child_failure), %esi
    int $0x80

    mov $1, %edi
    xor %eax, %eax
    int $0x80
    ud2

.section .rodata
parent_token_ok:
    .ascii "VANTAOS_USER_CHILD_PARENT_TOKEN_OK\n"
parent_token_ok_end:
child_endpoint_created:
    .ascii "VANTAOS_USER_CHILD_ENDPOINT_CREATED\n"
child_endpoint_created_end:
child_endpoint_sent:
    .ascii "VANTAOS_USER_CHILD_ENDPOINT_SENT\n"
child_endpoint_sent_end:
child_ack_received:
    .ascii "VANTAOS_USER_CHILD_ACK_RECEIVED\n"
child_ack_received_end:
child_message:
    .ascii "VANTAOS_USER_CHILD_PROGRAM_OK\n"
child_message_end:
child_bad_parent_token:
    .ascii "VANTAOS_USER_CHILD_PARENT_TOKEN_FAIL\n"
child_bad_parent_token_end:
child_endpoint_create_fail:
    .ascii "VANTAOS_USER_CHILD_ENDPOINT_CREATE_FAIL\n"
child_endpoint_create_fail_end:
child_endpoint_send_fail:
    .ascii "VANTAOS_USER_CHILD_ENDPOINT_SEND_FAIL\n"
child_endpoint_send_fail_end:
child_endpoint_reply_fail:
    .ascii "VANTAOS_USER_CHILD_ENDPOINT_REPLY_FAIL\n"
child_endpoint_reply_fail_end:
child_failure:
    .ascii "VANTAOS_USER_CHILD_PROGRAM_FAIL\n"
child_failure_end:

.section .bss
.align 8
child_endpoint_token:
    .skip 8
child_reply:
    .skip 3

.section .note.GNU-stack,"",@progbits
