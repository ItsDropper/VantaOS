#include "graphics_internal.h"

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
int settings_resolution_index;

const uint32_t settings_widths[3] = {800, 1024, 1280};
const uint32_t settings_heights[3] = {600, 768, 720};

uint32_t cursor_saved[20 * 20];
int cursor_saved_x;
int cursor_saved_y;
int cursor_saved_width;
int cursor_saved_height;
int cursor_saved_valid;

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
