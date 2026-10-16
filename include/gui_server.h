#ifndef GUI_SERVER_H
#define GUI_SERVER_H

#include "gui_abi.h"

#include <stdint.h>

int gui_server_create_window(uint32_t pid,
                             const vanta_gui_window_request_t* request);
int gui_server_close_window(uint32_t pid, uint32_t id);
int gui_server_submit_draw(uint32_t pid, uint32_t id,
                           const vanta_gui_draw_command_t* commands,
                           uint32_t count);
int gui_server_poll_event(uint32_t pid, uint32_t id,
                          vanta_gui_event_t* event_out);
void gui_server_draw_windows(void);
int gui_server_mouse_click(int x, int y, int button);
void gui_server_mouse_move(int x, int y);
char gui_server_read_terminal_char(void);
int gui_server_take_redraw_request(void);
void gui_server_cleanup_dead_windows(void);

#endif
