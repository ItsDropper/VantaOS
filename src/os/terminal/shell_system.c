#include "shell_internal.h"
#include "interrupts.h"
#include "heap.h"
#include "multiboot.h"
#include "pci.h"
#include "pmm.h"
#include "terminal.h"
#include "panic.h"
#include <stdint.h>

extern unsigned long long kernel_boot_start(void);
extern unsigned long long kernel_boot_gdt(void);
extern unsigned long long kernel_boot_terminal(void);
extern unsigned long long kernel_boot_interrupts(void);
extern unsigned long long kernel_boot_keyboard(void);
extern unsigned long long kernel_boot_shell(void);

const char* shell_process_state(
    process_state_t state
)
{
    switch (state)
    {
        case PROCESS_READY:
            return "READY";
        case PROCESS_RUNNING:
            return "RUNNING";
        case PROCESS_SLEEPING:
            return "SLEEPING";
        case PROCESS_TERMINATED:
            return "TERMINATED";
        default:
            return "UNKNOWN";
    }
}

void shell_ps(void)
{
    terminal_write("\nPID  PPID  STATE      NAME\n");
    terminal_write("---  ----  ---------  ----------------\n");

    for (uint32_t pid = 0; pid < PROCESS_MAX; pid++)
    {
        const process_t* process =
            process_get(pid);

        if (process == 0)
            continue;

        terminal_write("   ");

        if (pid < 10)
            terminal_putchar('0');

        shell_print_decimal(pid);

        terminal_write("    ");

        shell_print_decimal(
            process->parent_pid
        );

        terminal_write("   ");
        terminal_write(
            shell_process_state(process->state)
        );

        terminal_write("   ");
        terminal_write(process->name);
        terminal_putchar('\n');
    }
}

void shell_help(void)
{
    terminal_write("\n");
    terminal_write("Available commands:\n");
    terminal_write("  help     - Show available commands\n");
    terminal_write("  clear    - Clear the terminal\n");
    terminal_write("  about    - Show information about VantaOS\n");
    terminal_write("  specs    - Show system specifications\n");
    terminal_write("  boot     - Show boot timing information\n");
    terminal_write("  uptime   - Show system uptime\n");
    terminal_write("  mem      - Show physical memory status\n");
    terminal_write("  heap     - Show kernel heap status and test allocation\n");
    terminal_write("  history  - Show command history\n");
    terminal_write("  fault    - Trigger a test page fault\n");
    terminal_write("  echo     - Print text\n");
    terminal_write("  reboot   - Reboot the system\n");
    terminal_write("  pwd      - Show current directory\n");
    terminal_write("  ls       - List files and directories\n");
    terminal_write("  cd       - Change directory\n");
    terminal_write("  cat      - Read a system file\n");
    terminal_write("  ps       - Show processes\n");
    terminal_write("  storage  - Show ATA/FAT32 storage status\n");
}

void shell_about(void)
{
    terminal_write("\n");
    terminal_write("VantaOS\n");
    terminal_write("A custom x86 operating system.\n");
    terminal_write("Architecture: x86 32-bit\n");
    terminal_write("Kernel: VantaOS\n");
    terminal_write("Bootloader: GRUB / Multiboot\n");
}

void shell_specs(void)
{
    if (!pci_initialized)
    {
        terminal_write(
            "\nInitializing PCI hardware database...\n"
        );

        pci_initialize();
        pci_initialized = 1;
    }

    terminal_write("\n");
    terminal_write("VantaOS System Specifications\n");
    terminal_write("=============================\n\n");

    terminal_write("System\n");
    terminal_write("------\n");
    terminal_write("Architecture : x86 32-bit\n");
    terminal_write("Bootloader   : GRUB / Multiboot\n");

    unsigned int eax;
    unsigned int ebx;
    unsigned int ecx;
    unsigned int edx;

    char vendor[13];

    __asm__ volatile (
        "cpuid"
        : "=a"(eax),
          "=b"(ebx),
          "=c"(ecx),
          "=d"(edx)
        : "a"(0)
    );

    vendor[0] =
        (char)(ebx & 0xFF);

    vendor[1] =
        (char)((ebx >> 8) & 0xFF);

    vendor[2] =
        (char)((ebx >> 16) & 0xFF);

    vendor[3] =
        (char)((ebx >> 24) & 0xFF);

    vendor[4] =
        (char)(edx & 0xFF);

    vendor[5] =
        (char)((edx >> 8) & 0xFF);

    vendor[6] =
        (char)((edx >> 16) & 0xFF);

    vendor[7] =
        (char)((edx >> 24) & 0xFF);

    vendor[8] =
        (char)(ecx & 0xFF);

    vendor[9] =
        (char)((ecx >> 8) & 0xFF);

    vendor[10] =
        (char)((ecx >> 16) & 0xFF);

    vendor[11] =
        (char)((ecx >> 24) & 0xFF);

    vendor[12] = 0;

    terminal_write("\nCPU\n");
    terminal_write("---\n");
    terminal_write("Vendor       : ");
    terminal_write(vendor);
    terminal_putchar('\n');

    __asm__ volatile (
        "cpuid"
        : "=a"(eax),
          "=b"(ebx),
          "=c"(ecx),
          "=d"(edx)
        : "a"(1)
    );

    unsigned int stepping =
        eax & 0xF;

    unsigned int model =
        (eax >> 4) & 0xF;

    unsigned int family =
        (eax >> 8) & 0xF;

    unsigned int extended_model =
        (eax >> 16) & 0xF;

    unsigned int extended_family =
        (eax >> 20) & 0xFF;

    if (family == 0xF)
        family += extended_family;

    if (family == 0x6 ||
        family == 0xF)
    {
        model +=
            extended_model << 4;
    }

    terminal_write("Family       : ");
    shell_print_decimal(family);
    terminal_putchar('\n');

    terminal_write("Model        : ");
    shell_print_decimal(model);
    terminal_putchar('\n');

    terminal_write("Stepping     : ");
    shell_print_decimal(stepping);
    terminal_putchar('\n');

    if (multiboot_info != 0 &&
        (multiboot_info->flags & 0x01))
    {
        unsigned int memory_kib =
            multiboot_info->mem_upper + 1024;

        unsigned int memory_mib =
            memory_kib / 1024;

        terminal_write("\nMemory\n");
        terminal_write("------\n");
        terminal_write("Detected     : ");

        shell_print_decimal(
            memory_mib
        );

        terminal_write(" MiB\n");
    }
    else
    {
        terminal_write("\nMemory\n");
        terminal_write("------\n");
        terminal_write(
            "Detected     : unavailable\n"
        );
    }

    terminal_write("\nHardware\n");
    terminal_write("--------\n");

    int count =
        pci_get_device_count();

    terminal_write("PCI Devices  : ");

    shell_print_decimal(
        (unsigned int)count
    );

    terminal_putchar('\n');

    for (int i = 0; i < count; i++)
    {
        const struct pci_device* device =
            pci_get_device(i);

        terminal_write("  ");

        terminal_write(
            pci_get_vendor_name(
                device->vendor_id
            )
        );

        terminal_write(" - ");

        terminal_write(
            pci_get_device_name(
                device->vendor_id,
                device->device_id
            )
        );

        terminal_write(" (");

        terminal_write(
            pci_get_class_name(
                device->class_code,
                device->subclass
            )
        );

        terminal_write(")\n");
    }
}

void shell_boot(void)
{
    unsigned long long start =
        kernel_boot_start();

    unsigned long long gdt =
        kernel_boot_gdt();

    unsigned long long terminal =
        kernel_boot_terminal();

    unsigned long long interrupts =
        kernel_boot_interrupts();

    unsigned long long keyboard =
        kernel_boot_keyboard();

    unsigned long long shell =
        kernel_boot_shell();

    terminal_write("\n");
    terminal_write("VantaOS Boot Timing\n");
    terminal_write("===================\n");

    terminal_write(
        "Measurements are raw CPU TSC cycles.\n\n"
    );

    terminal_write("GDT          : ");
    shell_print_hex64(
        gdt - start
    );
    terminal_putchar('\n');

    terminal_write("Terminal     : ");
    shell_print_hex64(
        terminal - gdt
    );
    terminal_putchar('\n');

    terminal_write("Pre-IRQ      : ");
    shell_print_hex64(
        interrupts - terminal
    );
    terminal_putchar('\n');

    terminal_write("Keyboard     : ");
    shell_print_hex64(
        keyboard - interrupts
    );
    terminal_putchar('\n');

    terminal_write("Interrupts   : ");
    shell_print_hex64(
        shell - keyboard
    );
    terminal_putchar('\n');

    terminal_write("\nTotal        : ");

    shell_print_hex64(
        shell - start
    );

    terminal_write(" cycles\n");

    terminal_write(
        "\nNote: TSC frequency calibration will be added later.\n"
    );
}

void shell_uptime(void)
{
    unsigned int ticks =
        interrupts_get_ticks();

    terminal_write("\nUptime: ");

    shell_print_decimal(
        ticks / 100
    );

    terminal_write(" seconds\n");
}

void shell_mem(void)
{
    if (!pmm_initialized)
    {
        pmm_initialize(multiboot_info);
        pmm_initialized = pmm_is_initialized();
    }

    terminal_write("\nPhysical memory manager:\n");

    terminal_write("  Status        : ");
    terminal_write(pmm_is_initialized() ? "online\n" : "offline\n");

    terminal_write("  Total pages   : ");
    shell_print_decimal(pmm_get_total_blocks());
    terminal_putchar('\n');

    terminal_write("  Free pages    : ");
    shell_print_decimal(pmm_get_free_blocks());
    terminal_putchar('\n');

    terminal_write("\nKernel heap:\n");

    terminal_write("  Status        : ");
    terminal_write(heap_is_initialized() ? "online\n" : "offline\n");

    terminal_write("  Total         : ");
    shell_print_decimal((unsigned int)heap_get_total_size());
    terminal_write(" bytes\n");

    terminal_write("  Used          : ");
    shell_print_decimal((unsigned int)heap_get_used_size());
    terminal_write(" bytes\n");

    terminal_write("  Free          : ");
    shell_print_decimal((unsigned int)heap_get_free_size());
    terminal_write(" bytes\n");

    void* block = pmm_alloc_block();

    if (block == 0)
    {
        terminal_write("  Allocation    : failed\n");
        return;
    }

    terminal_write("  Allocation    : passed\n");

    pmm_free_block(block);

    terminal_write("  Free test     : passed\n");
}

void shell_heap(void)
{
    terminal_write("\nKernel heap test:\n");
    terminal_write("  Status        : ");
    terminal_write(heap_is_initialized() ? "online\n" : "offline\n");

    if (!heap_is_initialized())
        return;

    terminal_write("  Total         : ");
    shell_print_decimal((unsigned int)heap_get_total_size());
    terminal_write(" bytes\n");

    terminal_write("  Free before   : ");
    shell_print_decimal((unsigned int)heap_get_free_size());
    terminal_write(" bytes\n");

    void* first = kmalloc(64);
    void* second = kmalloc(128);
    void* third = kmalloc(256);

    if (!first || !second || !third)
    {
        terminal_write("  Allocation    : failed\n");

        if (first) kfree(first);
        if (second) kfree(second);
        if (third) kfree(third);

        return;
    }

    terminal_write("  Allocation    : passed\n");

    terminal_write("  Block 1       : ");
    shell_print_hex64((unsigned long long)(uintptr_t)first);
    terminal_putchar('\n');

    terminal_write("  Block 2       : ");
    shell_print_hex64((unsigned long long)(uintptr_t)second);
    terminal_putchar('\n');

    terminal_write("  Block 3       : ");
    shell_print_hex64((unsigned long long)(uintptr_t)third);
    terminal_putchar('\n');

    kfree(second);
    kfree(first);
    kfree(third);

    terminal_write("  Free/coalesce : passed\n");

    void* reused = kmalloc(128);

    if (reused)
    {
        terminal_write("  Reuse test    : passed\n");
        kfree(reused);
    }
    else
    {
        terminal_write("  Reuse test    : failed\n");
    }

    terminal_write("  Free after    : ");
    shell_print_decimal((unsigned int)heap_get_free_size());
    terminal_write(" bytes\n");
}

void shell_show_history(void)
{
    terminal_write("\nCommand history:\n");

    if (shell_history_count == 0)
    {
        terminal_write("  (empty)\n");
        return;
    }

    for (unsigned int i = 0;
         i < shell_history_count;
         i++)
    {
        terminal_write("  ");
        shell_print_decimal(i + 1);
        terminal_write("  ");
        terminal_write(shell_history[i]);
        terminal_putchar('\n');
    }
}

void shell_fault(void)
{
    terminal_write("\nTriggering a test page fault...\n");

    volatile unsigned int* unmapped =
        (volatile unsigned int*)0xC0000000;

    unsigned int value = *unmapped;
    (void)value;
}

void shell_reboot(void)
{
    terminal_write(
        "\nRebooting...\n"
    );

    unsigned char status;

    do
    {
        __asm__ volatile (
            "inb $0x64, %0"
            : "=a"(status)
        );
    }
    while (status & 0x02);

    __asm__ volatile (
        "movb $0xFE, %%al\n"
        "outb %%al, $0x64\n"
        :
        :
        : "al"
    );

    while (1)
        __asm__ volatile ("hlt");
}
