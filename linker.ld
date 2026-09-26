ENTRY(_start)

SECTIONS
{
    /* The kernel is loaded at 1 MiB by GRUB/multiboot */
    . = 1M;

    /* Multiboot header must come first, followed by .text */
    .text BLOCK(4K) : ALIGN(4K)
    {
        *(.multiboot)
        *(.text)
    }

    /* Read-only data */
    .rodata BLOCK(4K) : ALIGN(4K)
    {
        *(.rodata)
    }

    /* Read-write initialized data */
    .data BLOCK(4K) : ALIGN(4K)
    {
        *(.data)
    }

    /* Read-write uninitialized data and stack */
    .bss BLOCK(4K) : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
    }

    /* Define 'end' symbol right after the last kernel section */
    _end = .;
    end = .;
}