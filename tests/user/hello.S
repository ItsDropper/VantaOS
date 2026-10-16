.section .text
.global _start
.type _start, @function

_start:
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

.section .rodata
serial_message:
    .ascii "VANTAOS_FILESYSTEM_EXEC_OK\n"
serial_message_end:

terminal_message:
    .ascii "VantaOS userspace is running.\n"
terminal_message_end:

.section .note.GNU-stack,"",@progbits
