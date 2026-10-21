/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "graphics_internal.h"
#include "paging.h"
#include "pci.h"
#include "mouse.h"
#include "file_explorer.h"
#include <stddef.h>
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
#define BOCHS_VBE_INDEX_Y_OFFSET 0x09
#define BOCHS_VBE_INDEX_VIDEO_MEMORY_64K 0x0A
#define BOCHS_VBE_INDEX_LFB       0x0D

#define BOCHS_VBE_ID 0xB0C5
#define BOCHS_VBE_ENABLE_LFB 0x0041
#define GRAPHICS_BACKBUFFER_BASE 0xD0800000U
#define GRAPHICS_SCANOUT_WINDOW_BYTES ((uintptr_t)GRAPHICS_BACKBUFFER_BASE - (uintptr_t)GRAPHICS_VIRTUAL_BASE)
#define GRAPHICS_VBLANK_POLL_LIMIT 1000000U

static uint8_t* graphics_scanout_base;
static uint64_t graphics_scanout_page_bytes;
static uint32_t graphics_scanout_page_index;
static int graphics_bochs_mode;
static int graphics_pageflip_enabled;
static int graphics_pageflip_seeded

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

static inline uint8_t bochs_vbe_read_status(void)
{
    uint8_t status;
    __asm__ volatile (
        "inb %1, %0"
        : "=a"(status)
        : "Nd"((uint16_t)0x03DA)
    );
    return status;
}

static int bochs_vbe_wait_vblank(void)
{
    uint32_t i;
    for (i = 0; i < GRAPHICS_VBLANK_POLL_LIMIT; i++)
        if ((bochs_vbe_read_status() & 0x08U) == 0U)
            break;
    if (i == GRAPHICS_VBLANK_POLL_LIMIT)
        return 0;

    for (i = 0; i < GRAPHICS_VBLANK_POLL_LIMIT; i++)
        if (bochs_vbe_read_status() & 0x08U)
            return 1;
    return 0;
}

static int bochs_vbe_can_pageflip(uint32_t width, uint32_t height,
                                  uint32_t offset, uint64_t vram_bytes)
{
    uint64_t page_bytes = (uint64_t)width * 4U * height;
    uint64_t total_bytes = page_bytes * 2U;
    return width != 0U && height != 0U &&
           height <= UINT16_MAX / 2U &&
           vram_bytes >= total_bytes + offset &&
           total_bytes + offset <= GRAPHICS_SCANOUT_WINDOW_BYTES;
}

static void graphics_copy_bytes(uint8_t* target, const uint8_t* source,
                                size_t bytes)
{
    volatile uint32_t* dst = (volatile uint32_t*)target;
    const uint32_t* src = (const uint32_t*)source;
    size_t words = bytes / sizeof(uint32_t);
    for (size_t i = 0; i < words; i++)
        dst[i] = src[i];
    for (size_t i = words * sizeof(uint32_t); i < bytes; i++)
        target[i] = source[i];
}

static void graphics_copy_damage_tiles(
    uint8_t* target, const uint8_t* source,
    uint32_t width, uint32_t height, uint32_t pitch,
    const uint8_t* damage_bitmap, size_t damage_bitmap_bytes,
    uint32_t tile_size, uint32_t tiles_x, uint32_t tiles_y)
{
    for (uint32_t ty = 0; ty < tiles_y; ty++)
    {
        uint32_t y0 = ty * tile_size;
        uint32_t y1 = y0 + tile_size;
        if (y1 > height) y1 = height;
        for (uint32_t tx = 0; tx < tiles_x; tx++)
        {
            uint32_t index = ty * tiles_x + tx;
            size_t byte_index = index >> 3;
            if (byte_index >= damage_bitmap_bytes ||
                (damage_bitmap[byte_index] &
                 (uint8_t)(1U << (index & 7U))) == 0U)
                continue;

            uint32_t x0 = tx * tile_size;
            uint32_t x1 = x0 + tile_size;
            if (x1 > width) x1 = width;
            for (uint32_t y = y0; y < y1; y++)
            {
                volatile uint32_t* dst =
                    (volatile uint32_t*)(target + (size_t)y * pitch);
                const uint32_t* src =
                    (const uint32_t*)(source + (size_t)y * pitch);
                for (uint32_t x = x0; x < x1; x++)
                    dst[x] = src[x];
            }
        }
    }
}

int graphics_display_present_damage(
    uint8_t** display_framebuffer, uint32_t display_pitch,
    const uint8_t* backbuffer, uint32_t width, uint32_t height,
    uint32_t backbuffer_pitch, const uint8_t* damage_bitmap,
    size_t damage_bitmap_bytes, uint32_t tile_size)
{
    if (!display_framebuffer || !*display_framebuffer || !backbuffer ||
        !damage_bitmap || width == 0U || height == 0U || tile_size == 0U ||
        display_pitch < width * 4U || backbuffer_pitch < width * 4U)
        return 0;

    uint32_t tiles_x = (width + tile_size - 1U) / tile_size;
    uint32_t tiles_y = (height + tile_size - 1U) / tile_size;
    uint64_t tile_count = (uint64_t)tiles_x * tiles_y;
    if (tile_count > (uint64_t)damage_bitmap_bytes * 8U)
        return 0;

    int has_damage = 0;
    for (uint64_t i = 0; i < tile_count; i++)
        if (damage_bitmap[i >> 3] & (uint8_t)(1U << (i & 7U)))
        {
            has_damage = 1;
            break;
        }
    if (!has_damage)
        return 1;

    if (graphics_pageflip_enabled && graphics_bochs_mode &&
        graphics_scanout_base && display_pitch == width * 4U &&
        backbuffer_pitch == width * 4U &&
        graphics_scanout_page_bytes == (uint64_t)display_pitch * height)
    {
        uint32_t next_index = graphics_scanout_page_index ^ 1U;
        uint8_t* front = graphics_scanout_base +
                         (size_t)graphics_scanout_page_index *
                         (size_t)graphics_scanout_page_bytes;
        uint8_t* hidden = graphics_scanout_base +
                          (size_t)next_index *
                          (size_t)graphics_scanout_page_bytes;

        if (!graphics_pageflip_seeded)
            graphics_copy_bytes(hidden, front,
                                (size_t)graphics_scanout_page_bytes);

        graphics_copy_damage_tiles(hidden, backbuffer, width, height,
                                   display_pitch, damage_bitmap,
                                   damage_bitmap_bytes, tile_size,
                                   tiles_x, tiles_y);

        if (bochs_vbe_wait_vblank())
        {
            bochs_vbe_write(BOCHS_VBE_INDEX_Y_OFFSET,
                            (uint16_t)(next_index * height));
            graphics_scanout_page_index = next_index;
            *display_framebuffer = hidden;
            graphics_copy_damage_tiles(front, backbuffer, width, height,
                                       display_pitch, damage_bitmap,
                                       damage_bitmap_bytes, tile_size,
                                       tiles_x, tiles_y);
            graphics_pageflip_seeded = 1;
            return 1;
        }

        /* Retrace unavailable: preserve the current image and disable flips. */
        graphics_pageflip_enabled = 0;
        if (graphics_scanout_page_index != 0U)
            graphics_copy_bytes(graphics_scanout_base, front,
                                (size_t)graphics_scanout_page_bytes);
        bochs_vbe_write(BOCHS_VBE_INDEX_Y_OFFSET, 0U);
        bochs_vbe_write(BOCHS_VBE_INDEX_VIRT_HEIGHT, (uint16_t)height);
        graphics_scanout_page_index = 0U;
        graphics_pageflip_seeded = 0;
        *display_framebuffer = graphics_scanout_base;
    }

    graphics_copy_damage_tiles(*display_framebuffer, backbuffer, width,
                               height, display_pitch, damage_bitmap,
                               damage_bitmap_bytes, tile_size, tiles_x,
                               tiles_y);
    return 1;
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

    uint64_t vram_bytes =
        (uint64_t)bochs_vbe_read(BOCHS_VBE_INDEX_VIDEO_MEMORY_64K) * 65536U;
    int pageflip = bochs_vbe_can_pageflip(1024U, 768U, 0U, vram_bytes);

    bochs_vbe_write(BOCHS_VBE_INDEX_ENABLE, 0);
    bochs_vbe_write(BOCHS_VBE_INDEX_XRES, 1024);
    bochs_vbe_write(BOCHS_VBE_INDEX_YRES, 768);
    bochs_vbe_write(BOCHS_VBE_INDEX_BPP, 32);
    bochs_vbe_write(BOCHS_VBE_INDEX_VIRT_WIDTH, 1024);
    bochs_vbe_write(BOCHS_VBE_INDEX_VIRT_HEIGHT,
                    pageflip ? 1536U : 768U);
    bochs_vbe_write(BOCHS_VBE_INDEX_Y_OFFSET, 0U);
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
    if (pageflip &&
        !bochs_vbe_can_pageflip(1024U, 768U, offset, vram_bytes))
    {
        pageflip = 0;
        bochs_vbe_write(BOCHS_VBE_INDEX_VIRT_HEIGHT, 768U);
    }
    uint64_t mapped_bytes = bytes * (pageflip ? 2U : 1U);

    uint32_t pages =
        (uint32_t)((offset + mapped_bytes + 4095U) / 4096U);

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
    graphics_scanout_base = framebuffer;
    graphics_scanout_page_bytes = bytes;
    graphics_scanout_page_index = 0U;
    graphics_bochs_mode = 1;
    graphics_pageflip_enabled = pageflip;
    graphics_pageflip_seeded = 0;
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

    graphics_bochs_mode = 0;
    graphics_pageflip_enabled = 0;
    graphics_pageflip_seeded = 0;
    graphics_scanout_page_index = 0U;
    graphics_scanout_page_bytes = 0U;
    graphics_scanout_base = 0;

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
            graphics_scanout_base = framebuffer;
            graphics_scanout_page_bytes =
                (uint64_t)mbd->framebuffer_pitch * mbd->framebuffer_height;
            graphics_scanout_page_index = 0U;
            graphics_bochs_mode = 0;
            graphics_pageflip_enabled = 0;
            graphics_pageflip_seeded = 0;
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

    if (active_panel != panel)
        graphics_invalidate_scene();
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
    graphics_invalidate_scene();
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
        int first = -1;
        int last = -1;
        for (uint32_t x = 0; x < framebuffer_width; x++)
        {
            if (row[x] != packed)
            {
                row[x] = packed;
                if (first < 0) first = (int)x;
                last = (int)x;
            }
        }
        if (first >= 0)
            graphics_mark_damage(first, (int)y, last - first + 1, 1);
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
        int first = -1;
        int last = -1;
        for (int px = x0; px < x1; px++)
        {
            if (row[px] != packed)
            {
                row[px] = packed;
                if (first < 0) first = px;
                last = px;
            }
        }
        if (first >= 0)
            graphics_mark_damage(first, py, last - first + 1, 1);
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
        int first = -1;
        int last = -1;
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

            if (dx * dx + dy * dy <= r2 && row[px] != packed)
            {
                row[px] = packed;
                if (first < 0) first = px;
                last = px;
            }
        }
        if (first >= 0)
            graphics_mark_damage(first, py, last - first + 1, 1);
    }
}
