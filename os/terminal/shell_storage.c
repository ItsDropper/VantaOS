#include "shell_internal.h"
#include "terminal.h"
#include "ata.h"
#include "fat32.h"

void shell_storage(void)
{
    terminal_putchar('\n');
    terminal_write("Storage diagnostics");
    terminal_putchar('\n');

    terminal_write("ATA available: ");
    terminal_write(ata_is_available() ? "yes" : "no");
    terminal_putchar('\n');

    terminal_write("ATA status: ");
    shell_print_hex64(ata_status());
    terminal_putchar('\n');

    terminal_write("ATA error: ");
    shell_print_hex64(ata_error());
    terminal_putchar('\n');

    terminal_write("ATA signature: ");
    shell_print_hex64(ata_signature_mid());
    terminal_write(" ");
    shell_print_hex64(ata_signature_high());
    terminal_putchar('\n');

    terminal_write("FAT32 mounted: ");
    terminal_write(fat32_is_mounted() ? "yes" : "no");
    terminal_putchar('\n');

    terminal_write("FAT32 root cluster: ");
    shell_print_decimal(fat32_root_cluster());
    terminal_putchar('\n');
}
