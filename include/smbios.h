/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef SMBIOS_H
#define SMBIOS_H

/* Read-only, bounded firmware inventory. Never emits firmware serial numbers. */
void smbios_inventory_initialize(void);

#endif
