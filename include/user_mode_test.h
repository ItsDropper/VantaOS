/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef USER_MODE_TEST_H
#define USER_MODE_TEST_H

#include "interrupts.h"

int user_mode_selftest_is_active(void);
int user_mode_process_smoke_test_start(void);
void user_mode_selftest_syscall_dispatch(struct exception_frame* frame);
int user_mode_selftest_handle_exception(unsigned int number,
                                        struct exception_frame* frame);

#endif
