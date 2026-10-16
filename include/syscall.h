#ifndef SYSCALL_H
#define SYSCALL_H

#include "interrupts.h"

/* Returns nonzero when the interrupted process must be scheduled away. */
int user_mode_syscall_dispatch(struct exception_frame* frame);

#endif
