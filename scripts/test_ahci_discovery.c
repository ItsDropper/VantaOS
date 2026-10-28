/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "ahci.h"
#include "mmio.h"
#include "pci.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static struct pci_device fake_device = {
    .bus = 0U, .slot = 5U, .function = 0U,
    .vendor_id = 0x8086U, .device_id = 0x2922U,
    .class_code = 0x01U, .subclass = 0x06U, .prog_if = 0x01U,
    .revision = 2U, .segment_group = 0U
};
static uint16_t command_register;
static uint8_t fake_registers[0x1100] __attribute__((aligned(4)));
static uint32_t find_calls;
static uint32_t size_calls;
static uint32_t bar_calls;
static uint32_t enable_calls;
static uint32_t write_calls;
static uint32_t map_calls;
static uint32_t unmap_calls;
static int discovered_marker;
static int rejected_marker;

int pci_find_class(uint8_t class_code, uint8_t subclass, int prog_if,
                   int start_index)
{
    assert(class_code == 0x01U);
    assert(subclass == 0x06U);
    assert(prog_if == 0x01);
    ++find_calls;
    return start_index == 0 ? 0 : -1;
}

const struct pci_device* pci_get_device(int index)
{
    return index == 0 ? &fake_device : 0;
}

uint16_t pci_read_config16(const struct pci_device* device, uint8_t offset)
{
    assert(device == &fake_device);
    assert(offset == 0x04U);
    return command_register;
}

int pci_get_bar_size(const struct pci_device* device, uint8_t index,
                     uint64_t* size)
{
    assert(device == &fake_device);
    assert(index == 5U);
    assert((command_register & 0x0007U) == 0U);
    ++size_calls;
    *size = 0x2000U;
    return 1;
}

int pci_get_bar(const struct pci_device* device, uint8_t index,
                uint64_t* address, int* is_io, int* is_64bit)
{
    assert(device == &fake_device);
    assert(index == 5U);
    ++bar_calls;
    *address = 0xFEBF0000ULL;
    *is_io = 0;
    *is_64bit = 0;
    return 1;
}

int pci_enable_memory_space(const struct pci_device* device)
{
    assert(device == &fake_device);
    assert((command_register & 0x0007U) == 0U);
    ++enable_calls;
    command_register |= 0x0002U;
    return 1;
}

int pci_write_config16(const struct pci_device* device, uint8_t offset,
                       uint16_t value)
{
    assert(device == &fake_device);
    assert(offset == 0x04U);
    ++write_calls;
    command_register = value;
    return 1;
}

void* mmio_map_region(uint64_t physical_address, size_t length)
{
    assert(physical_address == 0xFEBF0000ULL);
    assert(length == 0x1100U);
    ++map_calls;
    return fake_registers;
}

int mmio_unmap_region(void* virtual_address, size_t length)
{
    assert(virtual_address == fake_registers);
    assert(length == 0x1100U);
    ++unmap_calls;
    return 1;
}

void serial_write(const char* text)
{
    if (strcmp(text, "VANTAOS_AHCI_IDLE_CONTROLLER_DISCOVERED\n") == 0)
        discovered_marker = 1;
    if (strcmp(text, "VANTAOS_AHCI_CONTROLLER_PROBE_REJECTED\n") == 0)
        rejected_marker = 1;
}

int main(void)
{
    const uint32_t cap = 3U;       /* Four ports described by CAP.NP. */
    const uint32_t pi = 5U;        /* Ports zero and two are implemented. */
    const uint32_t version = 0x00010300U;
    memcpy(fake_registers + 0x00U, &cap, sizeof(cap));
    memcpy(fake_registers + 0x0CU, &pi, sizeof(pi));
    memcpy(fake_registers + 0x10U, &version, sizeof(version));

#ifdef AHCI_TEST_UNSAFE
    command_register = 0x0004U; /* Bus mastering is already enabled. */
#endif
    ahci_initialize();

    assert(find_calls == 2U);
#ifdef AHCI_TEST_UNSAFE
    assert(size_calls == 0U);
    assert(bar_calls == 0U);
    assert(enable_calls == 0U);
    assert(write_calls == 0U);
    assert(map_calls == 0U);
    assert(unmap_calls == 0U);
    assert(command_register == 0x0004U);
    assert(rejected_marker);
    puts("VANTAOS_AHCI_DISCOVERY_UNSAFE_GUARD_OK");
#else
    assert(size_calls == 1U);
    assert(bar_calls == 1U);
    assert(enable_calls == 1U);
    assert(write_calls == 1U);
    assert(map_calls == 1U);
    assert(unmap_calls == 1U);
    assert(command_register == 0U);
    assert(discovered_marker);
    puts("VANTAOS_AHCI_DISCOVERY_OK");
#endif
    return 0;
}
