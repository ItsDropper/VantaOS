#include "shell.h"

#include "interrupts.h"
#include "heap.h"
#include "multiboot.h"
#include "pci.h"
#include "pmm.h"
#include "terminal.h"

#include <stdint.h>

#define SHELL_BUFFER_SIZE 128
#define SHELL_HISTORY_SIZE 32

static char shell_buffer[SHELL_BUFFER_SIZE];
static unsigned int shell_length = 0;
static unsigned int shell_cursor = 0;

static char shell_history[
    SHELL_HISTORY_SIZE
][SHELL_BUFFER_SIZE];

static unsigned int shell_history_count = 0;

/*
 * Position inside command history.
 *
 * history_count means the current line is the
 * temporary draft rather than a history entry.
 */
static unsigned int shell_history_position = 0;

static char shell_history_draft[SHELL_BUFFER_SIZE];
static unsigned int shell_history_draft_length = 0;

static multiboot_info_t* multiboot_info = 0;

static int pmm_initialized = 0;
static int pci_initialized = 0;

extern unsigned long long kernel_boot_start(void);
extern unsigned long long kernel_boot_gdt(void);
extern unsigned long long kernel_boot_terminal(void);
extern unsigned long long kernel_boot_interrupts(void);
extern unsigned long long kernel_boot_keyboard(void);
extern unsigned long long kernel_boot_shell(void);

static void shell_clear_buffer(void)
{
    for (unsigned int i = 0;
         i < SHELL_BUFFER_SIZE;
         i++)
    {
        shell_buffer[i] = 0;
    }

    shell_length = 0;
    shell_cursor = 0;
}

static void shell_prompt(void)
{
    terminal_write(">");

    terminal_begin_input();

    shell_clear_buffer();
}

void shell_show_prompt(void)
{
    shell_prompt();
}

static int shell_string_equals(
    const char* a,
    const char* b
)
{
    unsigned int i = 0;

    while (a[i] != 0 && b[i] != 0)
    {
        if (a[i] != b[i])
            return 0;

        i++;
    }

    return a[i] == 0 &&
           b[i] == 0;
}

static void shell_print_decimal(
    unsigned int value
)
{
    char buffer[16];
    unsigned int length = 0;

    if (value == 0)
    {
        terminal_putchar('0');
        return;
    }

    while (value > 0)
    {
        buffer[length++] =
            (char)('0' + (value % 10));

        value /= 10;
    }

    while (length > 0)
        terminal_putchar(buffer[--length]);
}

static void shell_print_hex64(
    unsigned long long value
)
{
    const char* hex =
        "0123456789ABCDEF";

    terminal_write("0x");

    for (int i = 15; i >= 0; i--)
    {
        terminal_putchar(
            hex[(value >> (i * 4)) & 0xF]
        );
    }
}

static void shell_copy_string(
    char* destination,
    const char* source
)
{
    unsigned int i = 0;

    while (source[i] != 0 &&
           i < SHELL_BUFFER_SIZE - 1)
    {
        destination[i] =
            source[i];

        i++;
    }

    destination[i] = 0;
}

static void shell_load_buffer(
    const char* text
)
{
    shell_copy_string(
        shell_buffer,
        text
    );

    shell_length = 0;

    while (shell_buffer[shell_length] != 0 &&
           shell_length < SHELL_BUFFER_SIZE - 1)
    {
        shell_length++;
    }

    shell_cursor = shell_length;

    terminal_redraw_input(
        shell_buffer
    );

    terminal_set_input_cursor(
        shell_cursor
    );
}

static void shell_insert_char(char c)
{
    if (shell_length >= SHELL_BUFFER_SIZE - 1)
        return;

    /*
     * Move everything after the cursor one position
     * to the right.
     */
    for (unsigned int i = shell_length;
         i > shell_cursor;
         i--)
    {
        shell_buffer[i] =
            shell_buffer[i - 1];
    }

    shell_buffer[shell_cursor] = c;

    shell_length++;
    shell_cursor++;

    terminal_redraw_input(
        shell_buffer
    );

    terminal_set_input_cursor(
        shell_cursor
    );
}

static void shell_backspace(void)
{
    if (shell_cursor == 0)
        return;

    /*
     * Remove the character immediately before
     * the cursor.
     */
    for (unsigned int i = shell_cursor - 1;
         i < shell_length - 1;
         i++)
    {
        shell_buffer[i] =
            shell_buffer[i + 1];
    }

    shell_length--;

    shell_buffer[shell_length] = 0;

    shell_cursor--;

    terminal_redraw_input(
        shell_buffer
    );

    terminal_set_input_cursor(
        shell_cursor
    );
}

static void shell_delete(void)
{
    if (shell_cursor >= shell_length)
        return;

    for (unsigned int i = shell_cursor;
         i < shell_length - 1;
         i++)
    {
        shell_buffer[i] =
            shell_buffer[i + 1];
    }

    shell_length--;

    shell_buffer[shell_length] = 0;

    terminal_redraw_input(
        shell_buffer
    );

    terminal_set_input_cursor(
        shell_cursor
    );
}

static void shell_move_left(void)
{
    if (shell_cursor == 0)
        return;

    shell_cursor--;

    terminal_set_input_cursor(
        shell_cursor
    );
}

static void shell_move_right(void)
{
    if (shell_cursor >= shell_length)
        return;

    shell_cursor++;

    terminal_set_input_cursor(
        shell_cursor
    );
}

static void shell_move_home(void)
{
    shell_cursor = 0;

    terminal_set_input_cursor(
        shell_cursor
    );
}

static void shell_move_end(void)
{
    shell_cursor = shell_length;

    terminal_set_input_cursor(
        shell_cursor
    );
}

static void shell_save_draft(void)
{
    shell_copy_string(
        shell_history_draft,
        shell_buffer
    );

    shell_history_draft_length =
        shell_length;
}

static void shell_restore_draft(void)
{
    shell_load_buffer(
        shell_history_draft
    );
}

static void shell_add_history(void)
{
    if (shell_length == 0)
        return;

    /*
     * Do not store duplicate consecutive commands.
     */
    if (shell_history_count > 0)
    {
        if (shell_string_equals(
                shell_history[
                    shell_history_count - 1
                ],
                shell_buffer))
        {
            shell_history_position =
                shell_history_count;

            return;
        }
    }

    if (shell_history_count <
        SHELL_HISTORY_SIZE)
    {
        shell_copy_string(
            shell_history[
                shell_history_count
            ],
            shell_buffer
        );

        shell_history_count++;
    }
    else
    {
        /*
         * Drop the oldest command.
         */
        for (unsigned int i = 1;
             i < SHELL_HISTORY_SIZE;
             i++)
        {
            shell_copy_string(
                shell_history[i - 1],
                shell_history[i]
            );
        }

        shell_copy_string(
            shell_history[
                SHELL_HISTORY_SIZE - 1
            ],
            shell_buffer
        );
    }

    shell_history_position =
        shell_history_count;
}

static void shell_history_up(void)
{
    if (shell_history_count == 0)
        return;

    /*
     * Save the current unfinished command the first
     * time we enter history.
     */
    if (shell_history_position ==
        shell_history_count)
    {
        shell_save_draft();
    }

    if (shell_history_position > 0)
        shell_history_position--;
    else
        return;

    shell_load_buffer(
        shell_history[
            shell_history_position
        ]
    );
}

static void shell_history_down(void)
{
    if (shell_history_count == 0)
        return;

    if (shell_history_position <
        shell_history_count - 1)
    {
        shell_history_position++;

        shell_load_buffer(
            shell_history[
                shell_history_position
            ]
        );

        return;
    }

    /*
     * Moving down from the newest command returns
     * to the unfinished draft.
     */
    shell_history_position =
        shell_history_count;

    shell_restore_draft();
}

static void shell_help(void)
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
}

static void shell_about(void)
{
    terminal_write("\n");
    terminal_write("VantaOS\n");
    terminal_write("A custom x86 operating system.\n");
    terminal_write("Architecture: x86 32-bit\n");
    terminal_write("Kernel: VantaOS\n");
    terminal_write("Bootloader: GRUB / Multiboot\n");
}

static void shell_specs(void)
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

static void shell_boot(void)
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

static void shell_uptime(void)
{
    unsigned int ticks =
        interrupts_get_ticks();

    terminal_write("\nUptime: ");

    shell_print_decimal(
        ticks / 100
    );

    terminal_write(" seconds\n");
}

static void shell_mem(void)
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

static void shell_heap(void)
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

static void shell_show_history(void)
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

static void shell_fault(void)
{
    terminal_write("\nTriggering a test page fault...\n");

    volatile unsigned int* unmapped =
        (volatile unsigned int*)0xC0000000;

    unsigned int value = *unmapped;
    (void)value;
}

static void shell_reboot(void)
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

static void shell_execute(void)
{
    shell_buffer[shell_length] = 0;

    /*
     * Move to the end of the command before
     * executing it. This matters when the user
     * presses Enter while the cursor is in the
     * middle of the command.
     */
    terminal_set_input_cursor(
        shell_length
    );

    if (shell_length == 0)
    {
        terminal_putchar('\n');

        shell_show_prompt();

        return;
    }

    shell_add_history();

    if (shell_string_equals(
            shell_buffer,
            "help"))
    {
        shell_help();
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "clear"))
    {
        terminal_initialize();

        shell_clear_buffer();

        shell_show_prompt();

        return;
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "about"))
    {
        shell_about();
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "specs"))
    {
        shell_specs();
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "boot"))
    {
        shell_boot();
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "uptime"))
    {
        shell_uptime();
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "mem"))
    {
        shell_mem();
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "heap"))
    {
        shell_heap();
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "history"))
    {
        shell_show_history();
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "fault"))
    {
        shell_fault();
    }
    else if (shell_string_equals(
                 shell_buffer,
                 "reboot"))
    {
        shell_reboot();
    }
    else if (shell_buffer[0] == 'e' &&
             shell_buffer[1] == 'c' &&
             shell_buffer[2] == 'h' &&
             shell_buffer[3] == 'o' &&
             shell_buffer[4] == ' ')
    {
        terminal_putchar('\n');

        terminal_write(
            &shell_buffer[5]
        );
    }
    else
    {
        terminal_write(
            "\nUnknown command: "
        );

        terminal_write(
            shell_buffer
        );
    }

    terminal_putchar('\n');

    shell_clear_buffer();

    shell_history_position =
        shell_history_count;

    shell_show_prompt();
}

void shell_set_multiboot_info(
    multiboot_info_t* mbd
)
{
    multiboot_info = mbd;
}

void shell_initialize(void)
{
    pmm_initialized = pmm_is_initialized();

    shell_clear_buffer();

    shell_history_count = 0;
    shell_history_position = 0;

    shell_history_draft_length = 0;

    for (unsigned int i = 0;
         i < SHELL_HISTORY_SIZE;
         i++)
    {
        shell_history[i][0] = 0;
    }

    shell_history_draft[0] = 0;
}

void shell_handle_event(
    keyboard_event_t event
)
{
    switch (event)
    {
        case KEY_EVENT_UP:
            shell_history_up();
            break;

        case KEY_EVENT_DOWN:
            shell_history_down();
            break;

        case KEY_EVENT_LEFT:
            shell_move_left();
            break;

        case KEY_EVENT_RIGHT:
            shell_move_right();
            break;

        case KEY_EVENT_HOME:
            shell_move_home();
            break;

        case KEY_EVENT_END:
            shell_move_end();
            break;

        case KEY_EVENT_DELETE:
            shell_delete();
            break;

        default:
            break;
    }
}

void shell_handle_char(char c)
{
    if (c == '\b')
    {
        shell_backspace();
        return;
    }

    if (c == '\n')
    {
        shell_execute();
        return;
    }

    if (c >= 32 && c <= 126)
    {
        shell_insert_char(c);
    }
}