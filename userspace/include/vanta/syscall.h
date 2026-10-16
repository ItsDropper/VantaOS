#ifndef VANTA_USER_SYSCALL_H
#define VANTA_USER_SYSCALL_H

/*
 * VantaOS x86-64 userspace syscall ABI.
 *
 * This is a small freestanding interface for statically linked ET_EXEC
 * programs. Calls return either a non-negative result or a negative error
 * number; no libc errno state is maintained.
 */
#include <stddef.h>
#include <stdint.h>

#define VANTA_USER_ABI_VERSION 1U

#define VANTA_SYS_EXIT          0U
#define VANTA_SYS_WRITE         1U
#define VANTA_SYS_CONSOLE_WRITE 2U
#define VANTA_SYS_OPEN          3U
#define VANTA_SYS_READ          4U
#define VANTA_SYS_CLOSE         5U
#define VANTA_SYS_SPAWN         6U
#define VANTA_SYS_WAITPID       7U
#define VANTA_SYS_READ_CHAR     8U
#define VANTA_SYS_YIELD          9U
#define VANTA_SYS_LISTDIR       10U
#define VANTA_SYS_TERMINAL_BEGIN_INPUT 11U
#define VANTA_SYS_TERMINAL_BACKSPACE   12U
#define VANTA_SYS_TERMINAL_CLEAR      13U

#define VANTA_OPEN_READ 1

/* Negative return values currently emitted by the kernel ABI. */
#define VANTA_EPERM       1
#define VANTA_ENOENT      2
#define VANTA_EIO         5
#define VANTA_EBADF       9
#define VANTA_ECHILD     10
#define VANTA_EFAULT     14
#define VANTA_EINVAL     22
#define VANTA_EMFILE     24
#define VANTA_ENOMEM     12
#define VANTA_ENOEXEC     8

static inline long vanta_syscall3(
    long number, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3)
{
    long result = number;
    __asm__ volatile (
        "int $0x80"
        : "+a"(result)
        : "D"(arg1), "S"(arg2), "d"(arg3)
        : "memory", "cc");
    return result;
}

static inline long vanta_write(const void* buffer, size_t length)
{
    return vanta_syscall3(VANTA_SYS_WRITE,
                          (uintptr_t)buffer, (uintptr_t)length, 0U);
}

static inline long vanta_console_write(const void* buffer, size_t length)
{
    return vanta_syscall3(VANTA_SYS_CONSOLE_WRITE,
                          (uintptr_t)buffer, (uintptr_t)length, 0U);
}

static inline long vanta_read_char(void)
{
    return vanta_syscall3(VANTA_SYS_READ_CHAR, 0U, 0U, 0U);
}

static inline long vanta_yield(void)
{
    return vanta_syscall3(VANTA_SYS_YIELD, 0U, 0U, 0U);
}

static inline long vanta_listdir(const char* path, char* buffer, size_t capacity)
{
    return vanta_syscall3(VANTA_SYS_LISTDIR, (uintptr_t)path,
                          (uintptr_t)buffer, (uintptr_t)capacity);
}

static inline long vanta_terminal_begin_input(void)
{
    return vanta_syscall3(VANTA_SYS_TERMINAL_BEGIN_INPUT, 0U, 0U, 0U);
}

static inline long vanta_terminal_backspace(void)
{
    return vanta_syscall3(VANTA_SYS_TERMINAL_BACKSPACE, 0U, 0U, 0U);
}

static inline long vanta_terminal_clear(void)
{
    return vanta_syscall3(VANTA_SYS_TERMINAL_CLEAR, 0U, 0U, 0U);
}

static inline long vanta_open(const char* path, int flags)
{
    return vanta_syscall3(VANTA_SYS_OPEN,
                          (uintptr_t)path, (uintptr_t)(intptr_t)flags, 0U);
}

static inline long vanta_read(int descriptor, void* buffer, size_t length)
{
    return vanta_syscall3(VANTA_SYS_READ,
                          (uintptr_t)(intptr_t)descriptor,
                          (uintptr_t)buffer, (uintptr_t)length);
}

static inline long vanta_close(int descriptor)
{
    return vanta_syscall3(VANTA_SYS_CLOSE,
                          (uintptr_t)(intptr_t)descriptor, 0U, 0U);
}

/* argc must be in [1, 16]; argv and all strings must be user-readable. */
static inline long vanta_spawn(
    const char* path, const char* const argv[], size_t argc)
{
    return vanta_syscall3(VANTA_SYS_SPAWN, (uintptr_t)path,
                          (uintptr_t)argv, (uintptr_t)argc);
}

/* status may be NULL. On success, returns the collected child PID. */
static inline long vanta_waitpid(int child_pid, int32_t* status)
{
    return vanta_syscall3(VANTA_SYS_WAITPID,
                          (uintptr_t)(intptr_t)child_pid,
                          (uintptr_t)status, 0U);
}

/* Successful exit does not return; the loop protects callers if it ever does. */
__attribute__((noreturn))
static inline void vanta_exit(int status)
{
    (void)vanta_syscall3(VANTA_SYS_EXIT,
                         (uintptr_t)(intptr_t)status, 0U, 0U);
    for (;;)
        __asm__ volatile ("" : : : "memory");
}

#endif
