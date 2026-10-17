#include <stddef.h>
#include "graphics_internal.h"
#include "pmm.h"
#include "paging.h"

uint8_t* framebuffer;
uint32_t framebuffer_pitch;
uint32_t framebuffer_width;
uint32_t framebuffer_height;

uint8_t red_position;
uint8_t red_mask_size;
uint8_t green_position;
uint8_t green_mask_size;
uint8_t blue_position;
uint8_t blue_mask_size;

int initialized;
int cursor_x;
int cursor_y;
int active_panel;
int start_menu_open;
uint32_t files_current_dir;
int files_open_file = -1;
int terminal_close_requested;
int terminal_running;
int terminal_open_requested;
int terminal_maximized;
int terminal_dragging;
int terminal_x;
int terminal_y;
int terminal_restore_x;
int terminal_restore_y;
int terminal_drag_offset_x;
int terminal_drag_offset_y;
int explorer_dragging;
int explorer_x;
int explorer_y;
int explorer_drag_offset_x;
int explorer_drag_offset_y;
int explorer_maximized;
int explorer_restore_x;
int explorer_restore_y;
int settings_resolution_index;

const uint32_t settings_widths[3] = {800, 1024, 1280};
const uint32_t settings_heights[3] = {600, 768, 720};

uint32_t cursor_saved[20 * 20];
int cursor_saved_x;
int cursor_saved_y;
int cursor_saved_width;
int cursor_saved_height;
int cursor_saved_valid;

/*
 * Keep the off-screen desktop buffer outside the kernel ELF's BSS. The UEFI
 * loader reserves the kernel's complete physical image range at a fixed
 * address, so a multi-megabyte static pixel array can make that allocation
 * fail on some firmware memory maps.
 */
#define GRAPHICS_BACKBUFFER_BASE 0xD0800000U
#define GRAPHICS_BACKBUFFER_MAX_WIDTH 1600U
#define GRAPHICS_BACKBUFFER_MAX_HEIGHT 900U
#define GRAPHICS_BACKBUFFER_MAX_BYTES \
    (GRAPHICS_BACKBUFFER_MAX_WIDTH * GRAPHICS_BACKBUFFER_MAX_HEIGHT * 4U)
#define GRAPHICS_BACKBUFFER_MAX_PAGES \
    ((GRAPHICS_BACKBUFFER_MAX_BYTES + PAGE_SIZE - 1U) / PAGE_SIZE)
static uintptr_t graphics_backbuffer_pages[GRAPHICS_BACKBUFFER_MAX_PAGES];
static uint32_t graphics_backbuffer_page_count;
static uint8_t* graphics_display_framebuffer;
static uint32_t graphics_display_pitch;
static int graphics_frame_active;

static int graphics_ensure_backbuffer(uint32_t required_pages)
{
    if (required_pages == 0U ||
        required_pages > GRAPHICS_BACKBUFFER_MAX_PAGES ||
        !pmm_is_initialized())
        return 0;
    if (graphics_backbuffer_page_count >= required_pages)
        return 1;

    uint32_t original_count = graphics_backbuffer_page_count;
    for (uint32_t i = original_count; i < required_pages; i++)
    {
        void* block = pmm_alloc_block();
        if (!block)
            goto rollback;
        uintptr_t physical = (uintptr_t)block;
        uintptr_t virtual_address = GRAPHICS_BACKBUFFER_BASE +
                                    (uintptr_t)i * PAGE_SIZE;
        if (!paging_map_page(virtual_address, physical))
        {
            pmm_free_block(block);
            goto rollback;
        }
        graphics_backbuffer_pages[i] = physical;
        graphics_backbuffer_page_count = i + 1U;
    }
    return 1;

rollback:
    while (graphics_backbuffer_page_count > original_count)
    {
        uint32_t i = --graphics_backbuffer_page_count;
        paging_unmap_page(GRAPHICS_BACKBUFFER_BASE +
                          (uintptr_t)i * PAGE_SIZE);
        pmm_free_block((void*)graphics_backbuffer_pages[i]);
        graphics_backbuffer_pages[i] = 0U;
    }
    return 0;
}

void graphics_begin_frame(void)
{
    if (!initialized || graphics_frame_active)
        return;
    if (framebuffer_width == 0U || framebuffer_height == 0U ||
        framebuffer_width > GRAPHICS_BACKBUFFER_MAX_WIDTH ||
        framebuffer_height > GRAPHICS_BACKBUFFER_MAX_HEIGHT ||
        framebuffer_pitch < framebuffer_width * 4U)
        return;

    uint32_t backbuffer_pitch = framebuffer_width * 4U;
    uint64_t bytes = (uint64_t)backbuffer_pitch * framebuffer_height;
    uint32_t required_pages = (uint32_t)((bytes + PAGE_SIZE - 1U) / PAGE_SIZE);
    if (!graphics_ensure_backbuffer(required_pages))
        return;

    graphics_display_framebuffer = framebuffer;
    graphics_display_pitch = framebuffer_pitch;
    uint8_t* backbuffer = (uint8_t*)(uintptr_t)GRAPHICS_BACKBUFFER_BASE;
    for (uint32_t y = 0; y < framebuffer_height; y++)
    {
        const uint8_t* source = graphics_display_framebuffer +
                                (size_t)y * graphics_display_pitch;
        uint8_t* target = backbuffer + (size_t)y * backbuffer_pitch;
        for (uint32_t x = 0; x < backbuffer_pitch; x++)
            target[x] = source[x];
    }
    framebuffer = backbuffer;
    framebuffer_pitch = backbuffer_pitch;
    graphics_frame_active = 1;
}

void graphics_end_frame(void)
{
    if (!graphics_frame_active)
        return;

    uint32_t row_bytes = framebuffer_width * 4U;
    uint8_t* backbuffer = (uint8_t*)(uintptr_t)GRAPHICS_BACKBUFFER_BASE;
    for (uint32_t y = 0; y < framebuffer_height; y++)
    {
        uint8_t* target = graphics_display_framebuffer +
                          (size_t)y * graphics_display_pitch;
        const uint8_t* source = backbuffer + (size_t)y * row_bytes;
        for (uint32_t x = 0; x < row_bytes; x++)
            target[x] = source[x];
    }
    framebuffer = graphics_display_framebuffer;
    framebuffer_pitch = graphics_display_pitch;
    graphics_frame_active = 0;
}


void graphics_clear_terminal_close_requested(void)
{
    terminal_close_requested = 0;
}

int graphics_terminal_open_requested(void)
{
    return terminal_open_requested;
}

void graphics_request_terminal_open(void)
{
    terminal_open_requested = 1;
}

void graphics_clear_terminal_open_request(void)
{
    terminal_open_requested = 0;
}
