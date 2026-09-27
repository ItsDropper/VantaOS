ENTRY(_start)

SECTIONS
{
    .text 1M :
    {
        KEEP(*(.text.start))
        KEEP(*(.multiboot))

        _kernel_text_start = .;
        *(.text)
        _kernel_text_end = .;
    }

    .rodata ALIGN(4K) :
    {
        *(.rodata)
    }

    .data ALIGN(4K) :
    {
        *(.data)
    }

    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss)
    }

    _end = .;
    end = .;
}
