/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_PARTITION_DEVICE_H
#define VANTA_PARTITION_DEVICE_H
#include "block_device.h"
void partition_device_initialize(void);\n/* Scan a newly registered disk once, including disks added after boot. */\nint partition_device_scan_disk(block_device_t *disk);
int partition_device_has_table(const block_device_t *disk);
#endif
