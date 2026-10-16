#include "syscall.h"

#include "paging.h"
#include "process.h"
#include "serial.h"
#include "selftest/user_mode_selftest.h"

#include <stdint.h>

#define USER_SYSCALL_EXIT 0U
#define USER_SYSCALL_WRITE 1U
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

    switch ((uint32_t)frame->rax)
    {
        case USER_SYSCALL_EXIT:
            process_terminate_current();
            return 1;
        case USER_SYSCALL_WRITE:
            return syscall_write(frame, &process);
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
