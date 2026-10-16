#include <vanta/syscall.h>

#define PROCESS_USER_MAX_ARGS 16U
#define LINE_CAPACITY 256U
#define LIST_CAPACITY 2048U
#define IO_CHUNK 256U

static char line[LINE_CAPACITY];
static char list_buffer[LIST_CAPACITY];
static char io_buffer[IO_CHUNK];

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

static void write_text(const char* text)
{
    (void)vanta_console_write(text, string_length(text));
}

static void write_bytes(const char* text, size_t length)
{
    if (length != 0U)
        (void)vanta_console_write(text, length);
}

static void prompt(void)
{
    write_text("vanta:/ $ ");
    (void)vanta_terminal_begin_input();
}

static void command_help(void)
{
    write_text("Commands: help, about, pwd, ls [path], cat <path>, echo <text>, exec <elf> [args...], clear, exit\n");
    write_text("Programs are static ELF64 files executed in ring 3.\n");
}

static void command_list(const char* path)
{
    long result = vanta_listdir(path && path[0] ? path : "/", list_buffer,
                                sizeof(list_buffer));
    if (result < 0)
    {
        write_text("ls: cannot list path (error ");
        if (result == -2)
            write_text("not found");
        else if (result == -22)
            write_text("invalid path or output too small");
        else
            write_text("I/O");
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
        write_text("exec: could not start ELF program (error ");
        if (child == -2)
            write_text("not found");
        else if (child == -8)
            write_text("invalid ELF");
        else
            write_text("spawn");
        write_text(")\n");
        return;
    }

    int32_t status = 0;
    long waited = vanta_waitpid((int)child, &status);
    if (waited < 0)
        write_text("exec: child wait failed\n");
    else
        write_text("Process exited with status ");
    if (waited >= 0)
    {
        char digits[12];
        size_t used = 0U;
        uint32_t value = status < 0 ? (uint32_t)(-status) : (uint32_t)status;
        do
        {
            digits[used++] = (char)('0' + value % 10U);
            value /= 10U;
        } while (value && used < sizeof(digits));
        if (status < 0)
            write_text("-");
        while (used)
            write_bytes(&digits[--used], 1U);
        write_text("\n");
    }
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
        write_text("VantaOS Terminal — user-mode shell (CPL3).\n");
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
        (void)vanta_terminal_clear();
        write_text("VantaOS Terminal — user-mode shell\n");
    }
    else if (string_equals(command, "exit"))
        vanta_exit(0);
    else
    {
        write_text("Unknown command: ");
        write_text(command);
        write_text("\nType 'help' to list available commands.\n");
    }
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    write_text("VantaOS Terminal — user-mode shell\n");
    write_text("Type 'help' for commands.\n");
    prompt();

    size_t length = 0U;
    for (;;)
    {
        long value = vanta_read_char();
        if (value <= 0)
        {
            (void)vanta_yield();
            continue;
        }

        char c = (char)value;
        if (c == '\r' || c == '\n')
        {
            write_text("\n");
            line[length] = '\0';
            execute_line(line);
            for (size_t i = 0; i < sizeof(line); i++)
                line[i] = '\0';
            length = 0U;
            prompt();
        }
        else if (c == '\b' || c == 127)
        {
            if (length > 0U)
            {
                length--;
                line[length] = '\0';
                (void)vanta_terminal_backspace();
            }
        }
        else if (c >= 32 && c <= 126 && length < sizeof(line) - 1U)
        {
            line[length++] = c;
            line[length] = '\0';
            write_bytes(&c, 1U);
        }
    }
}
