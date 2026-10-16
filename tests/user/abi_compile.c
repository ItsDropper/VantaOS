#include <vanta/syscall.h>

/* Compiled by CI to keep every public wrapper and argument order buildable. */
long vanta_user_abi_compile_check(
    const char* path, const char* const argv[], char* buffer, int32_t* status)
{
    long fd = vanta_open(path, VANTA_OPEN_READ);
    long result = vanta_write(buffer, 1U);
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

/* Keep the new persistent-file API part of the freestanding ABI compile gate. */
static long (*const file_write_api)(int, const void*, size_t) = vanta_file_write;
static long (*const create_file_api)(const char*) = vanta_create_file;
