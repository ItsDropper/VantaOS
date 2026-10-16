#include "syscall.h"

#include "paging.h"
#include "process.h"
#include "serial.h"
#include "terminal.h"
#include "vfs.h"
#include "user_mode_test.h"

#include <stdint.h>

#define USER_SYSCALL_EXIT 0U
#define USER_SYSCALL_WRITE 1U
#define USER_SYSCALL_CONSOLE_WRITE 2U
#define USER_SYSCALL_OPEN 3U
#define USER_SYSCALL_READ 4U
#define USER_SYSCALL_CLOSE 5U
#define USER_SYSCALL_SPAWN 6U
#define USER_SYSCALL_WAITPID 7U
#define USER_SYSCALL_IO_MAX 4096U
#define USER_SYSCALL_WRITE_MAX 4096U

static int syscall_write(struct exception_frame* frame,
                         const process_t* process)
{
    const uintptr_t address = (uintptr_t)frame->rdi;
    const uint64_t length = frame->rsi;

    if (length == 0U)
    {
        frame->rax = 0U;
        return 0;
    }
    if (length > USER_SYSCALL_WRITE_MAX)
    {
        frame->rax = (uint64_t)(int64_t)-22;
        return 0;
    }
    if (!paging_user_range_valid(process->address_space, address,
                                 (size_t)length, 0))
    {
        frame->rax = (uint64_t)(int64_t)-14;
        return 0;
    }

    serial_write_bytes((const char*)address, (size_t)length);
    frame->rax = length;
    return 0;
}

static int syscall_console_write(struct exception_frame* frame,
                                 const process_t* process)
{
    const uintptr_t address = (uintptr_t)frame->rdi;
    const uint64_t length = frame->rsi;

    if (length == 0U)
    {
        frame->rax = 0U;
        return 0;
    }
    if (length > USER_SYSCALL_WRITE_MAX)
    {
        frame->rax = (uint64_t)(int64_t)-22;
        return 0;
    }
    if (!paging_user_range_valid(process->address_space, address,
                                 (size_t)length, 0))
    {
        frame->rax = (uint64_t)(int64_t)-14;
        return 0;
    }

    char chunk[129];
    uint64_t offset = 0U;
    while (offset < length)
    {
        size_t count = (size_t)(length - offset);
        if (count > sizeof(chunk) - 1U)
            count = sizeof(chunk) - 1U;
        for (size_t i = 0; i < count; i++)
            chunk[i] = ((const char*)address)[offset + i];
        chunk[count] = '\0';
        terminal_write(chunk);
        offset += count;
    }
    frame->rax = length;
    return 0;
}

static int syscall_open(struct exception_frame* frame, uint32_t pid,
                        const process_t* process)
{
    uintptr_t address = (uintptr_t)frame->rdi;
    if (frame->rsi != VFS_OPEN_READ)
    {
        frame->rax = (uint64_t)(int64_t)-22;
        return 0;
    }

    char path[FS_PATH_MAX];
    size_t length = 0U;
    for (; length < sizeof(path); length++)
    {
        uintptr_t current = address + length;
        if (current < address ||
            !paging_user_range_valid(process->address_space, current, 1U, 0))
        {
            frame->rax = (uint64_t)(int64_t)-14;
            return 0;
        }

        path[length] = *(const char*)current;
        if (path[length] == '\0')
            break;
    }
    if (length == 0U || length == sizeof(path))
    {
        frame->rax = (uint64_t)(int64_t)-22;
        return 0;
    }

    int handle = vfs_open(path, VFS_OPEN_READ);
    if (handle < 0)
    {
        frame->rax = (uint64_t)(int64_t)-2;
        return 0;
    }

    int descriptor = process_fd_install(pid, handle);
    if (descriptor < 0)
    {
        (void)vfs_close(handle);
        frame->rax = (uint64_t)(int64_t)-24;
        return 0;
    }

    frame->rax = (uint64_t)descriptor;
    return 0;
}

static int syscall_read(struct exception_frame* frame, uint32_t pid,
                        const process_t* process)
{
    if (frame->rdi < PROCESS_FD_BASE ||
        frame->rdi >= PROCESS_FD_BASE + PROCESS_FD_COUNT)
    {
        frame->rax = (uint64_t)(int64_t)-9;
        return 0;
    }
    int descriptor = (int)frame->rdi;
    uintptr_t address = (uintptr_t)frame->rsi;
    uint64_t length = frame->rdx;

    if (length > USER_SYSCALL_IO_MAX)
    {
        frame->rax = (uint64_t)(int64_t)-22;
        return 0;
    }

    int handle = process_fd_get(pid, descriptor);
    if (handle < 0)
    {
        frame->rax = (uint64_t)(int64_t)-9;
        return 0;
    }
    if (length == 0U)
    {
        frame->rax = 0U;
        return 0;
    }
    if (!paging_user_range_valid(process->address_space, address,
                                 (size_t)length, 1))
    {
        frame->rax = (uint64_t)(int64_t)-14;
        return 0;
    }

    char buffer[USER_SYSCALL_IO_MAX];
    int result = vfs_read(handle, buffer, (unsigned int)length);
    if (result < 0)
    {
        frame->rax = (uint64_t)(int64_t)-5;
        return 0;
    }

    for (int i = 0; i < result; i++)
        ((char*)address)[i] = buffer[i];

    frame->rax = (uint64_t)result;
    return 0;
}

static int syscall_close(struct exception_frame* frame, uint32_t pid)
{
    if (frame->rdi < PROCESS_FD_BASE ||
        frame->rdi >= PROCESS_FD_BASE + PROCESS_FD_COUNT)
    {
        frame->rax = (uint64_t)(int64_t)-9;
        return 0;
    }
    int descriptor = (int)frame->rdi;
    int handle = -1;
    if (!process_fd_remove(pid, descriptor, &handle))
    {
        frame->rax = (uint64_t)(int64_t)-9;
        return 0;
    }

    frame->rax = vfs_close(handle) == 0 ?
        0U : (uint64_t)(int64_t)-5;
    return 0;
}

static int syscall_copy_user_string(char* destination, size_t capacity,
                                     uintptr_t address,
                                     const process_t* process,
                                     size_t* length_out)
{
    if (!destination || capacity == 0U || !process || !length_out)
        return -22;

    for (size_t i = 0; i < capacity; i++)
    {
        uintptr_t current = address + i;
        if (current < address ||
            !paging_user_range_valid(process->address_space, current, 1U, 0))
            return -14;

        destination[i] = *(const char*)current;
        if (destination[i] == '\0')
        {
            *length_out = i;
            return 0;
        }
    }
    return -22;
}

/*
 * spawn(path, argv, argc) loads a read-only ELF64 file from the VFS and
 * publishes it as a child of the calling user process. All user strings are
 * copied into kernel-owned storage before filesystem or ELF code consumes
 * them; no user pointer is retained by the child.
 */
static int syscall_spawn(struct exception_frame* frame,
                         const process_t* process)
{
    uint64_t argc_value = frame->rdx;
    if (argc_value == 0U || argc_value > PROCESS_USER_MAX_ARGS)
    {
        frame->rax = (uint64_t)(int64_t)-22;
        return 0;
    }

    char path[FS_PATH_MAX];
    size_t path_length = 0U;
    int copy_result = syscall_copy_user_string(
        path, sizeof(path), (uintptr_t)frame->rdi, process, &path_length);
    if (copy_result != 0 || path_length == 0U)
    {
        frame->rax = (uint64_t)(int64_t)(copy_result != 0 ?
                                         copy_result : -22);
        return 0;
    }

    const uint32_t argc = (uint32_t)argc_value;
    const uintptr_t argv_address = (uintptr_t)frame->rsi;
    const size_t vector_size = (size_t)argc * sizeof(uintptr_t);
    if (!paging_user_range_valid(process->address_space, argv_address,
                                 vector_size, 0))
    {
        frame->rax = (uint64_t)(int64_t)-14;
        return 0;
    }

    char argument_storage[PROCESS_USER_ARG_BYTES_MAX];
    const char* argv[PROCESS_USER_MAX_ARGS];
    size_t argument_bytes = 0U;
    const uintptr_t* user_argv = (const uintptr_t*)argv_address;
    for (uint32_t i = 0; i < argc; i++)
    {
        if (argument_bytes >= sizeof(argument_storage))
        {
            frame->rax = (uint64_t)(int64_t)-22;
            return 0;
        }

        size_t argument_length = 0U;
        copy_result = syscall_copy_user_string(
            argument_storage + argument_bytes,
            sizeof(argument_storage) - argument_bytes,
            user_argv[i], process, &argument_length);
        if (copy_result != 0)
        {
            frame->rax = (uint64_t)(int64_t)copy_result;
            return 0;
        }

        argv[i] = argument_storage + argument_bytes;
        argument_bytes += argument_length + 1U;
    }

    int child_pid = process_spawn_user_elf_file(
        path, process->pid, argc, argv);
    frame->rax = (uint64_t)(int64_t)child_pid;
    return 0;
}

/*
 * waitpid blocks the current user process until its direct child exits. The
 * child-exit path wakes a sleeping parent after publishing a waitable zombie.
 */
static int syscall_waitpid(struct exception_frame* frame,
                           const process_t* process)
{
    uint64_t child_value = frame->rdi;
    uintptr_t status_address = (uintptr_t)frame->rsi;
    if (child_value == 0U || child_value >= PROCESS_MAX ||
        child_value == process->pid)
    {
        frame->rax = (uint64_t)(int64_t)-22;
        return 0;
    }

    if (status_address != 0U &&
        !paging_user_range_valid(process->address_space, status_address,
                                 sizeof(int32_t), 1))
    {
        frame->rax = (uint64_t)(int64_t)-14;
        return 0;
    }

    for (;;)
    {
        int32_t exit_status = 0;
        int result = process_wait_child(process->pid, (uint32_t)child_value,
                                        &exit_status);
        if (result > 0)
        {
            if (status_address != 0U)
                *(int32_t*)status_address = exit_status;
            frame->rax = (uint64_t)result;
            return 0;
        }
        if (result < 0)
        {
            frame->rax = (uint64_t)(int64_t)-10;
            return 0;
        }

        /* STI followed by HLT avoids losing a wakeup between check and sleep. */
        __asm__ volatile ("sti; hlt" : : : "memory");
    }
}

static int syscall_dispatch(struct exception_frame* frame)
{
    if (!frame || (frame->cs & 3U) != 3U)
    {
        if (frame)
            frame->rax = (uint64_t)(int64_t)-38;
        return 0;
    }

    process_t process;
    if (!process_snapshot(process_current_pid(), &process) ||
        !process.is_user ||
        process.address_space != paging_get_current_address_space())
    {
        frame->rax = (uint64_t)(int64_t)-38;
        return 0;
    }

    switch (frame->rax)
    {
        case USER_SYSCALL_EXIT:
            process_terminate_current();
            return 1;
        case USER_SYSCALL_WRITE:
            return syscall_write(frame, &process);
        case USER_SYSCALL_CONSOLE_WRITE:
            return syscall_console_write(frame, &process);
        case USER_SYSCALL_OPEN:
            return syscall_open(frame, process.pid, &process);
        case USER_SYSCALL_READ:
            return syscall_read(frame, process.pid, &process);
        case USER_SYSCALL_CLOSE:
            return syscall_close(frame, process.pid);
        case USER_SYSCALL_SPAWN:
            return syscall_spawn(frame, &process);
        case USER_SYSCALL_WAITPID:
            return syscall_waitpid(frame, &process);
        default:
            frame->rax = (uint64_t)(int64_t)-38;
            return 0;
    }
}

int user_mode_syscall_dispatch(struct exception_frame* frame)
{
#ifdef VANTAOS_SELF_TEST
    if (user_mode_selftest_is_active())
    {
        user_mode_selftest_syscall_dispatch(frame);
        return 0;
    }
#endif
    return syscall_dispatch(frame);
}
