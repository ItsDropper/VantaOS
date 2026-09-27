ENTRY(_start)

SECTIONS
{
    /*
     * Keep the Multiboot header and the ELF entry point in the same
     * first loaded text segment. This follows the layout used by the
     * GRUB Multiboot reference example.
     */
    .text 1M :
    {
        KEEP(*(.text.start))
        KEEP(*(.multiboot))
        *(.text)
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
