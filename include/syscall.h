#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

#define SYSCALL_WRITE 1
#define SYSCALL_EXIT  60
#define SYSCALL_GETPID 39

void syscall_initialize(void);
uint64_t syscall_dispatch(uint64_t number,
                          uint64_t arg1,
                          uint64_t arg2,
                          uint64_t arg3);

#endif
