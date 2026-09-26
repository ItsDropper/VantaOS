#include "graphics.h"
#include "paging.h"

#include <stdint.h>

#define GRAPHICS_VIRTUAL_BASE 0x40800000U
#define GRAPHICS_MAX_PAGES 1024

static uint8_t* framebuffer;
static uint32_t framebuffer_pitch;
static uint32_t framebuffer_width;
static uint32_t framebuffer_height;

static uint8_t red_position;
static uint8_t red_mask_size;
static uint8_t green_position;
static uint8_t green_mask_size;
static uint8_t blue_position;
static uint8_t blue_mask_size;

static int initialized;
static int cursor_x;
static int cursor_y;
static int active_panel;

static const uint8_t font[36][7] =
{
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0F,0x10,0x10,0x10,0x10,0x10,0x0F},
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0F,0x10,0x10,0x17,0x11,0x11,0x0F},
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F},
    {0x01,0x01,0x01,0x01,0x11,0x11,0x0E},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    {0x11,0x19,0x19,0x15,0x13,0x13,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A},
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    {0x04,0x0C,0x14,0x04,0x04,0x04,0x1F},
    {0x1E,0x01,0x01,0x0E,0x10,0x10,0x1F},
    {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E},
    {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E},
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}
};

static uint32_t pack_color(uint32_t color)
{
    uint32_t red = (color >> 16) & 0xFF;
    uint32_t green = (color >> 8) & 0xFF;
    uint32_t blue = color & 0xFF;

    if (red_mask_size < 8)
        red >>= 8 - red_mask_size;
    if (green_mask_size < 8)
        green >>= 8 - green_mask_size;
    if (blue_mask_size < 8)
        blue >>= 8 - blue_mask_size;

    return (red << red_position) |
           (green << green_position) |
           (blue << blue_position);
}

static int font_index(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';

    if (c >= '0' && c <= '9')
        return 26 + (c - '0');

    return -1;
}

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

static inline void bochs_vbe_write(uint16_t index, uint16_t value)
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

static int graphics_initialize_bochs(void)
{
    /*
     * GRUB is now explicitly asked for a 1024x768x32 Multiboot
     * framebuffer. Keep the Bochs VBE fallback disabled until we have
     * a real physical LFB address instead of guessing one.
     */
    return 0;
}

int graphics_initialize(multiboot_info_t* mbd)
{
    initialized = 0;

    if (mbd &&
        (mbd->flags & MULTIBOOT_INFO_FRAMEBUFFER) &&
        mbd->framebuffer_addr <= 0xFFFFFFFFULL &&
        mbd->framebuffer_type == 1 &&
        mbd->framebuffer_bpp == 32 &&
        mbd->framebuffer_width != 0 &&
        mbd->framebuffer_height != 0)
    {
        uint32_t physical = (uint32_t)mbd->framebuffer_addr;
        uint32_t offset = physical & 0xFFF;
        uint32_t first_page = physical & 0xFFFFF000U;

        uint64_t bytes =
            (uint64_t)mbd->framebuffer_pitch *
            mbd->framebuffer_height;

        uint32_t pages =
            (uint32_t)((offset + bytes + 4095) / 4096);

        if (pages != 0 && pages <= GRAPHICS_MAX_PAGES)
        {
            for (uint32_t i = 0; i < pages; i++)
            {
                if (!paging_map_page(
                        GRAPHICS_VIRTUAL_BASE + i * 4096,
                        first_page + i * 4096))
                    return 0;
            }

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

            initialized = 1;
            return 1;
        }
    }

    return graphics_initialize_bochs();
}

int graphics_is_initialized(void)
{
    return initialized;
}

uint32_t graphics_get_width(void)
{
    return framebuffer_width;
}

uint32_t graphics_get_height(void)
{
    return framebuffer_height;
}

void graphics_clear(uint32_t color)
{
    if (!initialized)
        return;

    uint32_t packed = pack_color(color);

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
        x1 = framebuffer_width;
    if (y1 > (int)framebuffer_height)
        y1 = framebuffer_height;

    if (x0 >= x1 || y0 >= y1)
        return;

    uint32_t packed = pack_color(color);

    for (int py = y0; py < y1; py++)
    {
        volatile uint32_t* row =
            (volatile uint32_t*)(framebuffer + py * framebuffer_pitch);

        for (int px = x0; px < x1; px++)
            row[px] = packed;
    }
}

void graphics_draw_text(
    int x,
    int y,
    const char* text,
    uint32_t color,
    unsigned int scale
)
{
    if (!initialized || !text || scale == 0)
        return;

    int start_x = x;

    while (*text)
    {
        char c = *text++;

        if (c == '\n')
        {
            y += 8 * (int)scale;
            x = start_x;
            continue;
        }

        if (c == ' ')
        {
            x += 6 * (int)scale;
            continue;
        }

        int index = font_index(c);

        if (index < 0)
        {
            x += 6 * (int)scale;
            continue;
        }

        for (int row = 0; row < 7; row++)
        {
            uint8_t bits = font[index][row];

            for (int col = 0; col < 5; col++)
            {
                if (bits & (1U << (4 - col)))
                    graphics_fill_rect(
                        x + col * (int)scale,
                        y + row * (int)scale,
                        (int)scale,
                        (int)scale,
                        color
                    );
            }
        }

        x += 6 * (int)scale;
    }
}

void graphics_mouse_click(int button)
{
    if (!initialized || button != 1)
        return;

    int window_x = (int)framebuffer_width / 2 - 300;
    int window_y = 120;
    int card_y = window_y + 215;

    if (cursor_x >= window_x + 28 &&
        cursor_x < window_x + 193 &&
        cursor_y >= card_y &&
        cursor_y < card_y + 80)
        active_panel = 1;
    else if (cursor_x >= window_x + 210 &&
             cursor_x < window_x + 375 &&
             cursor_y >= card_y &&
             cursor_y < card_y + 80)
        active_panel = 2;
    else if (cursor_x >= window_x + 392 &&
             cursor_x < window_x + 557 &&
             cursor_y >= card_y &&
             cursor_y < card_y + 80)
        active_panel = 3;
    else
        return;
}

void graphics_mouse_move(int dx, int dy)
{
    if (!initialized)
        return;

    /*
     * PS/2 coordinates are relative. Keep the mapping 1:1 with
     * the framebuffer so the software cursor follows the host
     * pointer without artificial scaling or offset.
     */
    cursor_x += dx;
    cursor_y -= dy;

    if (cursor_x < 0)
        cursor_x = 0;
    if (cursor_y < 0)
        cursor_y = 0;

    if (cursor_x >= (int)framebuffer_width)
        cursor_x = (int)framebuffer_width - 1;
    if (cursor_y >= (int)framebuffer_height)
        cursor_y = (int)framebuffer_height - 1;
}

void graphics_draw_cursor(void)
{
    if (!initialized)
        return;

    graphics_fill_rect(cursor_x, cursor_y, 2, 20, 0xFFFFFFFF);
    graphics_fill_rect(cursor_x, cursor_y, 12, 2, 0xFFFFFFFF);
    graphics_fill_rect(cursor_x + 2, cursor_y + 4, 8, 2, 0xFFFFFFFF);
    graphics_fill_rect(cursor_x + 2, cursor_y + 6, 6, 2, 0xFFFFFFFF);
    graphics_fill_rect(cursor_x + 2, cursor_y + 8, 4, 2, 0xFFFFFFFF);
    graphics_fill_rect(cursor_x + 2, cursor_y + 10, 2, 8, 0xFFFFFFFF);
}

void graphics_present(void)
{
    if (!initialized)
        return;

    uint32_t w = framebuffer_width;
    uint32_t h = framebuffer_height;

    graphics_clear(0x00101820);

    graphics_fill_rect(0, 0, (int)w, 52, 0x0018202C);
    graphics_fill_rect(0, 52, (int)w, 2, 0x002A3544);

    graphics_fill_rect(0, (int)h - 64, (int)w, 64, 0x0018202C);
    graphics_fill_rect(0, (int)h - 66, (int)w, 2, 0x002A3544);

    graphics_fill_rect(18, 14, 24, 24, 0x003B82F6);
    graphics_fill_rect(24, 20, 12, 12, 0x00FFFFFF);

    graphics_draw_text(56, 17, "VANTAOS", 0x00FFFFFF, 3);
    graphics_draw_text((int)w - 112, 18, "SYSTEM", 0x009AA8B8, 2);

    int window_x = (int)w / 2 - 300;
    int window_y = 120;
    int window_w = 600;
    int window_h = 390;

    graphics_fill_rect(
        window_x + 6, window_y + 8,
        window_w, window_h, 0x000A0E14
    );

    graphics_fill_rect(
        window_x, window_y,
        window_w, window_h, 0x00151D27
    );

    graphics_fill_rect(
        window_x, window_y,
        window_w, 42, 0x00212C3A
    );

    graphics_fill_rect(
        window_x + 18, window_y + 13,
        14, 14, 0x003B82F6
    );

    graphics_draw_text(
        window_x + 48, window_y + 12,
        "TERMINAL", 0x00FFFFFF, 2
    );

    graphics_fill_rect(
        window_x + window_w - 78, window_y + 13,
        12, 12, 0x009AA8B8
    );

    graphics_fill_rect(
        window_x + window_w - 52, window_y + 13,
        12, 12, 0x009AA8B8
    );

    graphics_fill_rect(
        window_x + 28, window_y + 72,
        window_w - 56, 2, 0x002A3544
    );

    graphics_draw_text(
        window_x + 28, window_y + 98,
        "VANTAOS", 0x003B82F6, 3
    );

    graphics_draw_text(
        window_x + 28, window_y + 134,
        "SYSTEM", 0x00D7DEE7, 2
    );

    graphics_draw_text(
        window_x + 28, window_y + 164,
        "FILES", 0x009AA8B8, 2
    );

    if (active_panel == 1)
        graphics_draw_text(window_x + 28, window_y + 190, "SYSTEM READY", 0x0058D68D, 1);
    else if (active_panel == 2)
        graphics_draw_text(window_x + 28, window_y + 190, "FILES READY", 0x0058D68D, 1);
    else if (active_panel == 3)
        graphics_draw_text(window_x + 28, window_y + 190, "ALL SYSTEMS ONLINE", 0x0058D68D, 1);

    int card_y = window_y + 215;

    int hover_system =
        cursor_x >= window_x + 28 &&
        cursor_x < window_x + 193 &&
        cursor_y >= card_y &&
        cursor_y < card_y + 80;

    int hover_files =
        cursor_x >= window_x + 210 &&
        cursor_x < window_x + 375 &&
        cursor_y >= card_y &&
        cursor_y < card_y + 80;

    int hover_status =
        cursor_x >= window_x + 392 &&
        cursor_x < window_x + 557 &&
        cursor_y >= card_y &&
        cursor_y < card_y + 80;

    graphics_fill_rect(
        window_x + 28, card_y,
        165, 80,
        hover_system ? 0x002A3A4C : 0x001E2936
    );

    graphics_fill_rect(
        window_x + 210, card_y,
        165, 80,
        hover_files ? 0x002A3A4C : 0x001E2936
    );

    graphics_fill_rect(
        window_x + 392, card_y,
        165, 80,
        hover_status ? 0x002A3A4C : 0x001E2936
    );

    graphics_draw_text(
        window_x + 48, card_y + 18,
        "SYSTEM", 0x00FFFFFF, 2
    );

    graphics_draw_text(
        window_x + 230, card_y + 18,
        "FILES", 0x00FFFFFF, 2
    );

    graphics_draw_text(
        window_x + 412, card_y + 18,
        "STATUS", 0x00FFFFFF, 2
    );

    graphics_draw_text(
        window_x + 412, card_y + 48,
        "ONLINE", 0x0058D68D, 1
    );

    graphics_fill_rect(
        22, (int)h - 52,
        40, 40, 0x003B82F6
    );

    graphics_fill_rect(
        32, (int)h - 42,
        20, 20, 0x00FFFFFF
    );

    graphics_draw_text(
        82, (int)h - 47,
        "VANTAOS", 0x00FFFFFF, 2
    );

    graphics_draw_cursor();
}
