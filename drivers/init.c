/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "drivers_init.h"
#include "gpu_core.h"

#include "net.h"
#include "nvx_device.h"
#include "gpu_vendor.h"
#include "intel_gpu.h"
#include "amd_gpu.h"
#include "serial.h"
#include <stddef.h>
#include "gpu_core.h"
#include "storage_init.h"
#include "usb_init.h"

static void gpu_write_hex16(uint16_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    char buffer[4];
    for (size_t i = 0U; i < sizeof(buffer); ++i) {
        unsigned shift = (unsigned)((sizeof(buffer) - 1U - i) * 4U);
        buffer[i] = digits[(value >> shift) & 0xFU];
    }
    serial_write_bytes(buffer, sizeof(buffer));
}

void gpu_initialize(void)
{
    gpu_device_t devices[64];
    size_t count = 0U;

    if (!gpu_discover_pci(devices, sizeof(devices) / sizeof(devices[0]),
                          &count)) {
        serial_write("GPU: PCI display inventory unavailable or exceeds limit\n");
        return;
    }

    for (size_t i = 0U; i < count; ++i) {
        const gpu_device_identity_t *id = &devices[i].identity;
        serial_write("GPU: discovered ");
        serial_write(gpu_vendor_name(devices[i].vendor));
        serial_write(" device 0x");
        gpu_write_hex16(id->device_id);
        serial_write(" architecture=");
        serial_write(gpu_architecture_name(devices[i].architecture));
        serial_write("\n");

        /* Route known vendors through the common fail-closed lifecycle. */
        const gpu_driver_descriptor_t *driver = NULL;
        if (devices[i].vendor == GPU_VENDOR_INTEL)
            driver = intel_gpu_driver();
        else if (devices[i].vendor == GPU_VENDOR_AMD)
            driver = amd_gpu_driver();
        if (driver != NULL) {
            if (!gpu_driver_start(&devices[i], driver))
                serial_write("GPU: backend refused initialization; firmware/engine path unavailable\n");
            else
                serial_write("GPU: backend running (verified capabilities only)\n");
        }
    }
}

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
