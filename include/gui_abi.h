#ifndef VANTA_GUI_ABI_H
#define VANTA_GUI_ABI_H

#include <stdint.h>

/* VantaOS-specific ring-3 GUI ABI. This is not a Win32/Wayland ABI. */
#define VANTA_GUI_ABI_VERSION 1U
#define VANTA_GUI_MAX_WINDOWS 8U
#define VANTA_GUI_MAX_COMMANDS 24U
#define VANTA_GUI_TITLE_MAX 31U
#define VANTA_GUI_TEXT_MAX 63U

#define VANTA_GUI_DRAW_FILL_RECT 1U
#define VANTA_GUI_DRAW_TEXT      2U

#define VANTA_GUI_EVENT_NONE          0U
#define VANTA_GUI_EVENT_KEY           1U
#define VANTA_GUI_EVENT_POINTER_MOVE  2U
#define VANTA_GUI_EVENT_POINTER_DOWN  3U
#define VANTA_GUI_EVENT_CLOSE_REQUEST 4U
#define VANTA_GUI_EVENT_FOCUS_GAINED  5U
#define VANTA_GUI_EVENT_FOCUS_LOST    6U

/* For KEY events, values 0-255 are character bytes; 0x100 + keyboard_event_t
 * represents a special key such as Page Up, arrows, Home, End, or Delete. */

typedef struct
{
    uint32_t width;
    uint32_t height;
    char title[VANTA_GUI_TITLE_MAX + 1U];
} vanta_gui_window_request_t;

typedef struct
{
    uint32_t type;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint32_t color;
    uint32_t scale;
    char text[VANTA_GUI_TEXT_MAX + 1U];
} vanta_gui_draw_command_t;

typedef struct
{
    uint32_t type;
    int32_t x;
    int32_t y;
    uint32_t value;
    uint32_t reserved;
} vanta_gui_event_t;

#endif
