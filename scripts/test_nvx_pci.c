/* 
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_device.h"
#include "serial.h"
#include "mmio.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const struct pci_device devices[] = {
    {.bus=1,.slot=0,.function=0,.vendor_id=NVX_PCI_VENDOR_NVIDIA,.device_id=0x2503,.class_code=0x03,.subclass=0,.prog_if=0,.revision=1},
    {.bus=1,.slot=0,.function=1,.vendor_id=NVX_PCI_VENDOR_NVIDIA,.device_id=0x228B,.class_code=0x04,.subclass=0x03,.prog_if=0,.revision=1},
    {.bus=2,.slot=0,.function=0,.vendor_id=0x8086,.device_id=0x1234,.class_code=0x03,.subclass=0,.prog_if=0,.revision=1},
    {.bus=3,.slot=0,.function=0,.vendor_id=NVX_PCI_VENDOR_NVIDIA,.device_id=0x9999,.class_code=0x03,.subclass=0,.prog_if=0,.revision=1},
    {.bus=4,.slot=0,.function=0,.vendor_id=NVX_PCI_VENDOR_NVIDIA,.device_id=0x174D,.class_code=0x03,.subclass=0,.prog_if=0,.revision=1},
    {.bus=5,.slot=0,.function=0,.vendor_id=NVX_PCI_VENDOR_NVIDIA,.device_id=0x1D12,.class_code=0x03,.subclass=0,.prog_if=0,.revision=1}
};

static volatile uint32_t fake_registers[2U];
static char log_buffer[8192];
static size_t log_length;
static unsigned bar_queries, map_calls, unmap_calls;
static size_t mapped_lengths[8];
static uint16_t mock_command = NVX_PCI_COMMAND_MEMORY_SPACE;
static int map_failure, bad_ampere_identity, unmap_failure;

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
        return NULL;
    return &devices[index];
}

uint8_t pci_read_config8(const struct pci_device *device, uint8_t offset)
{
    (void)device;
    (void)offset;
    return 0U;
}

uint16_t pci_read_config16(const struct pci_device *device, uint8_t offset)
{
    (void)device;
    if (offset == 0x04U)
        return mock_command;
    if (offset == 0x2CU)
        return 0x1043U;
    if (offset == 0x2EU)
        return 0x88A1U;
    return 0xFFFFU;
}

int pci_get_bar(const struct pci_device *device, uint8_t index,
                uint64_t *address, int *is_io, int *is_64bit)
{
    assert(device != NULL);
    ++bar_queries;
    if (index != 0U || device->vendor_id != NVX_PCI_VENDOR_NVIDIA ||
        device->class_code != 0x03U)
        return 0;
    *address = 0x00000012ABC00000ULL;
    *is_io = 0;
    *is_64bit = 1;
    if (device->device_id == 0x2503U) {
        fake_registers[0] = bad_ampere_identity ? 0x16000000U : 0x17000000U;
        fake_registers[1] = 1U;
    } else if (device->device_id == 0x174DU) {
        fake_registers[0] = 0x11000000U;
        fake_registers[1] = 1U;
    } else if (device->device_id == 0x1D12U) {
        fake_registers[0] = 0x13000000U;
        fake_registers[1] = 1U;
    }
    return 1;
}

void *mmio_map_region(uint64_t physical_address, size_t length)
{
    assert(physical_address == 0x00000012ABC00000ULL);
    assert(length == 8U);
    assert(map_calls < sizeof(mapped_lengths) / sizeof(mapped_lengths[0]));
    mapped_lengths[map_calls++] = length;
    return map_failure ? NULL : (void *)(uintptr_t)fake_registers;
}

int mmio_unmap_region(void *virtual_address, size_t length)
{
    assert(virtual_address == (void *)(uintptr_t)fake_registers);
    assert(length == 8U);
    ++unmap_calls;
    return !unmap_failure;
}

static void clear_log(void)
{
    log_length = 0U;
    log_buffer[0] = '\0';
}

int main(void)
{
    assert(nvx_pci_discover() == 5);
    assert(bar_queries == 7U); /* Four inventory reads plus three profile probes. */
    assert(map_calls == 3U && unmap_calls == 3U);
    assert(mapped_lengths[0] == 8U && mapped_lengths[1] == 8U &&
           mapped_lengths[2] == 8U);
    assert(mapped_lengths[1] == 8U && mapped_lengths[2] == 8U);

    assert(strstr(log_buffer, "vendor=0x10DE device=0x2503") != NULL);
    assert(strstr(log_buffer, "role=GPU-candidate family=AMPERE profile=GEFORCE-RTX-3060 identification=PCI-ID-TABLE-ONLY pci-command=0x0002 memory-space=enabled bus-master=disabled") != NULL);
    assert(strstr(log_buffer, "subsystem=0x1043:0x88A1") != NULL);
    assert(strstr(log_buffer, "BAR0 address=0x00000012ABC00000 type=memory width=64") != NULL);
    assert(strstr(log_buffer, "PMC identity: verified family=AMPERE profile=GEFORCE-RTX-3060 architecture=0x17") != NULL);
    assert(strstr(log_buffer, "device=0x228B") != NULL);
    assert(strstr(log_buffer, "role=non-GPU-NVIDIA-function") != NULL);
    assert(strstr(log_buffer, "device=0x9999") != NULL);
    assert(strstr(log_buffer, "device=0x174D") != NULL);
    assert(strstr(log_buffer, "device=0x1D12") != NULL);
    assert(strstr(log_buffer, "PMC identity: verified family=MAXWELL profile=GEFORCE-MX130 architecture=0x11") != NULL);
    assert(strstr(log_buffer, "PMC identity: verified family=PASCAL profile=GEFORCE-MX150 architecture=0x13") != NULL);
    assert(strstr(log_buffer, "family=UNKNOWN profile=UNKNOWN identification=UNRECOGNIZED-PCI-ID") != NULL);
    assert(strstr(log_buffer, "state=DETECTED; initialization=NOT_ATTEMPTED; acceleration=UNAVAILABLE") != NULL);
    assert(strstr(log_buffer, "vendor=0x8086") == NULL);
    assert(strstr(log_buffer, "no NVIDIA PCI functions detected") == NULL);

    /* Memory decode disabled: no BAR mapping or register access is attempted. */
    clear_log();
    mock_command = 0U;
    unsigned maps_before = map_calls;
    assert(nvx_pci_discover() == 5);
    assert(map_calls == maps_before);
    assert(strstr(log_buffer, "PMC identity: skipped (memory decode disabled)") != NULL);

    /* Mapping failure is non-fatal and leaves the OS boot path unaffected. */
    clear_log();
    mock_command = NVX_PCI_COMMAND_MEMORY_SPACE;
    map_failure = 1;
    maps_before = map_calls;
    assert(nvx_pci_discover() == 5);
    assert(map_calls == maps_before + 3U);
    assert(strstr(log_buffer, "PMC identity: skipped (MMIO mapping unavailable)") != NULL);
    map_failure = 0;

    /* A mismatched live architecture must never be reported as verified. */
    clear_log();
    bad_ampere_identity = 1;
    maps_before = unmap_calls;
    assert(nvx_pci_discover() == 5);
    assert(unmap_calls == maps_before + 3U);
    assert(strstr(log_buffer, "PMC identity: failed (PCI/PMC identity did not agree)") != NULL);
    assert(strstr(log_buffer, "PMC identity: verified family=AMPERE") == NULL);
    bad_ampere_identity = 0;

    /* Failed unmap also suppresses success; this remains a diagnostic only. */
    clear_log();
    unmap_failure = 1;
    assert(nvx_pci_discover() == 5);
    assert(strstr(log_buffer, "PMC identity: failed (MMIO unmap failed)") != NULL);
    assert(strstr(log_buffer, "PMC identity: verified family=AMPERE") == NULL);
    return 0;
}
