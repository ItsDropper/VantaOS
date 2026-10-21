/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTAOS_POWER_H
#define VANTAOS_POWER_H

/* These routines do not return. Hardware support varies by platform. */
_Noreturn void power_restart(void);
_Noreturn void power_shutdown(void);

#endif
