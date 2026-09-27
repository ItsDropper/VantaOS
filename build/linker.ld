ENTRY(_start)

SECTIONS
{
    .multiboot 1M :
    {
        KEEP(*(.multiboot))
    }

    .text ALIGN(16) :
    {
        KEEP(*(.text.start))

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
