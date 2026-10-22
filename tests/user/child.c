/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include <vanta/syscall.h>
#include <vanta/gui.h>

static const char success[] = "VANTAOS_USER_CHILD_PROGRAM_OK\n";
static const char list_success[] = "VANTAOS_USER_DIRECTORY_LIST_OK\n";
static const char failure[] = "VANTAOS_USER_CHILD_PROGRAM_FAIL\n";

static int run_ipc_syscall_test(void)
{
    long endpoint = vanta_ipc_endpoint_create();
    if (endpoint <= 0)
        return 0;
    char buffer[VANTA_IPC_MESSAGE_MAX];
    if (vanta_ipc_receive((uint64_t)endpoint, buffer, sizeof(buffer)) !=
            -VANTA_EAGAIN ||
        vanta_ipc_send((uint64_t)endpoint,
                       (const void *)(uintptr_t)0x7ffffffff000ULL, 1U) !=
            -VANTA_EFAULT ||
        vanta_ipc_send((uint64_t)endpoint, "x",
                       VANTA_IPC_MESSAGE_MAX + 1U) != -VANTA_EINVAL)
        return 0;

    if (vanta_ipc_send((uint64_t)endpoint, "ab", 2U) != 0 ||
        vanta_ipc_send((uint64_t)endpoint, "b", 1U) != 0 ||
        vanta_ipc_receive((uint64_t)endpoint, buffer, 1U) !=
            -VANTA_EMSGSIZE ||
        vanta_ipc_receive((uint64_t)endpoint, buffer, sizeof(buffer)) != 2L ||
        buffer[0] != 'a' || buffer[1] != 'b' ||
        vanta_ipc_receive((uint64_t)endpoint, buffer, sizeof(buffer)) != 1L ||
        buffer[0] != 'b')
        return 0;

    for (unsigned int i = 0; i < 8U; ++i)
        if (vanta_ipc_send((uint64_t)endpoint, "q", 1U) != 0)
            return 0;
    if (vanta_ipc_send((uint64_t)endpoint, "overflow", 8U) !=
        -VANTA_EAGAIN)
        return 0;
    for (unsigned int i = 0; i < 8U; ++i)
        if (vanta_ipc_receive((uint64_t)endpoint, buffer, sizeof(buffer)) != 1L ||
            buffer[0] != 'q')
            return 0;
    if (vanta_ipc_close((uint64_t)endpoint) != 0 ||
        vanta_ipc_send((uint64_t)endpoint, "stale", 5U) != -VANTA_EBADF ||
        vanta_ipc_close((uint64_t)endpoint) != -VANTA_EBADF)
        return 0;
    return 1;
}

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

static int parse_endpoint(const char *text, uint64_t *out)
{
    uint64_t value = 0U;
    if (!text || !text[0] || !out)
        return 0;
    for (size_t i = 0; text[i]; ++i)
    {
        if (text[i] < '0' || text[i] > '9')
            return 0;
        uint64_t digit = (uint64_t)(text[i] - '0');
        if (value > (UINT64_MAX - digit) / 10U)
            return 0;
        value = value * 10U + digit;
    }
    if (!value || value > INT64_MAX)
        return 0;
    *out = value;
    return 1;
}

int main(int argc, char** argv)
{
    uint64_t parent_endpoint = 0U;
    if (argc != 3 || !string_equals(argv[1], "spawned-child") ||
        !parse_endpoint(argv[2], &parent_endpoint))
    {
        (void)vanta_write(failure, sizeof(failure) - 1U);
        return 1;
    }

    if (!run_ipc_syscall_test())
    {
        (void)vanta_write(failure, sizeof(failure) - 1U);
        return 13;
    }
    static const char ipc_success[] = "VANTAOS_IPC_RING3_SYSCALL_OK\n";
    (void)vanta_write(ipc_success, sizeof(ipc_success) - 1U);

    long child_endpoint = vanta_ipc_endpoint_create();
    if (child_endpoint <= 0 ||
        vanta_ipc_send(parent_endpoint, &child_endpoint,
                       sizeof(child_endpoint)) != 0)
    {
        (void)vanta_write(failure, sizeof(failure) - 1U);
        return 14;
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

    long write_fd = vanta_open(range_path,
                               VANTA_OPEN_WRITE | VANTA_OPEN_TRUNCATE);
    if (write_fd < 0)
    {
        if (vanta_create_file(range_path) < 0)
        {
            (void)vanta_write(failure, sizeof(failure) - 1U);
            return 7;
        }
        write_fd = vanta_open(range_path,
                              VANTA_OPEN_WRITE | VANTA_OPEN_TRUNCATE);
    }
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
    static const char range_success[] = "VANTAOS_USER_FILE_RANGE_IO_OK\n";
    (void)vanta_write(range_success, sizeof(range_success) - 1U);
    const char* settings_args[] = { "/SYSTEM/SETTINGS.ELF", "--self-test" };
    long settings_pid = vanta_spawn("/SYSTEM/SETTINGS.ELF", settings_args, 2U);
    if (settings_pid <= 0)
    {
        (void)vanta_write(failure, sizeof(failure) - 1U);
        return 11;
    }
    int32_t settings_status = -1;
    if (vanta_waitpid((int)settings_pid, &settings_status) != settings_pid ||
        settings_status != 0)
    {
        (void)vanta_write(failure, sizeof(failure) - 1U);
        return 12;
    }

    char ipc_reply[8] = {0};
    long ipc_reply_length = -VANTA_EAGAIN;
    for (unsigned int retries = 0U;
         retries < 500U && ipc_reply_length == -VANTA_EAGAIN; ++retries)
    {
        ipc_reply_length = vanta_ipc_receive((uint64_t)child_endpoint,
                                             ipc_reply, sizeof(ipc_reply));
        if (ipc_reply_length == -VANTA_EAGAIN)
            (void)vanta_yield();
    }
    if (ipc_reply_length != 3L || ipc_reply[0] != 'a' ||
        ipc_reply[1] != 'c' || ipc_reply[2] != 'k' ||
        vanta_ipc_close((uint64_t)child_endpoint) != 0)
    {
        (void)vanta_write(failure, sizeof(failure) - 1U);
        return 15;
    }
    static const char ipc_cross_success[] = "VANTAOS_IPC_CROSS_PROCESS_OK\n";
    (void)vanta_write(ipc_cross_success, sizeof(ipc_cross_success) - 1U);

    (void)vanta_write(list_success, sizeof(list_success) - 1U);
    (void)vanta_write(success, sizeof(success) - 1U);
    return 23;
}
