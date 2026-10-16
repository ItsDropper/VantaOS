#ifndef GRAPHICS_INTERNAL_H
#define GRAPHICS_INTERNAL_H

#include "graphics.h"
#include <stdint.h>

#define GRAPHICS_VIRTUAL_BASE 0xD0000000U
#define GRAPHICS_MAX_PAGES 4096
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
/* File Explorer window state shared with the graphics input and renderer. */
extern int explorer_dragging;
extern int explorer_x;
extern int explorer_y;
extern int explorer_drag_offset_x;
extern int explorer_drag_offset_y;
extern int explorer_maximized;
extern int explorer_restore_x;
extern int explorer_restore_y;
extern int settings_resolution_index;
extern const uint32_t settings_widths[3];
extern const uint32_t settings_heights[3];

extern uint32_t cursor_saved[CURSOR_SAVE_SIZE * CURSOR_SAVE_SIZE];
extern int cursor_saved_x;
extern int cursor_saved_y;
extern int cursor_saved_width;
extern int cursor_saved_height;
extern int cursor_saved_valid;

void graphics_draw_start_menu_detail(int width, int height);
void graphics_ui_explorer_sidebar_heading(int x, int y, const char* title);
void graphics_ui_explorer_column_header(int x, int y, int width);
void graphics_ui_explorer_row(int x, int y, int width, int height,
                              int selected, int alternate);
void graphics_ui_explorer_status(int x, int y, int width,
                                 const char* count_label,
                                 const char* location_label);
void graphics_ui_explorer_location(int x, int y, int width,
                                   const char* location);
void graphics_ui_explorer_toolbar_button(int x, int y, const char* glyph,
                                         int enabled);
void graphics_ui_draw_section(int x, int y, int width, const char* title,
                              const char* detail);
void graphics_ui_draw_callout(int x, int y, int width, int height,
                              const char* title, const char* body,
                              uint32_t accent);
void graphics_ui_draw_key_value(int x, int y, int width, const char* key,
                                const char* value, uint32_t value_color);
void graphics_ui_draw_progress(int x, int y, int width, int progress,
                               uint32_t tint);
void graphics_ui_draw_app_icon(int x, int y, int size, int app_kind);
void graphics_ui_draw_window_section(int x, int y, int width,
                                     const char* title);
void graphics_ui_draw_table_row(int x, int y, int width, int height,
                                const char* first, const char* second,
                                const char* third, int alternate);
void graphics_ui_draw_navigation_item(int x, int y, int width,
                                      const char* label, int selected,
                                      int app_kind);
void graphics_ui_draw_stat_card(int x, int y, int width, int height,
                                const char* label, const char* value,
                                const char* caption, uint32_t tint);
void graphics_ui_draw_settings_row(int x, int y, int width,
                                   const char* title, const char* subtitle,
                                   const char* value);
void graphics_ui_draw_separator_label(int x, int y, int width,
                                      const char* label);
void graphics_ui_draw_notice(int x, int y, int width, const char* message,
                             uint32_t tint);

void graphics_draw_start_menu_detail(int width, int height);
void graphics_draw_desktop_backdrop(int width, int height);\nvoid graphics_draw_desktop_dashboard(int width, int height);\nvoid graphics_draw_system_page_detail(int x, int y, int width, int height);\nvoid graphics_draw_settings_page_detail(int x, int y, int width, int height);\n\nuint32_t graphics_pack_color(uint32_t color);
void bochs_vbe_write(uint16_t index, uint16_t value);
uint16_t bochs_vbe_read(uint16_t index);

#endif
