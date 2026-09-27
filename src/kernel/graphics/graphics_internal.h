#ifndef GRAPHICS_INTERNAL_H
#define GRAPHICS_INTERNAL_H

#include <stdint.h>
#include "graphics.h"

#define GRAPHICS_MAX_PAGES 4096U
#define GRAPHICS_VIRTUAL_BASE 0xD0000000U
#define CURSOR_SAVE_SIZE 20

extern uint8_t* framebuffer;
extern uint32_t framebuffer_pitch;
extern uint32_t framebuffer_width;
extern uint32_t framebuffer_height;
extern uint8_t red_position;
extern uint8_t red_mask_size;
extern uint8_t green_position;
extern uint8_t green_mask_size;
extern uint8_t blue_position;
extern uint8_t blue_mask_size;
extern int initialized;
extern int cursor_x;
extern int cursor_y;
extern int active_panel;
extern int start_menu_open;
extern uint32_t files_current_dir;
extern int files_open_file;
extern int terminal_close_requested;
extern int terminal_running;
extern int terminal_maximized;
extern int terminal_dragging;
extern int terminal_x;
extern int terminal_y;
extern int terminal_restore_x;
extern int terminal_restore_y;
extern int terminal_drag_offset_x;
extern int terminal_drag_offset_y;
extern int settings_resolution_index;
extern const uint32_t settings_widths[3];
extern const uint32_t settings_heights[3];
extern uint32_t cursor_saved[CURSOR_SAVE_SIZE * CURSOR_SAVE_SIZE];
extern int cursor_saved_x;
extern int cursor_saved_y;
extern int cursor_saved_width;
extern int cursor_saved_height;
extern int cursor_saved_valid;

uint32_t graphics_pack_color(uint32_t color);
void graphics_cursor_restore(void);
int graphics_files_window_x(void);
int graphics_files_window_y(void);

#endif
