#include "apic.h"

#include "paging.h"
#include "pic.h"
#include "serial.h"

#include <stdint.h>

#define IA32_APIC_BASE_MSR 0x1BU
#define APIC_BASE_ENABLE (1ULL << 11)
#define APIC_BASE_X2APIC (1ULL << 10)
#define APIC_BASE_BSP (1ULL << 8)
#define APIC_MMIO_VIRTUAL 0xE0000000ULL
#define IOAPIC_MMIO_VIRTUAL 0xE0200000ULL
#define LAPIC_ID 0x020U
#define LAPIC_TPR 0x080U
#define LAPIC_EOI 0x0B0U
#define LAPIC_LVT_LINT0 0x350U
#define LAPIC_LVT_LINT1 0x360U
#define LAPIC_SVR 0x0F0U
#define LAPIC_SPURIOUS_VECTOR 0xFFU
#define IOAPIC_REG_ID 0x00U
#define IOAPIC_REG_VERSION 0x01U
#define IOAPIC_REG_REDIR 0x10U
#define IOAPIC_REDIR_MASK (1U << 16)
#define IOAPIC_REDIR_ACTIVE_LOW (1U << 13)
#define IOAPIC_REDIR_LEVEL (1U << 15)

static acpi_platform_info_t platform_info;
static volatile uint32_t* lapic;
static volatile uint32_t* ioapic;
static int enabled;

static uint64_t read_msr(uint32_t msr)
{
    uint32_t low, high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static void write_msr(uint32_t msr, uint64_t value)
{
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static int cpu_has_apic(void)
{
    uint32_t eax = 1, ebx, ecx, edx;
    __asm__ volatile ("cpuid"
                      : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (edx & (1U << 9)) != 0;
}

static uint32_t lapic_read(uint32_t offset)
{
    return lapic[offset / sizeof(uint32_t)];
}

static void lapic_write(uint32_t offset, uint32_t value)
{
    lapic[offset / sizeof(uint32_t)] = value;
}

static uint32_t ioapic_read(uint8_t reg)
{
    ioapic[0] = reg;
    return ioapic[4];
}

static void ioapic_write(uint8_t reg, uint32_t value)
{
    ioapic[0] = reg;
    ioapic[4] = value;
}

static int route_isa_irq(uint8_t irq, uint8_t vector, uint8_t destination,
                         uint32_t maximum_redirection)
{
    uint32_t gsi = irq;
    uint16_t flags = 0;

    for (uint32_t i = 0; i < platform_info.override_count; i++)
    {
        if (platform_info.overrides[i].source_irq == irq)
        {
            gsi = platform_info.overrides[i].gsi;
            flags = platform_info.overrides[i].flags;
            break;
        }
    }

    if (gsi < platform_info.io_apic_gsi_base)
        return 0;
    uint32_t index = gsi - platform_info.io_apic_gsi_base;
    if (index > maximum_redirection || index > 119U)
        return 0;

    uint32_t low = vector | IOAPIC_REDIR_MASK;
    uint16_t polarity = flags & 3U;
    uint16_t trigger = (flags >> 2) & 3U;

    /* ACPI conforming ISA defaults are active-high, edge-triggered. */
    if (polarity == 3U)
        low |= IOAPIC_REDIR_ACTIVE_LOW;
    else if (polarity == 2U)
        return 0;
    if (trigger == 3U)
        low |= IOAPIC_REDIR_LEVEL;
    else if (trigger == 2U)
        return 0;

    uint8_t low_register = (uint8_t)(IOAPIC_REG_REDIR + index * 2U);
    ioapic_write(low_register, low);
    ioapic_write((uint8_t)(low_register + 1U),
                 (uint32_t)destination << 24);
    ioapic_write(low_register, low & ~IOAPIC_REDIR_MASK);
    return 1;
}

int apic_initialize(void)
{
    enabled = 0;
    lapic = NULL;
    ioapic = NULL;

    if (!cpu_has_apic() ||
        !acpi_discover_platform(&platform_info) ||
        platform_info.local_apic_address >= 0x100000000ULL ||
        platform_info.io_apic_address >= 0x100000000ULL)
    {
        serial_write("VANTAOS_APIC_UNAVAILABLE\n");
        return 0;
    }

    if (!paging_map_mmio_page((uintptr_t)APIC_MMIO_VIRTUAL,
                              (uintptr_t)platform_info.local_apic_address) ||
        !paging_map_mmio_page((uintptr_t)IOAPIC_MMIO_VIRTUAL,
                              (uintptr_t)platform_info.io_apic_address))
    {
        serial_write("VANTAOS_APIC_MMIO_MAP_FAIL\n");
        return 0;
    }

    lapic = (volatile uint32_t*)(uintptr_t)APIC_MMIO_VIRTUAL;
    ioapic = (volatile uint32_t*)(uintptr_t)IOAPIC_MMIO_VIRTUAL;

    uint64_t base = read_msr(IA32_APIC_BASE_MSR);
    if (base & APIC_BASE_ENABLE)
    {
        base &= ~APIC_BASE_ENABLE;
        write_msr(IA32_APIC_BASE_MSR, base);
    }
    base &= ~(APIC_BASE_X2APIC | 0x0000000FFFFFF000ULL);
    base |= (platform_info.local_apic_address & 0x0000000FFFFF000ULL) |
            APIC_BASE_ENABLE | (read_msr(IA32_APIC_BASE_MSR) & APIC_BASE_BSP);
    write_msr(IA32_APIC_BASE_MSR, base);

    /* Enable the local APIC and establish a valid spurious-interrupt vector. */
    lapic_write(LAPIC_TPR, 0);
    lapic_write(LAPIC_SVR, (1U << 8) | LAPIC_SPURIOUS_VECTOR);

    uint32_t version = ioapic_read(IOAPIC_REG_VERSION);
    uint32_t maximum_redirection = (version >> 16) & 0xFFU;
    uint8_t destination = (uint8_t)(lapic_read(LAPIC_ID) >> 24);

    if (!route_isa_irq(0, 32, destination, maximum_redirection) ||
        !route_isa_irq(1, 33, destination, maximum_redirection) ||
        !route_isa_irq(12, 44, destination, maximum_redirection))
    {
        /* Undo partial IOAPIC routing; keep firmware's legacy PIC path intact. */
        for (uint32_t i = 0; i <= maximum_redirection && i <= 119U; i++)
        {
            uint8_t reg = (uint8_t)(IOAPIC_REG_REDIR + i * 2U);
            ioapic_write(reg, IOAPIC_REDIR_MASK);
            ioapic_write((uint8_t)(reg + 1U), 0);
        }
        serial_write("VANTAOS_APIC_IOAPIC_ROUTE_FAIL\n");
        return 0;
    }

    /* Once all required lines are routed, disable legacy PIC delivery to the CPU. */
    lapic_write(LAPIC_LVT_LINT0, 1U << 16);
    lapic_write(LAPIC_LVT_LINT1, 1U << 16);
    pic_mask_all();
    enabled = 1;
    serial_write("VANTAOS_APIC_LOCAL_OK\n");
    return 1;
}

int apic_is_enabled(void)
{
    return enabled;
}

void apic_send_eoi(void)
{
    if (enabled && lapic)
        lapic_write(LAPIC_EOI, 0);
}

const acpi_platform_info_t* apic_get_platform_info(void)
{
    return platform_info.cpu_count ? &platform_info : NULL;
}
