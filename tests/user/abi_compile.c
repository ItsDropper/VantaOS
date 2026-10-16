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

    long child = vanta_spawn(path, argv, 1U);
    if (child > 0)
        result += vanta_waitpid((int)child, status);

    return result;
}
