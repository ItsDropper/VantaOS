/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTA_USER_GUI_H
#define VANTA_USER_GUI_H

#include <gui_abi.h>
#include <vanta/syscall.h>

#define VANTA_SYS_GUI_CREATE_WINDOW 16U
#define VANTA_SYS_GUI_CLOSE_WINDOW  17U
#define VANTA_SYS_GUI_DRAW          18U
#define VANTA_SYS_GUI_POLL_EVENT    19U

static inline long vanta_gui_create_window(
    const vanta_gui_window_request_t* request)
{
    return vanta_syscall3(VANTA_SYS_GUI_CREATE_WINDOW,
                          (uintptr_t)request, 0U, 0U);
}

static inline long vanta_gui_close_window(uint32_t window_id)
{
    return vanta_syscall3(VANTA_SYS_GUI_CLOSE_WINDOW,
                          (uintptr_t)window_id, 0U, 0U);
}

/* Each draw call replaces the window's complete retained command list. */
static inline long vanta_gui_draw(
    uint32_t window_id,
    const vanta_gui_draw_command_t* commands,
    size_t count)
{
    return vanta_syscall3(VANTA_SYS_GUI_DRAW,
                          (uintptr_t)window_id, (uintptr_t)commands,
                          (uintptr_t)count);
}

/* Returns 1 for an event, 0 when empty, or a negative error. */
static inline long vanta_gui_poll_event(
    uint32_t window_id, vanta_gui_event_t* event)
{
    return vanta_syscall3(VANTA_SYS_GUI_POLL_EVENT,
                          (uintptr_t)window_id, (uintptr_t)event, 0U);
}

#endif
