#include <vanta/gui.h>

#define PATH_CAPACITY 128U
#define LIST_CAPACITY 2048U
#define ENTRY_CAPACITY 48U
#define ENTRY_NAME_CAPACITY 128U
#define DISPLAY_ROWS 12U
#define PREVIEW_ROWS 12U
#define WINDOW_WIDTH 760U
#define WINDOW_HEIGHT 460U

#define COLOR_TEXT 0x00E8EDF4U
#define COLOR_MUTED 0x008D9BACU
#define COLOR_SURFACE 0x000C1119U
#define COLOR_ROW 0x001D2835U
#define COLOR_ACCENT 0x004B9CFFU
#define COLOR_ERROR 0x00F87171U

static char current_path[PATH_CAPACITY] = "/";
static char list_buffer[LIST_CAPACITY];
static char entries[ENTRY_CAPACITY][ENTRY_NAME_CAPACITY];
static unsigned char entry_is_directory[ENTRY_CAPACITY];
static size_t entry_count;
static size_t selected_entry;
static size_t first_visible;
static char preview_lines[PREVIEW_ROWS][VANTA_GUI_TEXT_MAX + 1U];
static size_t preview_count;
static int preview_active;
static int more_entries;
static char status_text[VANTA_GUI_TEXT_MAX + 1U] = "Loading directory";
static uint32_t status_color = COLOR_MUTED;
static vanta_gui_draw_command_t commands[VANTA_GUI_MAX_COMMANDS];
static uint32_t command_count;
static uint32_t window_id;

static size_t text_length(const char* text)
{
    size_t length = 0U;
    while (text && text[length])
        length++;
    return length;
}

static int text_equals(const char* left, const char* right)
{
    size_t i = 0U;
    if (!left || !right)
        return 0;
    while (left[i] && right[i] && left[i] == right[i])
        i++;
    return left[i] == right[i];
}

static int ends_with_elf(const char* name)
{
    size_t length = text_length(name);
    if (length < 4U)
        return 0;
    const char* suffix = name + length - 4U;
    return (suffix[0] == '.' &&
            (suffix[1] == 'e' || suffix[1] == 'E') &&
            (suffix[2] == 'l' || suffix[2] == 'L') &&
            (suffix[3] == 'f' || suffix[3] == 'F'));
}

static void set_status(const char* text, uint32_t color)
{
    size_t length = text_length(text);
    if (length > VANTA_GUI_TEXT_MAX)
        length = VANTA_GUI_TEXT_MAX;
    for (size_t i = 0U; i < length; i++)
        status_text[i] = text[i];
    status_text[length] = '\0';
    status_color = color;
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

static void add_rect(int32_t x, int32_t y, int32_t width,
                     int32_t height, uint32_t color)
{
    if (command_count >= VANTA_GUI_MAX_COMMANDS)
        return;
    vanta_gui_draw_command_t* command = &commands[command_count++];
    *command = (vanta_gui_draw_command_t){
        .type = VANTA_GUI_DRAW_FILL_RECT,
        .x = x, .y = y, .width = width, .height = height,
        .color = color, .scale = 1U
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
        .type = VANTA_GUI_DRAW_TEXT,
        .x = x, .y = y, .color = color, .scale = 1U
    };
    for (size_t i = 0U; i <= length; i++)
        command->text[i] = text[i];
}

static void clear_entries(void)
{
    entry_count = 0U;
    selected_entry = 0U;
    first_visible = 0U;
    more_entries = 0;
    for (size_t i = 0U; i < ENTRY_CAPACITY; i++)
    {
        entries[i][0] = '\0';
        entry_is_directory[i] = 0U;
    }
}

static int load_directory(void)
{
    clear_entries();
    long result = vanta_listdir(current_path, list_buffer, sizeof(list_buffer));
    if (result < 0)
    {
        set_status(result == -2 ? "Directory not found" :
                   result == -22 ? "Directory listing exceeds buffer" :
                   "Directory read failed", COLOR_ERROR);
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
        int is_directory = list_buffer[start + length - 1U] == '/';
        if (is_directory)
            length--;
        if (length == 0U)
            continue;

        if (entry_count >= ENTRY_CAPACITY)
        {
            more_entries = 1;
            continue;
        }
        size_t copy_length = length;
        if (copy_length >= ENTRY_NAME_CAPACITY)
            copy_length = ENTRY_NAME_CAPACITY - 1U;
        for (size_t i = 0U; i < copy_length; i++)
            entries[entry_count][i] = list_buffer[start + i];
        entries[entry_count][copy_length] = '\0';
        entry_is_directory[entry_count] = (unsigned char)is_directory;
        entry_count++;
    }

    preview_active = 0;
    set_status(more_entries ? "Showing first 48 entries" :
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
    if (text_equals(current_path, "/"))
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

static int build_entry_path(size_t index, char* destination, size_t capacity)
{
    if (index >= entry_count || !destination || capacity < 2U)
        return 0;
    size_t used = 0U;
    if (text_equals(current_path, "/"))
        destination[used++] = '/';
    else
    {
        size_t path_length = text_length(current_path);
        if (path_length + 1U >= capacity)
            return 0;
        for (size_t i = 0U; i < path_length; i++)
            destination[used++] = current_path[i];
        destination[used++] = '/';
    }

    size_t name_length = text_length(entries[index]);
    if (used + name_length >= capacity)
        return 0;
    for (size_t i = 0U; i < name_length; i++)
        destination[used++] = entries[index][i];
    destination[used] = '\0';
    return 1;
}

static void preview_file(const char* path)
{
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

    for (size_t row = 0U; row < PREVIEW_ROWS; row++)
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
        if (ch == '\t')
            ch = ' ';
        if (ch < 32U || ch > 126U)
            ch = '.';
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

static void open_selected(void)
{
    if (selected_entry >= entry_count)
        return;
    char path[PATH_CAPACITY];
    if (!build_entry_path(selected_entry, path, sizeof(path)))
    {
        set_status("Path is too long for current filesystem API", COLOR_ERROR);
        return;
    }

    if (entry_is_directory[selected_entry])
    {
        copy_text(current_path, sizeof(current_path), path);
        (void)load_directory();
        return;
    }

    if (ends_with_elf(path))
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

    preview_file(path);
}

static void render(void)
{
    command_count = 0U;
    add_rect(8, 8, 720, 440, COLOR_SURFACE);
    add_rect(8, 8, 720, 2, COLOR_ACCENT);
    add_text(16, 16, preview_active ? "File preview" : "File Explorer",
             COLOR_TEXT);

    char path_display[VANTA_GUI_TEXT_MAX + 1U];
    size_t path_length = text_length(current_path);
    if (path_length <= VANTA_GUI_TEXT_MAX)
        copy_text(path_display, sizeof(path_display), current_path);
    else
    {
        path_display[0] = '.';
        path_display[1] = '.';
        path_display[2] = '.';
        size_t start = path_length - (VANTA_GUI_TEXT_MAX - 3U);
        for (size_t i = 3U; i < VANTA_GUI_TEXT_MAX; i++)
            path_display[i] = current_path[start + i - 3U];
        path_display[VANTA_GUI_TEXT_MAX] = '\0';
    }
    add_text(16, 38, path_display, COLOR_MUTED);

    add_text(16, 62, "Up     Home     Refresh", COLOR_ACCENT);

    add_rect(8, 88, 720, 250, 0x000F151EU);
    if (preview_active)
    {
        for (size_t i = 0U; i < preview_count && i < PREVIEW_ROWS; i++)
            if (preview_lines[i][0])
                add_text(16, 96 + (int32_t)i * 20,
                         preview_lines[i], COLOR_TEXT);
    }
    else
    {
        for (size_t i = first_visible;
             i < entry_count && i < first_visible + DISPLAY_ROWS; i++)
        {
            char label[VANTA_GUI_TEXT_MAX + 1U];
            size_t used = 0U;
            label[used++] = selected_entry == i ? '>' : ' ';
            label[used++] = ' ';
            size_t name_length = text_length(entries[i]);
            size_t maximum = VANTA_GUI_TEXT_MAX - used -
                             (entry_is_directory[i] ? 1U : 0U);
            if (name_length > maximum)
                name_length = maximum;
            for (size_t j = 0U; j < name_length; j++)
                label[used++] = entries[i][j];
            if (entry_is_directory[i])
                label[used++] = '/';
            label[used] = '\0';
            add_text(16, 96 + (int32_t)(i - first_visible) * 20, label,
                     entry_is_directory[i] ? COLOR_ACCENT : COLOR_TEXT);
        }
        if (entry_count == 0U)
            add_text(16, 100, "(empty directory)", COLOR_MUTED);
    }

    add_rect(8, 344, 720, 1, COLOR_BORDER);
    add_text(16, 350, status_text, status_color);
    add_text(16, 374, "Arrows select | Enter open | Backspace up | H home | R refresh",
             COLOR_MUTED);
    (void)vanta_gui_draw(window_id, commands, command_count);
}

static void close_app(void)
{
    if (window_id != 0U)
        (void)vanta_gui_close_window(window_id);
    vanta_exit(0);
}

int main(int argc, char** argv)
{
    (void)argv;
    if (argc != 1)
        vanta_exit(2);

    vanta_gui_window_request_t request = {
        .width = WINDOW_WIDTH, .height = WINDOW_HEIGHT, .title = "File Explorer"
    };
    long created = vanta_gui_create_window(&request);
    if (created <= 0)
        vanta_exit(3);
    window_id = (uint32_t)created;
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

        if (event.type == VANTA_GUI_EVENT_KEY)
        {
            uint32_t key = event.value;
            if (key == 'q' || key == 'Q')
                close_app();
            else if (key == 'h' || key == 'H')
            {
                preview_active = 0;
                go_home();
            }
            else if (key == 'r' || key == 'R')
            {
                if (preview_active)
                    preview_active = 0;
                (void)load_directory();
            }
            else if (key == '\b' || key == 127U)
            {
                if (preview_active)
                {
                    preview_active = 0;
                    set_status("Returned to directory", COLOR_MUTED);
                }
                else
                    go_up();
            }
            else if (key == '\r' || key == '\n')
            {
                if (preview_active)
                {
                    preview_active = 0;
                    set_status("Returned to directory", COLOR_MUTED);
                }
                else
                    open_selected();
            }
            else if (key == VANTA_GUI_KEY_UP && !preview_active)
            {
                if (selected_entry > 0U)
                    selected_entry--;
                if (selected_entry < first_visible)
                    first_visible = selected_entry;
            }
            else if (key == VANTA_GUI_KEY_DOWN && !preview_active)
            {
                if (selected_entry + 1U < entry_count)
                    selected_entry++;
                if (selected_entry >= first_visible + DISPLAY_ROWS)
                    first_visible = selected_entry - DISPLAY_ROWS + 1U;
            }
            else if (key == VANTA_GUI_KEY_PAGE_UP && !preview_active)
            {
                selected_entry = selected_entry > DISPLAY_ROWS ?
                    selected_entry - DISPLAY_ROWS : 0U;
                first_visible = selected_entry;
            }
            else if (key == VANTA_GUI_KEY_PAGE_DOWN && !preview_active)
            {
                size_t last = entry_count == 0U ? 0U : entry_count - 1U;
                selected_entry = selected_entry + DISPLAY_ROWS < last ?
                    selected_entry + DISPLAY_ROWS : last;
                first_visible = selected_entry >= DISPLAY_ROWS - 1U ?
                    selected_entry - DISPLAY_ROWS + 1U : 0U;
            }
            render();
        }
        else if (event.type == VANTA_GUI_EVENT_POINTER_DOWN)
        {
            if (event.y >= 58 && event.y < 82)
            {
                if (event.x >= 16 && event.x < 68)
                {
                    preview_active = 0;
                    go_up();
                }
                else if (event.x >= 76 && event.x < 140)
                {
                    preview_active = 0;
                    go_home();
                }
                else if (event.x >= 148 && event.x < 232)
                {
                    preview_active = 0;
                    (void)load_directory();
                }
            }
            else if (!preview_active && event.y >= 92 && event.y < 364)
            {
                size_t row = (size_t)(event.y - 92) / 20U;
                size_t index = first_visible + row;
                if (row < DISPLAY_ROWS && index < entry_count)
                {
                    selected_entry = index;
                    open_selected();
                }
            }
            render();
        }
    }
}