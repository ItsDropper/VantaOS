/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef DRIVERS_INIT_H
#define DRIVERS_INIT_H

/* Requires PCI enumeration and the kernel heap to be initialized. */
void drivers_initialize(void);

#endif
