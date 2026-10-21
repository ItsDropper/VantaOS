/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef RECOVERY_ENVIRONMENT_H
#define RECOVERY_ENVIRONMENT_H

#include "multiboot.h"

/* Enter only when the Multiboot command line explicitly requests recovery. */
int recovery_environment_requested(const multiboot_info_t* mbd);

/* Runs the interactive environment and returns only when normal boot is chosen. */
void recovery_environment_run(multiboot_info_t* mbd);

#endif
