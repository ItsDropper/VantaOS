/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "recovery_environment.h"

#include "filesystem.h"
#include "graphics.h"
#include "keyboard.h"
#include "pmm.h"
#include "pci.h"
#include "serial.h"
#include "serial_log.h"
#include "vanta_boot_info.h"

#include <stddef.h>
#include <stdint.h>

#define RECOVERY_BG       0x00111620U
#define RECOVERY_BLUE     0x003D8BFFU
#define RECOVERY_TEXT     0x00F1F5F9U
#define RECOVERY_MUTED    0x0097A6B7U
#define RECOVERY_SELECTED 0x00263F62U
#define RECOVERY_LINE     0x00303C4DU
#define RECOVERY_MENU_COUNT 6

static const char* const recovery_menu[] = {
    "Continue to VantaOS",
    "System diagnostics",
    "View boot and kernel logs",
    "Request QEMU screenshot",
    "Restart computer",
    "Shut down computer"
};

static void recovery_decimal(uint64_t value, char* output, size_t capacity)
{
    char reverse[24];
    size_t count = 0;
    if (!capacity) return;
    do {
        reverse[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value && count < sizeof(reverse));
    size_t out = 0;
    while (count && out + 1 < capacity)
        output[out++] = reverse[--count];
    output[out] = 0;
}

static void recovery_wait_for_key(void)
{
    while (!keyboard_has_char() && !keyboard_has_event())
        __asm__ volatile ("pause");
}

static int recovery_read_choice(int* selected)
{
    recovery_wait_for_key();

    if (keyboard_has_event()) {
        keyboard_event_t event = keyboard_get_event();
        if (event == KEY_EVENT_UP) {
            *selected = (*selected + RECOVERY_MENU_COUNT - 1) % RECOVERY_MENU_COUNT;
            return 0;
        }
        if (event == KEY_EVENT_DOWN) {
            *selected = (*selected + 1) % RECOVERY_MENU_COUNT;
            return 0;
        }
    }

    if (keyboard_has_char()) {
        char key = keyboard_get_char();
        if (key == '\n' || key == '\r')
            return 1;
        if (key == 'w' || key == 'W') {
            *selected = (*selected + RECOVERY_MENU_COUNT - 1) % RECOVERY_MENU_COUNT;
            return 0;
        }
        if (key == 's' || key == 'S') {
            *selected = (*selected + 1) % RECOVERY_MENU_COUNT;
            return 0;
        }
    }
    return 0;
}

static void recovery_header(const char* subtitle)
{
    uint32_t width = graphics_get_width();
    uint32_t height = graphics_get_height();
    int margin = width >= 900 ? 56 : 18;
    int top = height >= 600 ? 34 : 14;
    int panel_width = (int)width - margin * 2;

    graphics_clear(RECOVERY_BG);
    graphics_fill_rect(margin, top, 4, height > 120 ? (int)height - top * 2 : 80,
                       RECOVERY_BLUE);
    graphics_draw_vanta_logo(margin + 22, top + 8, 34);
    graphics_draw_text(margin + 70, top + 12, "VANTAOS", RECOVERY_TEXT, 2);
    graphics_draw_text(margin + 70, top + 34, "RECOVERY ENVIRONMENT", RECOVERY_BLUE, 1);
    graphics_draw_text(margin + 22, top + 66, subtitle, RECOVERY_MUTED, 1);
    graphics_fill_rect(margin + 22, top + 86, panel_width - 44, 1, RECOVERY_LINE);
}

static void recovery_draw_menu(int selected, const char* status)
{
    uint32_t width = graphics_get_width();
    uint32_t height = graphics_get_height();
    int margin = width >= 900 ? 56 : 18;
    int top = height >= 600 ? 34 : 14;
    int row_height = height >= 600 ? 38 : 28;
    int first_y = top + 104;
    int panel_width = (int)width - margin * 2;

    recovery_header("Choose a recovery action. Normal boot remains the default.");
    for (int i = 0; i < RECOVERY_MENU_COUNT; i++) {
        int y = first_y + i * row_height;
        if (i == selected)
            graphics_fill_rounded_rect(margin + 18, y - 5, panel_width - 36,
                                       row_height - 2, 5, RECOVERY_SELECTED);
        graphics_draw_text(margin + 30, y + 3, i == selected ? ">" : " ",
                           i == selected ? RECOVERY_BLUE : RECOVERY_MUTED, 1);
        graphics_draw_text(margin + 52, y + 3, recovery_menu[i],
                           i == selected ? RECOVERY_TEXT : RECOVERY_MUTED, 1);
    }

    int footer_y = (int)height - 48;
    graphics_fill_rect(margin + 22, footer_y - 10, panel_width - 44, 1, RECOVERY_LINE);
    graphics_draw_text(margin + 24, footer_y, "UP/DOWN or W/S  Navigate     ENTER  Select",
                       RECOVERY_MUTED, 1);
    if (status)
        graphics_draw_text(margin + 24, footer_y + 16, status, RECOVERY_BLUE, 1);
    graphics_present();
}

static void recovery_draw_diagnostics(void)
{
    const vanta_boot_info_t* info = kernel_get_boot_info();
    uint32_t width = graphics_get_width();
    uint32_t height = graphics_get_height();
    int margin = width >= 900 ? 56 : 18;
    int y = height >= 600 ? 142 : 92;
    char number[24];

    recovery_header("Live values from initialized kernel facilities.");
    graphics_draw_text(margin + 24, y, "FRAMEBUFFER", RECOVERY_BLUE, 1);
    graphics_draw_text(margin + 24, y + 20, "Width:", RECOVERY_MUTED, 1);
    recovery_decimal(info ? info->framebuffer.width : width, number, sizeof(number));
    graphics_draw_text(margin + 100, y + 20, number, RECOVERY_TEXT, 1);
    graphics_draw_text(margin + 24, y + 40, "Height:", RECOVERY_MUTED, 1);
    recovery_decimal(info ? info->framebuffer.height : height, number, sizeof(number));
    graphics_draw_text(margin + 100, y + 40, number, RECOVERY_TEXT, 1);
    graphics_draw_text(margin + 24, y + 60, "Bits per pixel:", RECOVERY_MUTED, 1);
    recovery_decimal(info ? info->framebuffer.bits_per_pixel : 0, number, sizeof(number));
    graphics_draw_text(margin + 140, y + 60, number, RECOVERY_TEXT, 1);

    graphics_draw_text(margin + 24, y + 92, "MEMORY", RECOVERY_BLUE, 1);
    graphics_draw_text(margin + 24, y + 112, "Physical blocks (total/free):", RECOVERY_MUTED, 1);
    recovery_decimal(pmm_get_total_blocks(), number, sizeof(number));
    graphics_draw_text(margin + 220, y + 112, number, RECOVERY_TEXT, 1);
    graphics_draw_text(margin + 280, y + 112, "/", RECOVERY_MUTED, 1);
    recovery_decimal(pmm_get_free_blocks(), number, sizeof(number));
    graphics_draw_text(margin + 296, y + 112, number, RECOVERY_TEXT, 1);

    graphics_draw_text(margin + 24, y + 144, "DEVICES", RECOVERY_BLUE, 1);
    graphics_draw_text(margin + 24, y + 164, "Enumerated PCI functions:", RECOVERY_MUTED, 1);
    recovery_decimal((uint64_t)pci_get_device_count(), number, sizeof(number));
    graphics_draw_text(margin + 220, y + 164, number, RECOVERY_TEXT, 1);
    graphics_draw_text(margin + 24, y + 184, "FAT32 filesystem:", RECOVERY_MUTED, 1);
    graphics_draw_text(margin + 160, y + 184,
                       filesystem_is_initialized() ? "initialized" : "unavailable",
                       RECOVERY_TEXT, 1);
    graphics_draw_text(margin + 24, (int)height - 38, "Press any key to return.", RECOVERY_MUTED, 1);
    graphics_present();
    recovery_wait_for_key();
    if (keyboard_has_char()) (void)keyboard_get_char();
    if (keyboard_has_event()) (void)keyboard_get_event();
}

static void recovery_draw_logs(void)
{
    static char snapshot[2048];
    size_t length = serial_log_snapshot(snapshot, sizeof(snapshot) - 1);
    uint32_t width = graphics_get_width();
    uint32_t height = graphics_get_height();
    int margin = width >= 900 ? 56 : 18;
    int y = height >= 600 ? 142 : 92;
    int line_count = 0;
    size_t start = length > 1200 ? length - 1200 : 0;

    recovery_header("Recent in-memory serial log. Full serial output remains external.");
    for (size_t i = start; i < length && line_count < 16; ) {
        size_t end = i;
        char text[96];
        size_t n = 0;
        while (end < length && snapshot[end] != '\n' && n < sizeof(text) - 1)
            text[n++] = snapshot[end++];
        text[n] = 0;
        graphics_draw_text(margin + 24, y + line_count * 18, text, RECOVERY_TEXT, 1);
        while (end < length && snapshot[end] != '\n') end++;
        i = end < length ? end + 1 : length;
        line_count++;
    }
    graphics_draw_text(margin + 24, (int)height - 38, "Press any key to return.", RECOVERY_MUTED, 1);
    graphics_present();
    recovery_wait_for_key();
    if (keyboard_has_char()) (void)keyboard_get_char();
    if (keyboard_has_event()) (void)keyboard_get_event();
}

static __attribute__((noreturn)) void recovery_restart(void)
{
    __asm__ volatile ("cli" ::: "memory");
    for (unsigned int i = 0; i < 100000U; i++) {
        uint8_t status;
        __asm__ volatile ("inb $0x64, %0" : "=a"(status));
        if ((status & 0x02U) == 0) {
            uint8_t reset = 0xFE;
            __asm__ volatile ("outb %0, $0x64" : : "a"(reset));
            break;
        }
    }
    for (;;) __asm__ volatile ("hlt");
}

static __attribute__((noreturn)) void recovery_shutdown(void)
{
    __asm__ volatile ("cli" ::: "memory");
    /* QEMU/Bochs ACPI power-off port; unsupported hardware may ignore it. */
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604));
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0xB004));
    for (;;) __asm__ volatile ("hlt");
}

int recovery_environment_requested(const multiboot_info_t* mbd)
{
    if (!mbd || (mbd->flags & (1U << 2)) == 0 || mbd->cmdline == 0)
        return 0;

    const char* command_line = (const char*)(uintptr_t)mbd->cmdline;
    static const char token[] = "recovery=1";
    for (size_t i = 0; i + sizeof(token) <= 4096 && command_line[i]; i++) {
        size_t j = 0;
        while (token[j] && command_line[i + j] == token[j]) j++;
        if (token[j] == 0 &&
            (i == 0 || command_line[i - 1] == ' ') &&
            (command_line[i + j] == 0 || command_line[i + j] == ' '))
            return 1;
    }
    return 0;
}

void recovery_environment_run(multiboot_info_t* mbd)
{
    (void)mbd;
    int selected = 0;
    char status[80];
    status[0] = 0;

    serial_write("VANTAOS_RECOVERY_ENTER\n");
    for (;;) {
        recovery_draw_menu(selected, status[0] ? status : NULL);
        status[0] = 0;
        if (!recovery_read_choice(&selected))
            continue;

        switch (selected) {
        case 0:
            serial_write("VANTAOS_RECOVERY_CONTINUE_NORMAL\n");
            return;
        case 1:
            recovery_draw_diagnostics();
            break;
        case 2:
            recovery_draw_logs();
            break;
        case 3:
            /* CI/QEMU harnesses can react to this marker using the monitor. */
            serial_write("VANTAOS_RECOVERY_SCREENSHOT_REQUEST\n");
            {
                const char message[] = "QEMU host capture requested";
                size_t i = 0;
                while (message[i] && i + 1 < sizeof(status)) {
                    status[i] = message[i];
                    i++;
                }
                status[i] = 0;
            }
            break;
        case 4:
            recovery_restart();
        case 5:
            recovery_shutdown();
        default:
            break;
        }
    }
}
