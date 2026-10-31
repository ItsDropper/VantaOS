/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "pci.h"
#include "ktrace.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define PCI_CONFIG_ADDRESS 0xCF8U
#define PCI_CONFIG_DATA 0xCFCU
#define TEST_DEVICE_MAX 8U

typedef struct
{
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint32_t config[64];
} test_pci_function_t;

static test_pci_function_t test_devices[TEST_DEVICE_MAX];
static uint32_t test_device_count;
static uint32_t selected_address;

void ktrace_emit(uint32_t type, uint32_t cpu_id, uint64_t arg0, uint64_t arg1)
{
    (void)type;
    (void)cpu_id;
    (void)arg0;
    (void)arg1;
}

static test_pci_function_t* find_function(uint8_t bus, uint8_t slot,
                                          uint8_t function)
{
    for (uint32_t i = 0U; i < test_device_count; ++i)
        if (test_devices[i].bus == bus &&
            test_devices[i].slot == slot &&
            test_devices[i].function == function)
            return &test_devices[i];
    return NULL;
}

static test_pci_function_t* add_function(uint8_t bus, uint8_t slot,
                                         uint16_t vendor, uint16_t device,
                                         uint8_t class_code, uint8_t subclass,
                                         uint8_t header_type)
{
    test_pci_function_t* function;
    assert(test_device_count < TEST_DEVICE_MAX);
    function = &test_devices[test_device_count++];
    memset(function, 0, sizeof(*function));
    function->bus = bus;
    function->slot = slot;
    function->config[0] = ((uint32_t)device << 16U) | vendor;
    function->config[2] = ((uint32_t)class_code << 24U) |
                          ((uint32_t)subclass << 16U);
    function->config[3] = (uint32_t)header_type << 16U;
    return function;
}

void pci_test_outl(uint16_t port, uint32_t value)
{
    if (port == PCI_CONFIG_ADDRESS)
    {
        selected_address = value;
        return;
    }
    assert(port == PCI_CONFIG_DATA);
    uint8_t bus = (uint8_t)(selected_address >> 16U);
    uint8_t slot = (uint8_t)((selected_address >> 11U) & 0x1FU);
    uint8_t function = (uint8_t)((selected_address >> 8U) & 0x07U);
    uint8_t offset = (uint8_t)(selected_address & 0xFCU);
    test_pci_function_t* target = find_function(bus, slot, function);
    if (target)
        target->config[offset / 4U] = value;
}

uint32_t pci_test_inl(uint16_t port)
{
    uint8_t bus, slot, function, offset;
    test_pci_function_t* target;
    assert(port == PCI_CONFIG_DATA);
    bus = (uint8_t)(selected_address >> 16U);
    slot = (uint8_t)((selected_address >> 11U) & 0x1FU);
    function = (uint8_t)((selected_address >> 8U) & 0x07U);
    offset = (uint8_t)(selected_address & 0xFCU);
    target = find_function(bus, slot, function);
    return target ? target->config[offset / 4U] : UINT32_MAX;
}

int pci_test_ecam_read32(uint16_t segment, uint8_t bus, uint8_t slot,
                         uint8_t function, uint16_t offset, uint32_t* value)
{
    (void)segment; (void)bus; (void)slot; (void)function; (void)offset;
    (void)value;
    return 0;
}

int pci_test_ecam_write32(uint16_t segment, uint8_t bus, uint8_t slot,
                          uint8_t function, uint16_t offset, uint32_t value)
{
    (void)segment; (void)bus; (void)slot; (void)function;
    (void)offset; (void)value;
    return 0;
}

int main(void)
{
    test_pci_function_t* root;
    test_pci_function_t* downstream;
    test_pci_function_t* malformed;
    test_pci_function_t* endpoint;
    const struct pci_device* root_device = NULL;
    const struct pci_device* downstream_device = NULL;
    const struct pci_device* malformed_device = NULL;
    const struct pci_device* endpoint_device = NULL;

    memset(test_devices, 0, sizeof(test_devices));
    test_device_count = 0U;
    selected_address = 0U;

    root = add_function(0U, 1U, 0x8086U, 0x2448U, 0x06U, 0x04U, 1U);
    root->config[0x18U / 4U] = 0U | (1U << 8U) | (5U << 16U);
    root->config[0x1CU / 4U] = 0x00002111U;
    root->config[0x20U / 4U] = 0x80F08000U;
    root->config[0x24U / 4U] = 0x90019001U;
    root->config[0x28U / 4U] = 0x00000012U;
    root->config[0x2CU / 4U] = 0x00000012U;
    root->config[0x30U / 4U] = 0x00010001U;

    downstream = add_function(1U, 2U, 0x8086U, 0x2448U, 0x06U, 0x04U, 1U);
    downstream->config[0x18U / 4U] = 1U | (2U << 8U) | (4U << 16U);

    /* This bridge advertises an impossible primary-bus relationship. */
    malformed = add_function(1U, 3U, 0x1234U, 0x1111U, 0x06U, 0x04U, 1U);
    malformed->config[0x18U / 4U] = 0U | (2U << 8U) | (4U << 16U);

    endpoint = add_function(3U, 4U, 0x10DEU, 0x2503U, 0x03U, 0x00U, 0U);
    (void)endpoint;

    pci_initialize();
    assert(pci_get_device_count() == 4);

    for (int i = 0; i < pci_get_device_count(); ++i)
    {
        const struct pci_device* device = pci_get_device(i);
        if (device->bus == 0U && device->slot == 1U)
            root_device = device;
        else if (device->bus == 1U && device->slot == 2U)
            downstream_device = device;
        else if (device->bus == 1U && device->slot == 3U)
            malformed_device = device;
        else if (device->bus == 3U && device->slot == 4U)
            endpoint_device = device;
    }

    assert(root_device && downstream_device && malformed_device &&
           endpoint_device);
    assert(root_device->header_type == 1U);
    assert(root_device->primary_bus == 0U);
    assert(root_device->secondary_bus == 1U);
    assert(root_device->subordinate_bus == 5U);
    assert(endpoint_device->header_type == 0U);

    assert(root_device->io_window_valid);
    assert(root_device->io_window_base == 0x00011000ULL);
    assert(root_device->io_window_limit == 0x00012FFFULL);
    assert(root_device->memory_window_valid);
    assert(root_device->memory_window_base == 0x80000000ULL);
    assert(root_device->memory_window_limit == 0x80FFFFFFULL);
    assert(root_device->prefetch_window_valid);
    assert(root_device->prefetch_window_base == 0x1290000000ULL);
    assert(root_device->prefetch_window_limit == 0x1290FFFFFFULL);
    assert(!malformed_device->io_window_valid);

    assert(pci_get_parent_bridge(root_device) == NULL);
    assert(pci_get_parent_bridge(downstream_device) == root_device);
    assert(pci_get_parent_bridge(malformed_device) == root_device);
    assert(pci_get_parent_bridge(endpoint_device) == downstream_device);
    assert(pci_get_parent_bridge(NULL) == NULL);
    return 0;
}
