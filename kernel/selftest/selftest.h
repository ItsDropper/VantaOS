/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef KERNEL_SELFTEST_H
#define KERNEL_SELFTEST_H
#include "vanta_boot_info.h"
/* Regression entry points; implementations exist only in self-test builds. */
void kernel_boot_info_selftest(const vanta_boot_info_t* normalized);
void kernel_platform_selftest(void);
void storage_framework_selftest(void);
void network_framework_selftest(void);
#endif
