/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "file_explorer.h"
#include "desktop_theme.h"

#include "filesystem.h"
#include "graphics.h"
#include "graphics_internal.h"
#include <stdint.h>

#define EXPLORER_MARGIN 24
#define EXPLORER_MAX_WIDTH 960
#define EXPLORER_MAX_HEIGHT 560
#define EXPLORER_TITLE 46
#define EXPLORER_SIDEBAR 188
#define EXPLORER_TOOLBAR 44
#define EXPLORER_ROW (desktop_theme_compact_rows() ? 28 : 34)
#define EXPLORER_BUTTON 46
#define EXPLORER_MAX_ROWS 12

static uint32_t explorer_directory;
static uint32_t explorer_previous_directory;
static int explorer_has_history;
static int explorer_file = -1;
static int explorer_initialized;
static int explorer_minimized;
static int explorer_system_id = -1;
static int explorer_home_id = -1;
static int explorer_etc_id = -1;
static int explorer_drivers_id = -1;
static int explorer_devices_id = -1;

static int explorer_directory_for_path(const char* path)
{
    int id = filesystem_ensure_directory(path);
    const fs_node_t* node;

    if (id < 0)
        return -1;

    node = filesystem_get_node((uint32_t)id);
    if (!node || node->type != FS_NODE_DIRECTORY)
        return -1;

    return id;
}

static void explorer_ensure_standard_directories(void)
{
    explorer_system_id = explorer_directory_for_path("/system");
    explorer_home_id = explorer_directory_for_path("/home");
    explorer_etc_id = explorer_directory_for_path("/etc");
    explorer_drivers_id = explorer_directory_for_path("/system/drivers");
    explorer_devices_id = explorer_directory_for_path("/system/devices");
}

static void explorer_set_directory(uint32_t id, int remember)
{
    const fs_node_t* node = filesystem_get_node(id);

    if (!node || node->type != FS_NODE_DIRECTORY)
        return;

    if (remember && explorer_directory != id)
    {
        explorer_previous_directory = explorer_directory;
        explorer_has_history = 1;
    }

    explorer_directory = id;
    explorer_file = -1;
}

static void explorer_go_back(void)
{
    if (!explorer_has_history)
        return;

    {
        uint32_t current = explorer_directory;
        explorer_directory = explorer_previous_directory;
        explorer_previous_directory = current;
        explorer_has_history = 0;
        explorer_file = -1;
    }
}

static void explorer_go_up(void)
{
    const fs_node_t* node = filesystem_get_node(explorer_directory);

    if (!node || node->id == filesystem_root())
        return;

    explorer_set_directory(node->parent, 1);
}

static int explorer_sidebar_hit(int x, int y, int wx, int wy)
{
    int sidebar_y = wy + EXPLORER_TITLE;
    int button_x = wx + 12;
    int button_w = EXPLORER_SIDEBAR - 24;

    /*
     * Use the actual rendered button rectangles, not the whole sidebar.
     * Items 0-2 are the location buttons; 3-5 are the system shortcuts.
     */
    if (x < button_x || x >= button_x + button_w)
        return -1;

    if (y >= sidebar_y + 38 && y < sidebar_y + 72)
        return 0;

    if (y >= sidebar_y + 76 && y < sidebar_y + 110)
        return 1;

    if (y >= sidebar_y + 114 && y < sidebar_y + 148)
        return 2;

    if (y >= sidebar_y + 188 && y < sidebar_y + 212)
        return 3;

    if (y >= sidebar_y + 212 && y < sidebar_y + 236)
        return 4;

    if (y >= sidebar_y + 236 && y < sidebar_y + 260)
        return 5;

    return -1;
}

static void explorer_sidebar_select(int item)
{
    int id = -1;

    if (item == 0)
    {
        explorer_set_directory(filesystem_root(), 1);
        return;
    }

    if (item == 1)
        id = explorer_system_id;
    else if (item == 2)
        id = explorer_home_id;
    else if (item == 3)
        id = explorer_etc_id;
    else if (item == 4)
        id = explorer_drivers_id;
    else if (item == 5)
        id = explorer_devices_id;

    if (id >= 0)
        explorer_set_directory((uint32_t)id, 1);
}

static const char* explorer_location_name(void)
{
    const fs_node_t* node;

    if (explorer_directory == filesystem_root())
        return "C:";

    node = filesystem_get_node(explorer_directory);
    if (!node || !node->name[0])
        return "C:";

    return node->name;
}

void file_explorer_window_geometry(
    int width, int height,
    int* x, int* y, int* w, int* h)
{
    if (explorer_maximized)
    {
        *x = 0;
        *y = 0;
        *w = width;
        *h = height - 64;
        return;
    }

    *w = width - EXPLORER_MARGIN * 2;
    *h = height - 100;

    if (*w > EXPLORER_MAX_WIDTH)
        *w = EXPLORER_MAX_WIDTH;

    if (*h > EXPLORER_MAX_HEIGHT)
        *h = EXPLORER_MAX_HEIGHT;

    *x = explorer_x >= 0 ? explorer_x : width / 2 - *w / 2;
    *y = explorer_y >= 0 ? explorer_y : height / 2 - *h / 2;
}

void file_explorer_initialize(void)
{
    if (explorer_initialized)
        return;

    explorer_initialized = 1;
    explorer_directory = filesystem_root();
    explorer_previous_directory = explorer_directory;
    explorer_has_history = 0;
    explorer_file = -1;
    explorer_minimized = 0;

    explorer_dragging = 0;
    explorer_drag_offset_x = 0;
    explorer_drag_offset_y = 0;
    explorer_maximized = 0;
    explorer_x = -1;
    explorer_y = -1;
    explorer_restore_x = -1;
    explorer_restore_y = -1;

    explorer_ensure_standard_directories();
}

int file_explorer_is_dragging(void)
{
    return explorer_dragging;
}

void file_explorer_minimize(void)
{
    explorer_dragging = 0;
    explorer_minimized = 1;
    active_panel = 0;
}

void file_explorer_restore(void)
{
    if (!explorer_initialized)
        file_explorer_initialize();

    explorer_minimized = 0;
    active_panel = 2;
}

void file_explorer_open_directory(uint32_t id)
{
    if (!explorer_initialized)
        file_explorer_initialize();

    explorer_set_directory(id, 0);
}

static int explorer_suffix_equal(const char* name, const char* suffix)
{
    int name_len = 0;
    int suffix_len = 0;

    while (name[name_len]) ++name_len;
    while (suffix[suffix_len]) ++suffix_len;
    if (suffix_len > name_len) return 0;

    int offset = name_len - suffix_len;
    for (int i = 0; i < suffix_len; ++i)
    {
        char a = name[offset + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

static int explorer_file_kind(const fs_node_t* node)
{
    if (!node || node->type == FS_NODE_DIRECTORY) return 0;
    if (explorer_suffix_equal(node->name, ".c") ||
        explorer_suffix_equal(node->name, ".h") ||
        explorer_suffix_equal(node->name, ".asm") ||
        explorer_suffix_equal(node->name, ".s")) return 1;
    if (explorer_suffix_equal(node->name, ".txt") ||
        explorer_suffix_equal(node->name, ".md")) return 2;
    if (explorer_suffix_equal(node->name, ".log")) return 3;
    if (explorer_suffix_equal(node->name, ".cfg") ||
        explorer_suffix_equal(node->name, ".ini")) return 4;
    if (explorer_suffix_equal(node->name, ".elf") ||
        explorer_suffix_equal(node->name, ".bin")) return 5;
    return 6;
}

static void explorer_icon(int x, int y, const fs_node_t* node, int selected)
{
    int kind = explorer_file_kind(node);
    uint32_t tint = kind == 0 ? 0x005AA9E6 :
                    kind == 1 ? 0x007DB8FF :
                    kind == 2 ? 0x0069D6AE :
                    kind == 3 ? 0x00E6BD78 :
                    kind == 4 ? 0x00B5A0FF :
                    kind == 5 ? 0x00E17F89 : 0x008EA3B7;

    if (selected)
        graphics_fill_rounded_rect(x - 6, y - 5, 30, 30, 6, 0x002A3B4E);

    if (kind == 0)
    {
        graphics_fill_rounded_rect(x, y + 5, 18, 13, 3, tint);
        graphics_fill_rounded_rect(x + 2, y + 2, 9, 5, 2, tint);
        graphics_fill_rect(x + 3, y + 10, 12, 1, 0x002C5B83);
    }
    else
    {
        graphics_fill_rounded_rect(x + 2, y, 15, 19, 2, tint);
        graphics_fill_rect(x + 5, y + 5, 9, 1, 0x00131D28);
        graphics_fill_rect(x + 5, y + 9, 9, 1, 0x00131D28);
        graphics_fill_rect(x + 5, y + 13, 6, 1, 0x00131D28);
        if (kind == 1)
            graphics_draw_text(x + 4, y + 6, "C", 0x00131D28, 1);
        else if (kind == 3)
            graphics_draw_text(x + 4, y + 6, "L", 0x00131D28, 1);
        else if (kind == 4)
            graphics_draw_text(x + 4, y + 6, "=", 0x00131D28, 1);
        else if (kind == 5)
            graphics_draw_text(x + 4, y + 6, "E", 0x00131D28, 1);
    }
}

static void explorer_sidebar_item(
    int x, int y, const char* label, int selected, int type)
{
    if (selected)
        graphics_fill_rounded_rect(
            x, y, EXPLORER_SIDEBAR - 24, 34, 7, 0x002A3B4E);

    if (type == 0)
    {
        graphics_fill_rect(x + 12, y + 9, 16, 12, 0x005AA9E6);
        graphics_fill_rect(x + 14, y + 6, 8, 5, 0x005AA9E6);
    }
    else if (type == 1)
    {
        graphics_fill_rounded_rect(
            x + 11, y + 7, 18, 18, 4, VANTA_UI_ACCENT);
        graphics_draw_text(x + 16, y + 11, "C", 0x00FFFFFF, 1);
    }
    else
    {
        graphics_fill_rounded_rect(
            x + 11, y + 7, 18, 18, 4, 0x0057B77E);
        graphics_draw_text(x + 15, y + 11, "~", 0x00FFFFFF, 1);
    }

    graphics_draw_text(
        x + 38, y + 11, label,
        selected ? 0x00FFFFFF : VANTA_UI_TEXT, 1);
}

static void explorer_title(int x, int y, int w, int h)
{
    graphics_draw_window_frame(x, y, w, h, "Files", 1);
}

static void explorer_toolbar(int x, int y, int w)
{
    int content = x + EXPLORER_SIDEBAR;

    graphics_fill_rect(
        content, y, w - EXPLORER_SIDEBAR,
        EXPLORER_TOOLBAR, VANTA_UI_TOOLBAR);

    graphics_ui_explorer_toolbar_button(content + 10, y + 6, "<",
                                        explorer_has_history);
    graphics_ui_explorer_toolbar_button(content + 48, y + 6, "^", 1);
    graphics_ui_explorer_location(content + 90, y + 6,
        w - EXPLORER_SIDEBAR - 104, explorer_location_name());
}

void file_explorer_draw(int width, int height)
{
    int ww, wh, wx, wy;
    int sidebar_y;
    int content_x;
    int content_w;
    int list_y;
    uint32_t ids[FS_MAX_NODES];
    int count;
    int i;

    if (!explorer_initialized)
        file_explorer_initialize();

    file_explorer_window_geometry(
        width, height, &wx, &wy, &ww, &wh);

    explorer_title(wx, wy, ww, wh);

    sidebar_y = wy + EXPLORER_TITLE;

    graphics_fill_rect(
        wx, sidebar_y, EXPLORER_SIDEBAR,
        wh - EXPLORER_TITLE, VANTA_UI_SURFACE);

    graphics_ui_explorer_sidebar_heading(wx + 16, sidebar_y + 18, "LOCATIONS");

    {
        int system_id = explorer_directory_for_path("/system");
        int home_id = explorer_directory_for_path("/home");

        explorer_sidebar_item(
            wx + 12, sidebar_y + 38,
            "C:", explorer_directory == filesystem_root(), 1);

        explorer_sidebar_item(
            wx + 12, sidebar_y + 76,
            "System",
            system_id >= 0 &&
            explorer_directory == (uint32_t)system_id, 0);

        explorer_sidebar_item(
            wx + 12, sidebar_y + 114,
            "Home",
            home_id >= 0 &&
            explorer_directory == (uint32_t)home_id, 2);
    }

    graphics_ui_explorer_sidebar_heading(wx + 16, sidebar_y + 166, "SYSTEM");

    /* System shortcuts use the same restrained row treatment. */
    for (int item = 0; item < 3; item++)
        graphics_fill_rounded_rect(wx + 12, sidebar_y + 188 + item * 24,
            EXPLORER_SIDEBAR - 24, 24, 5, VANTA_UI_SURFACE);
    graphics_draw_text(wx + 20, sidebar_y + 194, "/etc", VANTA_UI_TEXT, 1);
    graphics_draw_text(wx + 20, sidebar_y + 218, "/drivers", VANTA_UI_TEXT, 1);
    graphics_draw_text(wx + 20, sidebar_y + 242, "/devices", VANTA_UI_TEXT, 1);

    /*
     * Show the active location explicitly. This also makes it obvious that
     * a sidebar click changed Explorer state even when the directory is
     * currently empty.
     */
    graphics_fill_rect(
        wx + 12, sidebar_y + wh - EXPLORER_TITLE - 42,
        EXPLORER_SIDEBAR - 24, 1, 0x002A3B4E);
    graphics_draw_text(
        wx + 16, sidebar_y + wh - EXPLORER_TITLE - 28,
        explorer_location_name(), 0x006F879A, 1);

    explorer_toolbar(wx, sidebar_y, ww);

    content_x = wx + EXPLORER_SIDEBAR;
    content_w = ww - EXPLORER_SIDEBAR;
    list_y = sidebar_y + EXPLORER_TOOLBAR;

    count = filesystem_list(explorer_directory, ids, FS_MAX_NODES);
    if (count < 0)
        count = 0;
    int total_count = count;
    int visible_limit = (wh - EXPLORER_TITLE - EXPLORER_TOOLBAR - 32 - 26)
        / EXPLORER_ROW;
    if (visible_limit < 0)
        visible_limit = 0;
    if (visible_limit > EXPLORER_MAX_ROWS)
        visible_limit = EXPLORER_MAX_ROWS;
    if (count > visible_limit)
        count = visible_limit;

    graphics_ui_explorer_column_header(content_x, list_y, content_w);

    for (i = 0; i < count; i++)
    {
        const fs_node_t* node = filesystem_get_node(ids[i]);
        int row_y = list_y + 32 + i * EXPLORER_ROW;

        if (!node)
            continue;

        graphics_ui_explorer_row(
            content_x, row_y, content_w, EXPLORER_ROW,
            explorer_file == (int)node->id, (i & 1) == 0);

        explorer_icon(
            content_x + 18, row_y + 8,
            node,
            explorer_file == (int)node->id);

        graphics_draw_text(
            content_x + 48, row_y + 11,
            node->name[0] ? node->name : "C:",
            0x00E7EEF4, 1);

        graphics_draw_text(
            content_x + content_w - 150, row_y + 11,
            node->type == FS_NODE_DIRECTORY ? "Folder" : "File",
            VANTA_UI_TEXT_SECONDARY, 1);

        if (node->type != FS_NODE_DIRECTORY)
        {
            char size_text[12];
            unsigned int size = node->size;
            int p = 0;

            if (size >= 100)
                size_text[p++] = (char)('0' + (size / 100) % 10);
            if (size >= 10 || p)
                size_text[p++] = (char)('0' + (size / 10) % 10);
            size_text[p++] = (char)('0' + size % 10);
            size_text[p++] = 'B';
            size_text[p] = 0;

            graphics_draw_text(
                content_x + content_w - 66,
                row_y + 11, size_text,
                VANTA_UI_TEXT_SECONDARY, 1);
        }
    }

    if (count == 0)
    {
        graphics_ui_draw_empty_state(content_x, list_y + 32, content_w,
            wh - EXPLORER_TITLE - EXPLORER_TOOLBAR - 58,
            "No files here", "This directory has no visible entries.");
    }

    {
        char count_label[28];
        char number[12];
        unsigned int n = (unsigned int)total_count;
        int digits = 0;
        if (n == 0) number[digits++] = '0';
        else
        {
            char reverse[10];
            int r = 0;
            while (n && r < 9)
            {
                reverse[r++] = (char)('0' + n % 10);
                n /= 10;
            }
            while (r) number[digits++] = reverse[--r];
        }
        number[digits] = 0;
        int p = 0;
        for (int j = 0; number[j] && p < 20; ++j)
            count_label[p++] = number[j];
        const char* suffix = total_count == 1 ? " item" : " items";
        for (int j = 0; suffix[j] && p < 26; ++j)
            count_label[p++] = suffix[j];
        count_label[p] = 0;
        graphics_ui_explorer_status(wx + EXPLORER_SIDEBAR,
            wy + wh - 26, ww - EXPLORER_SIDEBAR, count_label, "VFS");
    }
}

int file_explorer_click(int x, int y, int width, int height)
{
    int ww, wh, wx, wy;
    int sidebar_y;
    int content_x;
    int list_y;

    if (!explorer_initialized)
        file_explorer_initialize();

    file_explorer_window_geometry(
        width, height, &wx, &wy, &ww, &wh);

    if (x < wx || x >= wx + ww || y < wy || y >= wy + wh)
        return 0;

    if (y < wy + EXPLORER_TITLE)
    {
        int close_x = wx + ww - EXPLORER_BUTTON;
        int max_x = close_x - EXPLORER_BUTTON;
        int min_x = max_x - EXPLORER_BUTTON;

        /*
         * Window chrome is handled here, not in graphics_input.c.
         * This gives drawing and hit-testing one owner and one geometry.
         */
        if (x >= close_x)
        {
            explorer_dragging = 0;
            active_panel = 0;
            return 1;
        }

        if (x >= max_x)
        {
            explorer_dragging = 0;

            if (!explorer_maximized)
            {
                explorer_restore_x = wx;
                explorer_restore_y = wy;
                explorer_maximized = 1;
            }
            else
            {
                explorer_maximized = 0;
                explorer_x = explorer_restore_x;
                explorer_y = explorer_restore_y;
            }

            return 1;
        }

        if (x >= min_x)
        {
            /*
             * Minimize is strictly a window-state operation. It never
             * touches the Explorer process or filesystem state.
             */
            explorer_dragging = 0;
            active_panel = 0;
            return 1;
        }

        if (!explorer_maximized)
        {
            explorer_dragging = 1;
            explorer_drag_offset_x = x - wx;
            explorer_drag_offset_y = y - wy;
        }

        return 1;
    }

    sidebar_y = wy + EXPLORER_TITLE;

    {
        int sidebar_item = explorer_sidebar_hit(x, y, wx, wy);

        if (sidebar_item >= 0)
        {
            explorer_sidebar_select(sidebar_item);
            return 1;
        }
    }

    content_x = wx + EXPLORER_SIDEBAR;
    list_y = sidebar_y + EXPLORER_TOOLBAR;

    if (x >= content_x + 10 &&
        x < content_x + 42 &&
        y >= sidebar_y + 6 &&
        y < sidebar_y + 36)
    {
        explorer_go_back();
        return 0;
    }

    if (x >= content_x + 48 &&
        x < content_x + 80 &&
        y >= sidebar_y + 6 &&
        y < sidebar_y + 36)
    {
        explorer_go_up();
        return 0;
    }

    if (x < content_x ||
        y < list_y + 32)
        return 0;

    {
        int row = (y - list_y - 32) / EXPLORER_ROW;
        uint32_t ids[FS_MAX_NODES];
        int count;

        if (row < 0 || row >= EXPLORER_MAX_ROWS)
            return 0;

        count = filesystem_list(
            explorer_directory, ids, FS_MAX_NODES);

        if (count < 0 || row >= count)
            return 0;

        {
            const fs_node_t* node =
                filesystem_get_node(ids[row]);

            if (!node)
                return 0;

            if (node->type == FS_NODE_DIRECTORY)
                explorer_set_directory(node->id, 1);
            else
                explorer_file = (int)node->id;
        }
    }

    return 0;
}
