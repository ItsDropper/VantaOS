/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef USER_MODE_SELFTEST_H
#define USER_MODE_SELFTEST_H

#include "interrupts.h"

/* Returns nonzero only when the ring-3 syscall and supervisor-page tests pass. */
int user_mode_selftest_run(void);
int user_mode_process_smoke_test_start(void);

/* Called by the IDT's DPL-3 test gate and by the exception dispatcher. */
int user_mode_selftest_is_active(void);
void user_mode_selftest_syscall_dispatch(struct exception_frame* frame);
int user_mode_selftest_handle_exception(unsigned int number,
                                        struct exception_frame* frame);

#endif
