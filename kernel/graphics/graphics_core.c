#include "graphics_internal.h"
#include "paging.h"
#include "pci.h"
#include "mouse.h"
#include "file_explorer.h"
#include <stdint.h>

#define BOCHS_VBE_INDEX 0x01CE
#define BOCHS_VBE_DATA  0x01CF

#define BOCHS_VBE_INDEX_ID        0x00
#define BOCHS_VBE_INDEX_XRES      0x01
#define BOCHS_VBE_INDEX_YRES      0x02
#define BOCHS_VBE_INDEX_BPP       0x03
#define BOCHS_VBE_INDEX_ENABLE    0x04
#define BOCHS_VBE_INDEX_VIRT_WIDTH  0x06
#define BOCHS_VBE_INDEX_VIRT_HEIGHT 0x07
#define BOCHS_VBE_INDEX_LFB       0x0D

#define BOCHS_VBE_ID 0xB0C5
#define BOCHS_VBE_ENABLE_LFB 0x0041

void bochs_vbe_write(uint16_t index, uint16_t value)
{
    __asm__ volatile (
        "outw %0, %1"
        :
        : "a"(index), "Nd"((uint16_t)BOCHS_VBE_INDEX)
    );

    __asm__ volatile (
        "outw %0, %1"
        :
        : "a"(value), "Nd"((uint16_t)BOCHS_VBE_DATA)
    );
}



static inline uint16_t bochs_vbe_read(uint16_t index)
{
    uint16_t value;

    __asm__ volatile (
        "outw %0, %1"
        :
        : "a"(index), "Nd"((uint16_t)BOCHS_VBE_INDEX)
    );

    __asm__ volatile (
        "inw %1, %0"
        : "=a"(value)
        : "Nd"((uint16_t)BOCHS_VBE_DATA)
    );

    return value;
}

uint32_t graphics_pack_color(uint32_t color)
{
    uint32_t red=(color>>16)&0xFF, green=(color>>8)&0xFF, blue=color&0xFF;
    if(red_mask_size<8) red>>=8-red_mask_size;
    if(green_mask_size<8) green>>=8-green_mask_size;
    if(blue_mask_size<8) blue>>=8-blue_mask_size;
    return (red<<red_position)|(green<<green_position)|(blue<<blue_position);
}

int graphics_initialize_bochs(void)
{
    if (bochs_vbe_read(BOCHS_VBE_INDEX_ID) != BOCHS_VBE_ID)
        return 0;

    bochs_vbe_write(BOCHS_VBE_INDEX_ENABLE, 0);
    bochs_vbe_write(BOCHS_VBE_INDEX_XRES, 1024);
    bochs_vbe_write(BOCHS_VBE_INDEX_YRES, 768);
    bochs_vbe_write(BOCHS_VBE_INDEX_BPP, 32);
    bochs_vbe_write(BOCHS_VBE_INDEX_VIRT_WIDTH, 1024);
    bochs_vbe_write(BOCHS_VBE_INDEX_VIRT_HEIGHT, 768);
    bochs_vbe_write(BOCHS_VBE_INDEX_ENABLE, BOCHS_VBE_ENABLE_LFB);

    /*
     * QEMU Standard VGA is a PCI device. Its framebuffer lives at
     * the device's memory BAR, so do not assume the legacy
     * 0xE0000000 address.
     */
    uint32_t physical = 0;

    for (int i = 0; i < pci_get_device_count(); i++)
    {
        const struct pci_device* device = pci_get_device(i);

        if (!device ||
            device->vendor_id != 0x1234 ||
            device->device_id != 0x1111 ||
            device->class_code != 0x03)
            continue;

        if (pci_get_bar0(device, &physical))
            break;
    }

    if (physical == 0)
        return 0;

    uint32_t offset = physical & 0xFFFU;
    uint32_t first_page = physical & 0xFFFFF000U;
    uint32_t pitch = 1024U * 4U;
    uint64_t bytes = (uint64_t)pitch * 768U;

    uint32_t pages =
        (uint32_t)((offset + bytes + 4095U) / 4096U);

    if (pages == 0 || pages > GRAPHICS_MAX_PAGES)
        return 0;

    for (uint32_t i = 0; i < pages; i++)
    {
        if (!paging_map_page(
                GRAPHICS_VIRTUAL_BASE + i * 4096U,
                first_page + i * 4096U))
            return 0;
    }

    /*
     * Do not mark graphics initialized until the first framebuffer page
     * can be resolved through the active page tables. This makes the
     * graphics layer fail closed instead of rendering through an invalid
     * virtual mapping.
     */
    if (paging_get_physical(GRAPHICS_VIRTUAL_BASE) != first_page)
        return 0;

    framebuffer = (uint8_t*)(GRAPHICS_VIRTUAL_BASE + offset);
    framebuffer_pitch = pitch;
    framebuffer_width = 1024;
    framebuffer_height = 768;

    red_position = 16;
    red_mask_size = 8;
    green_position = 8;
    green_mask_size = 8;
    blue_position = 0;
    blue_mask_size = 8;

    cursor_x = (int)framebuffer_width / 2;
    cursor_y = (int)framebuffer_height / 2;
    terminal_x = (int)framebuffer_width / 2 - 440;
    terminal_y = 64;
    terminal_restore_x = terminal_x;
    terminal_restore_y = terminal_y;
    terminal_dragging = 0;
    cursor_saved_valid = 0;

    initialized = 1;
    return 1;
}

int graphics_initialize(multiboot_info_t* mbd)
{
    initialized = 0;

    /*
     * Prefer the deterministic Bochs/QEMU path that was used by the
     * previously working graphics implementation.
     */
    if (graphics_initialize_bochs())
        return 1;

    /*
     * GRUB framebuffer is a fallback for non-Bochs framebuffer boots.
     */
    if (mbd &&
        (mbd->flags & MULTIBOOT_INFO_FRAMEBUFFER) &&
        mbd->framebuffer_addr <= 0xFFFFFFFFULL &&
        mbd->framebuffer_type == 1 &&
        mbd->framebuffer_bpp == 32 &&
        mbd->framebuffer_width != 0 &&
        mbd->framebuffer_height != 0 &&
        mbd->framebuffer_pitch >= mbd->framebuffer_width * 4U)
    {
        uint32_t physical = (uint32_t)mbd->framebuffer_addr;
        uint32_t offset = physical & 0xFFFU;
        uint32_t first_page = physical & 0xFFFFF000U;
        uint64_t bytes =
            (uint64_t)mbd->framebuffer_pitch *
            mbd->framebuffer_height;

        uint32_t pages =
            (uint32_t)((offset + bytes + 4095U) / 4096U);

        if (pages != 0 && pages <= GRAPHICS_MAX_PAGES)
        {
            for (uint32_t i = 0; i < pages; i++)
            {
                if (!paging_map_page(
                        GRAPHICS_VIRTUAL_BASE + i * 4096U,
                        first_page + i * 4096U))
                    return 0;
            }

            if (paging_get_physical(GRAPHICS_VIRTUAL_BASE) != first_page)
                return 0;

            framebuffer = (uint8_t*)(GRAPHICS_VIRTUAL_BASE + offset);
            framebuffer_pitch = mbd->framebuffer_pitch;
            framebuffer_width = mbd->framebuffer_width;
            framebuffer_height = mbd->framebuffer_height;

            red_position = mbd->framebuffer_red_position;
            red_mask_size = mbd->framebuffer_red_mask_size;
            green_position = mbd->framebuffer_green_position;
            green_mask_size = mbd->framebuffer_green_mask_size;
            blue_position = mbd->framebuffer_blue_position;
            blue_mask_size = mbd->framebuffer_blue_mask_size;

            cursor_x = (int)framebuffer_width / 2;
            cursor_y = (int)framebuffer_height / 2;
            terminal_x = (int)framebuffer_width / 2 - 440;
            if (terminal_x < 10)
                terminal_x = 10;
            terminal_y = 64;
            terminal_restore_x = terminal_x;
            terminal_restore_y = terminal_y;
            terminal_dragging = 0;
            cursor_saved_valid = 0;

            initialized = 1;
            return 1;
        }
    }

    return 0;
}

int graphics_is_initialized(void)
{
    return initialized;
}

int graphics_get_active_panel(void)
{
    return active_panel;
}

int graphics_terminal_close_requested(void)
{
    if (!terminal_close_requested)
        return 0;

    terminal_close_requested = 0;
    return 1;
}

void graphics_set_terminal_running(int running)
{
    terminal_running = running ? 1 : 0;
}

int graphics_terminal_is_running(void)
{
    return terminal_running;
}

void graphics_select_panel(int panel)
{
    if (!initialized)
        return;

    if (panel < 0 || panel > 4)
        return;

    active_panel = panel;
}

int graphics_set_resolution(uint32_t width, uint32_t height)
{
    if (!initialized ||
        width < 640 || height < 480 ||
        width > 1600 || height > 900)
        return 0;

    uint64_t bytes = (uint64_t)width * 4U * height;
    uint32_t pages = (uint32_t)((bytes + 4095U) / 4096U);

    if (pages == 0 || pages > GRAPHICS_MAX_PAGES)
        return 0;

    uint32_t physical = 0;

    for (int i = 0; i < pci_get_device_count(); i++)
    {
        const struct pci_device* device = pci_get_device(i);

        if (!device ||
            device->vendor_id != 0x1234 ||
            device->device_id != 0x1111 ||
            device->class_code != 0x03)
            continue;

        if (pci_get_bar0(device, &physical))
            break;
    }

    if (physical == 0)
        return 0;

    bochs_vbe_write(BOCHS_VBE_INDEX_ENABLE, 0);
    bochs_vbe_write(BOCHS_VBE_INDEX_XRES, (uint16_t)width);
    bochs_vbe_write(BOCHS_VBE_INDEX_YRES, (uint16_t)height);
    bochs_vbe_write(BOCHS_VBE_INDEX_BPP, 32);
    bochs_vbe_write(BOCHS_VBE_INDEX_VIRT_WIDTH, (uint16_t)width);
    bochs_vbe_write(BOCHS_VBE_INDEX_VIRT_HEIGHT, (uint16_t)height);
    bochs_vbe_write(BOCHS_VBE_INDEX_ENABLE, BOCHS_VBE_ENABLE_LFB);

    for (uint32_t i = 0; i < pages; i++)
    {
        if (!paging_map_page(
                GRAPHICS_VIRTUAL_BASE + i * 4096U,
                (physical & 0xFFFFF000U) + i * 4096U))
            return 0;
    }

    framebuffer = (uint8_t*)(GRAPHICS_VIRTUAL_BASE + (physical & 0xFFFU));
    framebuffer_pitch = width * 4U;
    framebuffer_width = width;
    framebuffer_height = height;

    mouse_set_resolution_scale(width, height);

    red_position = 16;
    red_mask_size = 8;
    green_position = 8;
    green_mask_size = 8;
    blue_position = 0;
    blue_mask_size = 8;

    cursor_x = (int)width / 2;
    cursor_y = (int)height / 2;
    terminal_x = (int)width / 2 - 440;

    if (terminal_x < 10)
        terminal_x = 10;

    terminal_y = 64;
    terminal_restore_x = terminal_x;
    terminal_restore_y = terminal_y;
    terminal_maximized = 0;
    terminal_dragging = 0;
    cursor_saved_valid = 0;
    active_panel = 0;

    return 1;
}

int graphics_terminal_is_maximized(void)
{
    return terminal_maximized;
}

void graphics_terminal_toggle_maximized(void)
{
    if (!initialized)
        return;

    if (!terminal_maximized)
    {
        terminal_restore_x = terminal_x;
        terminal_restore_y = terminal_y;
        terminal_maximized = 1;
        terminal_dragging = 0;
    }
    else
    {
        terminal_maximized = 0;
        terminal_x = terminal_restore_x;
        terminal_y = terminal_restore_y;
    }
}

int graphics_terminal_is_dragging(void)
{
    return terminal_dragging;
}

int graphics_explorer_is_dragging(void)
{
    return file_explorer_is_dragging();
}

int graphics_get_terminal_x(void)
{
    return terminal_x;
}

int graphics_get_terminal_y(void)
{
    return terminal_y;
}

uint32_t graphics_get_width(void)
{
    return framebuffer_width;
}

uint32_t graphics_get_height(void)
{
    return framebuffer_height;
}

uint32_t graphics_get_terminal_width(void)
{
    if (terminal_maximized)
        return framebuffer_width;

    return framebuffer_width < 900 ?
        framebuffer_width - 20 :
        880;
}

uint32_t graphics_get_terminal_height(void)
{
    if (terminal_maximized)
        return framebuffer_height;

    return framebuffer_height < 640 ?
        framebuffer_height - 20 :
        620;
}

void graphics_clear(uint32_t color)
{
    if (!initialized)
        return;

    uint32_t packed = graphics_pack_color(color);

    for (uint32_t y = 0; y < framebuffer_height; y++)
    {
        volatile uint32_t* row =
            (volatile uint32_t*)(framebuffer + y * framebuffer_pitch);

        for (uint32_t x = 0; x < framebuffer_width; x++)
            row[x] = packed;
    }
}

void graphics_fill_rect(
    int x,
    int y,
    int width,
    int height,
    uint32_t color
)
{
    if (!initialized || width <= 0 || height <= 0)
        return;

    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width;
    int y1 = y + height;

    if (x1 > (int)framebuffer_width)
        x1 = (int)framebuffer_width;

    if (y1 > (int)framebuffer_height)
        y1 = (int)framebuffer_height;

    if (x0 >= x1 || y0 >= y1)
        return;

    uint32_t packed = graphics_pack_color(color);

    for (int py = y0; py < y1; py++)
    {
        volatile uint32_t* row =
            (volatile uint32_t*)(framebuffer + py * framebuffer_pitch);

        for (int px = x0; px < x1; px++)
            row[px] = packed;
    }
}

void graphics_fill_rounded_rect(
    int x,
    int y,
    int width,
    int height,
    int radius,
    uint32_t color
)
{
    if (!initialized || width <= 0 || height <= 0)
        return;

    if (radius <= 0)
    {
        graphics_fill_rect(x, y, width, height, color);
        return;
    }

    if (radius * 2 > width)
        radius = width / 2;

    if (radius * 2 > height)
        radius = height / 2;

    uint32_t packed = graphics_pack_color(color);

    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width;
    int y1 = y + height;

    if (x1 > (int)framebuffer_width)
        x1 = (int)framebuffer_width;

    if (y1 > (int)framebuffer_height)
        y1 = (int)framebuffer_height;

    int r2 = radius * radius;

    for (int py = y0; py < y1; py++)
    {
        volatile uint32_t* row =
            (volatile uint32_t*)(framebuffer + py * framebuffer_pitch);

        for (int px = x0; px < x1; px++)
        {
            int cx =
                px < x + radius ?
                x + radius :
                (px >= x + width - radius ?
                    x + width - radius - 1 :
                    px);

            int cy =
                py < y + radius ?
                y + radius :
                (py >= y + height - radius ?
                    y + height - radius - 1 :
                    py);

            int dx = px - cx;
            int dy = py - cy;

            if (dx * dx + dy * dy <= r2)
                row[px] = packed;
        }
    }
}
