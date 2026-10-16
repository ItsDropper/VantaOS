.section .text
.global _start
.type _start, @function

_start:
    mov $1, %eax
    lea child_message(%rip), %rdi
    mov $(child_message_end - child_message), %esi
    int $0x80

    xor %edi, %edi
    xor %eax, %eax
    int $0x80
    ud2

.section .rodata
child_message:
    .ascii "VANTAOS_USER_CHILD_PROGRAM_OK\n"
child_message_end:

.section .note.GNU-stack,"",@progbits
