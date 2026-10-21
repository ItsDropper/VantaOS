/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include <stddef.h>
#include "graphics_internal.h"
#include "graphics_damage.h"
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
#define GRAPHICS_DAMAGE_TILE_SIZE 16U
#define GRAPHICS_DAMAGE_MAX_TILES_X \
    ((GRAPHICS_BACKBUFFER_MAX_WIDTH + GRAPHICS_DAMAGE_TILE_SIZE - 1U) / GRAPHICS_DAMAGE_TILE_SIZE)
#define GRAPHICS_DAMAGE_MAX_TILES_Y \
    ((GRAPHICS_BACKBUFFER_MAX_HEIGHT + GRAPHICS_DAMAGE_TILE_SIZE - 1U) / GRAPHICS_DAMAGE_TILE_SIZE)
#define GRAPHICS_DAMAGE_BITMAP_BYTES \
    ((GRAPHICS_DAMAGE_MAX_TILES_X * GRAPHICS_DAMAGE_MAX_TILES_Y + 7U) / 8U)
static uintptr_t graphics_backbuffer_pages[GRAPHICS_BACKBUFFER_MAX_PAGES];
static uint32_t graphics_backbuffer_page_count;
static uint8_t* graphics_display_framebuffer;
static uint32_t graphics_display_pitch;
static uint32_t graphics_backbuffer_width;
static uint32_t graphics_backbuffer_height;
static int graphics_backbuffer_valid;
static int graphics_frame_active;
static int graphics_scene_invalidated = 1;
/* A persistent shadow buffer plus a damage bitmap avoids flashing the whole
 * display for small UI changes. Tiles are copied to the scanout buffer only
 * when rendering actually changed their pixels. */
static uint8_t graphics_damage_bitmap[GRAPHICS_DAMAGE_BITMAP_BYTES];

void graphics_mark_damage(int x, int y, int width, int height)
{
    if (!graphics_frame_active)
        return;

    graphics_damage_tiles_t damage;
    if (!graphics_damage_compute_tiles(
            x, y, width, height, framebuffer_width, framebuffer_height,
            GRAPHICS_DAMAGE_TILE_SIZE, &damage))
        return;

    uint32_t tiles_x =
        (framebuffer_width + GRAPHICS_DAMAGE_TILE_SIZE - 1U) /
        GRAPHICS_DAMAGE_TILE_SIZE;

    for (uint32_t ty = damage.first_y;
         ty < damage.first_y + damage.count_y; ty++)
    {
        for (uint32_t tx = damage.first_x;
             tx < damage.first_x + damage.count_x; tx++)
        {
            uint32_t index = ty * tiles_x + tx;
            graphics_damage_bitmap[index >> 3] |=
                (uint8_t)(1U << (index & 7U));
        }
    }
}

static void graphics_damage_clear(void)
{
    for (size_t i = 0; i < sizeof(graphics_damage_bitmap); i++)
        graphics_damage_bitmap[i] = 0U;
}

void graphics_invalidate_scene(void)
{
    graphics_scene_invalidated = 1;
}

void graphics_reset_backbuffer(void)
{
    graphics_backbuffer_valid = 0;
    graphics_scene_invalidated = 1;
    cursor_saved_valid = 0;
}

/* Consume the scene invalidation once. Clock/taskbar updates can then redraw
 * just their changed pixels without repainting the static wallpaper. */
int graphics_take_scene_invalidation(void)
{
    int invalidated = graphics_scene_invalidated;
    graphics_scene_invalidated = 0;
    return invalidated;
}

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

int graphics_panic_prepare(void)
{
    if (!initialized || !framebuffer || framebuffer_width == 0U ||
        framebuffer_height == 0U || framebuffer_pitch < framebuffer_width * 4U)
        return 0;

    /* If a compositor frame was interrupted, abandon it and target scanout. */
    graphics_frame_active = 0;
    if (graphics_display_framebuffer && graphics_display_pitch >= framebuffer_width * 4U)
    {
        framebuffer = graphics_display_framebuffer;
        framebuffer_pitch = graphics_display_pitch;
    }
    cursor_saved_valid = 0;
    graphics_scene_invalidated = 1;
    return 1;
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

    /* The shadow surface persists between frames. Seed it once, and reseed
     * only when the display mode changes; never read back the whole scanout
     * framebuffer on every present. */
    if (!graphics_backbuffer_valid ||
        graphics_backbuffer_width != framebuffer_width ||
        graphics_backbuffer_height != framebuffer_height)
    {
        for (uint32_t y = 0; y < framebuffer_height; y++)
        {
            const uint8_t* source = graphics_display_framebuffer +
                                    (size_t)y * graphics_display_pitch;
            uint8_t* target = backbuffer + (size_t)y * backbuffer_pitch;
            for (uint32_t x = 0; x < backbuffer_pitch; x++)
                target[x] = source[x];
        }
        graphics_backbuffer_width = framebuffer_width;
        graphics_backbuffer_height = framebuffer_height;
        graphics_backbuffer_valid = 1;
    }

    graphics_damage_clear();
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

    /*
     * The display backend uses damage tiles for a normal framebuffer copy,
     * or a VBlank-gated page flip when the Bochs VBE backend proves support.
     * Unknown/GOP framebuffers never receive page-flip or GPU assumptions.
     */
    (void)graphics_display_present_damage(
        &graphics_display_framebuffer, graphics_display_pitch,
        backbuffer, framebuffer_width, framebuffer_height, row_bytes,
        graphics_damage_bitmap, sizeof(graphics_damage_bitmap),
        GRAPHICS_DAMAGE_TILE_SIZE);

    framebuffer = graphics_display_framebuffer;
    framebuffer_pitch = graphics_display_pitch;
    graphics_frame_active = 0;
    graphics_damage_clear();
}

/*
 * Cursor-only updates use a tiny damage footprint but still pass through the
 * shadow surface. Scanout is updated only by graphics_end_frame().
 */
void graphics_update_cursor(void)
{
    if (!initialized || graphics_frame_active)
        return;

    /*
     * Cursor motion uses the same shadow surface and damage commit as every
     * other scene update. Writing save-under pixels straight to scanout races
     * the display's readout and can tear even when only a few pixels change.
     */
    graphics_begin_frame();
    if (!graphics_frame_active)
        return;

    graphics_draw_cursor();
    graphics_end_frame();
}

int graphics_frame_is_active(void)
{
    return graphics_frame_active;
}

int graphics_can_redraw_windows_incrementally(void)
{
    return initialized && !graphics_frame_active && active_panel == 3 &&
           !start_menu_open && !terminal_dragging && !explorer_dragging;
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
