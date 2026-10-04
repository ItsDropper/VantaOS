#include "syscall.h"
#include "process.h"

static int initialized;

void syscall_initialize(void)
{
    initialized = 1;
}

uint64_t syscall_dispatch(uint64_t number,
                          uint64_t arg1,
                          uint64_t arg2,
                          uint64_t arg3)
{
    (void)arg2;
    (void)arg3;

    if (!initialized)
        return (uint64_t)-1;

    switch (number)
    {
        case SYSCALL_GETPID:
            return process_current_pid();

        case SYSCALL_EXIT:
            process_terminate_current();
            return 0;

        case SYSCALL_WRITE:
            /*
             * User-pointer validation is deliberately not performed yet.
             * The ring-3 memory manager must be installed before exposing
             * arbitrary user buffers to the terminal.
             */
            if (arg1 == 1 && arg2 != 0)
            {
                const char* text = (const char*)(uintptr_t)arg2;
                uint64_t length = arg3;

                for (uint64_t i = 0; i < length; i++)
                    if (text[i])
                        __asm__ volatile ("outb %%al, $0xE9" : : "a"(text[i]));

                return length;
            }
            return (uint64_t)-1;

        default:
            return (uint64_t)-1;
    }
}
