#include "gdt.h"

/*
 * The bootstrap assembly installs the initial 64-bit GDT before entering
 * the C kernel. Keep this hook for the kernel bootstrap API; a later
 * architecture layer can replace the bootstrap GDT when user mode is
 * introduced.
 */
void gdt_initialize(void)
{
}
