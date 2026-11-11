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
#include "pci.h"
#include "acpi.h"
#include "smbios.h"
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

static void hardware_write_hex8(uint8_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    char buffer[2] = {
        digits[(value >> 4U) & 0x0FU],
        digits[value & 0x0FU]
    };
    serial_write_bytes(buffer, sizeof(buffer));
}

static void hardware_write_hex16(uint16_t value)
{
    hardware_write_hex8((uint8_t)(value >> 8U));
    hardware_write_hex8((uint8_t)value);
}

/*
 * Report enumerated PCI functions before subsystem-specific startup.
 * This is deliberately an inventory, not a claim that a matching driver
 * exists or that a device has been initialized. It is useful on physical
 * machines where the exact chipset/NIC/audio/touchpad controller is unknown.
 */
static void hardware_inventory_initialize(void)
{
    int count = pci_get_device_count();
    if (count < 0) {
        serial_write("VANTAOS_HW_PCI_INVENTORY_UNAVAILABLE\n");
        return;
    }

    serial_write("VANTAOS_HW_PCI_DEVICE_COUNT=");
    /* Decimal count, bounded by the PCI subsystem's fixed device table. */
    {
        char digits[10];
        size_t length = 0U;
        unsigned int value = (unsigned int)count;
        do {
            digits[length++] = (char)('0' + (value % 10U));
            value /= 10U;
        } while (value != 0U && length < sizeof(digits));
        while (length != 0U) {
            char digit = digits[--length];
            serial_write_bytes(&digit, 1U);
        }
    }
    serial_write("\n");

    for (int index = 0; index < count; ++index) {
        const struct pci_device *device = pci_get_device(index);
        if (device == NULL)
            continue;

        serial_write("VANTAOS_HW_PCI bdf=");
        hardware_write_hex8(device->bus);
        serial_write(":");
        hardware_write_hex8(device->slot);
        serial_write(".");
        hardware_write_hex8(device->function);
        serial_write(" id=");
        hardware_write_hex16(device->vendor_id);
        serial_write(":");
        hardware_write_hex16(device->device_id);
        serial_write(" rev=");
        hardware_write_hex8(device->revision);
        serial_write(" header=");
        hardware_write_hex8(device->header_type);
        if ((device->header_type & 0x7FU) == 0U)
        {
            serial_write(" subsystem=");
            hardware_write_hex16(pci_read_config16(device, 0x2CU));
            serial_write(":");
            hardware_write_hex16(pci_read_config16(device, 0x2EU));
        }
        serial_write(" class=");
        hardware_write_hex8(device->class_code);
        serial_write(":");
        hardware_write_hex8(device->subclass);
        serial_write(":");
        hardware_write_hex8(device->prog_if);
        serial_write(" name=");
        serial_write(pci_get_class_name(device->class_code,
                                        device->subclass));
        serial_write(" vendor=");
        serial_write(pci_get_vendor_name(device->vendor_id));
        serial_write(" device_name=");
        serial_write(pci_get_device_name(device->vendor_id,
                                         device->device_id));
        serial_write("\n");
    }
}

/*
 * Cross-bus orchestration lives here. Each subsystem owns the concrete
 * driver/protocol registration sequence behind its own initializer.
 * Preconditions: PCI has been enumerated and the kernel heap is available.
 */
void drivers_initialize(void)
{
    acpi_log_table_inventory();
    smbios_inventory_initialize();
    hardware_inventory_initialize();
    net_stack_initialize();
    usb_host_controller_initialize();
    storage_initialize();
    gpu_initialize();

    /* Read-only inventory; never makes boot depend on NVIDIA hardware. */
    (void)nvx_pci_discover();
}
