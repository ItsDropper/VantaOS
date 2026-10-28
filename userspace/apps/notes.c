/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include <vanta/gui.h>
#include <vanta/theme.h>

#define DEFAULT_NOTES_PATH "/HOME/USER/NOTES.TXT"
#define TEXT_CAPACITY 4096U
#define LINE_CAPACITY 64U
#define VISIBLE_ROWS 15U
#define WINDOW_WIDTH 760U
#define WINDOW_HEIGHT 460U
#define COLOR_SELECTED 0x003B82F6U
#define COLOR_WHITE 0x00FFFFFFU
#define COLOR_ERROR 0x00F87171U
#define COLOR_SUCCESS 0x0022C55EU

static char document_path[256] = DEFAULT_NOTES_PATH;
static int document_writable = 1;
static char document[TEXT_CAPACITY];
static size_t document_length;
static size_t cursor;
static size_t selection_anchor;
static size_t selection_end;
static int selection_active;
static int selection_dragging;
static char clipboard[TEXT_CAPACITY];
static size_t clipboard_length;
static size_t first_row;
static size_t last_click_position = (size_t)-1;
static char status[LINE_CAPACITY] = "Ready";
static uint32_t status_color;
static uint32_t window_id;
static uint32_t command_count;
static vanta_gui_draw_command_t commands[VANTA_GUI_MAX_COMMANDS];
static vanta_theme_t theme;

static size_t text_length(const char* text)
{
    size_t n = 0U;
    while (text && text[n]) n++;
    return n;
}

static void copy_text(char* destination, size_t capacity, const char* source)
{
    if (!destination || capacity == 0U) return;
    size_t i = 0U;
    while (source && source[i] && i + 1U < capacity)
    {
        destination[i] = source[i];
        i++;
    }
    destination[i] = '\0';
}

static int path_is_home_document(const char* path)
{
    static const char prefix[] = "/HOME/USER/";
    if (!path || text_length(path) < sizeof(prefix) - 1U)
        return 0;
    for (size_t i = 0U; i < sizeof(prefix) - 1U; ++i)
    {
        char a = path[i], b = prefix[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
        if (a != b) return 0;
    }

    /* Never allow an argv path such as /HOME/USER/../SYSTEM/FILE to
       turn the editor's home-only save policy into a path traversal. */
    const char* component = path + sizeof(prefix) - 1U;
    if (!*component)
        return 0;
    for (const char* p = component;; ++p)
    {
        if (*p == '/' || *p == '\0')
        {
            size_t length = (size_t)(p - component);
            if (length == 0U ||
                (length == 1U && component[0] == '.') ||
                (length == 2U && component[0] == '.' && component[1] == '.'))
                return 0;
            if (*p == '\0')
                return 1;
            component = p + 1;
        }
    }
}

static int path_with_suffix(const char* path, const char* suffix,
                            char* output, size_t capacity)
{
    size_t path_length = text_length(path);
    size_t suffix_length = text_length(suffix);
    if (!path || !suffix || !output || path_length + suffix_length >= capacity)
        return 0;
    for (size_t i = 0U; i < path_length; ++i)
        output[i] = path[i];
    for (size_t i = 0U; i < suffix_length; ++i)
        output[path_length + i] = suffix[i];
    output[path_length + suffix_length] = '\0';
    return 1;
}

static void set_status(const char* message, uint32_t color)
{
    copy_text(status, sizeof(status), message);
    status_color = color;
}

static void add_rect(int32_t x, int32_t y, int32_t width,
                     int32_t height, uint32_t color)
{
    if (command_count >= VANTA_GUI_MAX_COMMANDS || width <= 0 || height <= 0)
        return;
    commands[command_count++] = (vanta_gui_draw_command_t){
        .type = VANTA_GUI_DRAW_FILL_RECT, .x = x, .y = y,
        .width = width, .height = height, .color = color, .scale = 1U
    };
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
    for (size_t i = 0U; i <= length; ++i) command->text[i] = text[i];
}

static void ordered_selection(size_t* begin, size_t* end)
{
    *begin = selection_anchor < selection_end ? selection_anchor : selection_end;
    *end = selection_anchor > selection_end ? selection_anchor : selection_end;
    if (*begin > document_length) *begin = document_length;
    if (*end > document_length) *end = document_length;
}

static size_t row_start(size_t wanted_row)
{
    size_t offset = 0U, row = 0U, column = 0U;
    while (offset < document_length && row < wanted_row)
    {
        if (document[offset] == '\n' || column >= LINE_CAPACITY - 1U)
        {
            if (document[offset] == '\n') offset++;
            row++;
            column = 0U;
            continue;
        }
        offset++;
        column++;
    }
    return offset;
}

static size_t offset_at(size_t wanted_row, size_t column)
{
    size_t offset = row_start(wanted_row);
    size_t used = 0U;
    while (offset < document_length && used < column &&
           document[offset] != '\n' && used < LINE_CAPACITY - 1U)
    {
        offset++;
        used++;
    }
    return offset;
}

static void ensure_cursor_visible(void)
{
    size_t row = 0U, column = 0U;
    for (size_t i = 0U; i < cursor && i < document_length; ++i)
    {
        if (document[i] == '\n' || column >= LINE_CAPACITY - 1U)
        {
            row++;
            column = 0U;
        }
        else column++;
    }
    if (row < first_row) first_row = row;
    if (row >= first_row + VISIBLE_ROWS)
        first_row = row - VISIBLE_ROWS + 1U;
}

static int is_selected(size_t offset, size_t begin, size_t end)
{
    return selection_active && offset >= begin && offset < end;
}

static void render(void)
{
    command_count = 0U;
    add_rect(8, 8, 720, 426, vanta_theme_surface_color(&theme));
    add_rect(8, 8, 720, 2, vanta_theme_accent_color(&theme));
    add_text(16, 16, "Notes editor | Ctrl+S saves", vanta_theme_text_color(&theme));
    add_rect(12, 40, 712, 1, vanta_theme_border_color(&theme));

    size_t selected_begin = 0U, selected_end = 0U;
    ordered_selection(&selected_begin, &selected_end);
    for (size_t visual = 0U; visual < VISIBLE_ROWS; ++visual)
    {
        size_t row = first_row + visual;
        size_t start = row_start(row);
        if (start > document_length) break;
        size_t end = start, column = 0U;
        while (end < document_length && document[end] != '\n' &&
               column < LINE_CAPACITY - 1U)
        {
            end++;
            column++;
        }
        int y = 48 + (int)visual * 20;
        size_t segment = start;
        while (segment < end)
        {
            int selected = is_selected(segment, selected_begin, selected_end);
            size_t run_end = segment + 1U;
            while (run_end < end &&
                   is_selected(run_end, selected_begin, selected_end) == selected)
                run_end++;
            char run[LINE_CAPACITY];
            size_t count = run_end - segment;
            for (size_t i = 0U; i < count; ++i) run[i] = document[segment + i];
            run[count] = '\0';
            if (selected)
                add_rect(16 + (int32_t)(segment - start) * 8, y - 1,
                         (int32_t)count * 8, 17, COLOR_SELECTED);
            add_text(16 + (int32_t)(segment - start) * 8, y, run,
                     selected ? COLOR_WHITE : vanta_theme_text_color(&theme));
            segment = run_end;
        }
        if (cursor >= start && cursor <= end)
        {
            size_t cursor_column = cursor - start;
            add_rect(16 + (int32_t)cursor_column * 8, y - 1, 1, 17,
                     vanta_theme_accent_color(&theme));
        }
        if (start == document_length) break;
    }

    add_rect(12, 354, 712, 1, vanta_theme_border_color(&theme));
    add_text(16, 362, status, status_color ? status_color :
             vanta_theme_muted_color(&theme));
    add_text(16, 390, "Ctrl+S Save | Ctrl+A Select all | Ctrl+C Copy | Ctrl+V Paste",
             vanta_theme_muted_color(&theme));
    add_text(16, 410, "Mouse drag selects text; double-click selects a word",
             vanta_theme_muted_color(&theme));
    (void)vanta_gui_draw(window_id, commands, command_count);
}

static void clear_selection(void)
{
    selection_active = 0;
    selection_anchor = cursor;
    selection_end = cursor;
}

static void delete_selection(void)
{
    if (!selection_active) return;
    size_t begin, end;
    ordered_selection(&begin, &end);
    if (end > begin)
    {
        for (size_t i = end; i < document_length; ++i)
            document[begin + i - end] = document[i];
        document_length -= end - begin;
        document[document_length] = '\0';
        cursor = begin;
    }
    clear_selection();
}

static void insert_char(char value)
{
    if (selection_active) delete_selection();
    if (document_length + 1U >= sizeof(document)) return;
    for (size_t i = document_length; i > cursor; --i)
        document[i] = document[i - 1U];
    document[cursor++] = value;
    document_length++;
    document[document_length] = '\0';
    clear_selection();
    ensure_cursor_visible();
    set_status("Unsaved changes", COLOR_SUCCESS);
}

static void copy_selection(void)
{
    if (!selection_active) return;
    size_t begin, end;
    ordered_selection(&begin, &end);
    clipboard_length = end - begin;
    if (clipboard_length >= sizeof(clipboard))
        clipboard_length = sizeof(clipboard) - 1U;
    for (size_t i = 0U; i < clipboard_length; ++i)
        clipboard[i] = document[begin + i];
    clipboard[clipboard_length] = '\0';
    (void)vanta_clipboard_set(clipboard, clipboard_length);
    set_status("Selection copied to system clipboard", COLOR_SUCCESS);
}

static void paste_clipboard(void)
{
    long clipboard_available = vanta_clipboard_get(clipboard, sizeof(clipboard));
    if (clipboard_available <= 0) return;
    clipboard_length = (size_t)clipboard_available;
    if (selection_active) delete_selection();
    size_t available = sizeof(document) - 1U - document_length;
    size_t count = clipboard_length < available ? clipboard_length : available;
    for (size_t i = document_length; i > cursor; --i)
        document[i + count - 1U] = document[i - 1U];
    for (size_t i = 0U; i < count; ++i)
        document[cursor + i] = clipboard[i];
    cursor += count;
    document_length += count;
    document[document_length] = '\0';
    clear_selection();
    ensure_cursor_visible();
    set_status("Pasted; save to keep changes", COLOR_SUCCESS);
}

static void save_document(void)
{
    if (!document_writable || !path_is_home_document(document_path))
    {
        set_status("Read-only: saving is limited to safe /HOME/USER paths",
                   COLOR_ERROR);
        return;
    }

    char temporary_path[sizeof(document_path)];
    char backup_path[sizeof(document_path)];
    if (!path_with_suffix(document_path, ".VANTA-TMP", temporary_path,
                          sizeof(temporary_path)) ||
        !path_with_suffix(document_path, ".VANTA-BAK", backup_path,
                          sizeof(backup_path)))
    {
        set_status("Path is too long to save safely", COLOR_ERROR);
        return;
    }

    vanta_file_info_t temporary_info, backup_info, destination_info;
    if (vanta_stat_path(temporary_path, &temporary_info) == 0 ||
        vanta_stat_path(backup_path, &backup_info) == 0)
    {
        set_status("Save recovery file exists; rename it before saving",
                   COLOR_ERROR);
        return;
    }

    /* Write the complete new contents to a separate file first. Never truncate
       the user's current document before the replacement is ready. */
    if (vanta_create_file(temporary_path) < 0)
    {
        set_status("Could not create temporary save file", COLOR_ERROR);
        return;
    }
    long fd = vanta_open(temporary_path, VANTA_OPEN_WRITE | VANTA_OPEN_TRUNCATE);
    if (fd < 0)
    {
        (void)vanta_delete_path(temporary_path);
        set_status("Could not open temporary save file", COLOR_ERROR);
        return;
    }

    size_t written = 0U;
    int write_ok = 1;
    while (written < document_length)
    {
        long result = vanta_file_write((int)fd, document + written,
                                       document_length - written);
        if (result <= 0 || (size_t)result > document_length - written)
        {
            write_ok = 0;
            break;
        }
        written += (size_t)result;
    }
    if (vanta_close((int)fd) < 0)
        write_ok = 0;
    if (!write_ok)
    {
        (void)vanta_delete_path(temporary_path);
        set_status("Save failed; original file was preserved", COLOR_ERROR);
        return;
    }

    int destination_exists = vanta_stat_path(document_path, &destination_info) == 0;
    if (destination_exists && destination_info.type != 1U)
    {
        (void)vanta_delete_path(temporary_path);
        set_status("Save target is not a regular file", COLOR_ERROR);
        return;
    }

    if (destination_exists && vanta_rename_path(document_path, backup_path) < 0)
    {
        (void)vanta_delete_path(temporary_path);
        set_status("Could not protect existing file; save cancelled", COLOR_ERROR);
        return;
    }

    if (vanta_rename_path(temporary_path, document_path) < 0)
    {
        if (destination_exists && vanta_rename_path(backup_path, document_path) < 0)
        {
            set_status("Save failed; original is preserved as .VANTA-BAK",
                       COLOR_ERROR);
            return;
        }
        (void)vanta_delete_path(temporary_path);
        set_status("Could not publish saved file; original restored", COLOR_ERROR);
        return;
    }

    if (destination_exists && vanta_delete_path(backup_path) < 0)
    {
        set_status("Saved; cleanup left a .VANTA-BAK recovery file",
                   COLOR_SUCCESS);
        return;
    }
    set_status("Saved current file", COLOR_SUCCESS);
}

static void load_document(void)
{
    long fd = vanta_open(document_path, VANTA_OPEN_READ);
    if (fd < 0)
    {
        document[0] = '\0';
        document_length = 0U;
        cursor = 0U;
        set_status("New note; Ctrl+S creates the file", vanta_theme_muted_color(&theme));
        return;
    }

    size_t total = 0U;
    int read_failed = 0;
    while (total < sizeof(document) - 1U)
    {
        long count = vanta_read((int)fd, document + total,
                                sizeof(document) - 1U - total);
        if (count < 0)
        {
            read_failed = 1;
            break;
        }
        if (count == 0)
            break;
        if ((size_t)count > sizeof(document) - 1U - total)
        {
            read_failed = 1;
            break;
        }
        total += (size_t)count;
    }
    if (vanta_close((int)fd) < 0)
        read_failed = 1;
    if (read_failed)
    {
        document[0] = '\0';
        document_length = 0U;
        cursor = 0U;
        set_status("Could not read note completely", COLOR_ERROR);
        return;
    }

    document_length = total;
    document[document_length] = '\0';
    cursor = document_length;
    set_status(total == sizeof(document) - 1U ?
               "Loaded first 4095 bytes" : "Loaded note", COLOR_SUCCESS);
    ensure_cursor_visible();
}

static void move_cursor(size_t next)
{
    cursor = next > document_length ? document_length : next;
    clear_selection();
    ensure_cursor_visible();
}

static void select_at(int32_t x, int32_t y, int extend)
{
    if (y < 48 || y >= 48 + (int32_t)VISIBLE_ROWS * 20 || x < 16)
    {
        if (!extend) { clear_selection(); selection_dragging = 0; }
        return;
    }
    size_t row = first_row + (size_t)(y - 48) / 20U;
    size_t column = (size_t)(x - 16) / 8U;
    size_t position = offset_at(row, column);
    if (!extend)
    {
        if (last_click_position != (size_t)-1 &&
            (position > last_click_position ? position - last_click_position :
             last_click_position - position) <= 2U && document_length > 0U)
        {
            size_t at = position == document_length && position > 0U ?
                position - 1U : position;
            int word_char = at < document_length &&
                ((document[at] >= 'a' && document[at] <= 'z') ||
                 (document[at] >= 'A' && document[at] <= 'Z') ||
                 (document[at] >= '0' && document[at] <= '9') ||
                 document[at] == '_');
            if (word_char)
            {
                size_t begin = at, end = at + 1U;
                while (begin > 0U &&
                       ((document[begin - 1U] >= 'a' && document[begin - 1U] <= 'z') ||
                        (document[begin - 1U] >= 'A' && document[begin - 1U] <= 'Z') ||
                        (document[begin - 1U] >= '0' && document[begin - 1U] <= '9') ||
                        document[begin - 1U] == '_'))
                    begin--;
                while (end < document_length &&
                       ((document[end] >= 'a' && document[end] <= 'z') ||
                        (document[end] >= 'A' && document[end] <= 'Z') ||
                        (document[end] >= '0' && document[end] <= '9') ||
                        document[end] == '_'))
                    end++;
                selection_anchor = begin;
                selection_end = end;
                selection_active = 1;
                selection_dragging = 0;
                cursor = end;
                last_click_position = (size_t)-1;
                return;
            }
        }
        selection_anchor = position;
        selection_end = position;
        selection_active = 1;
        selection_dragging = 1;
        last_click_position = position;
    }
    else if (selection_active)
        selection_end = position;
    cursor = position;
}

static void close_app(void)
{
    if (window_id) (void)vanta_gui_close_window(window_id);
    vanta_exit(0);
}

int main(int argc, char** argv)
{
    theme = vanta_theme_load();
    if (argc > 1 && argv && argv[1] && argv[1][0] == '/')
    {
        size_t length = text_length(argv[1]);
        if (length > 0U && length < sizeof(document_path))
        {
            copy_text(document_path, sizeof(document_path), argv[1]);
            document_writable = path_is_home_document(document_path);
        }
        else
        {
            set_status("Path too long; using default note", COLOR_ERROR);
        }
    }
#if defined(VANTAOS_NOTES_E2E_TEST)
    {
        load_document();
        static const char expected[] =
            "VantaOS Explorer end-to-end fixture: repeated repeated repeated\n";
        static const char success[] = "VANTAOS_NOTES_FILE_LOAD_OK\n";
        static const char failure[] = "VANTAOS_NOTES_FILE_LOAD_FAIL\n";
        int matches = document_length == text_length(expected);
        for (size_t i = 0U; matches && i < document_length; i++)
            if (document[i] != expected[i])
                matches = 0;
        if (matches)
        {
            (void)vanta_write(success, sizeof(success) - 1U);
            vanta_exit(0);
        }
        (void)vanta_write(failure, sizeof(failure) - 1U);
        vanta_exit(1);
    }
#endif

    vanta_gui_window_request_t request = {
        .width = WINDOW_WIDTH, .height = WINDOW_HEIGHT, .title = "Notes"
    };
    long created = vanta_gui_create_window(&request);
    if (created <= 0) vanta_exit(3);
    window_id = (uint32_t)created;
    load_document();
    render();

    for (;;)
    {
        vanta_gui_event_t event;
        long result = vanta_gui_poll_event(window_id, &event);
        if (result < 0) close_app();
        if (result == 0)
        {
            (void)vanta_yield();
            continue;
        }
        if (event.type == VANTA_GUI_EVENT_CLOSE_REQUEST) close_app();
        if (event.type == VANTA_GUI_EVENT_THEME_CHANGED)
        {
            theme = vanta_theme_load();
            render();
            continue;
        }
        if (event.type == VANTA_GUI_EVENT_POINTER_DOWN && event.value == 1U)
        {
            select_at(event.x, event.y, 0);
            render();
            continue;
        }
        if (event.type == VANTA_GUI_EVENT_POINTER_MOVE && event.value == 1U)
        {
            if (selection_dragging) select_at(event.x, event.y, 1);
            render();
            continue;
        }
        if (event.type == VANTA_GUI_EVENT_POINTER_UP)
        {
            if (selection_dragging) select_at(event.x, event.y, 1);
            selection_dragging = 0;
            render();
            continue;
        }
        if (event.type != VANTA_GUI_EVENT_KEY) continue;

        uint32_t key = event.value;
        if ((event.reserved & VANTA_GUI_MOD_CTRL) != 0U)
        {
            if (key == 's' || key == 'S' || key == 19U) save_document();
            else if (key == 'a' || key == 'A' || key == 1U)
            {
                selection_anchor = 0U;
                selection_end = document_length;
                selection_active = 1;
                selection_dragging = 0;
            }
            else if (key == 'c' || key == 'C' || key == 3U) copy_selection();
            else if (key == 'v' || key == 'V' || key == 22U) paste_clipboard();
            render();
            continue;
        }
        if (key == VANTA_GUI_KEY_LEFT)
        {
            if (selection_active)
            {
                size_t begin, end;
                ordered_selection(&begin, &end);
                (void)end;
                move_cursor(begin);
            }
            else if (cursor > 0U) move_cursor(cursor - 1U);
        }
        else if (key == VANTA_GUI_KEY_RIGHT)
        {
            if (selection_active)
            {
                size_t begin, end;
                ordered_selection(&begin, &end);
                (void)begin;
                move_cursor(end);
            }
            else if (cursor < document_length) move_cursor(cursor + 1U);
        }
        else if (key == VANTA_GUI_KEY_HOME)
        {
            size_t start = cursor;
            while (start > 0U && document[start - 1U] != '\n') start--;
            move_cursor(start);
        }
        else if (key == VANTA_GUI_KEY_END)
        {
            size_t end = cursor;
            while (end < document_length && document[end] != '\n') end++;
            move_cursor(end);
        }
        else if (key == VANTA_GUI_KEY_UP || key == VANTA_GUI_KEY_DOWN)
        {
            size_t start = cursor, column = 0U;
            while (start > 0U && document[start - 1U] != '\n') { start--; column++; }
            size_t target_row = key == VANTA_GUI_KEY_UP ? (start > 0U ? start - 1U : 0U) : cursor;
            if (key == VANTA_GUI_KEY_UP)
            {
                while (target_row > 0U && document[target_row - 1U] != '\n') target_row--;
            }
            else
            {
                while (target_row < document_length && document[target_row] != '\n') target_row++;
                if (target_row < document_length) target_row++;
            }
            size_t target = target_row;
            while (target < document_length && document[target] != '\n' &&
                   target - target_row < column && target - target_row < LINE_CAPACITY - 1U)
                target++;
            move_cursor(target);
        }
        else if (key == '\b' || key == 127U)
        {
            if (selection_active) delete_selection();
            else if (cursor > 0U)
            {
                for (size_t i = cursor; i < document_length; ++i)
                    document[i - 1U] = document[i];
                cursor--;
                document_length--;
                document[document_length] = '\0';
            }
            set_status("Unsaved changes", COLOR_SUCCESS);
        }
        else if (key == VANTA_GUI_KEY_DELETE)
        {
            if (selection_active) delete_selection();
            else if (cursor < document_length)
            {
                for (size_t i = cursor + 1U; i < document_length; ++i)
                    document[i - 1U] = document[i];
                document_length--;
                document[document_length] = '\0';
            }
            set_status("Unsaved changes", COLOR_SUCCESS);
        }
        else if (key == '\r' || key == '\n') insert_char('\n');
        else if (key >= 32U && key <= 126U) insert_char((char)key);
        render();
    }
}
