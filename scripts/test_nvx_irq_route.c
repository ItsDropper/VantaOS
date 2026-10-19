/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_irq_route.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define CONFIG_ADDRESS_PORT 0xCF8U
#define CONFIG_DATA_PORT 0xCFCU
static uint32_t config_space[64];
static uint32_t selected_address;
static unsigned eoi_count;
static unsigned callback_count;
static uint8_t callback_vector;
static void *callback_context;
static int apic_enabled = 1;
static void dummy_stub(void) {}
void (*device_irq_stub_table[DEVICE_IRQ_VECTOR_COUNT])(void);

void pci_test_outl(uint16_t port, uint32_t value)
{
    if (port == CONFIG_ADDRESS_PORT) { selected_address = value; return; }
    assert(port == CONFIG_DATA_PORT);
    config_space[(selected_address & 0xFCU) / 4U] = value;
}
uint32_t pci_test_inl(uint16_t port)
{
    assert(port == CONFIG_DATA_PORT);
    return config_space[(selected_address & 0xFCU) / 4U];
}
void idt_set_gate(int n, uint64_t b, uint16_t s, uint8_t f)
{
    assert(n >= 0 && b != 0U && s == 0x08U && f == 0x8EU);
}
int apic_is_enabled(void) { return apic_enabled; }
void apic_send_eoi(void) { ++eoi_count; }
uint32_t apic_get_bsp_id(void) { return 0U; }
static uint32_t msix_table[8U * 4U];
void *mmio_map_region(uint64_t address, size_t length)
{
    assert(address == 0x80001000ULL);
    assert(length == sizeof(msix_table));
    memset(msix_table, 0, sizeof(msix_table));
    return msix_table;
}
int mmio_unmap_region(void *address, size_t length)
{
    assert(address == msix_table && length == sizeof(msix_table));
    return 1;
}

static void set_byte(uint8_t offset, uint8_t value)
{
    unsigned i = offset / 4U, shift = (offset & 3U) * 8U;
    config_space[i] = (config_space[i] & ~(0xFFU << shift)) |
                      ((uint32_t)value << shift);
}
static void set_word(uint8_t offset, uint16_t value)
{
    set_byte(offset, (uint8_t)value);
    set_byte((uint8_t)(offset + 1U), (uint8_t)(value >> 8U));
}
static void set_dword(uint8_t offset, uint32_t value)
{
    config_space[offset / 4U] = value;
}
static void prepare_pci(void)
{
    memset(config_space, 0, sizeof(config_space));
    config_space[1] = 0x00100000U;
    set_byte(0x34U, 0x50U);
    set_byte(0x50U, 0x05U);
    set_byte(0x51U, 0x60U);
    set_word(0x52U, (uint16_t)(1U << 7U)); /* 64-bit MSI */
    set_byte(0x60U, 0x11U);
    set_word(0x62U, 7U); /* Eight MSI-X vectors, disabled */
    set_dword(0x64U, 0x1000U);
    set_dword(0x68U, 0x2000U);
}
static void handler(uint8_t vector, void *context)
{
    ++callback_count;
    callback_vector = vector;
    callback_context = context;
}

int main(void)
{
    const struct pci_device device = {
        .bus = 0U, .slot = 1U, .function = 0U,
        .vendor_id = 0x10DEU, .device_id = 0x2503U
    };
    nvx_bar_resource_t bars[6] = {{0}};
    nvx_irq_route_t route = {0};
    int context = 7;

    for (size_t i = 0; i < DEVICE_IRQ_VECTOR_COUNT; ++i)
        device_irq_stub_table[i] = dummy_stub;
    assert(device_irq_initialize());
    bars[0].base = 0x80000000ULL;
    bars[0].size = 0x10000U;
    bars[0].size_known = 1U;
    prepare_pci();

    assert(nvx_irq_route_prepare(&device, bars, handler, &context, &route) ==
           NVX_IRQ_ROUTE_OK);
    assert(route.prepared && route.mode == NVX_IRQ_ROUTE_MSIX);
    assert(route.capabilities.msix.present && route.vector != 0x80U);
    assert(nvx_irq_route_prepare(&device, bars, handler, &context, &route) ==
           NVX_IRQ_ROUTE_INVALID_ARGUMENT);
    assert(nvx_irq_route_enable(&route) == NVX_IRQ_ROUTE_OK);
    assert(route.enabled && msix_table[0] == 0xFEE00000U);
    assert(msix_table[1] == 0U && msix_table[2] == route.vector);
    assert(msix_table[3] == 0U);
    assert((config_space[0x62U / 4U] & 0xC0000000U) == 0x80000000U);
    assert(nvx_irq_route_enable(&route) == NVX_IRQ_ROUTE_INVALID_ARGUMENT);
    device_irq_dispatch(route.vector);
    assert(callback_count == 1U && callback_vector == route.vector);
    assert(callback_context == &context && eoi_count == 1U);

    assert(nvx_irq_route_disable(&route) == NVX_IRQ_ROUTE_OK);
    assert(!route.enabled);
    assert((config_space[0x62U / 4U] & 0xC0000000U) == 0x40000000U);
    assert(nvx_irq_route_release(&route) == NVX_IRQ_ROUTE_OK);
    assert(!route.prepared && route.mode == NVX_IRQ_ROUTE_NONE);
    assert(nvx_irq_route_release(&route) == NVX_IRQ_ROUTE_NOT_PREPARED);

    /* A device with no conventional MSI/MSI-X capability is not routed. */
    memset(config_space, 0, sizeof(config_space));
    assert(nvx_irq_route_prepare(&device, bars, handler, &context, &route) ==
           NVX_IRQ_ROUTE_UNSUPPORTED);
    assert(!route.prepared);

    /* MSI-only devices use a 64-bit address field and a single message. */
    prepare_pci();
    set_byte(0x34U, 0x50U);
    set_byte(0x51U, 0U);
    set_byte(0x60U, 0U);
    assert(nvx_irq_route_prepare(&device, bars, handler, &context, &route) ==
           NVX_IRQ_ROUTE_OK);
    assert(route.mode == NVX_IRQ_ROUTE_MSI);
    assert(nvx_irq_route_enable(&route) == NVX_IRQ_ROUTE_OK);
    assert((config_space[0x50U / 4U] & (1U << 16U)) != 0U);
    assert(nvx_irq_route_disable(&route) == NVX_IRQ_ROUTE_OK);
    assert((config_space[0x50U / 4U] & (1U << 16U)) == 0U);
    assert(nvx_irq_route_release(&route) == NVX_IRQ_ROUTE_OK);

    apic_enabled = 0;
    prepare_pci();
    assert(nvx_irq_route_prepare(&device, bars, handler, &context, &route) ==
           NVX_IRQ_ROUTE_VECTOR_UNAVAILABLE);
    assert(!route.prepared);
    return 0;
}
