#ifndef USER_MODE_TEST_H
#define USER_MODE_TEST_H

#include "interrupts.h"

int user_mode_selftest_is_active(void);
void user_mode_selftest_syscall_dispatch(struct exception_frame* frame);
int user_mode_selftest_handle_exception(unsigned int number,
                                        struct exception_frame* frame);

#endif
