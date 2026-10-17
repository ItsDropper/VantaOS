#include <vanta/gui.h>

#define PROCESS_USER_MAX_ARGS 16U
#define LINE_CAPACITY 256U
#define LIST_CAPACITY 2048U
#define IO_CHUNK 256U
#define OUTPUT_LINES 80U
#define OUTPUT_COLUMNS (VANTA_GUI_TEXT_MAX + 1U)
#define DISPLAY_LINES 17U
#define WINDOW_WIDTH 760U
#define WINDOW_HEIGHT 520U

#define COLOR_TEXT 0x00E8EDF4U
#define COLOR_MUTED 0x008D9BACU
#define COLOR_SURFACE 0x000C1119U
#define COLOR_BORDER 0x002A3746U
#define COLOR_ACCENT 0x004B9CFFU

static char line[LINE_CAPACITY];
static char list_buffer[LIST_CAPACITY];
static char io_buffer[IO_CHUNK];
static char output[OUTPUT_LINES][OUTPUT_COLUMNS];
static size_t output_count = 1U;
static size_t input_length;
static size_t prompt_length;
static vanta_gui_draw_command_t commands[VANTA_GUI_MAX_COMMANDS];
static uint32_t command_count;
static uint32_t window_id;

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

static void output_newline(void)
{
    if (output_count < OUTPUT_LINES)
    {
        output_count++;
        clear_line(output_count - 1U);
        return;
    }

    for (size_t row = 1U; row < OUTPUT_LINES; row++)
        for (size_t column = 0U; column < OUTPUT_COLUMNS; column++)
            output[row - 1U][column] = output[row][column];
    clear_line(OUTPUT_LINES - 1U);
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

    size_t row = output_count - 1U;
    size_t length = string_length(output[row]);
    if (length >= OUTPUT_COLUMNS - 1U)
    {
        output_newline();
        row = output_count - 1U;
        length = 0U;
    }
    output[row][length] = value;
    output[row][length + 1U] = '\0';
}

static void output_backspace(void)
{
    size_t row = output_count - 1U;
    size_t length = string_length(output[row]);
    if (length > prompt_length)
        output[row][length - 1U] = '\0';
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

static void render(void)
{
    command_count = 0U;
    render_add_rect(8, 8, 720, 440, COLOR_SURFACE);
    render_add_rect(8, 8, 720, 2, COLOR_ACCENT);
    render_add_text(16, 16, "VantaOS Terminal | user-mode shell", COLOR_TEXT);

    size_t first = output_count > DISPLAY_LINES ?
        output_count - DISPLAY_LINES : 0U;
    int y = 42;
    for (size_t i = first; i < output_count && y < 416; i++)
    {
        if (output[i][0])
            render_add_text(16, y, output[i], COLOR_TEXT);
        y += 22;
    }

    render_add_rect(8, 424, 720, 1, COLOR_BORDER);
    render_add_text(16, 430, "Type help for commands  |  exit closes Terminal",
                    COLOR_MUTED);
    (void)vanta_gui_draw(window_id, commands, command_count);
}

static void prompt(void)
{
    write_text("vanta:/ $ ");
    prompt_length = string_length(output[output_count - 1U]);
}

static void command_help(void)
{
    write_text("Commands: help, about, pwd, ls [path], cat <path>, echo <text>, exec <elf> [args...], clear, exit\n");
    write_text("Programs are static ELF64 files executed in ring 3.\n");
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
    else if (string_equals(command, "clear"))
    {
        for (size_t i = 0U; i < OUTPUT_LINES; i++)
            clear_line(i);
        output_count = 1U;
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

        if (event.type != VANTA_GUI_EVENT_KEY)
            continue;

        uint32_t key = event.value;
        if (key == '\r' || key == '\n')
        {
            output_character('\n');
            line[input_length] = '\0';
            prompt_length = 0U;
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
                 input_length < sizeof(line) - 1U)
        {
            line[input_length++] = (char)key;
            line[input_length] = '\0';
            output_character((char)key);
            render();
        }
    }
}