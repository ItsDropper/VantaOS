ENTRY(_start)

SECTIONS
{
    /*
     * Keep the Multiboot header at the beginning of the loaded image.
     * GRUB requires the complete header to be within the first 8192
     * bytes and 32-bit aligned.
     */
    .multiboot 1M :
    {
        KEEP(*(.multiboot))
    }

    /*
     * Start executable kernel code on its own aligned section.
     *
     * Previously .multiboot and .text were merged into one output
     * section. That put _start immediately after the 12-byte header.
     * Keeping the boot entry in a dedicated section makes the ELF
     * entry address unambiguous and prevents the CPU from ever being
     * directed into the middle of the Multiboot header/startup bytes.
     */
    .text ALIGN(4K) :
    {
        KEEP(*(.text.start))
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
