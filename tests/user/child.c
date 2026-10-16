#include <vanta/syscall.h>
#include <vanta/gui.h>

static const char success[] = "VANTAOS_USER_CHILD_PROGRAM_OK\n";
static const char list_success[] = "VANTAOS_USER_DIRECTORY_LIST_OK\n";
static const char failure[] = "VANTAOS_USER_CHILD_PROGRAM_FAIL\n";

static int string_contains(const char* text, const char* needle)
{
    if (!text || !needle || !needle[0])
        return 0;
    for (size_t i = 0; text[i]; i++)
    {
        size_t j = 0U;
        while (needle[j] && text[i + j] == needle[j])
            j++;
        if (!needle[j])
            return 1;
    }
    return 0;
}

static int string_equals(const char* left, const char* right)
{
    if (!left || !right)
        return 0;
    while (*left && *right)
    {
        if (*left != *right)
            return 0;
        ++left;
        ++right;
    }
    return *left == *right;
}

int main(int argc, char** argv)
{
    if (argc != 2 || !string_equals(argv[1], "spawned-child"))
    {
        (void)vanta_write(failure, sizeof(failure) - 1U);
        return 1;
    }

    char entries[1024];
    long listed = vanta_listdir("/", entries, sizeof(entries));
    if (listed < 0 || !string_contains(entries, "CHILD.ELF") ||
        !string_contains(entries, "USERDATA.TXT"))
    {
        (void)vanta_write(failure, sizeof(failure) - 1U);
        return 2;
    }

    vanta_gui_window_request_t request = {
        .width = 420U,
        .height = 280U,
        .title = "GUI ABI Test"
    };
    long window = vanta_gui_create_window(&request);
    if (window <= 0)
    {
        (void)vanta_write(failure, sizeof(failure) - 1U);
        return 3;
    }

    vanta_gui_draw_command_t commands[2] = {
        {
            .type = VANTA_GUI_DRAW_FILL_RECT,
            .x = 0,
            .y = 0,
            .width = 100,
            .height = 32,
            .color = 0x003B82F6U,
            .scale = 1U
        },
        {
            .type = VANTA_GUI_DRAW_TEXT,
            .x = 8,
            .y = 48,
            .color = 0x00FFFFFFU,
            .scale = 1U,
            .text = "Ring-3 GUI syscall works"
        }
    };

    if (vanta_gui_draw((uint32_t)window,
                       (const vanta_gui_draw_command_t*)(uintptr_t)0x7ffffffff000ULL,
                       1U) != -VANTA_EFAULT)
    {
        (void)vanta_gui_close_window((uint32_t)window);
        (void)vanta_write(failure, sizeof(failure) - 1U);
        return 4;
    }

    vanta_gui_draw_command_t invalid = {
        .type = VANTA_GUI_DRAW_FILL_RECT,
        .x = 0,
        .y = 0,
        .width = -1,
        .height = 20,
        .color = 0x00FFFFFFU
    };
    if (vanta_gui_draw((uint32_t)window, &invalid, 1U) != -VANTA_EINVAL ||
        vanta_gui_draw((uint32_t)window, commands, 2U) != 2L)
    {
        (void)vanta_gui_close_window((uint32_t)window);
        (void)vanta_write(failure, sizeof(failure) - 1U);
        return 5;
    }

    /* Give the desktop process a few ticks to present the retained commands. */
    for (unsigned int i = 0; i < 4U; i++)
        (void)vanta_yield();

    vanta_gui_event_t event;
    if (vanta_gui_poll_event((uint32_t)window,
                             (vanta_gui_event_t*)(uintptr_t)0x7ffffffff000ULL) !=
            -VANTA_EFAULT ||
        vanta_gui_poll_event((uint32_t)window, &event) != 1L ||
        event.type != VANTA_GUI_EVENT_FOCUS_GAINED ||
        vanta_gui_poll_event((uint32_t)window, &event) != 0L ||
        vanta_gui_close_window(UINT32_MAX) != -VANTA_EBADF ||
        vanta_gui_close_window((uint32_t)window) != 0L ||
        vanta_gui_close_window((uint32_t)window) != -VANTA_EBADF)
    {
        (void)vanta_gui_close_window((uint32_t)window);
        (void)vanta_write(failure, sizeof(failure) - 1U);
        return 6;
    }

    static const char gui_success[] = "VANTAOS_USER_GUI_SYSCALL_OK\n";
    (void)vanta_write(gui_success, sizeof(gui_success) - 1U);
    /* Verify user-mode file syscalls stream beyond the old 511-byte cap. */
    static const char range_path[] = "/USERIO.BIN";
    char range_write[1024];
    char range_read[1024];
    for (unsigned int i = 0; i < sizeof(range_write); i++)
        range_write[i] = (char)((i * 29U + 7U) & 0xFFU);

    if (vanta_create_file(range_path) < 0)
    {
        (void)vanta_write(failure, sizeof(failure) - 1U);
        return 7;
    }
    long write_fd = vanta_open(range_path,
                               VANTA_OPEN_WRITE | VANTA_OPEN_TRUNCATE);
    if (write_fd < 0 ||
        vanta_file_write((int)write_fd, range_write, sizeof(range_write)) !=
            (long)sizeof(range_write) ||
        vanta_close((int)write_fd) != 0)
    {
        (void)vanta_write(failure, sizeof(failure) - 1U);
        return 8;
    }

    long read_fd = vanta_open(range_path, VANTA_OPEN_READ);
    if (read_fd < 0 ||
        vanta_read((int)read_fd, range_read, sizeof(range_read)) !=
            (long)sizeof(range_read) ||
        vanta_close((int)read_fd) != 0)
    {
        (void)vanta_write(failure, sizeof(failure) - 1U);
        return 9;
    }
    for (unsigned int i = 0; i < sizeof(range_read); i++)
    {
        if (range_read[i] != range_write[i])
        {
            (void)vanta_write(failure, sizeof(failure) - 1U);
            return 10;
        }
    }
    static const char range_success[] = "VANTAOS_USER_FILE_RANGE_IO_OK\\n";
    (void)vanta_write(range_success, sizeof(range_success) - 1U);
    (void)vanta_write(list_success, sizeof(list_success) - 1U);
    (void)vanta_write(success, sizeof(success) - 1U);
    return 23;
}
