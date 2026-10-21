/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef GUI_SERVER_H
#define GUI_SERVER_H

#include "gui_abi.h"

#include <stdint.h>

typedef void (*gui_builtin_draw_fn)(int x, int y, int width, int height);
typedef int (*gui_builtin_click_fn)(int x, int y, int width, int height,
                                    int pointer_x, int pointer_y, int button);
typedef void (*gui_builtin_close_fn)(void);

int gui_server_register_builtin(const char* title, int x, int y,
                                int width, int height,
                                gui_builtin_draw_fn draw,
                                gui_builtin_click_fn click,
                                gui_builtin_close_fn close);
int gui_server_set_builtin_visible(const char* title, int visible);
void gui_server_reflow_windows(void);

int gui_server_create_window(uint32_t pid,
                             const vanta_gui_window_request_t* request);
int gui_server_close_window(uint32_t pid, uint32_t id);
int gui_server_submit_draw(uint32_t pid, uint32_t id,
                           const vanta_gui_draw_command_t* commands,
                           uint32_t count);
int gui_server_poll_event(uint32_t pid, uint32_t id,
                          vanta_gui_event_t* event_out);
int gui_server_set_owner_visible(uint32_t pid, int visible);
void gui_server_notify_theme_changed(void);
void gui_server_draw_windows(void);
void gui_server_draw_taskbar_items(int taskbar_y, int screen_width);
int gui_server_taskbar_click(int x, int y, int taskbar_y, int screen_width);
int gui_server_mouse_click(int x, int y, int button);
void gui_server_mouse_move(int x, int y);
void gui_server_mouse_release(int button);
char gui_server_read_terminal_char(void);
int gui_server_take_redraw_request(void);
void gui_server_cleanup_dead_windows(void);

#endif
