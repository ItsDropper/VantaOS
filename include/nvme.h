/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef NVME_H
#define NVME_H

/* Probe PCI NVMe controllers and register supported namespaces as block devices. */
void nvme_initialize(void);

#endif
