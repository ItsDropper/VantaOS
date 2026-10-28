/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include <vanta/gui.h>
#include <vanta/theme.h>

#define PROCESS_USER_MAX_ARGS 16U
#define LINE_CAPACITY 256U
#define LIST_CAPACITY 2048U
#define IO_CHUNK 256U
#define KERNEL_OUTPUT_CAPACITY 2048U
#define OUTPUT_LINES 512U
#define OUTPUT_COLUMNS (VANTA_GUI_TEXT_MAX + 1U)
#define DISPLAY_LINES 13U
#define HISTORY_ENTRIES 16U
#define WINDOW_WIDTH 760U
#define WINDOW_HEIGHT 460U

static vanta_theme_t current_theme;
#define COLOR_TEXT vanta_theme_text_color(&current_theme)
#define COLOR_MUTED vanta_theme_muted_color(&current_theme)
#define COLOR_SURFACE vanta_theme_surface_color(&current_theme)
#define COLOR_BORDER vanta_theme_border_color(&current_theme)
#define COLOR_ACCENT vanta_theme_accent_color(&current_theme)

static char line[LINE_CAPACITY];
static char history[HISTORY_ENTRIES][LINE_CAPACITY];
static size_t history_count;
static size_t history_start;
static size_t history_position;
static int history_browsing;
static char list_buffer[LIST_CAPACITY];
static char io_buffer[IO_CHUNK];
static char kernel_output[KERNEL_OUTPUT_CAPACITY];
static char output[OUTPUT_LINES][OUTPUT_COLUMNS];
static size_t output_count = 1U;
static size_t output_start;
static size_t input_length;
static size_t prompt_length;
static size_t output_scroll_offset;
static vanta_gui_draw_command_t commands[VANTA_GUI_MAX_COMMANDS];
static uint32_t command_count;
static uint32_t window_id;
static char clipboard[VANTA_CLIPBOARD_CAPACITY];
static size_t clipboard_length;
static size_t selection_anchor_line;
static size_t selection_anchor_column;
static size_t selection_end_line;
static size_t selection_end_column;
static size_t last_click_line = (size_t)-1;
static size_t last_click_column = (size_t)-1;
static int selection_active;
static int selection_dragging;

static size_t string_length(const char* text)
{
    size_t length = 0U;
    while (text && text[length])
        length++;
    return length;
}

static int string_equals(const char* left, const char* right)
{
    size_t i = 0U;
    if (!left || !right)
        return 0;
    while (left[i] && right[i] && left[i] == right[i])
        i++;
    return left[i] == right[i];
}

static int starts_with(const char* text, const char* prefix)
{
    size_t i = 0U;
    while (prefix[i])
    {
        if (text[i] != prefix[i])
            return 0;
        i++;
    }
    return 1;
}

static void clear_line(size_t index)
{
    for (size_t i = 0U; i < OUTPUT_COLUMNS; i++)
        output[index][i] = '\0';
}

/* Map a retained logical line to its ring-buffer slot. Keeping scrollback in
   a ring avoids copying 40 KiB of history every time output reaches capacity. */
static size_t output_slot(size_t logical_line)
{
    return (output_start + logical_line) % OUTPUT_LINES;
}

static char* output_line(size_t logical_line)
{
    return output[output_slot(logical_line)];
}

static void output_newline(void)
{
    output_scroll_offset = 0U;
    if (output_count < OUTPUT_LINES)
    {
        output_count++;
        clear_line(output_slot(output_count - 1U));
        return;
    }

    output_start = (output_start + 1U) % OUTPUT_LINES;
    clear_line(output_slot(output_count - 1U));
}

static void output_character(char value)
{
    if (value == '\r')
        return;
    if (value == '\n')
    {
        output_newline();
        return;
    }

    char* row_text = output_line(output_count - 1U);
    size_t length = string_length(row_text);
    if (length >= OUTPUT_COLUMNS - 1U)
    {
        output_newline();
        row_text = output_line(output_count - 1U);
        length = 0U;
    }
    row_text[length] = value;
    row_text[length + 1U] = '\0';
}

static void output_backspace(void)
{
    char* row_text = output_line(output_count - 1U);
    size_t length = string_length(row_text);
    if (length > prompt_length)
        row_text[length - 1U] = '\0';
}

static void write_text(const char* text)
{
    for (size_t i = 0U; text && text[i]; i++)
        output_character(text[i]);
}

static void write_bytes(const char* text, size_t length)
{
    for (size_t i = 0U; text && i < length; i++)
        output_character(text[i]);
}

static void render_add_rect(int32_t x, int32_t y, int32_t width,
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

static void render_add_text(int32_t x, int32_t y, const char* text,
                            uint32_t color)
{
    size_t length = string_length(text);
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

static void render_line_with_selection(size_t logical_line, int y)
{
    const char* text = output_line(logical_line);
    size_t length = string_length(text);
    if (length == 0U)
        return;
    if (!selection_active)
    {
        render_add_text(16, y, text, COLOR_TEXT);
        return;
    }

    size_t first_line = selection_anchor_line;
    size_t first_column = selection_anchor_column;
    size_t last_line = selection_end_line;
    size_t last_column = selection_end_column;
    if (first_line > last_line ||
        (first_line == last_line && first_column > last_column))
    {
        size_t t = first_line; first_line = last_line; last_line = t;
        t = first_column; first_column = last_column; last_column = t;
    }
    if (logical_line < first_line || logical_line > last_line)
    {
        render_add_text(16, y, text, COLOR_TEXT);
        return;
    }
    size_t begin = logical_line == first_line ? first_column : 0U;
    size_t end = logical_line == last_line ? last_column : length;
    if (begin > length) begin = length;
    if (end > length) end = length;
    if (end < begin) end = begin;
    size_t selected_end = end;
    if (selected_end > begin)
        render_add_rect(16 + (int32_t)begin * 8, y,
                        (int32_t)(selected_end - begin) * 8, 16,
                        0x003B82F6U);
    if (begin > 0U)
    {
        char part[OUTPUT_COLUMNS];
        for (size_t i = 0U; i < begin; ++i) part[i] = text[i];
        part[begin] = '\0';
        render_add_text(16, y, part, COLOR_TEXT);
    }
    if (end > begin)
    {
        char part[OUTPUT_COLUMNS];
        size_t count = end - begin;
        for (size_t i = 0U; i < count; ++i) part[i] = text[begin + i];
        part[count] = '\0';
        render_add_text(16 + (int32_t)begin * 8, y, part, 0x00FFFFFFU);
    }
    if (length > end)
        render_add_text(16 + (int32_t)end * 8, y, text + end, COLOR_TEXT);
}

static void terminal_pointer_select(int32_t x, int32_t y, int extend)
{
    size_t end = output_count - output_scroll_offset;
    size_t first = end > DISPLAY_LINES ? end - DISPLAY_LINES : 0U;
    if (y < 42 || y >= 42 + (int32_t)DISPLAY_LINES * 22 ||
        x < 16 || y >= 330)
    {
        if (!extend)
            selection_dragging = 0;
        return;
    }
    size_t logical_line = first + (size_t)(y - 42) / 22U;
    if (logical_line >= end)
        logical_line = end == 0U ? 0U : end - 1U;
    const char* text = output_line(logical_line);
    size_t length = string_length(text);
    size_t column = (size_t)(x - 16) / 8U;
    if (column > length) column = length;

    if (!extend)
    {
        if (last_click_line == logical_line && last_click_column != (size_t)-1 &&
            (column > last_click_column ? column - last_click_column :
             last_click_column - column) <= 2U && length > 0U)
        {
            size_t at = column == length && column > 0U ? column - 1U : column;
            while (at > 0U &&
                   ((text[at - 1U] >= 'a' && text[at - 1U] <= 'z') ||
                    (text[at - 1U] >= 'A' && text[at - 1U] <= 'Z') ||
                    (text[at - 1U] >= '0' && text[at - 1U] <= '9') ||
                    text[at - 1U] == '_'))
                at--;
            size_t word_end = at;
            while (word_end < length &&
                   ((text[word_end] >= 'a' && text[word_end] <= 'z') ||
                    (text[word_end] >= 'A' && text[word_end] <= 'Z') ||
                    (text[word_end] >= '0' && text[word_end] <= '9') ||
                    text[word_end] == '_'))
                word_end++;
            if (word_end > at)
            {
                selection_anchor_line = logical_line;
                selection_anchor_column = at;
                selection_end_line = logical_line;
                selection_end_column = word_end;
                selection_active = 1;
                selection_dragging = 0;
                last_click_line = (size_t)-1;
                last_click_column = (size_t)-1;
                return;
            }
        }
        selection_anchor_line = logical_line;
        selection_anchor_column = column;
        selection_end_line = logical_line;
        selection_end_column = column;
        selection_active = 1;
        selection_dragging = 1;
        last_click_line = logical_line;
        last_click_column = column;
    }
    else if (selection_dragging)
    {
        selection_end_line = logical_line;
        selection_end_column = column;
    }
}

static void terminal_copy_selection(void)
{
    if (!selection_active)
        return;
    clipboard_length = 0U;
    size_t first_line = selection_anchor_line;
    size_t first_column = selection_anchor_column;
    size_t last_line = selection_end_line;
    size_t last_column = selection_end_column;
    if (first_line > last_line ||
        (first_line == last_line && first_column > last_column))
    {
        size_t t = first_line; first_line = last_line; last_line = t;
        t = first_column; first_column = last_column; last_column = t;
    }
    if (last_line >= output_count)
        last_line = output_count - 1U;
    for (size_t row = first_line; row <= last_line &&
         clipboard_length + 2U < sizeof(clipboard); ++row)
    {
        const char* text = output_line(row);
        size_t length = string_length(text);
        size_t begin = row == first_line ? first_column : 0U;
        size_t end = row == last_line ? last_column : length;
        if (begin > length) begin = length;
        if (end > length) end = length;
        if (end < begin) end = begin;
        for (size_t i = begin; i < end &&
             clipboard_length + 2U < sizeof(clipboard); ++i)
            clipboard[clipboard_length++] = text[i];
        if (row != last_line && clipboard_length + 2U < sizeof(clipboard))
            clipboard[clipboard_length++] = '\n';
    }
    clipboard[clipboard_length] = '\0';
    (void)vanta_clipboard_set(clipboard, clipboard_length);
}

static void terminal_paste_clipboard(void)
{
    long available = vanta_clipboard_get(clipboard, sizeof(clipboard));
    if (available < 0)
        return;
    clipboard_length = (size_t)available;
    for (size_t i = 0U; i < clipboard_length &&
         input_length + 1U < sizeof(line) &&
         input_length + prompt_length < OUTPUT_COLUMNS - 1U; ++i)
    {
        char ch = clipboard[i];
        if (ch == '\n' || ch == '\r')
            continue;
        line[input_length++] = ch;
        line[input_length] = '\0';
        output_character(ch);
    }
}

static void render(void)
{
    command_count = 0U;
    render_add_rect(8, 8, 720, 330, COLOR_SURFACE);
    render_add_rect(8, 8, 720, 2, COLOR_ACCENT);
    render_add_text(16, 16, "VantaOS Terminal | user + kernel shell", COLOR_TEXT);

    size_t end = output_count - output_scroll_offset;
    size_t first = end > DISPLAY_LINES ? end - DISPLAY_LINES : 0U;
    int y = 42;
    for (size_t i = first; i < end && y < 416; i++)
    {
        if (output_line(i)[0])
            render_line_with_selection(i, y);
        y += 22;
    }

    render_add_rect(8, 344, 720, 1, COLOR_BORDER);
    render_add_text(16, 350, "Drag to select | double-click word | Ctrl+C copy | Ctrl+V paste",
                    COLOR_MUTED);
    render_add_text(16, 374, "PgUp/PgDn scrollback | type exit to close Terminal", COLOR_MUTED);
    (void)vanta_gui_draw(window_id, commands, command_count);
}

static void prompt(void)
{
    write_text("vanta:/ $ ");
    prompt_length = string_length(output_line(output_count - 1U));
}

static void command_help(void)
{
    write_text("VantaOS user terminal commands:\n");
    write_text("  help                 Show this command list\n");
    write_text("  about                Show terminal information\n");
    write_text("  pwd                  Show the current VFS location\n");
    write_text("  ls [path]            List a VFS directory\n");
    write_text("  cat <path>           Print a VFS file\n");
    write_text("  echo <text>          Print text\n");
    write_text("  exec <elf> [args...] Run a user ELF and wait for it\n");
    write_text("  curl <URL>           Fetch an HTTP URL and print its body\n");
    write_text("  curl -o <file> <URL> Download an HTTP response to a VFS file\n");
    write_text("  net connect          Configure networking with DHCP\n");
    write_text("  net status           Show network interfaces and counters\n");
    write_text("  curl supports plain HTTP only; HTTPS/TLS is not implemented.\n");
    write_text("  kernel <command>     Run a supported kernel-shell command\n");
    write_text("  clear                Clear visible terminal history\n");
    write_text("  exit                 Close the terminal\n");
    write_text("  Up / Down            Recall the last 16 commands\n");
    write_text("  PageUp / PageDown    Scroll retained output\n");
    write_text("Paths refer to VantaOS, not files on the host computer.\n");
    write_text("The kernel command interface is privileged; use it deliberately.\n");
}

static void command_list(const char* path)
{
    long result = vanta_listdir(path && path[0] ? path : "/",
                                list_buffer, sizeof(list_buffer));
    if (result < 0)
    {
        write_text("ls: cannot list path (");
        if (result == -2)
            write_text("not found");
        else if (result == -22)
            write_text("invalid path or output too small");
        else
            write_text("I/O error");
        write_text(")\n");
        return;
    }
    if (result == 0)
        write_text("(empty)\n");
    else
        write_bytes(list_buffer, (size_t)result);
}

static void command_cat(const char* path)
{
    long fd = vanta_open(path, VANTA_OPEN_READ);
    if (fd < 0)
    {
        write_text("cat: file not found or not readable\n");
        return;
    }

    for (;;)
    {
        long count = vanta_read((int)fd, io_buffer, sizeof(io_buffer));
        if (count < 0)
        {
            write_text("cat: read failed\n");
            break;
        }
        if (count == 0)
            break;
        write_bytes(io_buffer, (size_t)count);
    }
    (void)vanta_close((int)fd);
    write_text("\n");
}

static void command_exec(char* command)
{
    const char* arguments[PROCESS_USER_MAX_ARGS];
    size_t argc = 0U;
    char* cursor = command;

    while (*cursor == ' ')
        cursor++;
    while (*cursor && argc < PROCESS_USER_MAX_ARGS)
    {
        arguments[argc++] = cursor;
        while (*cursor && *cursor != ' ')
            cursor++;
        if (!*cursor)
            break;
        *cursor++ = '\0';
        while (*cursor == ' ')
            cursor++;
    }

    if (argc == 0U)
    {
        write_text("exec: usage: exec /path/to/program.elf [args...]\n");
        return;
    }

    long child = vanta_spawn(arguments[0], arguments, argc);
    if (child < 0)
    {
        write_text("exec: could not start ELF program (");
        if (child == -2)
            write_text("not found");
        else if (child == -8)
            write_text("invalid ELF");
        else
            write_text("spawn error");
        write_text(")\n");
        return;
    }

    int32_t status = 0;
    long waited = vanta_waitpid((int)child, &status);
    if (waited < 0)
    {
        write_text("exec: child wait failed\n");
        return;
    }

    write_text("Process exited with status ");
    char digits[12];
    size_t used = 0U;
    uint32_t value = status < 0 ? 0U - (uint32_t)status : (uint32_t)status;
    do
    {
        digits[used++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value && used < sizeof(digits));
    if (status < 0)
        write_text("-");
    while (used)
        output_character(digits[--used]);
    write_text("\n");
}

static void command_kernel(const char* command)
{
    long result = vanta_kernel_command(command, kernel_output,
                                       sizeof(kernel_output));
    if (result < 0)
    {
        write_text("kernel: command rejected or failed (");
        if (result == -1)
            write_text("terminal process required");
        else if (result == -22)
            write_text("invalid command or output size");
        else if (result == -14)
            write_text("invalid memory range");
        else
            write_text("kernel error");
        write_text(")\n");
        return;
    }

    size_t length = (size_t)result;
    if (length >= sizeof(kernel_output))
        length = sizeof(kernel_output) - 1U;
    kernel_output[length] = '\0';
    /* The legacy kernel shell prints its own prompt; the GUI terminal owns
       the visible prompt, so suppress that final prompt character. */
    if (length > 0U && kernel_output[length - 1U] == '>')
        kernel_output[--length] = '\0';
    write_text(kernel_output);
    if (length == 0U || kernel_output[length - 1U] != '\n')
        write_text("\n");
}

static void history_store(const char* command)
{
    if (!command || !command[0])
        return;

    size_t slot;
    if (history_count < HISTORY_ENTRIES)
    {
        slot = (history_start + history_count) % HISTORY_ENTRIES;
        history_count++;
    }
    else
    {
        history_start = (history_start + 1U) % HISTORY_ENTRIES;
        slot = (history_start + history_count - 1U) % HISTORY_ENTRIES;
    }

    size_t i = 0U;
    while (i + 1U < LINE_CAPACITY && command[i])
    {
        history[slot][i] = command[i];
        i++;
    }
    history[slot][i] = '\0';
    history_position = history_count;
    history_browsing = 0;
}

/* Replace the editable command without leaving stale characters on screen. */
static void history_recall(size_t position)
{
    if (position > history_count)
        position = history_count;

    while (input_length > 0U)
    {
        input_length--;
        output_backspace();
    }
    for (size_t i = 0U; i < sizeof(line); i++)
        line[i] = '\0';

    if (position < history_count)
    {
        size_t slot = (history_start + position) % HISTORY_ENTRIES;
        size_t i = 0U;
        while (i + 1U < sizeof(line) && history[slot][i])
        {
            line[i] = history[slot][i];
            output_character(line[i]);
            i++;
        }
        input_length = i;
    }

    history_position = position;
    history_browsing = position < history_count;
    render();
}

static void execute_line(char* command)
{
    while (*command == ' ')
        command++;
    if (!*command)
        return;

    if (string_equals(command, "help"))
        command_help();
    else if (string_equals(command, "about"))
        write_text("VantaOS Terminal - ring-3 application using the GUI syscall ABI.\n");
    else if (string_equals(command, "pwd"))
        write_text("/\n");
    else if (string_equals(command, "ls"))
        command_list("/");
    else if (starts_with(command, "ls "))
        command_list(command + 3);
    else if (starts_with(command, "cat "))
        command_cat(command + 4);
    else if (starts_with(command, "echo "))
    {
        write_text(command + 5);
        write_text("\n");
    }
    else if (starts_with(command, "exec "))
        command_exec(command + 5);
    else if (starts_with(command, "kernel "))
        command_kernel(command + 7);
    else if (starts_with(command, "curl "))
        command_kernel(command);
    else if (starts_with(command, "net "))
        command_kernel(command);
    else if (string_equals(command, "clear"))
    {
        for (size_t i = 0U; i < OUTPUT_LINES; i++)
            clear_line(i);
        output_start = 0U;
        output_count = 1U;
        output_scroll_offset = 0U;
        prompt_length = 0U;
        write_text("VantaOS Terminal - ring-3 shell\n");
    }
    else if (string_equals(command, "exit"))
    {
        (void)vanta_gui_close_window(window_id);
        vanta_exit(0);
    }
    else
    {
        write_text("Unknown command: ");
        write_text(command);
        write_text("\nType 'help' to list available commands.\n");
    }
}

static void close_app(void)
{
    if (window_id != 0U)
        (void)vanta_gui_close_window(window_id);
    vanta_exit(0);
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    current_theme = vanta_theme_load();

    vanta_gui_window_request_t request = {
        .width = WINDOW_WIDTH,
        .height = WINDOW_HEIGHT,
        .title = "Terminal"
    };
    long created = vanta_gui_create_window(&request);
    if (created <= 0)
        vanta_exit(3);
    window_id = (uint32_t)created;

    write_text("VantaOS Terminal - ring-3 shell\n");
    write_text("Type 'help' to list commands.\n");
    prompt();
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

        if (event.type == VANTA_GUI_EVENT_POINTER_DOWN && event.value == 1U)
        {
            terminal_pointer_select(event.x, event.y, 0);
            render();
            continue;
        }
        if (event.type == VANTA_GUI_EVENT_POINTER_MOVE && event.value == 1U)
        {
            terminal_pointer_select(event.x, event.y, 1);
            render();
            continue;
        }
        if (event.type == VANTA_GUI_EVENT_POINTER_UP)
        {
            terminal_pointer_select(event.x, event.y, 1);
            selection_dragging = 0;
            render();
            continue;
        }
        if (event.type != VANTA_GUI_EVENT_KEY)
            continue;

        uint32_t key = event.value;
        if ((event.reserved & VANTA_GUI_MOD_CTRL) != 0U)
        {
            if (key == 'c' || key == 'C' || key == 3U)
            {
                terminal_copy_selection();
                render();
                continue;
            }
            if (key == 'v' || key == 'V' || key == 22U)
            {
                terminal_paste_clipboard();
                render();
                continue;
            }
            if (key == 'a' || key == 'A' || key == 1U)
            {
                selection_anchor_line = 0U;
                selection_anchor_column = 0U;
                selection_end_line = output_count - 1U;
                selection_end_column = string_length(output_line(output_count - 1U));
                selection_active = 1;
                selection_dragging = 0;
                render();
                continue;
            }
        }
        if (key == VANTA_GUI_KEY_PAGE_UP)
        {
            size_t maximum = output_count > DISPLAY_LINES ?
                output_count - DISPLAY_LINES : 0U;
            if (output_scroll_offset + 8U > maximum)
                output_scroll_offset = maximum;
            else
                output_scroll_offset += 8U;
            render();
            continue;
        }
        if (key == VANTA_GUI_KEY_PAGE_DOWN)
        {
            output_scroll_offset = output_scroll_offset > 8U ?
                output_scroll_offset - 8U : 0U;
            render();
            continue;
        }
        if (key == VANTA_GUI_KEY_UP)
        {
            if (history_count != 0U && history_position > 0U)
                history_recall(history_position - 1U);
            continue;
        }
        if (key == VANTA_GUI_KEY_DOWN)
        {
            if (history_browsing && history_position < history_count)
                history_recall(history_position + 1U);
            continue;
        }
        if (key == '\r' || key == '\n')
        {
            output_character('\n');
            line[input_length] = '\0';
            prompt_length = 0U;
            history_store(line);
            execute_line(line);
            for (size_t i = 0U; i < sizeof(line); i++)
                line[i] = '\0';
            input_length = 0U;
            prompt();
            render();
        }
        else if (key == '\b' || key == 127U)
        {
            if (input_length > 0U)
            {
                input_length--;
                line[input_length] = '\0';
                output_backspace();
                render();
            }
        }
        else if (key >= 32U && key <= 126U &&
                 input_length + prompt_length < OUTPUT_COLUMNS - 1U)
        {
            output_scroll_offset = 0U;
            line[input_length++] = (char)key;
            line[input_length] = '\0';
            output_character((char)key);
            render();
        }
    }
}