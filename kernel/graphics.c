#include "graphics.h"
#include "paging.h"
#include "pci.h"
#include "filesystem.h"
#include "pmm.h"
#include "process.h"

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
static int start_menu_open;
static int terminal_close_requested;
static int terminal_maximized;
static int terminal_dragging;
static int terminal_x;
static int terminal_y;
static int terminal_restore_x;
static int terminal_restore_y;
static int terminal_drag_offset_x;
static int terminal_drag_offset_y;

#define CURSOR_SAVE_SIZE 20
static uint32_t cursor_saved[CURSOR_SAVE_SIZE * CURSOR_SAVE_SIZE];
static int cursor_saved_x;
static int cursor_saved_y;
static int cursor_saved_width;
static int cursor_saved_height;
static int cursor_saved_valid;

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

static const uint8_t lowercase_font[26][7] =
{
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F},
    {0x10,0x10,0x16,0x19,0x11,0x11,0x1E},
    {0x00,0x00,0x0E,0x11,0x10,0x11,0x0E},
    {0x01,0x01,0x0D,0x13,0x11,0x11,0x0F},
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E},
    {0x06,0x09,0x08,0x1E,0x08,0x08,0x08},
    {0x00,0x0F,0x11,0x11,0x0F,0x01,0x0E},
    {0x10,0x10,0x16,0x19,0x11,0x11,0x11},
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E},
    {0x02,0x00,0x06,0x02,0x02,0x12,0x0C},
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12},
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E},
    {0x00,0x00,0x1A,0x15,0x15,0x11,0x11},
    {0x00,0x00,0x16,0x19,0x11,0x11,0x11},
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E},
    {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10},
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01},
    {0x00,0x00,0x16,0x19,0x10,0x10,0x10},
    {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E},
    {0x08,0x08,0x1F,0x08,0x08,0x09,0x06},
    {0x00,0x00,0x11,0x11,0x11,0x13,0x0D},
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04},
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0A},
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11},
    {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E},
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}
};

static int lowercase_font_index(char c)
{
    if (c >= 'a' && c <= 'z')
        return c - 'a';

    return -1;
}

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
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');

    if (c >= 'A' && c <= 'Z')
        return c - 'A';

    if (c >= '0' && c <= '9')
        return 26 + (c - '0');

    return -1;
}

static const uint8_t glyph_slash[7] =
{
    0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10
};

static const uint8_t glyph_dot[7] =
{
    0, 0, 0, 0, 0, 0x0C, 0x0C
};

static const uint8_t glyph_dash[7] =
{
    0, 0, 0, 0x1F, 0, 0, 0
};

static const uint8_t glyph_underscore[7] =
{
    0, 0, 0, 0, 0, 0, 0x1F
};

static const uint8_t glyph_colon[7] =
{
    0, 0x0C, 0x0C, 0, 0, 0x0C, 0x0C
};

static const uint8_t glyph_plus[7] =
{
    0, 0x04, 0x04, 0x1F, 0x04, 0x04, 0
};

static const uint8_t glyph_equal[7] =
{
    0, 0x1F, 0, 0x1F, 0, 0, 0
};

static const uint8_t glyph_greater[7] =
{
    0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10
};

static const uint8_t glyph_less[7] =
{
    0x01, 0x02, 0x04, 0x08, 0x04, 0x02, 0x01
};

static const uint8_t glyph_bracket_open[7] =
{
    0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E
};

static const uint8_t glyph_bracket_close[7] =
{
    0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E
};

static const uint8_t* symbol_glyph(char c)
{
    switch (c)
    {
        case '/': return glyph_slash;
        case '.': return glyph_dot;
        case '-': return glyph_dash;
        case '_': return glyph_underscore;
        case ':': return glyph_colon;
        case '+': return glyph_plus;
        case '=': return glyph_equal;
        case '>': return glyph_greater;
        case '<': return glyph_less;
        case '[': return glyph_bracket_open;
        case ']': return glyph_bracket_close;
        default: return 0;
    }
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
     * Bochs/QEMU VBE exposes the linear framebuffer at the standard
     * PCI/VBE LFB address 0xE0000000.  The VBE DISPI register at 0x0D
     * is X_OFFSET, not the framebuffer address, so reading it here
     * produces a bogus physical address and can corrupt rendering.
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

    uint32_t offset = physical & 0xFFF;
    uint32_t first_page = physical & 0xFFFFF000U;
    uint32_t pitch = 1024 * 4;
    uint64_t bytes = (uint64_t)pitch * 768;

    uint32_t pages =
        (uint32_t)((offset + bytes + 4095) / 4096);

    if (pages == 0 || pages > GRAPHICS_MAX_PAGES)
        return 0;

    for (uint32_t i = 0; i < pages; i++)
    {
        if (!paging_map_page(
                GRAPHICS_VIRTUAL_BASE + i * 4096,
                first_page + i * 4096))
            return 0;
    }

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

    initialized = 1;
    return 1;
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
            terminal_x = (int)framebuffer_width / 2 - 440;
            terminal_y = 64;
            terminal_restore_x = terminal_x;
            terminal_restore_y = terminal_y;
            terminal_dragging = 0;

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

void graphics_select_panel(int panel)
{
    if (!initialized)
        return;

    if (panel < 0 || panel > 3)
        return;

    active_panel = panel;
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
        int lower_index = lowercase_font_index(c);
        const uint8_t* glyph = 0;

        if (lower_index >= 0)
            glyph = lowercase_font[lower_index];
        else if (index >= 0)
            glyph = font[index];
        else
            glyph = symbol_glyph(c);

        if (!glyph)
        {
            x += 6 * (int)scale;
            continue;
        }

        for (int row = 0; row < 7; row++)
        {
            uint8_t bits = glyph[row];

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

    int width = (int)framebuffer_width;
    int height = (int)framebuffer_height;
    int taskbar_y = height - 64;

    /* Start menu is a real launcher for the applications implemented below. */
    if (start_menu_open)
    {
        int menu_x = width / 2 - 230;
        int menu_y = height - 520;

        if (cursor_x >= menu_x + 24 &&
            cursor_x < menu_x + 436 &&
            cursor_y >= menu_y + 110 &&
            cursor_y < menu_y + 164)
        {
            active_panel = 3;
            start_menu_open = 0;
            return;
        }

        if (cursor_x >= menu_x + 24 &&
            cursor_x < menu_x + 436 &&
            cursor_y >= menu_y + 164 &&
            cursor_y < menu_y + 218)
        {
            active_panel = 2;
            start_menu_open = 0;
            return;
        }

        if (cursor_x >= menu_x + 24 &&
            cursor_x < menu_x + 436 &&
            cursor_y >= menu_y + 218 &&
            cursor_y < menu_y + 272)
        {
            active_panel = 1;
            start_menu_open = 0;
            return;
        }

        if (!(cursor_x >= menu_x &&
              cursor_x < menu_x + 460 &&
              cursor_y >= menu_y &&
              cursor_y < height - 8))
            start_menu_open = 0;

        return;
    }

    /* Start button. */
    if (cursor_x >= 18 &&
        cursor_x < 74 &&
        cursor_y >= taskbar_y + 8 &&
        cursor_y < taskbar_y + 56)
    {
        start_menu_open = 1;
        return;
    }

    /* Running application buttons. */
    if (cursor_y >= taskbar_y + 8 &&
        cursor_y < taskbar_y + 56)
    {
        if (cursor_x >= 86 && cursor_x < 154)
        {
            active_panel = 2;
            return;
        }

        if (cursor_x >= 162 && cursor_x < 230)
        {
            active_panel = 3;
            return;
        }
    }

    /* Terminal owns its complete title bar while open. */
    if (active_panel == 3)
    {
        int terminal_w = terminal_maximized ?
            width : 880;
        int terminal_x_current = terminal_maximized ?
            0 : terminal_x;
        int terminal_y_current = terminal_maximized ?
            0 : terminal_y;

        if (cursor_x >= terminal_x_current + terminal_w - 140 &&
            cursor_x < terminal_x_current + terminal_w - 96 &&
            cursor_y >= terminal_y_current + 4 &&
            cursor_y < terminal_y_current + 42)
        {
            terminal_close_requested = 1;
            active_panel = 0;
            return;
        }

        if (cursor_x >= terminal_x_current + terminal_w - 96 &&
            cursor_x < terminal_x_current + terminal_w - 48 &&
            cursor_y >= terminal_y_current + 4 &&
            cursor_y < terminal_y_current + 42)
        {
            graphics_terminal_toggle_maximized();
            return;
        }

        if (!terminal_maximized &&
            cursor_x >= terminal_x_current + 8 &&
            cursor_x < terminal_x_current + terminal_w - 140 &&
            cursor_y >= terminal_y_current + 4 &&
            cursor_y < terminal_y_current + 42)
        {
            terminal_dragging = 1;
            terminal_drag_offset_x = cursor_x - terminal_x;
            terminal_drag_offset_y = cursor_y - terminal_y;
            return;
        }

        if (cursor_x >= terminal_x_current + terminal_w - 48 &&
            cursor_x < terminal_x_current + terminal_w &&
            cursor_y >= terminal_y_current + 4 &&
            cursor_y < terminal_y_current + 42)
        {
            terminal_close_requested = 1;
            active_panel = 0;
            return;
        }

        return;
    }

    /* System and Files are real application windows. */
    if (active_panel == 1 || active_panel == 2)
    {
        int window_w = 760;
        int window_h = 480;
        int window_x = width / 2 - window_w / 2;
        int window_y = height / 2 - window_h / 2;

        if (cursor_x >= window_x + window_w - 52 &&
            cursor_x < window_x + window_w &&
            cursor_y >= window_y &&
            cursor_y < window_y + 44)
        {
            active_panel = 0;
            return;
        }

        return;
    }

    /* Desktop icons launch the same real applications as Start. */
    if (cursor_x >= 28 && cursor_x < 120 &&
        cursor_y >= 30 && cursor_y < 118)
    {
        active_panel = 1;
        return;
    }

    if (cursor_x >= 140 && cursor_x < 232 &&
        cursor_y >= 30 && cursor_y < 118)
    {
        active_panel = 2;
        return;
    }

    if (cursor_x >= 252 && cursor_x < 344 &&
        cursor_y >= 30 && cursor_y < 118)
    {
        active_panel = 3;
        return;
    }
}
void graphics_mouse_move(int dx, int dy)
{
    if (!initialized)
        return;

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

    if (terminal_dragging && !terminal_maximized)
    {
        terminal_x = cursor_x - terminal_drag_offset_x;
        terminal_y = cursor_y - terminal_drag_offset_y;

        if (terminal_x < 0)
            terminal_x = 0;
        if (terminal_y < 0)
            terminal_y = 0;

        int drag_width = 880;
        int drag_height = 620;

        if (drag_width > (int)framebuffer_width)
            drag_width = (int)framebuffer_width;
        if (drag_height > (int)framebuffer_height)
            drag_height = (int)framebuffer_height;

        if (terminal_x + drag_width > (int)framebuffer_width)
            terminal_x = (int)framebuffer_width - drag_width;

        if (terminal_y + drag_height > (int)framebuffer_height)
            terminal_y = (int)framebuffer_height - drag_height;

        if (terminal_x < 0)
            terminal_x = 0;
        if (terminal_y < 0)
            terminal_y = 0;
    }
}

void graphics_mouse_release(int button)
{
    if (!initialized || button != 1)
        return;

    terminal_dragging = 0;
}

static void graphics_cursor_restore(void)
{
    if (!initialized || !cursor_saved_valid)
        return;

    for (int y = 0; y < cursor_saved_height; y++)
    {
        volatile uint32_t* row =
            (volatile uint32_t*)(framebuffer +
                (cursor_saved_y + y) * framebuffer_pitch);

        for (int x = 0; x < cursor_saved_width; x++)
            row[cursor_saved_x + x] =
                cursor_saved[y * CURSOR_SAVE_SIZE + x];
    }

    cursor_saved_valid = 0;
}

void graphics_draw_cursor(void)
{
    if (!initialized)
        return;

    /* The logical pointer is the visual pointer. No host/window offset. */
    int x = cursor_x;
    int y = cursor_y;

    graphics_cursor_restore();

    cursor_saved_x = x;
    cursor_saved_y = y;
    cursor_saved_width = CURSOR_SAVE_SIZE;
    cursor_saved_height = CURSOR_SAVE_SIZE;

    if (cursor_saved_x + cursor_saved_width > (int)framebuffer_width)
        cursor_saved_width = (int)framebuffer_width - cursor_saved_x;

    if (cursor_saved_y + cursor_saved_height > (int)framebuffer_height)
        cursor_saved_height = (int)framebuffer_height - cursor_saved_y;

    if (cursor_saved_width <= 0 || cursor_saved_height <= 0)
        return;

    for (int py = 0; py < cursor_saved_height; py++)
    {
        volatile uint32_t* row =
            (volatile uint32_t*)(framebuffer +
                (cursor_saved_y + py) * framebuffer_pitch);

        for (int px = 0; px < cursor_saved_width; px++)
            cursor_saved[py * CURSOR_SAVE_SIZE + px] =
                row[cursor_saved_x + px];
    }

    cursor_saved_valid = 1;

    graphics_fill_rect(x, y, 2, 19, 0x00000000);
    graphics_fill_rect(x, y, 4, 2, 0x00000000);
    graphics_fill_rect(x + 2, y + 2, 4, 2, 0x00000000);
    graphics_fill_rect(x + 4, y + 4, 4, 2, 0x00000000);
    graphics_fill_rect(x + 6, y + 6, 4, 2, 0x00000000);
    graphics_fill_rect(x + 8, y + 8, 4, 2, 0x00000000);
    graphics_fill_rect(x + 10, y + 10, 4, 2, 0x00000000);
    graphics_fill_rect(x + 12, y + 12, 3, 2, 0x00000000);
    graphics_fill_rect(x + 12, y + 14, 2, 5, 0x00000000);
    graphics_fill_rect(x + 10, y + 16, 3, 2, 0x00000000);

    graphics_fill_rect(x + 2, y + 2, 2, 13, 0x00FFFFFF);
    graphics_fill_rect(x + 4, y + 4, 2, 13, 0x00FFFFFF);
    graphics_fill_rect(x + 6, y + 6, 2, 11, 0x00FFFFFF);
    graphics_fill_rect(x + 8, y + 8, 2, 9, 0x00FFFFFF);
    graphics_fill_rect(x + 10, y + 10, 2, 6, 0x00FFFFFF);
    graphics_fill_rect(x + 12, y + 12, 1, 3, 0x00FFFFFF);
}
void graphics_present(void)
{
    if (!initialized)
        return;

    graphics_cursor_restore();

    uint32_t w = framebuffer_width;
    uint32_t h = framebuffer_height;
    int taskbar_y = (int)h - 64;

    /* Windows-like desktop: simple wallpaper, real desktop shortcuts, real taskbar. */
    graphics_clear(0x000B1726);

    /* Subtle wallpaper bands; no non-functional widgets. */
    graphics_fill_rect(0, 0, (int)w, (int)h, 0x000B1726);
    graphics_fill_rect(0, 0, (int)w, (int)h / 2, 0x000D1D30);

    /* Desktop shortcuts. */
    graphics_fill_rect(28, 34, 64, 52, 0x001C4D7A);
    graphics_fill_rect(44, 46, 32, 24, 0x005AA9E6);
    graphics_draw_text(32, 94, "SYSTEM", 0x00FFFFFF, 1);

    graphics_fill_rect(140, 34, 64, 52, 0x001D5B3A);
    graphics_fill_rect(154, 46, 36, 28, 0x0058D68D);
    graphics_draw_text(148, 94, "FILES", 0x00FFFFFF, 1);

    graphics_fill_rect(252, 34, 64, 52, 0x002B3550);
    graphics_fill_rect(266, 46, 36, 28, 0x0099A8C2);
    graphics_draw_text(258, 94, "TERMINAL", 0x00FFFFFF, 1);

    /* System window. */
    if (active_panel == 1)
    {
        int window_w = 760;
        int window_h = 480;
        int window_x = (int)w / 2 - window_w / 2;
        int window_y = (int)h / 2 - window_h / 2;

        graphics_fill_rect(window_x + 8, window_y + 10,
                           window_w, window_h, 0x00000000);
        graphics_fill_rect(window_x, window_y,
                           window_w, window_h, 0x00151D27);
        graphics_fill_rect(window_x, window_y,
                           window_w, 44, 0x00202A36);

        graphics_draw_text(window_x + 18, window_y + 15,
                           "SYSTEM", 0x00FFFFFF, 2);
        graphics_draw_text(window_x + window_w - 28, window_y + 15,
                           "X", 0x00FFFFFF, 2);

        graphics_draw_text(window_x + 28, window_y + 78,
                           "VANTAOS SYSTEM", 0x003B82F6, 3);
        graphics_draw_text(window_x + 28, window_y + 124,
                           "GRAPHICS", 0x00D7DEE7, 2);

        char resolution[32];
        unsigned int rw = w;
        unsigned int rh = h;
        resolution[0] = 'R';
        resolution[1] = 'E';
        resolution[2] = 'S';
        resolution[3] = ':';
        resolution[4] = ' ';
        resolution[5] = (char)('0' + ((rw / 1000) % 10));
        resolution[6] = (char)('0' + ((rw / 100) % 10));
        resolution[7] = (char)('0' + ((rw / 10) % 10));
        resolution[8] = (char)('0' + (rw % 10));
        resolution[9] = 'x';
        resolution[10] = (char)('0' + ((rh / 1000) % 10));
        resolution[11] = (char)('0' + ((rh / 100) % 10));
        resolution[12] = (char)('0' + ((rh / 10) % 10));
        resolution[13] = (char)('0' + (rh % 10));
        resolution[14] = 0;

        graphics_draw_text(window_x + 28, window_y + 154,
                           resolution, 0x009AA8B8, 1);
        graphics_draw_text(window_x + 28, window_y + 190,
                           "FILESYSTEM", 0x00D7DEE7, 2);
        graphics_draw_text(window_x + 28, window_y + 220,
                           filesystem_is_initialized() ?
                           "ONLINE" : "OFFLINE",
                           0x0058D68D, 1);
        graphics_draw_text(window_x + 28, window_y + 256,
                           "PROCESS MANAGER", 0x00D7DEE7, 2);
        graphics_draw_text(window_x + 28, window_y + 286,
                           process_is_initialized() ?
                           "ONLINE" : "OFFLINE",
                           0x0058D68D, 1);
        graphics_draw_text(window_x + 28, window_y + 322,
                           "FRAMEBUFFER", 0x00D7DEE7, 2);
        graphics_draw_text(window_x + 28, window_y + 352,
                           "ACTIVE", 0x0058D68D, 1);
    }

    /* Files window. */
    if (active_panel == 2)
    {
        int window_w = 760;
        int window_h = 480;
        int window_x = (int)w / 2 - window_w / 2;
        int window_y = (int)h / 2 - window_h / 2;

        graphics_fill_rect(window_x + 8, window_y + 10,
                           window_w, window_h, 0x00000000);
        graphics_fill_rect(window_x, window_y,
                           window_w, window_h, 0x00151D27);
        graphics_fill_rect(window_x, window_y,
                           window_w, 44, 0x00202A36);

        graphics_draw_text(window_x + 18, window_y + 15,
                           "FILES", 0x00FFFFFF, 2);
        graphics_draw_text(window_x + window_w - 28, window_y + 15,
                           "X", 0x00FFFFFF, 2);

        graphics_draw_text(window_x + 28, window_y + 78,
                           "VANTAOS FILES", 0x003B82F6, 3);
        graphics_draw_text(window_x + 28, window_y + 118,
                           "ROOT", 0x00D7DEE7, 2);

        uint32_t ids[18];
        int count = filesystem_list(filesystem_root(), ids, 18);

        if (count < 0)
            count = 0;
        if (count > 18)
            count = 18;

        int file_y = window_y + 154;

        for (int i = 0; i < count; i++)
        {
            const fs_node_t* node = filesystem_get_node(ids[i]);
            if (!node)
                continue;

            char name[FS_NAME_MAX + 1];
            unsigned int n = 0;

            while (node->name[n] && n < FS_NAME_MAX)
            {
                char ch = node->name[n];
                if (ch >= 'a' && ch <= 'z')
                    ch = (char)(ch - 'a' + 'A');
                name[n++] = ch;
            }
            name[n] = 0;

            graphics_fill_rect(window_x + 30, file_y - 3,
                               12, 12,
                               node->type == FS_NODE_DIRECTORY ?
                               0x005AA9E6 : 0x009AA8C2);
            graphics_draw_text(window_x + 54, file_y,
                               name, 0x00FFFFFF, 1);
            file_y += 22;

            if (file_y > window_y + 420)
                break;
        }

        if (count == 0)
            graphics_draw_text(window_x + 30, file_y,
                               "EMPTY", 0x009AA8B8, 1);
    }

    /* Terminal is still handled by the real terminal window renderer. */
    if (active_panel == 3)
        return;

    /* Windows 11-style taskbar: only real application launchers are shown. */
    graphics_fill_rect(0, taskbar_y, (int)w, 64, 0x00161D27);
    graphics_fill_rect(0, taskbar_y, (int)w, 1, 0x00304458);

    graphics_fill_rect(18, taskbar_y + 8, 48, 48, 0x003B82F6);
    graphics_fill_rect(29, taskbar_y + 19, 10, 10, 0x00FFFFFF);
    graphics_fill_rect(42, taskbar_y + 19, 10, 10, 0x00FFFFFF);
    graphics_fill_rect(29, taskbar_y + 32, 10, 10, 0x00FFFFFF);
    graphics_fill_rect(42, taskbar_y + 32, 10, 10, 0x00FFFFFF);

    graphics_fill_rect(86, taskbar_y + 8, 68, 48,
                       active_panel == 2 ? 0x002A4A63 : 0x00202A36);
    graphics_draw_text(101, taskbar_y + 25, "FILES", 0x00FFFFFF, 1);

    graphics_fill_rect(162, taskbar_y + 8, 68, 48,
                       active_panel == 3 ? 0x002A4A63 : 0x00202A36);
    graphics_draw_text(172, taskbar_y + 25, "TERM", 0x00FFFFFF, 1);

    graphics_draw_text(252, taskbar_y + 25,
                       "VANTAOS", 0x009AA8B8, 1);

    if (start_menu_open)
    {
        int menu_w = 460;
        int menu_h = 500;
        int menu_x = (int)w / 2 - menu_w / 2;
        int menu_y = (int)h - menu_h - 8;

        graphics_fill_rect(menu_x + 8, menu_y + 10,
                           menu_w, menu_h, 0x00000000);
        graphics_fill_rect(menu_x, menu_y,
                           menu_w, menu_h, 0x001A222D);

        graphics_draw_text(menu_x + 24, menu_y + 28,
                           "VANTAOS", 0x00FFFFFF, 3);
        graphics_draw_text(menu_x + 24, menu_y + 66,
                           "APPLICATIONS", 0x009AA8B8, 1);

        graphics_fill_rect(menu_x + 24, menu_y + 110,
                           412, 54, 0x00212C3A);
        graphics_draw_text(menu_x + 42, menu_y + 129,
                           "TERMINAL", 0x00FFFFFF, 2);

        graphics_fill_rect(menu_x + 24, menu_y + 164,
                           412, 54, 0x00212C3A);
        graphics_draw_text(menu_x + 42, menu_y + 183,
                           "FILES", 0x00FFFFFF, 2);

        graphics_fill_rect(menu_x + 24, menu_y + 218,
                           412, 54, 0x00212C3A);
        graphics_draw_text(menu_x + 42, menu_y + 237,
                           "SYSTEM", 0x00FFFFFF, 2);

        graphics_draw_text(menu_x + 24, menu_y + 460,
                           "Select an application", 0x009AA8B8, 1);
    }

    graphics_draw_cursor();
}
