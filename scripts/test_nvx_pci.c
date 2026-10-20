/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "nvx_device.h"
#include "serial.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const struct pci_device devices[] = {
    {
        .bus = 1, .slot = 0, .function = 0,
        .vendor_id = NVX_PCI_VENDOR_NVIDIA, .device_id = 0x2503,
        .class_code = 0x03, .subclass = 0x00, .prog_if = 0, .revision = 1
    },
    {
        .bus = 1, .slot = 0, .function = 1,
        .vendor_id = NVX_PCI_VENDOR_NVIDIA, .device_id = 0x228B,
        .class_code = 0x04, .subclass = 0x03, .prog_if = 0, .revision = 1
    },
    {
        .bus = 2, .slot = 0, .function = 0,
        .vendor_id = 0x8086, .device_id = 0x1234,
        .class_code = 0x03, .subclass = 0x00, .prog_if = 0, .revision = 1
    },
    {
        .bus = 3, .slot = 0, .function = 0,
        .vendor_id = NVX_PCI_VENDOR_NVIDIA, .device_id = 0x9999,
        .class_code = 0x03, .subclass = 0x00, .prog_if = 0, .revision = 1
    }
};

static char log_buffer[4096];
static size_t log_length;
static unsigned bar_queries;

void serial_write(const char *text)
{
    size_t length = strlen(text);
    assert(log_length + length < sizeof(log_buffer));
    memcpy(log_buffer + log_length, text, length);
    log_length += length;
    log_buffer[log_length] = '\0';
}

void serial_write_bytes(const char *data, size_t length)
{
    assert(log_length + length < sizeof(log_buffer));
    memcpy(log_buffer + log_length, data, length);
    log_length += length;
    log_buffer[log_length] = '\0';
}

int pci_get_device_count(void)
{
    return (int)(sizeof(devices) / sizeof(devices[0]));
}

const struct pci_device *pci_get_device(int index)
{
    if (index < 0 || index >= pci_get_device_count())
        return 0;
    return &devices[index];
}

uint8_t pci_read_config8(const struct pci_device *device, uint8_t offset)
{
    (void)device;
    return offset == 0x0EU ? 0U : 0U;
}

uint16_t pci_read_config16(const struct pci_device *device, uint8_t offset)
{
    (void)device;
    if (offset == 0x04U)
        return NVX_PCI_COMMAND_MEMORY_SPACE;
    if (offset == 0x2CU)
        return 0x1043U;
    if (offset == 0x2EU)
        return 0x88A1U;
    return 0xFFFFU;
}

int pci_get_bar(const struct pci_device *device, uint8_t index,
                uint64_t *address, int *is_io, int *is_64bit)
{
    assert(device != 0);
    ++bar_queries;
    if (index != 0 || device->vendor_id != NVX_PCI_VENDOR_NVIDIA ||
        device->class_code != 0x03U)
        return 0;

    *address = 0x00000012ABC00000ULL;
    *is_io = 0;
    *is_64bit = 1;
    return 1;
}

int main(void)
{
    assert(nvx_pci_discover() == 3);
    assert(bar_queries == 2); /* Both display candidates; never the audio function. */

    assert(strstr(log_buffer, "vendor=0x10DE device=0x2503") != 0);
    assert(strstr(log_buffer, "role=GPU-candidate family=AMPERE profile=GEFORCE-RTX-3060 identification=PCI-ID-TABLE-ONLY pci-command=0x0002 memory-space=enabled bus-master=disabled") != 0);
    assert(strstr(log_buffer, "subsystem=0x1043:0x88A1") != 0);
    assert(strstr(log_buffer, "BAR0 address=0x00000012ABC00000 type=memory width=64") != 0);
    assert(strstr(log_buffer, "device=0x228B") != 0);
    assert(strstr(log_buffer, "role=non-GPU-NVIDIA-function") != 0);
    assert(strstr(log_buffer, "device=0x9999") != 0);
    assert(strstr(log_buffer, "family=UNKNOWN profile=UNKNOWN identification=UNRECOGNIZED-PCI-ID") != 0);
    assert(strstr(log_buffer, "state=DETECTED; initialization=NOT_ATTEMPTED; acceleration=UNAVAILABLE") != 0);
    assert(strstr(log_buffer, "vendor=0x8086") == 0);
    assert(strstr(log_buffer, "no NVIDIA PCI functions detected") == 0);

    return 0;
}
