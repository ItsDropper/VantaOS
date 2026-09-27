#include "file_explorer.h"

#include "filesystem.h"
#include "graphics.h"
#include "graphics_internal.h"
#include <stdint.h>

#define FILES_WINDOW_MARGIN 24
#define FILES_SIDEBAR_WIDTH 190
#define FILES_HEADER_HEIGHT 48
#define FILES_TOOLBAR_HEIGHT 42
#define FILES_ROW_HEIGHT 32
#define FILES_MAX_VISIBLE 12

static uint32_t explorer_directory;
static int explorer_file = -1;
static int explorer_initialized;

static void explorer_set_directory(uint32_t id)
{
    const fs_node_t* node = filesystem_get_node(id);

    if (!node || node->type != FS_NODE_DIRECTORY)
        return;

    /*
     * Directory nodes backed by FAT32 are lazy-loaded. Open the directory
     * through the filesystem layer before changing Explorer state so a
     * directory that cannot be read is never presented as successfully
     * opened.
     */
    if (filesystem_list(id, 0, 0) < 0)
        return;

    explorer_directory = id;
    explorer_file = -1;
}

static void explorer_go_up(void)
{
    const fs_node_t* node =
        filesystem_get_node(explorer_directory);

    if (!node || node->id == filesystem_root())
        return;

    explorer_set_directory(node->parent);
}

static const char* explorer_directory_name(void)
{
    if (explorer_directory == filesystem_root())
        return "C:";

    const fs_node_t* node =
        filesystem_get_node(explorer_directory);

    if (!node || !node->name[0])
        return "C:";

    return node->name;
}

static void explorer_draw_icon(
    int x,
    int y,
    int directory
)
{
    if (directory)
    {
        graphics_fill_rect(x, y + 4, 18, 13, 0x005AA9E6);
        graphics_fill_rect(x + 2, y + 1, 8, 5, 0x005AA9E6);
        return;
    }

    graphics_fill_rect(x + 2, y, 14, 18, 0x0095A8BF);
    graphics_fill_rect(x + 5, y + 3, 8, 2, 0x00161E28);
    graphics_fill_rect(x + 5, y + 7, 8, 2, 0x00161E28);
    graphics_fill_rect(x + 5, y + 11, 6, 2, 0x00161E28);
}

static void explorer_draw_sidebar_item(
    int x,
    int y,
    const char* label,
    int selected,
    int icon_type
)
{
    if (selected)
        graphics_fill_rounded_rect(
            x, y, FILES_SIDEBAR_WIDTH - 24, 34, 7,
            0x00263B50
        );

    if (icon_type == 0)
    {
        graphics_fill_rect(x + 12, y + 9, 14, 12, 0x005AA9E6);
        graphics_fill_rect(x + 14, y + 7, 7, 4, 0x005AA9E6);
    }
    else if (icon_type == 1)
    {
        graphics_fill_rect(x + 12, y + 7, 16, 16, 0x003B82F6);
        graphics_draw_text(x + 15, y + 10, "C", 0x00FFFFFF, 1);
    }
    else
    {
        graphics_fill_rect(x + 12, y + 7, 16, 16, 0x0057B77E);
        graphics_draw_text(x + 15, y + 10, "~", 0x00FFFFFF, 1);
    }

    graphics_draw_text(
        x + 38, y + 11, label,
        selected ? 0x00FFFFFF : 0x00B7C5D1, 1
    );
}

void file_explorer_initialize(void)
{
    explorer_initialized = 1;
    explorer_directory = filesystem_root();
    explorer_file = -1;
}

void file_explorer_open_directory(uint32_t id)
{
    if (!explorer_initialized)
        file_explorer_initialize();

    explorer_set_directory(id);
}

void file_explorer_draw(int width, int height)
{
    if (!explorer_initialized)
        file_explorer_initialize();

    int ww = width - FILES_WINDOW_MARGIN * 2;
    int wh = height - 100;

    if (ww > 920)
        ww = 920;
    if (wh > 560)
        wh = 560;

    int wx = width / 2 - ww / 2;
    int wy = height / 2 - wh / 2;

    graphics_fill_rounded_rect(
        wx + 7, wy + 9, ww, wh, 12, 0x00000000
    );

    graphics_fill_rounded_rect(
        wx, wy, ww, wh, 12, 0x00161E28
    );

    graphics_fill_rect(
        wx, wy, ww, FILES_HEADER_HEIGHT,
        0x00212B37
    );

    graphics_draw_text(
        wx + 20, wy + 15, "File Explorer",
        0x00FFFFFF, 2
    );

    graphics_draw_text(
        wx + ww - 30, wy + 15, "X",
        0x00FFFFFF, 2
    );

    int sidebar_x = wx;
    int sidebar_y = wy + FILES_HEADER_HEIGHT;
    int sidebar_h = wh - FILES_HEADER_HEIGHT;

    graphics_fill_rect(
        sidebar_x, sidebar_y,
        FILES_SIDEBAR_WIDTH, sidebar_h,
        0x00121B24
    );

    graphics_draw_text(
        sidebar_x + 16, sidebar_y + 22,
        "QUICK ACCESS", 0x007F95A8, 1
    );

    explorer_draw_sidebar_item(
        sidebar_x + 12, sidebar_y + 38,
        "C:", explorer_directory == filesystem_root(), 1
    );

    int system_id = filesystem_lookup("/system");
    explorer_draw_sidebar_item(
        sidebar_x + 12, sidebar_y + 76,
        "System",
        system_id >= 0 &&
        explorer_directory == (uint32_t)system_id,
        0
    );

    int home_id = filesystem_lookup("/home");
    explorer_draw_sidebar_item(
        sidebar_x + 12, sidebar_y + 114,
        "Home",
        home_id >= 0 &&
        explorer_directory == (uint32_t)home_id,
        2
    );

    graphics_draw_text(
        sidebar_x + 16, sidebar_y + 170,
        "SYSTEM FILES", 0x007F95A8, 1
    );

    graphics_draw_text(
        sidebar_x + 16, sidebar_y + 194,
        "etc", 0x00B7C5D1, 1
    );
    graphics_draw_text(
        sidebar_x + 16, sidebar_y + 218,
        "drivers", 0x00B7C5D1, 1
    );
    graphics_draw_text(
        sidebar_x + 16, sidebar_y + 242,
        "devices", 0x00B7C5D1, 1
    );

    int content_x = wx + FILES_SIDEBAR_WIDTH;
    int content_w = ww - FILES_SIDEBAR_WIDTH;

    graphics_fill_rect(
        content_x, sidebar_y,
        content_w, FILES_TOOLBAR_HEIGHT,
        0x0019232D
    );

    graphics_fill_rounded_rect(
        content_x + 12, sidebar_y + 6, 34, 30, 6,
        0x00202C39
    );
    graphics_draw_text(
        content_x + 24, sidebar_y + 14,
        "<", 0x00FFFFFF, 1
    );

    graphics_fill_rounded_rect(
        content_x + 52, sidebar_y + 6, 34, 30, 6,
        0x00202C39
    );
    graphics_draw_text(
        content_x + 64, sidebar_y + 14,
        "^", 0x00FFFFFF, 1
    );

    graphics_fill_rect(
        content_x + 98, sidebar_y + 6,
        content_w - 112, 30,
        0x00202C39
    );

    graphics_draw_text(
        content_x + 110, sidebar_y + 14,
        explorer_directory_name(),
        0x00E7EEF4, 1
    );

    int list_y = sidebar_y + FILES_TOOLBAR_HEIGHT;
    graphics_fill_rect(
        content_x, list_y, content_w,
        30, 0x001A2633
    );

    graphics_draw_text(
        content_x + 18, list_y + 9,
        "Name", 0x007F95A8, 1
    );
    graphics_draw_text(
        content_x + content_w - 150, list_y + 9,
        "Type", 0x007F95A8, 1
    );
    graphics_draw_text(
        content_x + content_w - 68, list_y + 9,
        "Size", 0x007F95A8, 1
    );

    if (explorer_file >= 0)
    {
        const fs_node_t* file =
            filesystem_get_node((uint32_t)explorer_file);

        if (file)
        {
            graphics_draw_text(
                content_x + 18, list_y + 54,
                file->name, 0x005AA9E6, 2
            );

            graphics_draw_text(
                content_x + 18, list_y + 88,
                "File contents", 0x007F95A8, 1
            );

            char content[FS_FILE_MAX];
            int bytes = filesystem_read(
                file->id, content, sizeof(content)
            );

            if (bytes >= 0)
                graphics_draw_text(
                    content_x + 18, list_y + 116,
                    content, 0x00F2F5F8, 1
                );
            else
                graphics_draw_text(
                    content_x + 18, list_y + 116,
                    "Unable to read this file.",
                    0x00F2F5F8, 1
                );

            graphics_draw_text(
                content_x + 18, list_y + 150,
                "Click Up to return to the directory.",
                0x007F95A8, 1
            );
        }

        return;
    }

    uint32_t ids[FS_MAX_NODES];
    int count = filesystem_list(
        explorer_directory, ids, FS_MAX_NODES
    );

    if (count < 0)
        count = 0;

    if (count > FILES_MAX_VISIBLE)
        count = FILES_MAX_VISIBLE;

    for (int i = 0; i < count; i++)
    {
        const fs_node_t* node =
            filesystem_get_node(ids[i]);

        if (!node)
            continue;

        int row_y = list_y + 30 + i * FILES_ROW_HEIGHT;

        if ((i & 1) == 0)
            graphics_fill_rect(
                content_x, row_y,
                content_w, FILES_ROW_HEIGHT,
                0x0019232D
            );

        explorer_draw_icon(
            content_x + 18,
            row_y + 7,
            node->type == FS_NODE_DIRECTORY
        );

        graphics_draw_text(
            content_x + 46, row_y + 10,
            node->name[0] ? node->name : "C:",
            0x00E7EEF4, 1
        );

        graphics_draw_text(
            content_x + content_w - 150,
            row_y + 10,
            node->type == FS_NODE_DIRECTORY ?
            "Folder" :
            "File",
            0x007F95A8, 1
        );

        if (node->type != FS_NODE_DIRECTORY)
        {
            char size_text[12];
            unsigned int size = node->size;
            int pos = 0;

            if (size >= 100)
                size_text[pos++] = (char)('0' + (size / 100) % 10);
            if (size >= 10 || pos)
                size_text[pos++] = (char)('0' + (size / 10) % 10);
            size_text[pos++] = (char)('0' + size % 10);
            size_text[pos++] = 'B';
            size_text[pos] = 0;

            graphics_draw_text(
                content_x + content_w - 68,
                row_y + 10,
                size_text, 0x007F95A8, 1
            );
        }
    }

    if (count == 0)
        graphics_draw_text(
            content_x + 20, list_y + 58,
            "This folder is empty.",
            0x007F95A8, 1
        );
}

int file_explorer_click(
    int x,
    int y,
    int width,
    int height
)
{
    int ww = width - FILES_WINDOW_MARGIN * 2;
    int wh = height - 100;

    if (ww > 920)
        ww = 920;
    if (wh > 560)
        wh = 560;

    int wx = width / 2 - ww / 2;
    int wy = height / 2 - wh / 2;

    if (x >= wx + ww - 52 &&
        x < wx + ww &&
        y >= wy &&
        y < wy + FILES_HEADER_HEIGHT)
        return 1;

    int sidebar_y = wy + FILES_HEADER_HEIGHT;

    if (x >= wx &&
        x < wx + FILES_SIDEBAR_WIDTH)
    {
        if (y >= sidebar_y + 38 &&
            y < sidebar_y + 72)
        {
            explorer_set_directory(filesystem_root());
            return 0;
        }

        if (y >= sidebar_y + 76 &&
            y < sidebar_y + 110)
        {
            int id = filesystem_lookup("/system");
            if (id >= 0)
                explorer_set_directory((uint32_t)id);
            return 0;
        }

        if (y >= sidebar_y + 114 &&
            y < sidebar_y + 148)
        {
            int id = filesystem_lookup("/home");
            if (id >= 0)
                explorer_set_directory((uint32_t)id);
            return 0;
        }

        return 0;
    }

    int content_x = wx + FILES_SIDEBAR_WIDTH;
    int list_y = sidebar_y + FILES_TOOLBAR_HEIGHT;

    if (x >= content_x + 12 &&
        x < content_x + 46 &&
        y >= sidebar_y + 6 &&
        y < sidebar_y + 36)
    {
        explorer_go_up();
        return 0;
    }

    if (x >= content_x + 52 &&
        x < content_x + 86 &&
        y >= sidebar_y + 6 &&
        y < sidebar_y + 36)
    {
        explorer_go_up();
        return 0;
    }

    if (explorer_file >= 0)
        return 0;

    if (x < content_x ||
        x >= wx + ww ||
        y < list_y + 30)
        return 0;

    int row = (y - list_y - 30) / FILES_ROW_HEIGHT;

    if (row < 0 || row >= FILES_MAX_VISIBLE)
        return 0;

    uint32_t ids[FS_MAX_NODES];
    int count = filesystem_list(
        explorer_directory, ids, FS_MAX_NODES
    );

    if (count < 0 || row >= count)
        return 0;

    const fs_node_t* node =
        filesystem_get_node(ids[row]);

    if (!node)
        return 0;

    if (node->type == FS_NODE_DIRECTORY)
        explorer_set_directory(node->id);
    else
        explorer_file = (int)node->id;

    return 0;
}
