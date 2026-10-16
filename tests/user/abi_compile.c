#include <vanta/syscall.h>
#include <vanta/gui.h>

/* Compiled by CI to keep every public wrapper and argument order buildable. */
long vanta_user_abi_compile_check(
    const char* path, const char* const argv[], char* buffer, int32_t* status)
{
    vanta_gui_window_request_t window_request = {0};
    vanta_gui_draw_command_t draw_command = {0};
    vanta_gui_event_t gui_event = {0};
    window_request.width = 420U;
    window_request.height = 280U;
    window_request.title[0] = 'T';
    long gui_window = vanta_gui_create_window(&window_request);
    long fd = vanta_open(path, VANTA_OPEN_READ);
    long result = vanta_write(buffer, 1U);
    result += vanta_gui_draw(gui_window > 0 ? (uint32_t)gui_window : 1U,
                             &draw_command, 1U);
    result += vanta_gui_poll_event(gui_window > 0 ? (uint32_t)gui_window : 1U,
                                   &gui_event);
    result += vanta_gui_close_window(gui_window > 0 ?
                                     (uint32_t)gui_window : 1U);
    result += vanta_create_file(path);
    long write_fd = vanta_open(path, VANTA_OPEN_WRITE | VANTA_OPEN_TRUNCATE);
    if (write_fd >= 0)
    {
        result += vanta_file_write((int)write_fd, buffer, 1U);
        result += vanta_close((int)write_fd);
    }
    result += vanta_console_write(buffer, 1U);
    if (fd >= 0)
    {
        result += vanta_read((int)fd, buffer, 1U);
        result += vanta_close((int)fd);
    }

    result += vanta_read_char();
    result += vanta_yield();
    result += vanta_listdir(path, buffer, 1U);
    result += vanta_terminal_begin_input();
    result += vanta_terminal_backspace();
    result += vanta_terminal_clear();

    long child = vanta_spawn(path, argv, 1U);
    if (child > 0)
        result += vanta_waitpid((int)child, status);

    return result;
}

