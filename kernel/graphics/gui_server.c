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
} gui_window_t;

static gui_window_t windows[VANTA_GUI_MAX_WINDOWS];
static spinlock_t gui_lock = SPINLOCK_INITIALIZER;
static uint32_t next_window_id = 1U;
static uint64_t next_z_order = 1U;
static uint32_t focused_window_id;
static uint32_t dragging_window_id;
static int32_t drag_offset_x;
static int32_t drag_offset_y;
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
    char title[VANTA_GUI_TITLE_MAX + 1U];
    vanta_gui_draw_command_t commands[VANTA_GUI_MAX_COMMANDS];
    uint32_t command_count;
} gui_render_snapshot_t;

static gui_render_snapshot_t render_snapshot[VANTA_GUI_MAX_WINDOWS];


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
    }
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
            if (windows[i].used &&
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
        if (window->visible != visible)
            changed = 1;
        if (window->id == focused_window_id)
            focused_owned = 1;
        if (!newest_owned || window->z_order > newest_owned->z_order)
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
            window->visible = visible;
    }

    if (visible && newest_owned)
        gui_focus_locked(newest_owned);
    else if (!visible && focused_window_id == 0U)
    {
        gui_window_t* newest_visible = 0;
        for (unsigned int i = 0; i < VANTA_GUI_MAX_WINDOWS; i++)
            if (windows[i].used && windows[i].visible &&
                (!newest_visible ||
                 windows[i].z_order > newest_visible->z_order))
                newest_visible = &windows[i];
        if (newest_visible)
            gui_focus_locked(newest_visible);
    }

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
                                   window->title, 0);
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
                graphics_fill_rect(cx, cy, command->width, command->height,
                                   command->color & 0x00FFFFFFU);
            else if (command->type == VANTA_GUI_DRAW_TEXT)
                graphics_draw_text(cx, cy, command->text,
                                   command->color & 0x00FFFFFFU,
                                   command->scale);
        }
    }
}

int gui_server_mouse_click(int x, int y, int button)
{
    if (button != 1 && button != 3)
        return 0;

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
    if (y >= target->y && y < target->y + (int)GUI_TITLEBAR_HEIGHT &&
        x >= target->x + (int)target->width - 46)
    {
        gui_emit_simple_event_locked(target, VANTA_GUI_EVENT_CLOSE_REQUEST);
    }
    else if (y >= target->y &&
             y < target->y + (int)GUI_TITLEBAR_HEIGHT)
    {
        /* Titlebar drag belongs to the window manager, not the client. */
        dragging_window_id = target->id;
        drag_offset_x = x - target->x;
        drag_offset_y = y - target->y;
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
    return 1;
}

void gui_server_mouse_move(int x, int y)
{
    uint64_t flags = spinlock_lock_irqsave(&gui_lock);
    gui_window_t* window = 0;

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
    dragging_window_id = 0U;
    spinlock_unlock_irqrestore(&gui_lock, flags);
}

char gui_server_read_terminal_char(void)
{
    uint64_t flags = spinlock_lock_irqsave(&gui_lock);
    if (focused_window_id != 0U)
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
        if (windows[i].used)
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
