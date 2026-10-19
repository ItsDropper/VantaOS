/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include "interrupts.h"

/* Returns nonzero when the interrupted process must be scheduled away. */
int user_mode_syscall_dispatch(struct exception_frame* frame);

#endif
