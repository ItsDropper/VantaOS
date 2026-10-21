/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "gui_server.h"

#include "graphics.h"
#include "keyboard.h"
#include "process.h"
#include "spinlock.h"
#include "terminal.h"
#include "terminal_process.h"
#include "desktop_theme.h"

#include <stddef.h>
#include <stdint.h>

#define GUI_EVENT_QUEUE_CAPACITY 32U
#define GUI_WINDOW_MIN_WIDTH 240U
#define GUI_WINDOW_MIN_HEIGHT 160U
#define GUI_TITLEBAR_HEIGHT 46U
#define GUI_CLIENT_INSET_X 8
#define GUI_CLIENT_INSET_Y 50

typedef struct
{
    int used;
    int visible;
    int owner_visible;
    int minimized;
    int maximized;
    int32_t restore_x;
    int32_t restore_y;
    uint32_t restore_width;
    uint32_t restore_height;
    uint32_t id;
    uint32_t owner_pid;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint64_t z_order;
    char title[VANTA_GUI_TITLE_MAX + 1U];
    vanta_gui_draw_command_t commands[VANTA_GUI_MAX_COMMANDS];
    uint32_t command_count;
    vanta_gui_event_t events[GUI_EVENT_QUEUE_CAPACITY];
    uint32_t event_head;
    uint32_t event_count;
    int builtin;
    gui_builtin_draw_fn builtin_draw;
    gui_builtin_click_fn builtin_click;
    gui_builtin_close_fn builtin_close;
} gui_window_t;

static gui_window_t windows[VANTA_GUI_MAX_WINDOWS];
static spinlock_t gui_lock = SPINLOCK_INITIALIZER;
static uint32_t next_window_id = 1U;
static uint64_t next_z_order = 1U;
static uint32_t focused_window_id;
static uint32_t dragging_window_id;
static int32_t drag_offset_x;
static int32_t drag_offset_y;
static uint32_t resizing_window_id;
static int32_t resize_start_x;
static int32_t resize_start_y;
static uint32_t resize_start_width;
static uint32_t resize_start_height;
static int redraw_pending;

/*
 * Rendering is deliberately performed from a snapshot outside gui_lock.
 * Holding the input/window lock while painting every pixel made keyboard
 * syscalls spin behind software framebuffer rendering in QEMU.
 */
typedef struct
{
    int visible;
    int focused;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint64_t z_order;
    int maximized;
    char title[VANTA_GUI_TITLE_MAX + 1U];
    vanta_gui_draw_command_t commands[VANTA_GUI_MAX_COMMANDS];
    uint32_t command_count;
    gui_builtin_draw_fn builtin_draw;
} gui_render_snapshot_t;

static gui_render_snapshot_t render_snapshot[VANTA_GUI_MAX_WINDOWS];

static gui_window_t* gui_topmost_visible_locked(uint32_t exclude_id);
static void gui_focus_locked(gui_window_t* target);
static void gui_emit_resize_locked(gui_window_t* window);
static int gui_title_equal(const char* a, const char* b)
{
    if (!a || !b) return 0;
    while (*a && *b)
    {
        if (*a++ != *b++) return 0;
    }
    return *a == *b;
}

int gui_server_register_builtin(const char* title, int x, int y,
                                int width, int height,
                                gui_builtin_draw_fn draw,
                                gui_builtin_click_fn click,
                                gui_builtin_close_fn close)
{
    if (!title || !draw || !graphics_is_initialized() ||
        width < (int)GUI_WINDOW_MIN_WIDTH ||
        height < (int)GUI_WINDOW_MIN_HEIGHT)
        return -22;

    size_t length = 0U;
    while (length <= VANTA_GUI_TITLE_MAX && title[length]) length++;
    if (length == 0U || length > VANTA_GUI_TITLE_MAX ||
        length > (size_t)(width - 100) / 8U)
        return -22;

    uint64_t flags = spinlock_lock_irqsave(&gui_lock);
    gui_window_t* slot = 0;
    for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
    {
        if (windows[i].used && windows[i].builtin &&
            gui_title_equal(windows[i].title, title))
        {
            slot = &windows[i];
            break;
        }
        if (!slot && !windows[i].used)
            slot = &windows[i];
    }
    if (!slot)
    {
        spinlock_unlock_irqrestore(&gui_lock, flags);
        return -12;
    }

    if (!slot->used)
    {
        for (size_t i = 0; i < sizeof(*slot); i++)
            ((uint8_t*)slot)[i] = 0U;
        slot->used = 1;
        slot->builtin = 1;
        slot->id = next_window_id++;
        if (slot->id == 0U) slot->id = next_window_id++;
        slot->owner_pid = 0U;
        slot->owner_visible = 0;
        slot->visible = 0;
        slot->width = (uint32_t)width;
        slot->height = (uint32_t)height;
        slot->x = x;
        slot->y = y;
        for (size_t i = 0; i <= length; i++) slot->title[i] = title[i];
    }

    slot->builtin_draw = draw;
    slot->builtin_click = click;
    slot->builtin_close = close;
    spinlock_unlock_irqrestore(&gui_lock, flags);
    return (int)slot->id;
}

int gui_server_set_builtin_visible(const char* title, int visible)
{
    if (!title) return -22;
    uint64_t flags = spinlock_lock_irqsave(&gui_lock);
    gui_window_t* target = 0;
    for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
        if (windows[i].used && windows[i].builtin &&
            gui_title_equal(windows[i].title, title))
        {
            target = &windows[i];
            break;
        }
    if (!target)
    {
        spinlock_unlock_irqrestore(&gui_lock, flags);
        return -2;
    }

    target->owner_visible = visible != 0;
    target->visible = visible != 0;
    target->minimized = 0;
    if (visible)
        gui_focus_locked(target);
    else if (focused_window_id == target->id)
        gui_focus_locked(gui_topmost_visible_locked(target->id));
    redraw_pending = 1;
    spinlock_unlock_irqrestore(&gui_lock, flags);
    return 0;
}



static gui_window_t* gui_find_window_locked(uint32_t pid, uint32_t id)
{
    if (id == 0U)
        return 0;
    for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
        if (windows[i].used && windows[i].id == id &&
            windows[i].owner_pid == pid)
            return &windows[i];
    return 0;
}

static void gui_queue_event_locked(gui_window_t* window,
                                   const vanta_gui_event_t* event)
{
    if (!window || !event)
        return;

    /* Bounded queues retain the newest input when a client falls behind. */
    if (window->event_count == GUI_EVENT_QUEUE_CAPACITY)
    {
        window->event_head = (window->event_head + 1U) %
                             GUI_EVENT_QUEUE_CAPACITY;
        window->event_count--;
    }

    uint32_t tail = (window->event_head + window->event_count) %
                    GUI_EVENT_QUEUE_CAPACITY;
    window->events[tail] = *event;
    window->event_count++;
}

static void gui_emit_simple_event_locked(gui_window_t* window,
                                         uint32_t type)
{
    vanta_gui_event_t event = { type, 0, 0, 0U, 0U };
    gui_queue_event_locked(window, &event);
}

static void gui_focus_locked(gui_window_t* target)
{
    if (target && focused_window_id == target->id)
    {
        target->z_order = next_z_order++;
        redraw_pending = 1;
        return;
    }

    if (focused_window_id != 0U)
    {
        for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
        {
            if (windows[i].used && windows[i].visible &&
            windows[i].id == focused_window_id)
            {
                gui_emit_simple_event_locked(&windows[i],
                                             VANTA_GUI_EVENT_FOCUS_LOST);
                break;
            }
        }
    }

    focused_window_id = target ? target->id : 0U;
    if (target)
    {
        target->z_order = next_z_order++;
        gui_emit_simple_event_locked(target, VANTA_GUI_EVENT_FOCUS_GAINED);
        if (target->builtin)
        {
            if (gui_title_equal(target->title, "Files"))
                graphics_select_panel(2);
            else if (gui_title_equal(target->title, "Terminal"))
                graphics_select_panel(3);
            else if (gui_title_equal(target->title, "System"))
                graphics_select_panel(1);
            else if (gui_title_equal(target->title, "Settings"))
                graphics_select_panel(4);
        }
        else
            graphics_select_panel(0);
    }
    else
        graphics_select_panel(0);
    redraw_pending = 1;
}

static int gui_command_valid(const vanta_gui_draw_command_t* command,
                             uint32_t width, uint32_t height)
{
    if (!command)
        return 0;

    int64_t client_width = (int64_t)width - 2 * GUI_CLIENT_INSET_X;
    int64_t client_height = (int64_t)height - GUI_CLIENT_INSET_Y - 8;
    if (client_width <= 0 || client_height <= 0 ||
        command->x < 0 || command->y < 0 ||
        command->x >= client_width || command->y >= client_height)
        return 0;

    if (command->type == VANTA_GUI_DRAW_FILL_RECT)
    {
        if (command->width <= 0 || command->height <= 0)
            return 0;
        if ((int64_t)command->x + command->width > client_width ||
            (int64_t)command->y + command->height > client_height)
            return 0;
        return 1;
    }

    if (command->type == VANTA_GUI_DRAW_TEXT)
    {
        if (command->scale < 1U || command->scale > 2U)
            return 0;
        size_t length = 0U;
        while (length <= VANTA_GUI_TEXT_MAX && command->text[length])
            length++;
        if (length == 0U || length > VANTA_GUI_TEXT_MAX)
            return 0;
        if ((int64_t)command->x + (int64_t)length * 8 * command->scale >
                client_width ||
            (int64_t)command->y + 16 * command->scale > client_height)
            return 0;
        return 1;
    }

    return 0;
}

void gui_server_reflow_windows(void)
{
    if (!graphics_is_initialized())
        return;
    int32_t screen_w = (int32_t)graphics_get_width();
    int32_t work_h = (int32_t)graphics_get_height() - 64;
    if (work_h < 1) work_h = (int32_t)graphics_get_height();
    uint64_t flags = spinlock_lock_irqsave(&gui_lock);
    for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
    {
        gui_window_t* window = &windows[i];
        if (!window->used) continue;
        uint32_t old_width = window->width;
        uint32_t old_height = window->height;
        if (window->maximized)
        {
            window->x = 0;
            window->y = 0;
            window->width = (uint32_t)screen_w;
            window->height = (uint32_t)work_h;
        }
        else
        {
            uint32_t max_width = screen_w > 16 ? (uint32_t)(screen_w - 16) : 1U;
            uint32_t max_height = work_h > 8 ? (uint32_t)(work_h - 8) : 1U;
            if (window->width > max_width) window->width = max_width;
            if (window->height > max_height) window->height = max_height;
            int32_t max_x = screen_w - (int32_t)window->width;
            int32_t max_y = work_h - (int32_t)window->height;
            if (max_x < 0) max_x = 0;
            if (max_y < 0) max_y = 0;
            if (window->x < 0) window->x = 0;
            if (window->y < 0) window->y = 0;
            if (window->x > max_x) window->x = max_x;
            if (window->y > max_y) window->y = max_y;
        }
        if (!window->builtin &&
            (old_width != window->width || old_height != window->height))
            gui_emit_resize_locked(window);
    }
    redraw_pending = 1;
    spinlock_unlock_irqrestore(&gui_lock, flags);
}

int gui_server_create_window(uint32_t pid,
                             const vanta_gui_window_request_t* request)
{
    if (!request || !graphics_is_initialized())
        return -19;
    if (request->width < GUI_WINDOW_MIN_WIDTH ||
        request->height < GUI_WINDOW_MIN_HEIGHT ||
        request->width > graphics_get_width() - 16U ||
        request->height > graphics_get_height() - 128U)
        return -22;

    size_t title_length = 0U;
    while (title_length <= VANTA_GUI_TITLE_MAX && request->title[title_length])
        title_length++;
    if (title_length == 0U || title_length > VANTA_GUI_TITLE_MAX ||
        title_length > (request->width - 100U) / 8U)
        return -22;

    uint64_t flags = spinlock_lock_irqsave(&gui_lock);
    gui_window_t* slot = 0;
    for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
        if (!windows[i].used)
        {
            slot = &windows[i];
            break;
        }
    if (!slot)
    {
        spinlock_unlock_irqrestore(&gui_lock, flags);
        return -12;
    }

    for (size_t i = 0; i < sizeof(*slot); i++)
        ((uint8_t*)slot)[i] = 0U;
    slot->used = 1;
    slot->visible = 1;
    slot->owner_visible = 1;
    slot->minimized = 0;
    slot->maximized = 0;
    slot->id = next_window_id++;
    if (slot->id == 0U)
        slot->id = next_window_id++;
    slot->owner_pid = pid;
    slot->width = request->width;
    slot->height = request->height;
    slot->x = ((int32_t)graphics_get_width() - (int32_t)slot->width) / 2;
    slot->y = ((int32_t)graphics_get_height() - (int32_t)slot->height) / 2;
    for (size_t i = 0; i <= title_length; i++)
        slot->title[i] = request->title[i];

    gui_focus_locked(slot);
    uint32_t id = slot->id;
    spinlock_unlock_irqrestore(&gui_lock, flags);
    return (int)id;
}

int gui_server_close_window(uint32_t pid, uint32_t id)
{
    uint64_t flags = spinlock_lock_irqsave(&gui_lock);
    gui_window_t* window = gui_find_window_locked(pid, id);
    if (!window)
    {
        spinlock_unlock_irqrestore(&gui_lock, flags);
        return -9;
    }

    int was_focused = focused_window_id == id;
    for (size_t i = 0; i < sizeof(*window); i++)
        ((uint8_t*)window)[i] = 0U;

    if (was_focused)
    {
        focused_window_id = 0U;
        gui_window_t* newest = 0;
        for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
            if (windows[i].used && windows[i].visible &&
                (!newest || windows[i].z_order > newest->z_order))
                newest = &windows[i];
        gui_focus_locked(newest);
    }
    redraw_pending = 1;
    spinlock_unlock_irqrestore(&gui_lock, flags);
    return 0;
}

int gui_server_submit_draw(uint32_t pid, uint32_t id,
                           const vanta_gui_draw_command_t* commands,
                           uint32_t count)
{
    if (count > VANTA_GUI_MAX_COMMANDS || (count != 0U && !commands))
        return -22;
    uint64_t flags = spinlock_lock_irqsave(&gui_lock);
    gui_window_t* window = gui_find_window_locked(pid, id);
    if (!window)
    {
        spinlock_unlock_irqrestore(&gui_lock, flags);
        return -9;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        if (!gui_command_valid(&commands[i], window->width, window->height))
        {
            spinlock_unlock_irqrestore(&gui_lock, flags);
            return -22;
        }
    }
    for (uint32_t i = 0; i < count; i++)
        window->commands[i] = commands[i];
    window->command_count = count;
    redraw_pending = 1;
    spinlock_unlock_irqrestore(&gui_lock, flags);
    return (int)count;
}

int gui_server_set_owner_visible(uint32_t pid, int visible)
{
    if (pid == 0U)
        return 0;
    visible = visible != 0;
    uint64_t flags = spinlock_lock_irqsave(&gui_lock);
    int found = 0;
    int changed = 0;
    int focused_owned = 0;
    gui_window_t* newest_owned = 0;

    for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
    {
        gui_window_t* window = &windows[i];
        if (!window->used || window->owner_pid != pid)
            continue;
        found = 1;
        if (window->owner_visible != visible)
            changed = 1;
        if (window->id == focused_window_id)
            focused_owned = 1;
        if (!window->minimized &&
            (!newest_owned || window->z_order > newest_owned->z_order))
            newest_owned = window;
    }

    if (!found || !changed)
    {
        spinlock_unlock_irqrestore(&gui_lock, flags);
        return found;
    }

    /* Emit focus-lost while the old window is still visible in the registry. */
    if (!visible && focused_owned)
        gui_focus_locked(0);

    for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
    {
        gui_window_t* window = &windows[i];
        if (window->used && window->owner_pid == pid)
        {
            window->owner_visible = visible;
            window->visible = visible && !window->minimized;
        }
    }

    if (visible && newest_owned)
        gui_focus_locked(newest_owned);
    else if (focused_window_id == 0U)
        gui_focus_locked(gui_topmost_visible_locked(0U));

    redraw_pending = 1;
    spinlock_unlock_irqrestore(&gui_lock, flags);
    return 1;
}

void gui_server_notify_theme_changed(void)
{
    uint64_t flags = spinlock_lock_irqsave(&gui_lock);
    for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
        if (windows[i].used)
            gui_emit_simple_event_locked(&windows[i],
                                         VANTA_GUI_EVENT_THEME_CHANGED);
    redraw_pending = 1;
    spinlock_unlock_irqrestore(&gui_lock, flags);
}

int gui_server_poll_event(uint32_t pid, uint32_t id,
                          vanta_gui_event_t* event_out)
{
    if (!event_out)
        return -14;
    uint64_t flags = spinlock_lock_irqsave(&gui_lock);
    gui_window_t* window = gui_find_window_locked(pid, id);
    if (!window)
    {
        spinlock_unlock_irqrestore(&gui_lock, flags);
        return -9;
    }
    if (window->event_count == 0U && focused_window_id == id)
    {
        if (keyboard_has_event())
        {
            keyboard_event_t key = keyboard_get_event();
            vanta_gui_event_t event = {
                VANTA_GUI_EVENT_KEY, 0, 0, 0x100U + (uint32_t)key, 0U
            };
            *event_out = event;
            spinlock_unlock_irqrestore(&gui_lock, flags);
            return 1;
        }
        if (keyboard_has_char())
        {
            char character = keyboard_get_char();
            vanta_gui_event_t event = {
                VANTA_GUI_EVENT_KEY, 0, 0,
                (uint32_t)(unsigned char)character, 0U
            };
            *event_out = event;
            spinlock_unlock_irqrestore(&gui_lock, flags);
            return 1;
        }
    }
    if (window->event_count == 0U)
    {
        spinlock_unlock_irqrestore(&gui_lock, flags);
        return 0;
    }
    *event_out = window->events[window->event_head];
    window->event_head = (window->event_head + 1U) %
                         GUI_EVENT_QUEUE_CAPACITY;
    window->event_count--;
    spinlock_unlock_irqrestore(&gui_lock, flags);
    return 1;
}

void gui_server_draw_windows(void)
{
    if (!graphics_is_initialized())
        return;

    /*
     * Snapshot only render state under the lock. Drawing is CPU-intensive on
     * the software framebuffer and must not hold gui_lock: keyboard reads,
     * pointer events, and GUI syscalls need to proceed while the window is
     * being painted.
     */
    uint64_t flags = spinlock_lock_irqsave(&gui_lock);
    for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
    {
        gui_window_t* source = &windows[i];
        gui_render_snapshot_t* target = &render_snapshot[i];
        target->visible = source->used && source->visible;
        target->focused = target->visible &&
                          source->id == focused_window_id;
        if (!target->visible)
        {
            target->command_count = 0U;
            continue;
        }

        target->x = source->x;
        target->y = source->y;
        target->width = source->width;
        target->height = source->height;
        target->z_order = source->z_order;
        target->maximized = source->maximized;
        target->builtin_draw = source->builtin_draw;
        target->command_count = source->command_count;
        for (size_t j = 0; j < sizeof(target->title); j++)
            target->title[j] = source->title[j];
        if (target->command_count > VANTA_GUI_MAX_COMMANDS)
            target->command_count = VANTA_GUI_MAX_COMMANDS;
        for (uint32_t j = 0; j < target->command_count; j++)
            target->commands[j] = source->commands[j];
    }
    spinlock_unlock_irqrestore(&gui_lock, flags);

    uint64_t previous_z = 0U;
    for (unsigned int pass = 0; pass < VANTA_GUI_MAX_WINDOWS; pass++)
    {
        gui_render_snapshot_t* window = 0;
        for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
        {
            gui_render_snapshot_t* candidate = &render_snapshot[i];
            if (candidate->visible && candidate->z_order > previous_z &&
                (!window || candidate->z_order < window->z_order))
                window = candidate;
        }
        if (!window)
            break;
        previous_z = window->z_order;

        int32_t x = window->x;
        int32_t y = window->y;
        uint32_t width = window->width;
        uint32_t height = window->height;
        graphics_draw_window_frame(x, y, (int)width, (int)height,
                                   window->title, 1);
        if (window->focused)
            graphics_fill_rect(x + 16, y + GUI_TITLEBAR_HEIGHT - 2,
                               48, 2,
                               desktop_theme_color(VANTA_UI_ROLE_ACCENT));
        graphics_fill_rounded_rect(x + 2, y + GUI_TITLEBAR_HEIGHT,
                                   (int)width - 4,
                                   (int)height - GUI_TITLEBAR_HEIGHT - 2,
                                   6, VANTA_UI_DESKTOP);

        for (uint32_t j = 0; j < window->command_count; j++)
        {
            const vanta_gui_draw_command_t* command = &window->commands[j];
            int cx = x + GUI_CLIENT_INSET_X + command->x;
            int cy = y + GUI_CLIENT_INSET_Y + command->y;
            if (command->type == VANTA_GUI_DRAW_FILL_RECT)
            {
                int max_w = (int)width - 2 * GUI_CLIENT_INSET_X;
                int max_h = (int)height - GUI_CLIENT_INSET_Y - 8;
                int draw_w = command->width;
                int draw_h = command->height;
                if (command->x >= 0 && command->y >= 0 &&
                    command->x < max_w && command->y < max_h)
                {
                    if (draw_w > max_w - command->x)
                        draw_w = max_w - command->x;
                    if (draw_h > max_h - command->y)
                        draw_h = max_h - command->y;
                    if (draw_w > 0 && draw_h > 0)
                        graphics_fill_rect(cx, cy, draw_w, draw_h,
                                           command->color & 0x00FFFFFFU);
                }
            }
            else if (command->type == VANTA_GUI_DRAW_TEXT &&
                     gui_command_valid(command, width, height))
                graphics_draw_text(cx, cy, command->text,
                                   command->color & 0x00FFFFFFU,
                                   command->scale);
        }

        if (window->builtin_draw)
            window->builtin_draw(x, y, (int)width, (int)height);

        /* Subtle resize affordance; disabled for maximized windows. */
        if (!window->maximized)
        {
            int gx = x + (int)width - 14;
            int gy = y + (int)height - 14;
            uint32_t grip = window->focused ? 0x005DA9FFU : 0x00334455U;
            graphics_fill_rect(gx + 7, gy + 5, 2, 2, grip);
            graphics_fill_rect(gx + 4, gy + 8, 5, 2, grip);
            graphics_fill_rect(gx + 1, gy + 11, 8, 2, grip);
        }
    }
}

static gui_window_t* gui_topmost_visible_locked(uint32_t exclude_id)
{
    gui_window_t* top = 0;
    for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
    {
        gui_window_t* candidate = &windows[i];
        if (!candidate->used || !candidate->visible ||
            candidate->id == exclude_id)
            continue;
        if (!top || candidate->z_order > top->z_order)
            top = candidate;
    }
    return top;
}

static void gui_minimize_locked(gui_window_t* window)
{
    if (!window || !window->used || window->minimized)
        return;
    if (focused_window_id == window->id)
    {
        gui_focus_locked(0);
        window->minimized = 1;
        window->visible = 0;
        gui_focus_locked(gui_topmost_visible_locked(window->id));
    }
    else
    {
        window->minimized = 1;
        window->visible = 0;
    }
    if (dragging_window_id == window->id)
        dragging_window_id = 0U;
    if (resizing_window_id == window->id)
        resizing_window_id = 0U;
    redraw_pending = 1;
}

static void gui_restore_locked(gui_window_t* window)
{
    if (!window || !window->used || !window->owner_visible)
        return;
    window->minimized = 0;
    window->visible = 1;
    gui_focus_locked(window);
    redraw_pending = 1;
}

static void gui_emit_resize_locked(gui_window_t* window)
{
    if (!window)
        return;
    vanta_gui_event_t event = {
        VANTA_GUI_EVENT_RESIZE,
        (int32_t)window->width,
        (int32_t)window->height,
        0U,
        0U
    };
    gui_queue_event_locked(window, &event);
}

static void gui_toggle_maximized_locked(gui_window_t* window)
{
    if (!window || !window->used)
        return;
    if (!window->maximized)
    {
        window->restore_x = window->x;
        window->restore_y = window->y;
        window->restore_width = window->width;
        window->restore_height = window->height;
        window->x = 0;
        window->y = 0;
        window->width = graphics_get_width();
        window->height = graphics_get_height() > 64U
                       ? graphics_get_height() - 64U
                       : graphics_get_height();
        window->maximized = 1;
        dragging_window_id = 0U;
        resizing_window_id = 0U;
    }
    else
    {
        window->x = window->restore_x;
        window->y = window->restore_y;
        window->width = window->restore_width;
        window->height = window->restore_height;
        window->maximized = 0;
        int32_t max_x = (int32_t)graphics_get_width() -
                        (int32_t)window->width;
        int32_t max_y = (int32_t)graphics_get_height() -
                        (int32_t)GUI_TITLEBAR_HEIGHT;
        if (max_x < 0) max_x = 0;
        if (max_y < 0) max_y = 0;
        if (window->x < 0) window->x = 0;
        if (window->y < 0) window->y = 0;
        if (window->x > max_x) window->x = max_x;
        if (window->y > max_y) window->y = max_y;
    }
    gui_emit_resize_locked(window);
    redraw_pending = 1;
}

int gui_server_mouse_click(int x, int y, int button)
{
    if (button != 1 && button != 3)
        return 0;

    gui_builtin_click_fn builtin_click = 0;
    gui_builtin_close_fn builtin_close = 0;
    int close_builtin = 0;
    int builtin_x = 0, builtin_y = 0, builtin_w = 0, builtin_h = 0;
    uint64_t flags = spinlock_lock_irqsave(&gui_lock);
    gui_window_t* target = 0;
    for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
    {
        gui_window_t* window = &windows[i];
        if (!window->used || !window->visible || x < window->x || y < window->y ||
            x >= window->x + (int)window->width ||
            y >= window->y + (int)window->height)
            continue;
        if (!target || window->z_order > target->z_order)
            target = window;
    }

    if (!target)
    {
        if (focused_window_id != 0U)
            gui_focus_locked(0);
        spinlock_unlock_irqrestore(&gui_lock, flags);
        return 0;
    }

    gui_focus_locked(target);
    int titlebar = y >= target->y &&
                   y < target->y + (int)GUI_TITLEBAR_HEIGHT;
    if (button == 1 && titlebar &&
        x >= target->x + (int)target->width - 138)
    {
        int control_x = x - (target->x + (int)target->width - 138);
        if (control_x < 46)
            gui_minimize_locked(target);
        else if (control_x < 92)
            gui_toggle_maximized_locked(target);
        else if (target->builtin)
        {
            builtin_close = target->builtin_close;
            close_builtin = 1;
            target->visible = 0;
            target->owner_visible = 0;
            target->minimized = 0;
            if (focused_window_id == target->id)
                gui_focus_locked(gui_topmost_visible_locked(target->id));
        }
        else
            gui_emit_simple_event_locked(target, VANTA_GUI_EVENT_CLOSE_REQUEST);
    }
    else if (button == 1 && !target->maximized &&
             x >= target->x + (int)target->width - 18 &&
             y >= target->y + (int)target->height - 18)
    {
        resizing_window_id = target->id;
        resize_start_x = x;
        resize_start_y = y;
        resize_start_width = target->width;
        resize_start_height = target->height;
        dragging_window_id = 0U;
    }
    else if (button == 1 && titlebar)
    {
        /* Restore from maximized state before starting a conventional drag. */
        if (target->maximized)
        {
            gui_toggle_maximized_locked(target);
            target->x = x - (int32_t)(target->width / 2U);
            target->y = 0;
            if (target->x < 0) target->x = 0;
            int32_t max_x = (int32_t)graphics_get_width() -
                            (int32_t)target->width;
            if (max_x < 0) max_x = 0;
            if (target->x > max_x) target->x = max_x;
        }
        dragging_window_id = target->id;
        drag_offset_x = x - target->x;
        drag_offset_y = y - target->y;
    }
    else if (target->builtin)
    {
        builtin_click = target->builtin_click;
        builtin_x = target->x;
        builtin_y = target->y;
        builtin_w = (int)target->width;
        builtin_h = (int)target->height;
    }
    else
    {
        vanta_gui_event_t event = {
            VANTA_GUI_EVENT_POINTER_DOWN,
            x - target->x - GUI_CLIENT_INSET_X,
            y - target->y - GUI_CLIENT_INSET_Y,
            (uint32_t)button,
            keyboard_ctrl_pressed() ? VANTA_GUI_MOD_CTRL : 0U
        };
        gui_queue_event_locked(target, &event);
    }
    redraw_pending = 1;
    spinlock_unlock_irqrestore(&gui_lock, flags);
    if (close_builtin && builtin_close)
        builtin_close();
    else if (builtin_click)
        (void)builtin_click(builtin_x, builtin_y, builtin_w, builtin_h,
                            x, y, button);
    return 1;
}

void gui_server_mouse_move(int x, int y)
{
    uint64_t flags = spinlock_lock_irqsave(&gui_lock);
    gui_window_t* window = 0;

    if (resizing_window_id != 0U)
    {
        for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
            if (windows[i].used && windows[i].visible &&
                windows[i].id == resizing_window_id)
            {
                window = &windows[i];
                break;
            }
        if (!window || window->maximized)
            resizing_window_id = 0U;
        else
        {
            int32_t max_width = (int32_t)graphics_get_width() - window->x;
            int32_t work_bottom = (int32_t)graphics_get_height() - 64;
            int32_t max_height = work_bottom - window->y;
            int32_t title_length = 0;
            while (title_length <= (int32_t)VANTA_GUI_TITLE_MAX &&
                   window->title[title_length])
                title_length++;
            int32_t min_width = (int32_t)GUI_WINDOW_MIN_WIDTH;
            int32_t title_min_width = title_length * 8 + 180;
            if (title_min_width > min_width)
                min_width = title_min_width;
            int32_t min_height = (int32_t)GUI_WINDOW_MIN_HEIGHT;
            if (max_width < min_width) min_width = max_width;
            if (max_height < min_height) min_height = max_height;
            if (max_width < 1) max_width = 1;
            if (max_height < 1) max_height = 1;
            int32_t new_width = (int32_t)resize_start_width + x - resize_start_x;
            int32_t new_height = (int32_t)resize_start_height + y - resize_start_y;
            if (new_width < min_width) new_width = min_width;
            if (new_height < min_height) new_height = min_height;
            if (new_width > max_width) new_width = max_width;
            if (new_height > max_height) new_height = max_height;
            if (new_width < 1) new_width = 1;
            if (new_height < 1) new_height = 1;
            if (window->width != (uint32_t)new_width ||
                window->height != (uint32_t)new_height)
            {
                window->width = (uint32_t)new_width;
                window->height = (uint32_t)new_height;
                gui_emit_resize_locked(window);
                redraw_pending = 1;
            }
            spinlock_unlock_irqrestore(&gui_lock, flags);
            return;
        }
    }

    if (dragging_window_id != 0U)
    {
        for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
            if (windows[i].used && windows[i].visible &&
                windows[i].id == dragging_window_id)
            {
                window = &windows[i];
                break;
            }
        if (!window)
            dragging_window_id = 0U;
        else
        {
            int32_t max_x = (int32_t)graphics_get_width() -
                            (int32_t)window->width;
            int32_t max_y = (int32_t)graphics_get_height() -
                            (int32_t)GUI_TITLEBAR_HEIGHT;
            if (max_x < 0) max_x = 0;
            if (max_y < 0) max_y = 0;
            int32_t new_x = x - drag_offset_x;
            int32_t new_y = y - drag_offset_y;
            if (new_x < 0) new_x = 0;
            if (new_y < 0) new_y = 0;
            if (new_x > max_x) new_x = max_x;
            if (new_y > max_y) new_y = max_y;
            if (window->x != new_x || window->y != new_y)
            {
                window->x = new_x;
                window->y = new_y;
                redraw_pending = 1;
            }
            spinlock_unlock_irqrestore(&gui_lock, flags);
            return;
        }
    }

    for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
        if (windows[i].used && windows[i].visible &&
            windows[i].id == focused_window_id)
        {
            window = &windows[i];
            break;
        }
    if (window && x >= window->x && y >= window->y &&
        x < window->x + (int)window->width &&
        y < window->y + (int)window->height)
    {
        vanta_gui_event_t event = {
            VANTA_GUI_EVENT_POINTER_MOVE,
            x - window->x - GUI_CLIENT_INSET_X,
            y - window->y - GUI_CLIENT_INSET_Y,
            0U,
            0U
        };
        gui_queue_event_locked(window, &event);
    }
    spinlock_unlock_irqrestore(&gui_lock, flags);
}

void gui_server_mouse_release(int button)
{
    if (button != 1)
        return;
    uint64_t flags = spinlock_lock_irqsave(&gui_lock);

    /* Dragging a titlebar to the top edge is the conventional maximize gesture. */
    if (dragging_window_id != 0U)
    {
        for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
        {
            gui_window_t* window = &windows[i];
            if (window->used && window->visible &&
                window->id == dragging_window_id)
            {
                if (!window->maximized && window->y == 0)
                    gui_toggle_maximized_locked(window);
                break;
            }
        }
    }

    dragging_window_id = 0U;
    resizing_window_id = 0U;
    spinlock_unlock_irqrestore(&gui_lock, flags);
}

static void gui_taskbar_geometry(int screen_width, unsigned int count,
                                int* left_start, int* left_end,
                                int* right_start, int* right_end,
                                int* slot_width)
{
    int center = screen_width / 2;
    int clock_x = screen_width - 116;
    if (clock_x < center + 116)
        clock_x = center + 116;
    *left_start = 8;
    *left_end = center - 116;
    *right_start = center + 116;
    *right_end = clock_x - 8;
    if (*left_end < *left_start) *left_end = *left_start;
    if (*right_end < *right_start) *right_end = *right_start;
    int left_width = *left_end - *left_start;
    int right_width = *right_end - *right_start;
    if (count == 0U)
    {
        *slot_width = 0;
        return;
    }
    if (right_width > 0 && count > 1U)
    {
        int left_slots = ((int)count + 1) / 2;
        int right_slots = (int)count / 2;
        int left_slot_width = left_width / left_slots;
        int right_slot_width = right_width / right_slots;
        *slot_width = left_slot_width < right_slot_width
                    ? left_slot_width : right_slot_width;
    }
    else
        *slot_width = left_width / (int)count;
    if (*slot_width > 120) *slot_width = 120;
    if (*slot_width < 16) *slot_width = 16;
}

void gui_server_draw_taskbar_items(int taskbar_y, int screen_width)
{
    if (!graphics_is_initialized() || screen_width <= 0)
        return;

    typedef struct
    {
        int focused;
        int minimized;
        char title[VANTA_GUI_TITLE_MAX + 1U];
    } taskbar_item_t;
    taskbar_item_t items[VANTA_GUI_MAX_WINDOWS];
    unsigned int count = 0U;

    uint64_t flags = spinlock_lock_irqsave(&gui_lock);
    for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
    {
        gui_window_t* window = &windows[i];
        if (!window->used || !window->owner_visible)
            continue;
        items[count].focused = window->id == focused_window_id && window->visible;
        items[count].minimized = window->minimized;
        for (size_t n = 0; n < sizeof(items[count].title); n++)
            items[count].title[n] = window->title[n];
        count++;
    }
    spinlock_unlock_irqrestore(&gui_lock, flags);
    if (count == 0U)
        return;

    int left_start, left_end, right_start, right_end, slot_width;
    gui_taskbar_geometry(screen_width, count, &left_start, &left_end,
                         &right_start, &right_end, &slot_width);
    int slot_x = left_start;
    int in_right = 0;
    for (unsigned int i = 0; i < count; i++)
    {
        if (!in_right && slot_x + slot_width > left_end)
        {
            slot_x = right_start;
            in_right = 1;
        }
        int region_end = in_right ? right_end : left_end;
        if (slot_x + slot_width > region_end)
            break;
        int button_width = slot_width - 4;
        if (button_width < 16) button_width = 16;
        int focused = items[i].focused;
        uint32_t fill = focused ? 0x00253648U
                      : items[i].minimized ? 0x00131D29U : 0x00192533U;
        graphics_fill_rounded_rect(slot_x, taskbar_y, button_width, 42, 7, fill);
        graphics_fill_rect(slot_x + 1, taskbar_y + 1, button_width - 2, 1,
                           focused ? 0x005DA9FFU : 0x00334455U);
        int icon_size = button_width >= 48 ? 18 : 12;
        graphics_draw_vanta_logo(slot_x + 4, taskbar_y + (42 - icon_size) / 2,
                                 icon_size);
        if (button_width >= 64)
        {
            char label[9];
            size_t max_chars = (size_t)(button_width - 30) / 8U;
            if (max_chars > 7U) max_chars = 7U;
            size_t n = 0U;
            while (n < max_chars && items[i].title[n])
            {
                label[n] = items[i].title[n];
                n++;
            }
            label[n] = '\0';
            graphics_draw_text(slot_x + 26, taskbar_y + 17, label,
                               items[i].minimized ? 0x009AAABD : 0x00F3F6FA, 1);
        }
        if (focused)
            graphics_fill_rounded_rect(slot_x + button_width / 2 - 12,
                                       taskbar_y + 38, 24, 2, 1, 0x005DA9FFU);
        slot_x += slot_width;
    }
}

int gui_server_taskbar_click(int x, int y, int taskbar_y, int screen_width)
{
    if (y < taskbar_y + 8 || y >= taskbar_y + 56 || screen_width <= 0)
        return 0;
    uint64_t flags = spinlock_lock_irqsave(&gui_lock);
    unsigned int count = 0U;
    for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
        if (windows[i].used && windows[i].owner_visible)
            count++;
    int left_start, left_end, right_start, right_end, slot_width;
    gui_taskbar_geometry(screen_width, count, &left_start, &left_end,
                         &right_start, &right_end, &slot_width);
    int slot_x = left_start;
    int in_right = 0;
    unsigned int item_index = 0U;
    for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
    {
        gui_window_t* window = &windows[i];
        if (!window->used || !window->owner_visible)
            continue;
        if (!in_right && slot_x + slot_width > left_end)
        {
            slot_x = right_start;
            in_right = 1;
        }
        int region_end = in_right ? right_end : left_end;
        if (slot_x + slot_width > region_end)
            break;
        int button_width = slot_width - 4;
        if (button_width < 16) button_width = 16;
        if (x >= slot_x && x < slot_x + button_width)
        {
            if (window->visible && window->id == focused_window_id)
                gui_minimize_locked(window);
            else
                gui_restore_locked(window);
            spinlock_unlock_irqrestore(&gui_lock, flags);
            return 1;
        }
        slot_x += slot_width;
        item_index++;
    }
    (void)item_index;
    spinlock_unlock_irqrestore(&gui_lock, flags);
    return 0;
}

char gui_server_read_terminal_char(void)
{
    uint64_t flags = spinlock_lock_irqsave(&gui_lock);
    gui_window_t* focused = 0;
    for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
        if (windows[i].used && windows[i].visible &&
            windows[i].id == focused_window_id)
        {
            focused = &windows[i];
            break;
        }
    if (!focused || !focused->builtin ||
        !gui_title_equal(focused->title, "Terminal"))
    {
        spinlock_unlock_irqrestore(&gui_lock, flags);
        return 0;
    }

    int input_enabled = terminal_process_input_enabled();
    while (keyboard_has_event())
    {
        keyboard_event_t event = keyboard_get_event();
        if (!input_enabled)
            continue;
        if (event == KEY_EVENT_PAGE_UP)
        {
            terminal_scroll_up();
            terminal_process_mark_redraw();
        }
        else if (event == KEY_EVENT_PAGE_DOWN)
        {
            terminal_scroll_down();
            terminal_process_mark_redraw();
        }
    }

    char character = keyboard_has_char() ? keyboard_get_char() : 0;
    spinlock_unlock_irqrestore(&gui_lock, flags);
    return input_enabled ? character : 0;
}

int gui_server_take_redraw_request(void)
{
    uint64_t flags = spinlock_lock_irqsave(&gui_lock);
    int pending = redraw_pending;
    redraw_pending = 0;
    spinlock_unlock_irqrestore(&gui_lock, flags);
    return pending;
}

void gui_server_cleanup_dead_windows(void)
{
    uint32_t ids[VANTA_GUI_MAX_WINDOWS];
    uint32_t owners[VANTA_GUI_MAX_WINDOWS];
    unsigned int count = 0U;
    uint64_t flags = spinlock_lock_irqsave(&gui_lock);
    for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
        if (windows[i].used && !windows[i].builtin)
        {
            ids[count] = windows[i].id;
            owners[count] = windows[i].owner_pid;
            count++;
        }
    spinlock_unlock_irqrestore(&gui_lock, flags);

    for (unsigned int i = 0; i < count; i++)
    {
        process_t process;
        if (!process_snapshot(owners[i], &process) ||
            process.state == PROCESS_UNUSED ||
            process.state == PROCESS_TERMINATED ||
            process.state == PROCESS_ZOMBIE ||
            process.state == PROCESS_REAPING)
            (void)gui_server_close_window(owners[i], ids[i]);
    }
}
