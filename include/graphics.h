#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>
#include "multiboot.h"

int graphics_initialize(multiboot_info_t* mbd);
void graphics_clear(uint32_t color);
void graphics_present(void);

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

int graphics_is_initialized(void);
int graphics_get_active_panel(void);
int graphics_terminal_close_requested(void);
void graphics_select_panel(int panel);
int graphics_terminal_is_maximized(void);
void graphics_terminal_toggle_maximized(void);
int graphics_terminal_is_dragging(void);
int graphics_get_terminal_x(void);
int graphics_get_terminal_y(void);
uint32_t graphics_get_width(void);
uint32_t graphics_get_height(void);

#endif
