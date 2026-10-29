/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

ENTRY(uefi_kernel_entry)

SECTIONS
{
    /* Keep the fixed-address kernel image clear of low-memory firmware allocations. */
    .multiboot 16M :
    {
        KEEP(*(.multiboot))
    }

    .text ALIGN(16) :
    {
        KEEP(*(.text.start))
        _kernel_text_start = .;
        *(.text)
        *(.text.*)
        _kernel_text_end = .;
    }

    .rodata ALIGN(4K) :
    {
        *(.rodata)
        *(.rodata.*)
    }

    .data ALIGN(4K) :
    {
        *(.data)
        *(.data.*)
    }

    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    _end = .;
    end = .;
}
