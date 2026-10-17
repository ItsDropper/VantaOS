#include "syscall.h"

#include "paging.h"
#include "process.h"
#include "scheduler.h"
#include "serial.h"
#include "terminal.h"
#include "terminal_process.h"
#include "keyboard.h"
#include "vfs.h"
#include "user_mode_test.h"
#include "gui_server.h"
#include "desktop_theme.h"

#include <stdint.h>

#define USER_SYSCALL_EXIT 0U
#define USER_SYSCALL_WRITE 1U
#define USER_SYSCALL_CONSOLE_WRITE 2U
#define USER_SYSCALL_OPEN 3U
#define USER_SYSCALL_READ 4U
#define USER_SYSCALL_CLOSE 5U
#define USER_SYSCALL_SPAWN 6U
#define USER_SYSCALL_WAITPID 7U
#define USER_SYSCALL_READ_CHAR 8U
#define USER_SYSCALL_YIELD 9U
#define USER_SYSCALL_LISTDIR 10U
#define USER_SYSCALL_TERMINAL_BEGIN_INPUT 11U
#define USER_SYSCALL_TERMINAL_BACKSPACE 12U
#define USER_SYSCALL_TERMINAL_CLEAR 13U
#define USER_SYSCALL_FILE_WRITE 14U
#define USER_SYSCALL_CREATE_FILE 15U
#define USER_SYSCALL_GUI_CREATE_WINDOW 16U
#define USER_SYSCALL_GUI_CLOSE_WINDOW 17U
#define USER_SYSCALL_GUI_DRAW 18U
#define USER_SYSCALL_GUI_POLL_EVENT 19U
#define USER_SYSCALL_SETTINGS_APPLY 20U
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
    terminal_process_mark_redraw();
    frame->rax = length;
    return 0;
}

static int syscall_open(struct exception_frame* frame, uint32_t pid,
                        const process_t* process)
{
    uintptr_t address = (uintptr_t)frame->rdi;
    uint64_t flags = frame->rsi;
    if ((flags & ~(VFS_OPEN_READ | VFS_OPEN_WRITE | VFS_OPEN_TRUNCATE)) != 0U ||
        (flags & (VFS_OPEN_READ | VFS_OPEN_WRITE)) == 0U ||
        ((flags & VFS_OPEN_TRUNCATE) && !(flags & VFS_OPEN_WRITE)))
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

    int handle = vfs_open(path, (uint32_t)flags);
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


/* Return one buffered character without exposing kernel keyboard state. */
static int syscall_read_char(struct exception_frame* frame)
{
    frame->rax = (uint64_t)(unsigned char)gui_server_read_terminal_char();
    return 0;
}

static int syscall_yield(struct exception_frame* frame)
{
    /* Wait for the next interrupt; timer preemption remains responsible for
       selecting another runnable process. */
    __asm__ volatile ("sti; hlt; cli" : : : "memory");
    frame->rax = 0U;
    return 0;
}

static int syscall_listdir(struct exception_frame* frame,
                           const process_t* process)
{
    uintptr_t buffer_address = (uintptr_t)frame->rsi;
    uint64_t capacity_value = frame->rdx;
    if (capacity_value == 0U || capacity_value > USER_SYSCALL_IO_MAX)
    {
        frame->rax = (uint64_t)(int64_t)-22;
        return 0;
    }

    size_t capacity = (size_t)capacity_value;
    if (!paging_user_range_valid(process->address_space, buffer_address,
                                 capacity, 1))
    {
        frame->rax = (uint64_t)(int64_t)-14;
        return 0;
    }

    char path[FS_PATH_MAX];
    size_t path_length = 0U;
    int copied = syscall_copy_user_string(
        path, sizeof(path), (uintptr_t)frame->rdi, process, &path_length);
    if (copied != 0 || path_length == 0U)
    {
        frame->rax = (uint64_t)(int64_t)(copied != 0 ? copied : -22);
        return 0;
    }

    int directory_id = vfs_lookup(path);
    if (directory_id < 0)
    {
        frame->rax = (uint64_t)(int64_t)-2;
        return 0;
    }
    const fs_node_t* directory = vfs_get_node((uint32_t)directory_id);
    if (!directory || directory->type != FS_NODE_DIRECTORY)
    {
        frame->rax = (uint64_t)(int64_t)-22;
        return 0;
    }

    uint32_t ids[FS_MAX_NODES];
    int count = vfs_list((uint32_t)directory_id, ids, FS_MAX_NODES);
    if (count < 0 || count > FS_MAX_NODES)
    {
        frame->rax = (uint64_t)(int64_t)-5;
        return 0;
    }

    char output[USER_SYSCALL_IO_MAX];
    size_t used = 0U;
    for (int i = 0; i < count; i++)
    {
        const fs_node_t* node = vfs_get_node(ids[i]);
        if (!node)
        {
            frame->rax = (uint64_t)(int64_t)-5;
            return 0;
        }

        size_t name_length = 0U;
        while (name_length <= FS_NAME_MAX && node->name[name_length])
            name_length++;
        if (name_length == 0U || name_length > FS_NAME_MAX)
        {
            frame->rax = (uint64_t)(int64_t)-5;
            return 0;
        }

        size_t suffix = node->type == FS_NODE_DIRECTORY ? 1U : 0U;
        if (used + name_length + suffix + 1U >= capacity)
        {
            frame->rax = (uint64_t)(int64_t)-22;
            return 0;
        }
        for (size_t j = 0; j < name_length; j++)
            output[used++] = node->name[j];
        if (suffix)
            output[used++] = '/';
        output[used++] = '\n';
    }

    if (used >= capacity)
    {
        frame->rax = (uint64_t)(int64_t)-22;
        return 0;
    }
    output[used] = '\0';
    for (size_t i = 0; i <= used; i++)
        ((char*)buffer_address)[i] = output[i];

    frame->rax = (uint64_t)used;
    return 0;
}

static int syscall_terminal_control(struct exception_frame* frame,
                                    uint64_t operation)
{
    (void)frame;
    if (operation == USER_SYSCALL_TERMINAL_BEGIN_INPUT)
        terminal_begin_input();
    else if (operation == USER_SYSCALL_TERMINAL_BACKSPACE)
        terminal_backspace();
    else if (operation == USER_SYSCALL_TERMINAL_CLEAR)
        terminal_reset();
    terminal_process_mark_redraw();
    frame->rax = 0U;
    return 0;
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

        /*
         * int 0x80 entered with IF cleared. Temporarily enable interrupts and
         * halt so the BSP timer can run the normal scheduler path. The parent
         * is already SLEEPING under the process-table lock, so the scheduler
         * selects the child; child exit publishes its zombie and wakes us.
         * Avoid reusing the hardware-IPI entry point as a user software trap:
         * that stub's return-frame assumptions are specific to interrupt
         * delivery and are not part of the syscall ABI.
         */
        serial_write("VANTAOS_WAITPID_BLOCK\n");
        __asm__ volatile ("sti; hlt; cli" : : : "memory");
        serial_write("VANTAOS_WAITPID_RESUME\n");
    }
}


static int syscall_file_write(struct exception_frame* frame, uint32_t pid,
                              const process_t* process)
{
    if (frame->rdi < PROCESS_FD_BASE ||
        frame->rdi >= PROCESS_FD_BASE + PROCESS_FD_COUNT)
    {
        frame->rax = (uint64_t)(int64_t)-9;
        return 0;
    }

    uint64_t length = frame->rdx;
    if (length > USER_SYSCALL_IO_MAX)
    {
        frame->rax = (uint64_t)(int64_t)-22;
        return 0;
    }
    if (length == 0U)
    {
        frame->rax = 0U;
        return 0;
    }

    uintptr_t address = (uintptr_t)frame->rsi;
    if (!paging_user_range_valid(process->address_space, address,
                                 (size_t)length, 0))
    {
        frame->rax = (uint64_t)(int64_t)-14;
        return 0;
    }

    int handle = process_fd_get(pid, (int)frame->rdi);
    if (handle < 0)
    {
        frame->rax = (uint64_t)(int64_t)-9;
        return 0;
    }

    char buffer[USER_SYSCALL_IO_MAX];
    for (size_t i = 0; i < (size_t)length; i++)
        buffer[i] = ((const char*)address)[i];

    int result = vfs_write(handle, buffer, (unsigned int)length);
    frame->rax = result < 0 ? (uint64_t)(int64_t)-5 : (uint64_t)result;
    return 0;
}

static int syscall_create_file(struct exception_frame* frame,
                               const process_t* process)
{
    char path[FS_PATH_MAX];
    size_t length = 0U;
    int copied = syscall_copy_user_string(
        path, sizeof(path), (uintptr_t)frame->rdi, process, &length);
    if (copied != 0 || length == 0U)
    {
        frame->rax = (uint64_t)(int64_t)(copied != 0 ? copied : -22);
        return 0;
    }

    int result = vfs_create_file(path);
    if (result < 0)
    {
        /* create-file is idempotent for an existing regular file. */
        int existing = vfs_lookup(path);
        const fs_node_t* node = existing >= 0 ?
            vfs_get_node((uint32_t)existing) : 0;
        if (!node || node->type != FS_NODE_FILE)
        {
            frame->rax = (uint64_t)(int64_t)-5;
            return 0;
        }
    }
    frame->rax = 0U;
    return 0;
}


static int syscall_gui_create_window(struct exception_frame* frame,
                                     const process_t* process)
{
    uintptr_t address = (uintptr_t)frame->rdi;
    if (!paging_user_range_valid(process->address_space, address,
                                 sizeof(vanta_gui_window_request_t), 0))
    {
        frame->rax = (uint64_t)(int64_t)-14;
        return 0;
    }

    vanta_gui_window_request_t request;
    const uint8_t* source = (const uint8_t*)address;
    uint8_t* destination = (uint8_t*)&request;
    for (size_t i = 0; i < sizeof(request); i++)
        destination[i] = source[i];

    int result = gui_server_create_window(process->pid, &request);
    frame->rax = (uint64_t)(int64_t)result;
    return 0;
}

static int syscall_gui_close_window(struct exception_frame* frame,
                                    const process_t* process)
{
    if (frame->rdi == 0U || frame->rdi > UINT32_MAX)
    {
        frame->rax = (uint64_t)(int64_t)-22;
        return 0;
    }
    int result = gui_server_close_window(process->pid, (uint32_t)frame->rdi);
    frame->rax = (uint64_t)(int64_t)result;
    return 0;
}

static int syscall_gui_draw(struct exception_frame* frame,
                            const process_t* process)
{
    if (frame->rdi == 0U || frame->rdi > UINT32_MAX ||
        frame->rdx > VANTA_GUI_MAX_COMMANDS)
    {
        frame->rax = (uint64_t)(int64_t)-22;
        return 0;
    }

    uint32_t count = (uint32_t)frame->rdx;
    uintptr_t address = (uintptr_t)frame->rsi;
    vanta_gui_draw_command_t commands[VANTA_GUI_MAX_COMMANDS];
    if (count != 0U)
    {
        size_t bytes = (size_t)count * sizeof(commands[0]);
        if (!paging_user_range_valid(process->address_space, address,
                                     bytes, 0))
        {
            frame->rax = (uint64_t)(int64_t)-14;
            return 0;
        }
        const uint8_t* source = (const uint8_t*)address;
        uint8_t* destination = (uint8_t*)commands;
        for (size_t i = 0; i < bytes; i++)
            destination[i] = source[i];
    }

    int result = gui_server_submit_draw(process->pid, (uint32_t)frame->rdi,
                                        commands, count);
    frame->rax = (uint64_t)(int64_t)result;
    return 0;
}

static int syscall_gui_poll_event(struct exception_frame* frame,
                                  const process_t* process)
{
    if (frame->rdi == 0U || frame->rdi > UINT32_MAX)
    {
        frame->rax = (uint64_t)(int64_t)-22;
        return 0;
    }
    uintptr_t address = (uintptr_t)frame->rsi;
    if (!paging_user_range_valid(process->address_space, address,
                                 sizeof(vanta_gui_event_t), 1))
    {
        frame->rax = (uint64_t)(int64_t)-14;
        return 0;
    }

    vanta_gui_event_t event;
    int result = gui_server_poll_event(process->pid, (uint32_t)frame->rdi,
                                       &event);
    if (result > 0)
    {
        uint8_t* destination = (uint8_t*)address;
        const uint8_t* source = (const uint8_t*)&event;
        for (size_t i = 0; i < sizeof(event); i++)
            destination[i] = source[i];
    }
    frame->rax = (uint64_t)(int64_t)result;
    return 0;
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
            process_terminate_current_with_status((int32_t)frame->rdi);
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
        case USER_SYSCALL_READ_CHAR:
            return syscall_read_char(frame);
        case USER_SYSCALL_YIELD:
            return syscall_yield(frame);
        case USER_SYSCALL_LISTDIR:
            return syscall_listdir(frame, &process);
        case USER_SYSCALL_TERMINAL_BEGIN_INPUT:
        case USER_SYSCALL_TERMINAL_BACKSPACE:
        case USER_SYSCALL_TERMINAL_CLEAR:
            return syscall_terminal_control(frame, frame->rax);
        case USER_SYSCALL_FILE_WRITE:
            return syscall_file_write(frame, process.pid, &process);
        case USER_SYSCALL_CREATE_FILE:
            return syscall_create_file(frame, &process);
        case USER_SYSCALL_GUI_CREATE_WINDOW:
            return syscall_gui_create_window(frame, &process);
        case USER_SYSCALL_GUI_CLOSE_WINDOW:
            return syscall_gui_close_window(frame, &process);
        case USER_SYSCALL_GUI_DRAW:
            return syscall_gui_draw(frame, &process);
        case USER_SYSCALL_GUI_POLL_EVENT:
            return syscall_gui_poll_event(frame, &process);
        case USER_SYSCALL_SETTINGS_APPLY:
            frame->rax = desktop_theme_reload() ? 0U : (uint64_t)(int64_t)-22;
            return 0;
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
