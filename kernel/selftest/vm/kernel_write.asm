%include "common.inc"
    mov byte [abs 0x100000], 1       ; kernel image
    EXIT 5
