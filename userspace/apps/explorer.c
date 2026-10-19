/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include <vanta/gui.h>
#include <vanta/theme.h>

#define PATH_CAPACITY 256U
#define LIST_CAPACITY 4096U
#define ENTRY_CAPACITY 96U
#define ENTRY_NAME_CAPACITY 256U
#define DISPLAY_ROWS 11U
#define PREVIEW_ROWS 12U
#define WINDOW_WIDTH 760U
#define WINDOW_HEIGHT 460U
#define ROW_HEIGHT 20
#define INPUT_NONE 0
#define INPUT_PATH 1
#define INPUT_SEARCH 2
#define INPUT_DIALOG 3
#define DIALOG_NONE 0
#define DIALOG_CREATE_FILE 1
#define DIALOG_CREATE_DIRECTORY 2
#define DIALOG_RENAME 3
#define DIALOG_CONFIRM_DELETE 4
#define CONTEXT_NONE 0

static vanta_theme_t current_theme;
#define COLOR_TEXT vanta_theme_text_color(&current_theme)
#define COLOR_MUTED vanta_theme_muted_color(&current_theme)
#define COLOR_SURFACE vanta_theme_surface_color(&current_theme)
#define COLOR_ROW vanta_theme_row_color(&current_theme)
#define COLOR_ACCENT vanta_theme_accent_color(&current_theme)
#define COLOR_BORDER vanta_theme_border_color(&current_theme)
#define COLOR_SELECTED (current_theme.light ? 0x00DCEBFAU : 0x00253D56U)
#define COLOR_ERROR (current_theme.light ? 0x00B83C49U : 0x00F87171U)

static char current_path[PATH_CAPACITY] = "/";
static char list_buffer[LIST_CAPACITY];
static char entries[ENTRY_CAPACITY][ENTRY_NAME_CAPACITY];
static unsigned char entry_is_directory[ENTRY_CAPACITY];
static unsigned char entry_selected[ENTRY_CAPACITY];
static size_t entry_count;
static size_t visible_indices[ENTRY_CAPACITY];
static size_t visible_count;
static size_t selected_entry;
static size_t first_visible;
static int more_entries;
static char search_query[64];
static char clipboard_path[PATH_CAPACITY];
static char input_buffer[PATH_CAPACITY];
static int input_mode;
static int dialog_mode;
static int context_menu;
static int properties_active;
static int context_x;
static int context_y;
static size_t last_clicked_index = (size_t)-1;
static unsigned int consecutive_clicks;
static char preview_lines[PREVIEW_ROWS][VANTA_GUI_TEXT_MAX + 1U];
static size_t preview_count;
static int preview_active;
static char properties_path[PATH_CAPACITY];
static vanta_file_info_t properties_info;
static char status_text[VANTA_GUI_TEXT_MAX + 1U] = "Loading directory";
static uint32_t status_color = 0x008D9BACU;
static vanta_gui_draw_command_t commands[VANTA_GUI_MAX_COMMANDS];
static uint32_t command_count;
static uint32_t window_id;

/* Separate recursion levels keep recursive delete buffers off the user stack. */
static char delete_list_buffers[8][LIST_CAPACITY];
static char delete_path_buffers[8][PATH_CAPACITY];
static char copy_list_buffers[8][LIST_CAPACITY];
static char copy_source_paths[9][PATH_CAPACITY];
static char copy_target_paths[9][PATH_CAPACITY];
static char file_clipboard[ENTRY_CAPACITY][PATH_CAPACITY];
static size_t file_clipboard_count;
static int file_clipboard_cut;

static size_t text_length(const char* text)
{
    size_t length = 0U;
    while (text && text[length])
        length++;
    return length;
}

static char fold_char(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int text_equals_ci(const char* left, const char* right)
{
    size_t i = 0U;
    if (!left || !right)
        return 0;
    while (left[i] && right[i] && fold_char(left[i]) == fold_char(right[i]))
        i++;
    return left[i] == right[i];
}

static int text_contains_ci(const char* text, const char* query)
{
    if (!query || !query[0])
        return 1;
    size_t n = text_length(text), q = text_length(query);
    if (q > n)
        return 0;
    for (size_t i = 0; i + q <= n; i++)
    {
        size_t j = 0U;
        while (j < q && fold_char(text[i + j]) == fold_char(query[j]))
            j++;
        if (j == q)
            return 1;
    }
    return 0;
}

static int path_is_under(const char* path, const char* root)
{
    size_t path_length = text_length(path);
    size_t root_length = text_length(root);
    if (root_length == 1U && root[0] == '/')
        return path_length > 0U && path[0] == '/';
    if (path_length < root_length)
        return 0;
    for (size_t i = 0; i < root_length; i++)
        if (fold_char(path[i]) != fold_char(root[i]))
            return 0;
    return path_length == root_length || path[root_length] == '/';
}

static void copy_text(char* destination, size_t capacity, const char* source)
{
    if (!destination || capacity == 0U)
        return;
    size_t i = 0U;
    while (source && source[i] && i + 1U < capacity)
    {
        destination[i] = source[i];
        i++;
    }
    destination[i] = '\0';
}

static int append_text(char* destination, size_t capacity, const char* suffix)
{
    size_t used = text_length(destination);
    size_t i = 0U;
    if (used >= capacity)
        return 0;
    while (suffix && suffix[i] && used + i + 1U < capacity)
    {
        destination[used + i] = suffix[i];
        i++;
    }
    destination[used + i] = '\0';
    return !suffix || suffix[i] == '\0';
}

static int build_child_path(const char* directory, const char* leaf,
                            char* output, size_t capacity)
{
    if (!directory || !leaf || !leaf[0] || !output || capacity < 2U ||
        leaf[0] == '/' || text_contains_ci(leaf, "/"))
        return 0;
    copy_text(output, capacity, directory);
    size_t length = text_length(output);
    if (length == 0U || output[length - 1U] != '/')
        if (!append_text(output, capacity, "/"))
            return 0;
    return append_text(output, capacity, leaf);
}

static int build_entry_path(size_t index, char* output, size_t capacity)
{
    if (index >= entry_count)
        return 0;
    return build_child_path(current_path, entries[index], output, capacity);
}

static void set_status(const char* text, uint32_t color)
{
    copy_text(status_text, sizeof(status_text), text);
    status_color = color;
}

static void add_rect(int32_t x, int32_t y, int32_t width,
                     int32_t height, uint32_t color)
{
    if (command_count >= VANTA_GUI_MAX_COMMANDS || width <= 0 || height <= 0)
        return;
    vanta_gui_draw_command_t* command = &commands[command_count++];
    *command = (vanta_gui_draw_command_t){
        .type = VANTA_GUI_DRAW_FILL_RECT, .x = x, .y = y,
        .width = width, .height = height, .color = color, .scale = 1U
    };
}

static void add_outline(int32_t x, int32_t y, int32_t width,
                        int32_t height, uint32_t color)
{
    add_rect(x, y, width, 1, color);
    add_rect(x, y + height - 1, width, 1, color);
    add_rect(x, y, 1, height, color);
    add_rect(x + width - 1, y, 1, height, color);
}

static void add_text(int32_t x, int32_t y, const char* text, uint32_t color)
{
    size_t length = text_length(text);
    if (command_count >= VANTA_GUI_MAX_COMMANDS || length == 0U ||
        length > VANTA_GUI_TEXT_MAX)
        return;
    vanta_gui_draw_command_t* command = &commands[command_count++];
    *command = (vanta_gui_draw_command_t){
        .type = VANTA_GUI_DRAW_TEXT, .x = x, .y = y,
        .color = color, .scale = 1U
    };
    for (size_t i = 0; i <= length; i++)
        command->text[i] = text[i];
}

static void add_text_tail(int32_t x, int32_t y, const char* text,
                          uint32_t color, size_t max_chars)
{
    size_t length = text_length(text);
    if (length <= max_chars)
    {
        add_text(x, y, text, color);
        return;
    }
    char clipped[VANTA_GUI_TEXT_MAX + 1U];
    if (max_chars > VANTA_GUI_TEXT_MAX)
        max_chars = VANTA_GUI_TEXT_MAX;
    size_t start = length - max_chars;
    for (size_t i = 0; i < max_chars; i++)
        clipped[i] = text[start + i];
    clipped[max_chars] = '\0';
    add_text(x, y, clipped, color);
}

static size_t selected_count(void)
{
    size_t count = 0U;
    for (size_t i = 0; i < entry_count; i++)
        if (entry_selected[i])
            count++;
    return count;
}

static size_t first_selected(void)
{
    for (size_t i = 0; i < entry_count; i++)
        if (entry_selected[i])
            return i;
    return (size_t)-1;
}

static void clear_selection(void)
{
    for (size_t i = 0; i < entry_count; i++)
        entry_selected[i] = 0U;
    selected_entry = (size_t)-1;
}

static void select_single(size_t index)
{
    clear_selection();
    if (index < entry_count)
    {
        entry_selected[index] = 1U;
        selected_entry = index;
    }
}

static void build_visible(void)
{
    visible_count = 0U;
    for (size_t i = 0; i < entry_count; i++)
    {
        if (!text_contains_ci(entries[i], search_query))
            continue;
        visible_indices[visible_count++] = i;
    }
    if (first_visible > visible_count)
        first_visible = visible_count;
    if (visible_count == 0U)
        first_visible = 0U;
}

static void sort_entries(void)
{
    for (size_t i = 1U; i < entry_count; i++)
    {
        char name[ENTRY_NAME_CAPACITY];
        unsigned char is_dir = entry_is_directory[i];
        unsigned char selected = entry_selected[i];
        copy_text(name, sizeof(name), entries[i]);
        size_t j = i;
        while (j > 0U)
        {
            int move = 0;
            if (is_dir != entry_is_directory[j - 1U])
                move = is_dir > entry_is_directory[j - 1U];
            else
            {
                size_t k = 0U;
                while (name[k] && entries[j - 1U][k] &&
                       fold_char(name[k]) == fold_char(entries[j - 1U][k]))
                    k++;
                move = fold_char(name[k]) < fold_char(entries[j - 1U][k]);
            }
            if (!move)
                break;
            copy_text(entries[j], sizeof(entries[j]), entries[j - 1U]);
            entry_is_directory[j] = entry_is_directory[j - 1U];
            entry_selected[j] = entry_selected[j - 1U];
            j--;
        }
        copy_text(entries[j], sizeof(entries[j]), name);
        entry_is_directory[j] = is_dir;
        entry_selected[j] = selected;
    }
}

static int load_directory(void)
{
    entry_count = 0U;
    more_entries = 0;
    preview_active = 0;
    long result = vanta_listdir(current_path, list_buffer, sizeof(list_buffer));
    if (result < 0)
    {
        set_status(result == -2 ? "Directory not found" :
                   result == -22 ? "Directory listing exceeds buffer" :
                   "Directory read failed", COLOR_ERROR);
        build_visible();
        return 0;
    }

    size_t cursor = 0U;
    size_t bytes = (size_t)result;
    while (cursor < bytes)
    {
        size_t start = cursor;
        while (cursor < bytes && list_buffer[cursor] != '\n')
            cursor++;
        size_t length = cursor - start;
        if (cursor < bytes)
            cursor++;
        if (length == 0U)
            continue;
        int is_dir = list_buffer[start + length - 1U] == '/';
        if (is_dir)
            length--;
        if (length == 0U)
            continue;
        if (entry_count >= ENTRY_CAPACITY)
        {
            more_entries = 1;
            continue;
        }
        if (length >= ENTRY_NAME_CAPACITY)
            length = ENTRY_NAME_CAPACITY - 1U;
        for (size_t i = 0; i < length; i++)
            entries[entry_count][i] = list_buffer[start + i];
        entries[entry_count][length] = '\0';
        entry_is_directory[entry_count] = (unsigned char)is_dir;
        entry_selected[entry_count] = 0U;
        entry_count++;
    }
    selected_entry = (size_t)-1;
    first_visible = 0U;
    sort_entries();
    build_visible();
    set_status(more_entries ? "Showing first 96 entries" :
               entry_count == 0U ? "This directory is empty" : "Ready",
               COLOR_MUTED);
    return 1;
}

static void go_home(void)
{
    copy_text(current_path, sizeof(current_path), "/home");
    (void)load_directory();
}

static void go_up(void)
{
    if (text_equals_ci(current_path, "/"))
        return;
    size_t length = text_length(current_path);
    while (length > 1U && current_path[length - 1U] == '/')
        current_path[--length] = '\0';
    while (length > 1U && current_path[length - 1U] != '/')
        current_path[--length] = '\0';
    if (length > 1U)
        current_path[length - 1U] = '\0';
    if (length == 1U)
        current_path[0] = '/';
    (void)load_directory();
}

static int ends_with_ci(const char* name, const char* suffix)
{
    size_t n = text_length(name), s = text_length(suffix);
    if (s > n)
        return 0;
    return text_equals_ci(name + n - s, suffix);
}

static const char* file_type_name(const char* name)
{
    if (ends_with_ci(name, ".ELF")) return "Executable";
    if (ends_with_ci(name, ".VZA")) return "Vanta archive";
    if (ends_with_ci(name, ".TXT") || ends_with_ci(name, ".MD") ||
        ends_with_ci(name, ".LOG")) return "Text document";
    if (ends_with_ci(name, ".CFG") || ends_with_ci(name, ".INI"))
        return "Configuration";
    if (ends_with_ci(name, ".C") || ends_with_ci(name, ".H") ||
        ends_with_ci(name, ".ASM") || ends_with_ci(name, ".S"))
        return "Source code";
    if (ends_with_ci(name, ".BIN")) return "Binary file";
    return "File";
}

static int read_exact(int descriptor, char* buffer, size_t length)
{
    size_t used = 0U;
    while (used < length)
    {
        long result = vanta_read(descriptor, buffer + used, length - used);
        if (result <= 0)
            return 0;
        used += (size_t)result;
    }
    return 1;
}

static int write_all(int descriptor, const char* buffer, size_t length)
{
    size_t used = 0U;
    while (used < length)
    {
        long result = vanta_file_write(descriptor, buffer + used, length - used);
        if (result <= 0)
            return 0;
        used += (size_t)result;
    }
    return 1;
}

static int output_path_for_suffix(const char* source, const char* suffix,
                                  char* output, size_t capacity)
{
    copy_text(output, capacity, source);
    return append_text(output, capacity, suffix);
}

static int compress_file(const char* source_path)
{
    char output_path[PATH_CAPACITY];
    if (!output_path_for_suffix(source_path, ".VZA", output_path,
                                sizeof(output_path)))
        return 0;
    vanta_file_info_t existing;
    if (vanta_stat_path(output_path, &existing) == 0)
    {
        set_status("Archive already exists; rename it first", COLOR_ERROR);
        return 0;
    }
    long input = vanta_open(source_path, VANTA_OPEN_READ);
    if (input < 0)
        return 0;
    if (vanta_create_file(output_path) < 0)
    {
        (void)vanta_close((int)input);
        return 0;
    }
    long output = vanta_open(output_path, VANTA_OPEN_WRITE | VANTA_OPEN_TRUNCATE);
    if (output < 0)
    {
        (void)vanta_close((int)input);
        return 0;
    }
    static const char header[8] = {'V','Z','A','R','L','E','1','\n'};
    int ok = write_all((int)output, header, sizeof(header));
    char source[1024];
    char encoded[1100];
    while (ok)
    {
        long got = vanta_read((int)input, source, sizeof(source));
        if (got < 0)
        {
            ok = 0;
            break;
        }
        if (got == 0)
            break;
        size_t i = 0U, used = 0U, length = (size_t)got;
        while (i < length)
        {
            size_t run = 1U;
            while (i + run < length && source[i + run] == source[i] && run < 130U)
                run++;
            if (run >= 4U)
            {
                encoded[used++] = (char)(0x80U | (unsigned char)(run - 3U));
                encoded[used++] = source[i];
                i += run;
                continue;
            }
            size_t start = i;
            while (i < length && i - start < 127U)
            {
                run = 1U;
                while (i + run < length && source[i + run] == source[i] && run < 130U)
                    run++;
                if (run >= 4U && i > start)
                    break;
                if (run >= 4U)
                    break;
                size_t room = 127U - (i - start);
                if (run > room)
                    run = room;
                i += run;
            }
            size_t literal = i - start;
            if (literal == 0U)
                continue;
            encoded[used++] = (char)literal;
            for (size_t j = 0; j < literal; j++)
                encoded[used++] = source[start + j];
        }
        if (used && !write_all((int)output, encoded, used))
            ok = 0;
    }
    (void)vanta_close((int)input);
    (void)vanta_close((int)output);
    if (!ok)
    {
        (void)vanta_delete_path(output_path);
        set_status("Compression failed; partial archive removed", COLOR_ERROR);
        return 0;
    }
    return 1;
}

static int extract_file(const char* source_path)
{
    if (!ends_with_ci(source_path, ".VZA"))
    {
        set_status("Select a .VZA archive to extract", COLOR_ERROR);
        return 0;
    }
    char output_path[PATH_CAPACITY];
    copy_text(output_path, sizeof(output_path), source_path);
    size_t length = text_length(output_path);
    output_path[length - 4U] = '\0';
    vanta_file_info_t existing;
    if (vanta_stat_path(output_path, &existing) == 0)
    {
        set_status("Extract target exists; rename it first", COLOR_ERROR);
        return 0;
    }
    long input = vanta_open(source_path, VANTA_OPEN_READ);
    if (input < 0)
        return 0;
    char header[8];
    if (!read_exact((int)input, header, sizeof(header)) ||
        header[0] != 'V' || header[1] != 'Z' || header[2] != 'A' ||
        header[3] != 'R' || header[4] != 'L' || header[5] != 'E' ||
        header[6] != '1' || header[7] != '\n')
    {
        (void)vanta_close((int)input);
        set_status("Unsupported or corrupt Vanta archive", COLOR_ERROR);
        return 0;
    }
    if (vanta_create_file(output_path) < 0)
    {
        (void)vanta_close((int)input);
        return 0;
    }
    long output = vanta_open(output_path, VANTA_OPEN_WRITE | VANTA_OPEN_TRUNCATE);
    if (output < 0)
    {
        (void)vanta_close((int)input);
        return 0;
    }
    char input_buffer[1024];
    char output_buffer[1024];
    size_t out_used = 0U;
    unsigned int literal_remaining = 0U;
    unsigned int run_remaining = 0U;
    int run_needs_value = 0;
    char run_value = 0;
    int ok = 1;
    while (ok)
    {
        long got = vanta_read((int)input, input_buffer, sizeof(input_buffer));
        if (got < 0)
        {
            ok = 0;
            break;
        }
        if (got == 0)
            break;
        for (long i = 0; i < got && ok; i++)
        {
            unsigned char byte = (unsigned char)input_buffer[i];
            if (run_needs_value)
            {
                run_value = (char)byte;
                run_needs_value = 0;
                while (run_remaining)
                {
                    output_buffer[out_used++] = run_value;
                    run_remaining--;
                    if (out_used == sizeof(output_buffer))
                    {
                        if (!write_all((int)output, output_buffer, out_used))
                        {
                            ok = 0;
                            break;
                        }
                        out_used = 0U;
                    }
                }
            }
            else if (literal_remaining)
            {
                output_buffer[out_used++] = (char)byte;
                literal_remaining--;
                if (out_used == sizeof(output_buffer))
                {
                    if (!write_all((int)output, output_buffer, out_used))
                    {
                        ok = 0;
                        break;
                    }
                    out_used = 0U;
                }
            }
            else if (byte & 0x80U)
            {
                run_remaining = (unsigned int)(byte & 0x7FU) + 3U;
                run_needs_value = 1;
            }
            else if (byte != 0U)
                literal_remaining = byte;
            else
                ok = 0;
        }
    }
    if (literal_remaining || run_remaining || run_needs_value)
        ok = 0;
    if (ok && out_used)
        ok = write_all((int)output, output_buffer, out_used);
    (void)vanta_close((int)input);
    (void)vanta_close((int)output);
    if (!ok)
    {
        (void)vanta_delete_path(output_path);
        set_status("Extraction failed; partial output removed", COLOR_ERROR);
        return 0;
    }
    return 1;
}

static int delete_tree(const char* path, unsigned int depth)
{
    if (depth > 8U)
        return 0;
    vanta_file_info_t info;
    if (vanta_stat_path(path, &info) < 0)
        return 0;
    if (info.type == 2U)
    {
        if (depth >= 8U)
            return 0;
        char* listing = delete_list_buffers[depth];
        long bytes = vanta_listdir(path, listing, LIST_CAPACITY);
        if (bytes < 0)
            return 0;
        size_t cursor = 0U;
        while (cursor < (size_t)bytes)
        {
            size_t start = cursor;
            while (cursor < (size_t)bytes && listing[cursor] != '\n')
                cursor++;
            size_t length = cursor - start;
            if (cursor < (size_t)bytes)
                cursor++;
            if (length == 0U)
                continue;
            int is_dir = listing[start + length - 1U] == '/';
            if (is_dir)
                length--;
            if (length == 0U || length >= ENTRY_NAME_CAPACITY)
                return 0;
            char name[ENTRY_NAME_CAPACITY];
            for (size_t i = 0; i < length; i++)
                name[i] = listing[start + i];
            name[length] = '\0';
            char* child = delete_path_buffers[depth];
            if (!build_child_path(path, name, child, PATH_CAPACITY))
                return 0;
            if (!delete_tree(child, depth + 1U))
                return 0;
        }
    }
    return vanta_delete_path(path) == 0;
}

static const char* path_leaf(const char* path)
{
    const char* leaf = path;
    for (const char* p = path; p && *p; p++)
        if (*p == '/')
            leaf = p + 1;
    return leaf;
}

static int copy_tree(const char* source_path, const char* target_path,
                     unsigned int depth)
{
    if (depth > 8U)
        return 0;
    vanta_file_info_t info;
    if (vanta_stat_path(source_path, &info) < 0)
        return 0;
    vanta_file_info_t existing;
    if (vanta_stat_path(target_path, &existing) == 0)
        return 0;

    if (info.type == 2U)
    {
        if (depth >= 8U || vanta_create_directory(target_path) < 0)
            return 0;
        char* listing = copy_list_buffers[depth];
        long bytes = vanta_listdir(source_path, listing, LIST_CAPACITY);
        if (bytes < 0)
        {
            (void)delete_tree(target_path, 0U);
            return 0;
        }
        size_t cursor = 0U;
        while (cursor < (size_t)bytes)
        {
            size_t start = cursor;
            while (cursor < (size_t)bytes && listing[cursor] != '\n')
                cursor++;
            size_t length = cursor - start;
            if (cursor < (size_t)bytes)
                cursor++;
            if (length == 0U)
                continue;
            if (listing[start + length - 1U] == '/')
                length--;
            if (length == 0U || length >= ENTRY_NAME_CAPACITY)
            {
                (void)delete_tree(target_path, 0U);
                return 0;
            }
            char name[ENTRY_NAME_CAPACITY];
            for (size_t i = 0; i < length; i++)
                name[i] = listing[start + i];
            name[length] = '\0';
            if (!build_child_path(source_path, name,
                                  copy_source_paths[depth + 1U], PATH_CAPACITY) ||
                !build_child_path(target_path, name,
                                  copy_target_paths[depth + 1U], PATH_CAPACITY) ||
                !copy_tree(copy_source_paths[depth + 1U],
                           copy_target_paths[depth + 1U], depth + 1U))
            {
                (void)delete_tree(target_path, 0U);
                return 0;
            }
        }
        return 1;
    }

    if (vanta_create_file(target_path) < 0)
        return 0;
    long input = vanta_open(source_path, VANTA_OPEN_READ);
    if (input < 0)
    {
        (void)vanta_delete_path(target_path);
        return 0;
    }
    long output = vanta_open(target_path, VANTA_OPEN_WRITE | VANTA_OPEN_TRUNCATE);
    if (output < 0)
    {
        (void)vanta_close((int)input);
        (void)vanta_delete_path(target_path);
        return 0;
    }
    char buffer[1024];
    int ok = 1;
    while (ok)
    {
        long got = vanta_read((int)input, buffer, sizeof(buffer));
        if (got < 0)
        {
            ok = 0;
            break;
        }
        if (got == 0)
            break;
        if (!write_all((int)output, buffer, (size_t)got))
            ok = 0;
    }
    (void)vanta_close((int)input);
    (void)vanta_close((int)output);
    if (!ok)
    {
        (void)vanta_delete_path(target_path);
        return 0;
    }
    return 1;
}

static void set_file_clipboard(int cut)
{
    file_clipboard_count = 0U;
    size_t selected = selected_count();
    if (selected == 0U)
    {
        set_status("Select items to copy or cut", COLOR_ERROR);
        return;
    }
    for (size_t i = 0; i < entry_count && file_clipboard_count < ENTRY_CAPACITY; i++)
    {
        if (!entry_selected[i])
            continue;
        if (!build_entry_path(i, file_clipboard[file_clipboard_count],
                              sizeof(file_clipboard[0])))
            continue;
        file_clipboard_count++;
    }
    if (file_clipboard_count == 0U)
    {
        set_status("Could not copy selected paths", COLOR_ERROR);
        return;
    }
    file_clipboard_cut = cut;
    copy_text(clipboard_path, sizeof(clipboard_path), file_clipboard[0]);
    set_status(cut ? "Items marked to move; navigate and paste" :
               "Items copied; navigate and paste", COLOR_ACCENT);
}

static void paste_file_clipboard(void)
{
    if (file_clipboard_count == 0U)
    {
        (void)load_directory();
        set_status("Clipboard is empty; directory refreshed", COLOR_MUTED);
        return;
    }
    size_t total = file_clipboard_count, done = 0U;
    size_t removed = 0U;
    for (size_t i = 0; i < total; i++)
    {
        const char* leaf = path_leaf(file_clipboard[i]);
        char target[PATH_CAPACITY];
        if (!build_child_path(current_path, leaf, target, sizeof(target)) ||
            !copy_tree(file_clipboard[i], target, 0U))
            continue;
        done++;
        if (file_clipboard_cut && delete_tree(file_clipboard[i], 0U))
            removed++;
    }
    int was_cut = file_clipboard_cut;
    if (was_cut && done == total && removed == total)
    {
        file_clipboard_count = 0U;
        file_clipboard_cut = 0;
    }
    (void)load_directory();
    if (done == total && (!was_cut || removed == total))
        set_status(was_cut ? "Move completed" : "Paste completed", COLOR_ACCENT);
    else if (done > 0U)
        set_status("Partial paste; existing names are not overwritten", COLOR_ERROR);
    else
        set_status("Paste failed; target may already exist", COLOR_ERROR);
}

static void show_properties(size_t index)
{
    if (index >= entry_count || !build_entry_path(index, properties_path,
                                                   sizeof(properties_path)) ||
        vanta_stat_path(properties_path, &properties_info) < 0)
    {
        set_status("Could not read file properties", COLOR_ERROR);
        return;
    }
    properties_active = 1;
    context_menu = CONTEXT_NONE;
}

static void start_dialog(int mode)
{
    dialog_mode = mode;
    input_mode = INPUT_DIALOG;
    input_buffer[0] = '\0';
    if (mode == DIALOG_RENAME)
    {
        size_t index = first_selected();
        if (index == (size_t)-1 || selected_count() != 1U)
        {
            input_mode = INPUT_NONE;
            dialog_mode = DIALOG_NONE;
            set_status("Select exactly one item to rename", COLOR_ERROR);
            return;
        }
        copy_text(input_buffer, sizeof(input_buffer), entries[index]);
    }
    else if (mode == DIALOG_CONFIRM_DELETE)
        copy_text(input_buffer, sizeof(input_buffer), "Delete selected items? Enter=yes, Esc=no");
}

static void commit_dialog(void)
{
    if (dialog_mode == DIALOG_CONFIRM_DELETE)
    {
        size_t count = selected_count();
        size_t removed = 0U;
        for (size_t i = 0; i < entry_count; i++)
        {
            if (!entry_selected[i])
                continue;
            char path[PATH_CAPACITY];
            if (build_entry_path(i, path, sizeof(path)) &&
                delete_tree(path, 0U))
                removed++;
        }
        input_mode = INPUT_NONE;
        dialog_mode = DIALOG_NONE;
        (void)load_directory();
        set_status(removed == count ? "Selected items deleted" :
                   "Some items could not be deleted", removed == count ?
                   COLOR_ACCENT : COLOR_ERROR);
        return;
    }

    if (!input_buffer[0] || text_contains_ci(input_buffer, "/"))
    {
        set_status("Enter a valid filename without slashes", COLOR_ERROR);
        return;
    }
    char target[PATH_CAPACITY];
    int renamed = dialog_mode == DIALOG_RENAME;
    char renamed_name[ENTRY_NAME_CAPACITY] = {0};
    if (renamed)
    {
        size_t index = first_selected();
        char old_path[PATH_CAPACITY];
        if (index == (size_t)-1 || !build_entry_path(index, old_path, sizeof(old_path)) ||
            !build_child_path(current_path, input_buffer, target, sizeof(target)))
        {
            set_status("Invalid rename path", COLOR_ERROR);
            return;
        }
        copy_text(renamed_name, sizeof(renamed_name), input_buffer);
        if (vanta_rename_path(old_path, target) < 0)
        {
            set_status("Rename failed; target may exist or be invalid", COLOR_ERROR);
            return;
        }
    }
    else
    {
        if (!build_child_path(current_path, input_buffer, target, sizeof(target)))
        {
            set_status("Invalid path or filename too long", COLOR_ERROR);
            return;
        }
        vanta_file_info_t existing;
        if (vanta_stat_path(target, &existing) == 0)
        {
            set_status("An item with that name already exists", COLOR_ERROR);
            return;
        }
        long result = dialog_mode == DIALOG_CREATE_DIRECTORY ?
            vanta_create_directory(target) : vanta_create_file(target);
        if (result < 0)
        {
            set_status("Create failed; check name and free space", COLOR_ERROR);
            return;
        }
    }
    input_mode = INPUT_NONE;
    dialog_mode = DIALOG_NONE;
    (void)load_directory();
    if (renamed)
    {
        /* Preserve the user's focus across the directory reload after rename. */
        for (size_t i = 0; i < entry_count; i++)
        {
            if (!text_equals_ci(entries[i], renamed_name))
                continue;
            select_single(i);
            for (size_t visible = 0; visible < visible_count; visible++)
            {
                if (visible_indices[visible] != i)
                    continue;
                if (visible < first_visible)
                    first_visible = visible;
                else if (visible >= first_visible + DISPLAY_ROWS)
                    first_visible = visible - DISPLAY_ROWS + 1U;
                break;
            }
            break;
        }
    }
    set_status("File operation completed", COLOR_ACCENT);
}

static void run_compress_selection(void)
{
    size_t count = selected_count(), done = 0U;
    if (!count)
    {
        set_status("Select one or more files to compress", COLOR_ERROR);
        return;
    }
    for (size_t i = 0; i < entry_count; i++)
    {
        if (!entry_selected[i] || entry_is_directory[i])
            continue;
        char path[PATH_CAPACITY];
        if (build_entry_path(i, path, sizeof(path)) && compress_file(path))
            done++;
    }
    (void)load_directory();
    set_status(done == count ? "Compression complete" :
               done ? "Some files compressed; folders are skipped" :
               "Compression failed or selection contains folders",
               done == count ? COLOR_ACCENT : COLOR_ERROR);
}

static void run_extract_selection(void)
{
    size_t count = selected_count(), done = 0U;
    if (!count)
    {
        set_status("Select one or more .VZA archives", COLOR_ERROR);
        return;
    }
    for (size_t i = 0; i < entry_count; i++)
    {
        if (!entry_selected[i] || entry_is_directory[i])
            continue;
        char path[PATH_CAPACITY];
        if (build_entry_path(i, path, sizeof(path)) && extract_file(path))
            done++;
    }
    (void)load_directory();
    set_status(done == count ? "Extraction complete" :
               done ? "Some archives extracted" :
               "Extraction failed; select .VZA files",
               done == count ? COLOR_ACCENT : COLOR_ERROR);
}

static void toolbar_action(unsigned int action)
{
    context_menu = CONTEXT_NONE;
    switch (action)
    {
        case 0: start_dialog(DIALOG_CREATE_FILE); break;
        case 1: start_dialog(DIALOG_CREATE_DIRECTORY); break;
        case 2: start_dialog(DIALOG_RENAME); break;
        case 3:
            if (selected_count() == 0U)
                set_status("Select items to delete", COLOR_ERROR);
            else
                start_dialog(DIALOG_CONFIRM_DELETE);
            break;
        case 4: run_compress_selection(); break;
        case 5: run_extract_selection(); break;
        case 6:
        {
            size_t index = first_selected();
            if (index == (size_t)-1 || selected_count() != 1U)
                set_status("Select one item to view properties", COLOR_ERROR);
            else
                show_properties(index);
            break;
        }
        case 7: paste_file_clipboard(); break;
    }
}

static void dispatch_context_action(unsigned int item)
{
    switch (item)
    {
        case 0:
        {
            size_t index = first_selected();
            if (index != (size_t)-1 && selected_count() == 1U)
                show_properties(index);
            break;
        }
        case 1: start_dialog(DIALOG_RENAME); break;
        case 2: set_file_clipboard(0); break;
        case 3: set_file_clipboard(1); break;
        case 4: toolbar_action(3U); break;
        case 5: run_compress_selection(); break;
        case 6: run_extract_selection(); break;
        default: break;
    }
    context_menu = CONTEXT_NONE;
}

static void open_selected(void)
{
    size_t index = selected_entry;
    if (index >= entry_count)
        index = first_selected();
    if (index >= entry_count)
        return;
    char path[PATH_CAPACITY];
    if (!build_entry_path(index, path, sizeof(path)))
    {
        set_status("Path exceeds the filesystem limit", COLOR_ERROR);
        return;
    }
    if (entry_is_directory[index])
    {
        copy_text(current_path, sizeof(current_path), path);
        search_query[0] = '\0';
        (void)load_directory();
        return;
    }
    if (ends_with_ci(path, ".ELF"))
    {
        const char* argv[1] = { path };
        long child = vanta_spawn(path, argv, 1U);
        if (child < 0)
        {
            set_status(child == -8 ? "Executable format rejected" :
                       "Could not launch executable", COLOR_ERROR);
            return;
        }
        set_status("Program running; waiting for exit", COLOR_MUTED);
        int32_t exit_status = 0;
        long waited = vanta_waitpid((int)child, &exit_status);
        set_status(waited < 0 ? "Program wait failed" :
                   exit_status == 0 ? "Program exited successfully" :
                   "Program exited with a non-zero status",
                   waited < 0 || exit_status != 0 ? COLOR_ERROR : COLOR_ACCENT);
        return;
    }

    long descriptor = vanta_open(path, VANTA_OPEN_READ);
    if (descriptor < 0)
    {
        set_status("File cannot be opened for reading", COLOR_ERROR);
        return;
    }
    char buffer[768];
    long count = vanta_read((int)descriptor, buffer, sizeof(buffer));
    (void)vanta_close((int)descriptor);
    if (count < 0)
    {
        set_status("File read failed", COLOR_ERROR);
        return;
    }
    for (size_t row = 0; row < PREVIEW_ROWS; row++)
        preview_lines[row][0] = '\0';
    preview_count = 1U;
    size_t column = 0U;
    for (long i = 0; i < count && preview_count <= PREVIEW_ROWS; i++)
    {
        unsigned char ch = (unsigned char)buffer[i];
        if (ch == '\r')
            continue;
        if (ch == '\n')
        {
            if (preview_count == PREVIEW_ROWS)
                break;
            preview_count++;
            column = 0U;
            continue;
        }
        if (ch == '\t') ch = ' ';
        if (ch < 32U || ch > 126U) ch = '.';
        if (column >= VANTA_GUI_TEXT_MAX)
        {
            if (preview_count == PREVIEW_ROWS)
                break;
            preview_count++;
            column = 0U;
        }
        preview_lines[preview_count - 1U][column++] = (char)ch;
        preview_lines[preview_count - 1U][column] = '\0';
    }
    preview_active = 1;
    set_status(count == (long)sizeof(buffer) ?
               "Preview limited to first 768 bytes" : "Text preview",
               COLOR_MUTED);
}

static void path_input_begin(void)
{
    input_mode = INPUT_PATH;
    copy_text(input_buffer, sizeof(input_buffer), current_path);
}

static void search_input_begin(void)
{
    input_mode = INPUT_SEARCH;
    copy_text(input_buffer, sizeof(input_buffer), search_query);
}

static void commit_input(void)
{
    if (input_mode == INPUT_PATH)
    {
        char path[PATH_CAPACITY];
        if (input_buffer[0] == '/')
            copy_text(path, sizeof(path), input_buffer);
        else
        {
            path[0] = '/';
            path[1] = '\0';
            (void)append_text(path, sizeof(path), input_buffer);
        }
        vanta_file_info_t info;
        if (vanta_stat_path(path, &info) < 0 || info.type != 2U)
            set_status("Path is not an accessible directory", COLOR_ERROR);
        else
        {
            copy_text(current_path, sizeof(current_path), path);
            search_query[0] = '\0';
            (void)load_directory();
        }
    }
    else if (input_mode == INPUT_SEARCH)
    {
        copy_text(search_query, sizeof(search_query), input_buffer);
        build_visible();
    }
    else if (input_mode == INPUT_DIALOG)
    {
        commit_dialog();
        return;
    }
    input_mode = INPUT_NONE;
}

static void handle_input_key(uint32_t key)
{
    if (key == 27U)
    {
        input_mode = INPUT_NONE;
        dialog_mode = DIALOG_NONE;
        return;
    }
    if (key == 1U)
        input_buffer[0] = '\0';
    else if (key == 10U || key == 13U)
    {
        commit_input();
        return;
    }
    if (key == 8U || key == 127U)
    {
        size_t length = text_length(input_buffer);
        if (length)
            input_buffer[length - 1U] = '\0';
    }
    else if (key == 3U)
        copy_text(clipboard_path, sizeof(clipboard_path), input_buffer);
    else if (key == 22U)
        copy_text(input_buffer, sizeof(input_buffer), clipboard_path);
    else if (key >= 32U && key <= 126U)
    {
        size_t length = text_length(input_buffer);
        size_t limit = input_mode == INPUT_SEARCH ? 62U : sizeof(input_buffer) - 1U;
        if (length < limit)
        {
            input_buffer[length] = (char)key;
            input_buffer[length + 1U] = '\0';
        }
    }
    if (input_mode == INPUT_SEARCH)
    {
        copy_text(search_query, sizeof(search_query), input_buffer);
        build_visible();
    }
}

static void render(void)
{
    command_count = 0U;
    build_visible();
    add_rect(0, 0, 744, 402, COLOR_SURFACE);
    add_rect(0, 0, 744, 3, COLOR_ACCENT);
    add_text(8, 8, preview_active ? "File preview" : "File Explorer", COLOR_TEXT);
    add_text(152, 8, "Ctrl+click multi-select  |  Right-click for actions", COLOR_MUTED);

    add_rect(8, 30, 448, 24, COLOR_ROW);
    add_rect(8, 53, 448, 1, input_mode == INPUT_PATH ? COLOR_ACCENT : COLOR_BORDER);
    if (input_mode == INPUT_PATH)
        add_text_tail(14, 34, input_buffer, COLOR_TEXT, 54U);
    else
        add_text_tail(14, 34, current_path, COLOR_TEXT, 54U);

    add_rect(466, 30, 270, 24, COLOR_ROW);
    add_rect(466, 53, 270, 1, input_mode == INPUT_SEARCH ? COLOR_ACCENT : COLOR_BORDER);
    add_text(472, 34, "Find:", COLOR_MUTED);
    if (input_mode == INPUT_SEARCH)
        add_text_tail(520, 34, input_buffer, COLOR_TEXT, 25U);
    else if (search_query[0])
        add_text_tail(520, 34, search_query, COLOR_TEXT, 25U);
    else
        add_text(520, 34, "search this folder", COLOR_MUTED);

    static const char* button_labels[8] = {
        "New File [N]", "New Folder [D]", "Rename [R]", "Delete [Del]",
        "Compress [Z]", "Extract [X]", "Properties [I]", "Paste [Ctrl+V]"
    };
    for (unsigned int i = 0; i < 8U; i++)
    {
        int row = i / 4U;
        int col = i % 4U;
        int x = 8 + (int)col * 162;
        int y = 60 + row * 24;
        add_rect(x, y, 154, 20, COLOR_ROW);
        add_text(x + 6, y + 2, button_labels[i], COLOR_TEXT);
    }

    add_rect(8, 112, 136, 236, COLOR_ROW);
    static const char* sidebar_labels[6] = {
        "Computer /", "System", "Home", "Config (/etc)", "Drivers", "Devices"
    };
    static const char* sidebar_paths[6] = {
        "/", "/system", "/home", "/etc", "/system/drivers", "/system/devices"
    };
    int active_sidebar = -1;
    size_t active_path_length = 0U;
    for (unsigned int i = 0; i < 6U; i++)
    {
        if (path_is_under(current_path, sidebar_paths[i]) &&
            text_length(sidebar_paths[i]) > active_path_length)
        {
            active_sidebar = (int)i;
            active_path_length = text_length(sidebar_paths[i]);
        }
    }
    for (unsigned int i = 0; i < 6U; i++)
    {
        int y = 118 + (int)i * 26;
        int active = active_sidebar == (int)i;
        if (active)
        {
            add_rect(12, y - 2, 128, 23, COLOR_SELECTED);
            add_rect(12, y + 20, 128, 1, COLOR_ACCENT);
        }
        add_text(18, y + 1, sidebar_labels[i],
                 active ? COLOR_ACCENT : COLOR_TEXT);
    }
    add_text(14, 300, "Current folder", COLOR_MUTED);
    add_text_tail(14, 320, current_path, COLOR_TEXT, 15U);

    add_rect(152, 112, 584, 236, COLOR_ROW);
    if (!context_menu)
    {
        add_text(160, 114, "Name", COLOR_MUTED);
        add_text(600, 114, "Kind", COLOR_MUTED);
    }
    size_t end = first_visible + DISPLAY_ROWS;
    if (end > visible_count)
        end = visible_count;
    size_t row = 0U;
    for (size_t v = first_visible; v < end; v++, row++)
    {
        size_t index = visible_indices[v];
        int y = 132 + (int)row * ROW_HEIGHT;
        if (entry_selected[index])
        {
            add_rect(156, y - 2, 576, 19, COLOR_SELECTED);
            add_outline(156, y - 2, 576, 19, COLOR_ACCENT);
        }
        add_text_tail(164, y, entries[index], COLOR_TEXT, 52U);
        add_text(600, y, entry_is_directory[index] ? "Folder" : file_type_name(entries[index]),
                 entry_is_directory[index] ? COLOR_ACCENT : COLOR_MUTED);
    }
    if (visible_count == 0U)
        add_text(166, 156, search_query[0] ? "No matching items" : "This folder is empty",
                 COLOR_MUTED);
    else if (visible_count > DISPLAY_ROWS)
        add_text(164, 342, "Use arrows / PageUp / PageDown to browse", COLOR_MUTED);

    add_rect(8, 354, 728, 1, COLOR_BORDER);
    char status[VANTA_GUI_TEXT_MAX + 1U];
    size_t count = selected_count();
    if (count)
    {
        status[0] = '\0';
        char digits[16];
        size_t pos = 0U;
        size_t n = count;
        char rev[12];
        size_t nd = 0U;
        do { rev[nd++] = (char)('0' + n % 10U); n /= 10U; } while (n && nd < sizeof(rev));
        while (nd) digits[pos++] = rev[--nd];
        digits[pos] = '\0';
        copy_text(status, sizeof(status), digits);
        (void)append_text(status, sizeof(status), " selected");
        add_text(12, 360, status, COLOR_ACCENT);
    }
    else
        add_text(12, 360, status_text, status_color);
    add_text(12, 382, "Enter=open Ctrl+C=copy Ctrl+X=cut Ctrl+V=paste Del=delete",
             COLOR_MUTED);

    if (context_menu)
    {
        int x = context_x;
        int y = context_y;
        if (x > 540) x = 540;
        if (y > 234) y = 234;
        if (x < 300) x = 300;
        if (y < 120) y = 120;
        context_x = x;
        context_y = y;
        add_rect(x, y, 180, 168, COLOR_SURFACE);
        add_rect(x, y + 167, 180, 1, COLOR_ACCENT);
        static const char* menu[7] = {"Properties", "Rename", "Copy",
                                      "Cut / Move", "Delete", "Compress", "Extract"};
        for (unsigned int i = 0; i < 7U; i++)
            add_text(x + 10, y + 5 + (int)i * 23, menu[i], COLOR_TEXT);
    }

    if (input_mode == INPUT_DIALOG)
    {
        add_rect(170, 140, 404, 106, COLOR_SURFACE);
        add_rect(170, 245, 404, 1, COLOR_ACCENT);
        const char* prompt = dialog_mode == DIALOG_CREATE_FILE ? "Create file name:" :
            dialog_mode == DIALOG_CREATE_DIRECTORY ? "Create folder name:" :
            dialog_mode == DIALOG_RENAME ? "Rename selected item:" :
            "Confirm delete";
        add_text(184, 152, prompt, COLOR_TEXT);
        if (dialog_mode == DIALOG_CONFIRM_DELETE)
            add_text(184, 178, input_buffer, COLOR_ERROR);
        else
        {
            add_rect(182, 178, 380, 24, COLOR_ROW);
            add_rect(182, 201, 380, 1, COLOR_BORDER);
            add_text_tail(188, 182, input_buffer, COLOR_TEXT, 46U);
        }
        add_text(184, 216, "Enter confirm   Esc cancel", COLOR_MUTED);
    }

    if (properties_active)
    {
        add_rect(164, 118, 420, 230, COLOR_SURFACE);
        add_rect(164, 347, 420, 1, COLOR_ACCENT);
        add_text(178, 128, "File properties", COLOR_TEXT);
        add_text_tail(178, 154, properties_info.name, COLOR_ACCENT, 60U);
        add_text(178, 178, properties_info.type == 2U ? "Type: Folder" : file_type_name(properties_info.name), COLOR_TEXT);
        const char* dot = properties_info.name;
        for (size_t i = 0; properties_info.name[i]; i++)
            if (properties_info.name[i] == '.') dot = properties_info.name + i + 1U;
        if (dot != properties_info.name)
            add_text_tail(178, 202, dot, COLOR_MUTED, 60U);
        char size_text[64] = "Size: ";
        char reverse[16];
        size_t digits = 0U;
        uint32_t size = properties_info.size;
        do { reverse[digits++] = (char)('0' + size % 10U); size /= 10U; }
        while (size && digits < sizeof(reverse));
        while (digits)
        {
            size_t used = text_length(size_text);
            size_text[used] = reverse[--digits];
            size_text[used + 1U] = '\0';
        }
        (void)append_text(size_text, sizeof(size_text), " bytes");
        add_text(178, 226, size_text, COLOR_TEXT);
        add_text_tail(178, 250, properties_path, COLOR_MUTED, 60U);
        add_text(178, 320, "Enter or Esc closes properties", COLOR_MUTED);
    }

    (void)vanta_gui_draw(window_id, commands, command_count);
}

static void close_app(void)
{
    if (window_id != 0U)
        (void)vanta_gui_close_window(window_id);
    vanta_exit(0);
}

#if defined(VANTAOS_EXPLORER_E2E_TEST)
#pragma GCC diagnostic ignored "-Wunused-function"

static void explorer_e2e_fail(void)
{
    static const char marker[] = "VANTAOS_EXPLORER_E2E_FAIL\n";
    (void)vanta_write(marker, sizeof(marker) - 1U);
    vanta_exit(1);
}

#define EXPLORER_E2E_STRINGIFY_INNER(value) #value
#define EXPLORER_E2E_STRINGIFY(value) EXPLORER_E2E_STRINGIFY_INNER(value)
#define EXPLORER_E2E_REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            static const char marker[] = \
                "VANTAOS_EXPLORER_E2E_FAIL line=" \
                EXPLORER_E2E_STRINGIFY(__LINE__) "\n"; \
            (void)vanta_write(marker, sizeof(marker) - 1U); \
            vanta_exit(1); \
        } \
    } while (0)

static int explorer_e2e_file_equals(const char* path, const char* expected)
{
    char actual[256];
    long descriptor = vanta_open(path, VANTA_OPEN_READ);
    if (descriptor < 0)
        return 0;
    size_t expected_length = text_length(expected);
    long got = vanta_read((int)descriptor, actual, sizeof(actual));
    (void)vanta_close((int)descriptor);
    if (got != (long)expected_length)
        return 0;
    for (size_t i = 0; i < expected_length; i++)
        if (actual[i] != expected[i])
            return 0;
    return 1;
}

static int explorer_e2e_create_through_dialog(const char* name, int directory)
{
    toolbar_action(directory ? 1U : 0U);
    copy_text(input_buffer, sizeof(input_buffer), name);
    handle_input_key(13U);
    char path[PATH_CAPACITY];
    vanta_file_info_t info;
    return build_child_path(current_path, name, path, sizeof(path)) &&
           vanta_stat_path(path, &info) == 0 &&
           info.type == (directory ? 2U : 1U);
}

static size_t explorer_e2e_find_entry(const char* name)
{
    for (size_t i = 0; i < entry_count; i++)
        if (text_equals_ci(entries[i], name))
            return i;
    return (size_t)-1;
}

static void explorer_e2e_run(void)
{
    static const char fixture[] = "/EXPLORER-E2E";
    static const char payload[] =
        "VantaOS Explorer end-to-end fixture: repeated repeated repeated\n";
    static const char success[] = "VANTAOS_EXPLORER_E2E_OK\n";
    char path[PATH_CAPACITY];
    vanta_file_info_t info;

    if (vanta_stat_path(fixture, &info) == 0)
        (void)delete_tree(fixture, 0U);
    EXPLORER_E2E_REQUIRE(vanta_create_directory(fixture) >= 0);
    copy_text(current_path, sizeof(current_path), fixture);
    search_query[0] = '\0';
    EXPLORER_E2E_REQUIRE(load_directory());
    EXPLORER_E2E_REQUIRE(explorer_e2e_create_through_dialog("Source", 1));
    EXPLORER_E2E_REQUIRE(explorer_e2e_create_through_dialog("Dest", 1));

    copy_text(current_path, sizeof(current_path), "/EXPLORER-E2E/Source");
    EXPLORER_E2E_REQUIRE(load_directory());
    EXPLORER_E2E_REQUIRE(explorer_e2e_create_through_dialog("note.txt", 0));
    EXPLORER_E2E_REQUIRE(build_child_path(current_path, "note.txt",
                                          path, sizeof(path)));
    long descriptor = vanta_open(path, VANTA_OPEN_WRITE | VANTA_OPEN_TRUNCATE);
    EXPLORER_E2E_REQUIRE(descriptor >= 0);
    EXPLORER_E2E_REQUIRE(write_all((int)descriptor, payload,
                                   text_length(payload)));
    EXPLORER_E2E_REQUIRE(vanta_close((int)descriptor) == 0);
    EXPLORER_E2E_REQUIRE(explorer_e2e_create_through_dialog("Nested", 1));
    copy_text(current_path, sizeof(current_path), "/EXPLORER-E2E/Source/Nested");
    EXPLORER_E2E_REQUIRE(load_directory());
    EXPLORER_E2E_REQUIRE(explorer_e2e_create_through_dialog("inside.txt", 0));
    EXPLORER_E2E_REQUIRE(build_child_path(current_path, "inside.txt",
                                          path, sizeof(path)));
    descriptor = vanta_open(path, VANTA_OPEN_WRITE | VANTA_OPEN_TRUNCATE);
    EXPLORER_E2E_REQUIRE(descriptor >= 0);
    EXPLORER_E2E_REQUIRE(write_all((int)descriptor, "nested", 6U));
    EXPLORER_E2E_REQUIRE(vanta_close((int)descriptor) == 0);

    /* Exercise the same context-menu Copy and toolbar Paste handlers as the UI. */
    copy_text(current_path, sizeof(current_path), fixture);
    EXPLORER_E2E_REQUIRE(load_directory());
    size_t source_index = explorer_e2e_find_entry("Source");
    EXPLORER_E2E_REQUIRE(source_index != (size_t)-1);
    select_single(source_index);
    dispatch_context_action(2U);
    copy_text(current_path, sizeof(current_path), "/EXPLORER-E2E/Dest");
    EXPLORER_E2E_REQUIRE(load_directory());
    toolbar_action(7U);
    EXPLORER_E2E_REQUIRE(vanta_stat_path(
        "/EXPLORER-E2E/Dest/Source/Nested/inside.txt", &info) == 0);
    EXPLORER_E2E_REQUIRE(explorer_e2e_file_equals(
        "/EXPLORER-E2E/Dest/Source/note.txt", payload));

    /* Exercise Cut/Move and verify source removal plus destination contents. */
    copy_text(current_path, sizeof(current_path), "/EXPLORER-E2E/Source");
    EXPLORER_E2E_REQUIRE(load_directory());
    EXPLORER_E2E_REQUIRE(explorer_e2e_create_through_dialog("move.txt", 0));
    EXPLORER_E2E_REQUIRE(build_child_path(current_path, "move.txt",
                                          path, sizeof(path)));
    descriptor = vanta_open(path, VANTA_OPEN_WRITE | VANTA_OPEN_TRUNCATE);
    EXPLORER_E2E_REQUIRE(descriptor >= 0);
    EXPLORER_E2E_REQUIRE(write_all((int)descriptor, "move-data", 9U));
    EXPLORER_E2E_REQUIRE(vanta_close((int)descriptor) == 0);
    size_t move_index = explorer_e2e_find_entry("move.txt");
    EXPLORER_E2E_REQUIRE(move_index != (size_t)-1);
    select_single(move_index);
    dispatch_context_action(3U);
    copy_text(current_path, sizeof(current_path), "/EXPLORER-E2E/Dest");
    EXPLORER_E2E_REQUIRE(load_directory());
    toolbar_action(7U);
    EXPLORER_E2E_REQUIRE(vanta_stat_path("/EXPLORER-E2E/Source/move.txt",
                                         &info) < 0);
    EXPLORER_E2E_REQUIRE(explorer_e2e_file_equals(
        "/EXPLORER-E2E/Dest/move.txt", "move-data"));

    /* Search uses the app's actual keyboard-input and visible-list code. */
    search_input_begin();
    handle_input_key(1U);
    const char query[] = "sOuRcE";
    for (size_t i = 0; i < sizeof(query) - 1U; i++)
        handle_input_key((uint32_t)(unsigned char)query[i]);
    handle_input_key(13U);
    EXPLORER_E2E_REQUIRE(visible_count == 1U);
    EXPLORER_E2E_REQUIRE(text_equals_ci(
        entries[visible_indices[0]], "Source"));
    search_input_begin();
    handle_input_key(1U);
    handle_input_key(13U);
    EXPLORER_E2E_REQUIRE(visible_count == entry_count);

    /* Rename through the app's dialog/key path, then verify retained selection. */
    copy_text(current_path, sizeof(current_path), "/EXPLORER-E2E/Dest/Source");
    EXPLORER_E2E_REQUIRE(load_directory());
    size_t note_index = explorer_e2e_find_entry("note.txt");
    EXPLORER_E2E_REQUIRE(note_index != (size_t)-1);
    select_single(note_index);
    toolbar_action(2U);
    handle_input_key(1U);
    const char renamed[] = "renamed.log";
    for (size_t i = 0; i < sizeof(renamed) - 1U; i++)
        handle_input_key((uint32_t)(unsigned char)renamed[i]);
    handle_input_key(13U);
    EXPLORER_E2E_REQUIRE(vanta_stat_path(
        "/EXPLORER-E2E/Dest/Source/renamed.log", &info) == 0);
    EXPLORER_E2E_REQUIRE(first_selected() != (size_t)-1);
    EXPLORER_E2E_REQUIRE(text_equals_ci(
        entries[first_selected()], "renamed.log"));

    /* Context-menu Properties, Compress, Extract, and Delete are covered. */
    size_t renamed_index = explorer_e2e_find_entry("renamed.log");
    EXPLORER_E2E_REQUIRE(renamed_index != (size_t)-1);
    select_single(renamed_index);
    dispatch_context_action(0U);
    EXPLORER_E2E_REQUIRE(properties_active &&
                          text_equals_ci(properties_info.name, "renamed.log"));
    properties_active = 0;
    dispatch_context_action(5U);
    EXPLORER_E2E_REQUIRE(vanta_stat_path(
        "/EXPLORER-E2E/Dest/Source/renamed.log.VZA", &info) == 0);
    EXPLORER_E2E_REQUIRE(vanta_delete_path(
        "/EXPLORER-E2E/Dest/Source/renamed.log") == 0);
    EXPLORER_E2E_REQUIRE(load_directory());
    size_t archive_index = explorer_e2e_find_entry("renamed.log.VZA");
    EXPLORER_E2E_REQUIRE(archive_index != (size_t)-1);
    select_single(archive_index);
    dispatch_context_action(6U);
    EXPLORER_E2E_REQUIRE(explorer_e2e_file_equals(
        "/EXPLORER-E2E/Dest/Source/renamed.log", payload));
    EXPLORER_E2E_REQUIRE(load_directory());
    archive_index = explorer_e2e_find_entry("renamed.log.VZA");
    EXPLORER_E2E_REQUIRE(archive_index != (size_t)-1);
    select_single(archive_index);
    dispatch_context_action(4U);
    handle_input_key(13U);
    EXPLORER_E2E_REQUIRE(vanta_stat_path(
        "/EXPLORER-E2E/Dest/Source/renamed.log.VZA", &info) < 0);

    EXPLORER_E2E_REQUIRE(delete_tree(fixture, 0U));
    (void)vanta_write(success, sizeof(success) - 1U);
    vanta_exit(0);
}
#endif


int main(int argc, char** argv)
{
    (void)argv;
    if (argc != 1)
        vanta_exit(2);
#if defined(VANTAOS_EXPLORER_E2E_TEST)
    explorer_e2e_run();
    vanta_exit(0);
#endif
    vanta_gui_window_request_t request = {
        .width = WINDOW_WIDTH, .height = WINDOW_HEIGHT, .title = "File Explorer"
    };
    long created = vanta_gui_create_window(&request);
    if (created <= 0)
        vanta_exit(3);
    window_id = (uint32_t)created;
    current_theme = vanta_theme_load();
    status_color = COLOR_MUTED;
    (void)load_directory();
    render();

    for (;;)
    {
        vanta_gui_event_t event;
        long result = vanta_gui_poll_event(window_id, &event);
        if (result < 0)
            close_app();
        if (result == 0)
        {
            (void)vanta_yield();
            continue;
        }
        if (event.type == VANTA_GUI_EVENT_CLOSE_REQUEST)
            close_app();
        if (event.type == VANTA_GUI_EVENT_THEME_CHANGED)
        {
            current_theme = vanta_theme_load();
            render();
            continue;
        }
        if (event.type == VANTA_GUI_EVENT_KEY)
        {
            uint32_t key = event.value;
            if (properties_active)
            {
                if (key == 27U || key == 10U || key == 13U)
                    properties_active = 0;
                render();
                continue;
            }
            if (input_mode != INPUT_NONE)
            {
                handle_input_key(key);
                render();
                continue;
            }
            if (context_menu)
            {
                if (key == 27U)
                    context_menu = CONTEXT_NONE;
                render();
                continue;
            }
            if (key == 3U)
                set_file_clipboard(0);
            else if (key == 24U)
                set_file_clipboard(1);
            else if (key == 22U)
                paste_file_clipboard();
            else if (key == 1U)
            {
                for (size_t i = 0; i < entry_count; i++)
                    entry_selected[i] = (unsigned char)text_contains_ci(entries[i], search_query);
                selected_entry = first_selected();
            }
            else if (key == 'q' || key == 'Q')
                close_app();
            else if (key == 'n' || key == 'N')
                start_dialog(DIALOG_CREATE_FILE);
            else if (key == 'd' || key == 'D')
                start_dialog(DIALOG_CREATE_DIRECTORY);
            else if (key == 'r' || key == 'R')
                start_dialog(DIALOG_RENAME);
            else if (key == 'z' || key == 'Z')
                run_compress_selection();
            else if (key == 'x' || key == 'X')
                run_extract_selection();
            else if (key == 'i' || key == 'I')
            {
                size_t index = first_selected();
                if (index != (size_t)-1 && selected_count() == 1U)
                    show_properties(index);
                else
                    set_status("Select one item to view properties", COLOR_ERROR);
            }
            else if (key == 't' || key == 'T')
                (void)load_directory();
            else if (key == 'h' || key == 'H')
                go_home();
            else if (key == 'f' || key == 'F')
                search_input_begin();
            else if (key == 'p' || key == 'P')
                path_input_begin();
            else if (key == 0x109U)
                toolbar_action(3U);
            else if (key == 8U || key == 127U)
                go_up();
            else if (key == 10U || key == 13U)
                open_selected();
            else if (key == VANTA_GUI_KEY_UP || key == VANTA_GUI_KEY_DOWN ||
                     key == VANTA_GUI_KEY_PAGE_UP || key == VANTA_GUI_KEY_PAGE_DOWN)
            {
                if (visible_count)
                {
                    size_t visible_pos = 0U;
                    for (size_t i = 0; i < visible_count; i++)
                        if (visible_indices[i] == selected_entry) { visible_pos = i; break; }
                    if (key == VANTA_GUI_KEY_UP && visible_pos > 0U) visible_pos--;
                    else if (key == VANTA_GUI_KEY_DOWN && visible_pos + 1U < visible_count) visible_pos++;
                    else if (key == VANTA_GUI_KEY_PAGE_UP)
                        visible_pos = visible_pos > DISPLAY_ROWS ? visible_pos - DISPLAY_ROWS : 0U;
                    else if (key == VANTA_GUI_KEY_PAGE_DOWN)
                    {
                        visible_pos += DISPLAY_ROWS;
                        if (visible_pos >= visible_count) visible_pos = visible_count - 1U;
                    }
                    select_single(visible_indices[visible_pos]);
                    if (visible_pos < first_visible) first_visible = visible_pos;
                    if (visible_pos >= first_visible + DISPLAY_ROWS)
                        first_visible = visible_pos - DISPLAY_ROWS + 1U;
                }
            }
            render();
        }
        else if (event.type == VANTA_GUI_EVENT_POINTER_DOWN)
        {
            int x = event.x, y = event.y;
            if (properties_active)
            {
                properties_active = 0;
            }
            else if (context_menu)
            {
                if (x >= context_x && x < context_x + 180 &&
                    y >= context_y && y < context_y + 168)
                    dispatch_context_action((unsigned int)(y - context_y) / 23U);
                else
                    context_menu = CONTEXT_NONE;
            }
            else if (y >= 30 && y < 54 && x >= 8 && x < 456)
                path_input_begin();
            else if (y >= 30 && y < 54 && x >= 466 && x < 736)
                search_input_begin();
            else if (y >= 60 && y < 80)
            {
                unsigned int col = (unsigned int)(x - 8) / 162U;
                if (x >= 8 && col < 4U) toolbar_action(col);
            }
            else if (y >= 84 && y < 104)
            {
                unsigned int col = (unsigned int)(x - 8) / 162U;
                if (x >= 8 && col < 4U) toolbar_action(col + 4U);
            }
            else if (x >= 8 && x < 144 && y >= 116 && y < 274)
            {
                unsigned int item = (unsigned int)(y - 116) / 26U;
                static const char* paths[6] = {
                    "/", "/system", "/home", "/etc", "/system/drivers", "/system/devices"
                };
                if (item < 6U)
                {
                    copy_text(current_path, sizeof(current_path), paths[item]);
                    search_query[0] = '\0';
                    (void)load_directory();
                }
            }
            else if (x >= 152 && x < 736 && y >= 132 && y < 352)
            {
                size_t row = (size_t)(y - 132) / ROW_HEIGHT;
                size_t visible_pos = first_visible + row;
                if (row < DISPLAY_ROWS && visible_pos < visible_count)
                {
                    size_t index = visible_indices[visible_pos];
                    if (event.value == 3U)
                    {
                        if (!entry_selected[index])
                            select_single(index);
                        context_menu = 1;
                        context_x = x;
                        context_y = y;
                    }
                    else if (event.reserved & VANTA_GUI_MOD_CTRL)
                    {
                        entry_selected[index] = !entry_selected[index];
                        selected_entry = index;
                        last_clicked_index = index;
                        consecutive_clicks = 1U;
                    }
                    else
                    {
                        if (last_clicked_index == index)
                            consecutive_clicks++;
                        else
                            consecutive_clicks = 1U;
                        last_clicked_index = index;
                        if (entry_selected[index] && consecutive_clicks >= 2U)
                            open_selected();
                        else
                            select_single(index);
                    }
                }
            }
            render();
        }
    }
}
