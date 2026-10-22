/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "drivers_init.h"

#include "net.h"
#include "nvx_device.h"
#include "gpu_core.h"
#include "storage_init.h"
#include "usb_init.h"

/*
 * Cross-bus orchestration lives here. Each subsystem owns the concrete
 * driver/protocol registration sequence behind its own initializer.
 * Preconditions: PCI has been enumerated and the kernel heap is available.
 */
void drivers_initialize(void)
{
    net_stack_initialize();
    usb_host_controller_initialize();
    storage_initialize();
    gpu_initialize();

    /* Read-only inventory; never makes boot depend on NVIDIA hardware. */
    (void)nvx_pci_discover();
}
