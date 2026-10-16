#ifndef USER_MODE_SELFTEST_H
#define USER_MODE_SELFTEST_H

#include "interrupts.h"

/* Returns nonzero only when the ring-3 syscall and supervisor-page tests pass. */
int user_mode_selftest_run(void);

/* Called by the IDT's DPL-3 test gate and by the exception dispatcher. */
void user_mode_selftest_syscall_dispatch(struct exception_frame* frame);
int user_mode_selftest_handle_exception(unsigned int number,
                                        struct exception_frame* frame);

#endif
