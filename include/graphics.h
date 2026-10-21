/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>
#include "multiboot.h"

int graphics_initialize(multiboot_info_t* mbd);
void graphics_clear(uint32_t color);
void graphics_present(void);
void graphics_begin_frame(void);
void graphics_end_frame(void);
int graphics_frame_is_active(void);
void graphics_draw_window_frame(int x, int y, int width, int height, const char* title, int show_window_controls);

void graphics_fill_rect(
    int x,
    int y,
    int width,
    int height,
    uint32_t color
);

void graphics_draw_text(
    int x,
    int y,
    const char* text,
    uint32_t color,
    unsigned int scale
);

void graphics_mouse_move(int dx, int dy);
void graphics_mouse_click(int button);
void graphics_mouse_release(int button);
void graphics_draw_cursor(void);
/* Update only the cursor on the visible framebuffer, without a full frame copy. */
void graphics_update_cursor(void);
void graphics_cursor_restore(void);
int graphics_can_redraw_windows_in_place(void);
void graphics_fill_rounded_rect(int x,int y,int width,int height,int radius,uint32_t color);
int graphics_set_resolution(uint32_t width,uint32_t height);

int graphics_is_initialized(void);
int graphics_get_active_panel(void);
int graphics_terminal_close_requested(void);
void graphics_clear_terminal_close_requested(void);
void graphics_set_terminal_running(int running);
int graphics_terminal_is_running(void);
void graphics_select_panel(int panel);
void graphics_toggle_start_menu(void);
void graphics_draw_start_menu_overlay(void);
int graphics_terminal_open_requested(void);
void graphics_request_terminal_open(void);
void graphics_clear_terminal_open_request(void);
int graphics_terminal_is_maximized(void);
void graphics_terminal_toggle_maximized(void);
int graphics_terminal_is_dragging(void);
int graphics_explorer_is_dragging(void);
int graphics_get_terminal_x(void);
int graphics_get_terminal_y(void);
uint32_t graphics_get_width(void);
uint32_t graphics_get_height(void);
uint32_t graphics_get_terminal_width(void);
uint32_t graphics_get_terminal_height(void);

#endif
