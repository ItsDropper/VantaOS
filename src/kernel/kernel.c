#include "ata.h"
#include "gdt.h"
#include "heap.h"
#include "interrupts.h"
#include "multiboot.h"
#include "os.h"
#include "paging.h"
#include "pci.h"
#include "pmm.h"

static inline unsigned long long read_tsc(void)
{
    unsigned int low;
    unsigned int high;

    __asm__ volatile (
        "rdtsc"
        : "=a"(low), "=d"(high)
    );

    return ((unsigned long long)high << 32) | low;
}

static unsigned long long boot_start;
static unsigned long long boot_gdt;
static unsigned long long boot_interrupts;

static void kernel_initialize(multiboot_info_t* mbd)
{
    gdt_initialize();
    boot_gdt = read_tsc();

    pmm_initialize(mbd);
    interrupts_initialize();
    boot_interrupts = read_tsc();

    paging_initialize();
    pci_initialize();
    ata_initialize();
    heap_initialize();
}

static void kernel_start_os(multiboot_info_t* mbd)
{
    os_initialize(mbd);
    os_run();
}

void kernel_main(multiboot_info_t* mbd)
{
    boot_start = read_tsc();

    kernel_initialize(mbd);
    kernel_start_os(mbd);
}


unsigned long long kernel_boot_start(void)
{
    return boot_start;
}

unsigned long long kernel_boot_gdt(void)
{
    return boot_gdt;
}

unsigned long long kernel_boot_interrupts(void)
{
    return boot_interrupts;
}
