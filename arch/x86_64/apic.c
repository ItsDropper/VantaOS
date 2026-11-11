/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "apic.h"
#include "apic_x2apic.h"

#include "paging.h"
#include "pic.h"
#include "serial.h"

#include <stddef.h>
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
#define LAPIC_LVT_TIMER 0x320U
#define LAPIC_TIMER_INITIAL_COUNT 0x380U
#define LAPIC_TIMER_CURRENT_COUNT 0x390U
#define PIT_COMMAND_PORT 0x43U
#define PIT_CHANNEL2_DATA_PORT 0x42U
#define PIT_SPEAKER_PORT 0x61U
#define PIT_BASE_FREQUENCY 1193182ULL
#define PIT_CALIBRATION_DIVISOR 11932U
#define PIT_CALIBRATION_PERIODS 4U
#define PIT_CALIBRATION_SPIN_LIMIT 20000000U
#define LAPIC_TIMER_DIVIDE 0x3E0U
#define APIC_RESCHEDULE_VECTOR 0xF3U
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
static int x2apic_mode;
static int legacy_pic_routing;

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

static uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint32_t lapic_read(uint32_t offset)
{
    if (x2apic_mode)
        return (uint32_t)read_msr(apic_x2apic_msr_for_offset(offset));
    return lapic[offset / sizeof(uint32_t)];
}

static void lapic_write(uint32_t offset, uint32_t value)
{
    if (x2apic_mode)
        write_msr(apic_x2apic_msr_for_offset(offset), value);
    else
        lapic[offset / sizeof(uint32_t)] = value;
}

static uint32_t lapic_id(void)
{
    return x2apic_mode ? lapic_read(LAPIC_ID) :
                         (lapic_read(LAPIC_ID) >> 24U);
}

static int local_apic_ready(void)
{
    return enabled && (x2apic_mode || lapic != NULL);
}

static int cpu_has_x2apic(void)
{
    uint32_t eax = 1U, ebx, ecx, edx;
    __asm__ volatile ("cpuid"
                      : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    (void)ebx;
    (void)edx;
    return (ecx & (1U << 21U)) != 0U;
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
    x2apic_mode = 0;
    legacy_pic_routing = 0;
    lapic = NULL;
    ioapic = NULL;

    if (!cpu_has_apic() || !acpi_discover_platform(&platform_info) ||
        platform_info.local_apic_address >= 0x100000000ULL ||
        platform_info.io_apic_address >= 0x100000000ULL)
    {
        serial_write("VANTAOS_APIC_UNAVAILABLE\n");
        return 0;
    }

    int topology_needs_x2apic = 0;
    for (uint32_t i = 0; i < platform_info.cpu_count; ++i)
        if (apic_id_requires_x2apic(platform_info.cpu_apic_ids[i]))
            topology_needs_x2apic = 1;

    uint64_t base = read_msr(IA32_APIC_BASE_MSR);
    x2apic_mode = (base & APIC_BASE_X2APIC) != 0U;
    if ((x2apic_mode || topology_needs_x2apic) && !cpu_has_x2apic())
    {
        serial_write("VANTAOS_APIC_X2APIC_UNSUPPORTED\n");
        return 0;
    }

    if (topology_needs_x2apic && !x2apic_mode)
    {
        /* Preserve the firmware APIC base while enabling x2APIC mode. */
        base |= APIC_BASE_ENABLE;
        write_msr(IA32_APIC_BASE_MSR, base);
        base |= APIC_BASE_X2APIC;
        write_msr(IA32_APIC_BASE_MSR, base);
        x2apic_mode = 1;
        if ((read_msr(IA32_APIC_BASE_MSR) & APIC_BASE_X2APIC) == 0U)
        {
            serial_write("VANTAOS_APIC_X2APIC_ENABLE_FAIL\n");
            return 0;
        }
    }
    else if (!x2apic_mode)
    {
        if (!paging_map_mmio_page((uintptr_t)APIC_MMIO_VIRTUAL,
                                  (uintptr_t)platform_info.local_apic_address))
        {
            serial_write("VANTAOS_APIC_MMIO_MAP_FAIL\n");
            return 0;
        }
        lapic = (volatile uint32_t*)(uintptr_t)APIC_MMIO_VIRTUAL;
        if (base & APIC_BASE_ENABLE)
        {
            base &= ~APIC_BASE_ENABLE;
            write_msr(IA32_APIC_BASE_MSR, base);
        }
        base &= ~0x0000000FFFFFF000ULL;
        base |= (platform_info.local_apic_address & 0x0000000FFFFF000ULL) |
                APIC_BASE_ENABLE;
        write_msr(IA32_APIC_BASE_MSR, base);
    }
    else
    {
        base |= APIC_BASE_ENABLE;
        write_msr(IA32_APIC_BASE_MSR, base);
    }

    if (!paging_map_mmio_page((uintptr_t)IOAPIC_MMIO_VIRTUAL,
                              (uintptr_t)platform_info.io_apic_address))
    {
        if (lapic)
            paging_unmap_page((uintptr_t)APIC_MMIO_VIRTUAL);
        lapic = NULL;
        serial_write("VANTAOS_APIC_MMIO_MAP_FAIL\n");
        return 0;
    }
    ioapic = (volatile uint32_t*)(uintptr_t)IOAPIC_MMIO_VIRTUAL;

    base = read_msr(IA32_APIC_BASE_MSR);
    if ((base & APIC_BASE_ENABLE) == 0U ||
        (x2apic_mode && (base & APIC_BASE_X2APIC) == 0U))
    {
        serial_write("VANTAOS_APIC_MODE_VERIFY_FAIL\\n");
        return 0;
    }

    lapic_write(LAPIC_TPR, 0);
    lapic_write(LAPIC_SVR, (1U << 8) | LAPIC_SPURIOUS_VECTOR);
    uint32_t bsp_id = lapic_id();

    /*
     * IOAPIC physical destinations are 8-bit without interrupt remapping.
     * Do not truncate a high x2APIC BSP ID: retain the 8259 -> LINT0 ExtINT
     * route for legacy keyboard/timer interrupts instead.
     */
    if (bsp_id > 255U)
    {
        if (!x2apic_mode)
        {
            serial_write("VANTAOS_APIC_DESTINATION_UNSUPPORTED\n");
            return 0;
        }
        uint32_t ioapic_version = ioapic_read(IOAPIC_REG_VERSION);
        uint32_t maximum_redirection = (ioapic_version >> 16U) & 0xFFU;
        for (uint32_t i = 0; i <= maximum_redirection && i <= 119U; ++i)
        {
            uint8_t reg = (uint8_t)(IOAPIC_REG_REDIR + i * 2U);
            ioapic_write(reg, ioapic_read(reg) | IOAPIC_REDIR_MASK);
        }
        lapic_write(LAPIC_LVT_LINT0, 7U << 8U);
        lapic_write(LAPIC_LVT_LINT1, 1U << 16);
        legacy_pic_routing = 1;
        enabled = 1;
        serial_write("VANTAOS_APIC_X2APIC_PIC_FALLBACK_OK\n");
        return 1;
    }

    uint32_t version = ioapic_read(IOAPIC_REG_VERSION);
    uint32_t maximum_redirection = (version >> 16) & 0xFFU;
    uint8_t destination = (uint8_t)bsp_id;
    if (!route_isa_irq(0, 32, destination, maximum_redirection) ||
        !route_isa_irq(1, 33, destination, maximum_redirection) ||
        !route_isa_irq(12, 44, destination, maximum_redirection))
    {
        for (uint32_t i = 0; i <= maximum_redirection && i <= 119U; i++)
        {
            uint8_t reg = (uint8_t)(IOAPIC_REG_REDIR + i * 2U);
            ioapic_write(reg, IOAPIC_REDIR_MASK);
            ioapic_write((uint8_t)(reg + 1U), 0);
        }
        serial_write("VANTAOS_APIC_IOAPIC_ROUTE_FAIL\n");
        return 0;
    }

    lapic_write(LAPIC_LVT_LINT0, 1U << 16);
    lapic_write(LAPIC_LVT_LINT1, 1U << 16);
    pic_mask_all();
    enabled = 1;
    serial_write(x2apic_mode ? "VANTAOS_APIC_X2APIC_LOCAL_OK\n" :
                               "VANTAOS_APIC_LOCAL_OK\n");
    return 1;
}

int apic_timer_initialize(uint8_t vector, uint32_t initial_count)
{
    if (!local_apic_ready() || vector < 0x10U || vector == 0xFFU ||
        initial_count == 0U)
        return 0;

    /* Divide by 16; use periodic mode, masking the vector during setup. */
    lapic_write(LAPIC_LVT_TIMER, (1U << 16) | vector);
    lapic_write(LAPIC_TIMER_DIVIDE, 0x3U);
    lapic_write(LAPIC_TIMER_INITIAL_COUNT, initial_count);
    lapic_write(LAPIC_LVT_TIMER, (1U << 17) | vector);
    return 1;
}

static int pit_channel2_wait_period(uint8_t original_port)
{
    /* Channel 2 mode 0 provides a polled reference without relying on IRQ0. */
    outb(PIT_SPEAKER_PORT, original_port & (uint8_t)~0x03U);
    outb(PIT_COMMAND_PORT, 0xB0U); /* channel 2, lobyte/hibyte, mode 0 */
    outb(PIT_CHANNEL2_DATA_PORT, (uint8_t)PIT_CALIBRATION_DIVISOR);
    outb(PIT_CHANNEL2_DATA_PORT,
         (uint8_t)(PIT_CALIBRATION_DIVISOR >> 8));
    outb(PIT_SPEAKER_PORT,
         (original_port & (uint8_t)~0x02U) | 0x01U); /* gate on, speaker off */

    uint32_t spins = 0;
    while ((inb(PIT_SPEAKER_PORT) & 0x20U) != 0U &&
           spins++ < PIT_CALIBRATION_SPIN_LIMIT)
        __asm__ volatile ("pause" ::: "memory");

    int output_went_low =
        (inb(PIT_SPEAKER_PORT) & 0x20U) == 0U;
    while (output_went_low &&
           (inb(PIT_SPEAKER_PORT) & 0x20U) == 0U &&
           spins++ < PIT_CALIBRATION_SPIN_LIMIT)
        __asm__ volatile ("pause" ::: "memory");

    int elapsed = output_went_low &&
                  (inb(PIT_SPEAKER_PORT) & 0x20U) != 0U;
    outb(PIT_SPEAKER_PORT, original_port);
    return elapsed;
}

int apic_timer_calibrate(uint8_t vector, uint32_t target_hz,
                         uint32_t* count_per_tick)
{
    if (!local_apic_ready() || vector < 0x10U || vector == 0xFFU ||
        target_hz == 0U || !count_per_tick)
        return 0;

    uint64_t saved_flags;
    __asm__ volatile ("pushfq; popq %0" : "=r"(saved_flags));
    uint8_t original_port = inb(PIT_SPEAKER_PORT);

    /* Mask delivery but let the one-shot LAPIC counter run down. */
    lapic_write(LAPIC_LVT_TIMER, (1U << 16) | vector);
    lapic_write(LAPIC_TIMER_DIVIDE, 0x3U);
    lapic_write(LAPIC_TIMER_INITIAL_COUNT, UINT32_MAX);

    int ok = 1;
    for (uint32_t i = 0; i < PIT_CALIBRATION_PERIODS; i++)
    {
        if (!pit_channel2_wait_period(original_port))
        {
            ok = 0;
            break;
        }
    }

    uint32_t remaining = lapic_read(LAPIC_TIMER_CURRENT_COUNT);
    lapic_write(LAPIC_LVT_TIMER, (1U << 16) | vector);
    outb(PIT_SPEAKER_PORT, original_port);

    if ((saved_flags & (1ULL << 9)) != 0U)
        __asm__ volatile ("sti" ::: "memory");
    else
        __asm__ volatile ("cli" ::: "memory");

    if (!ok || remaining >= UINT32_MAX)
        return 0;

    uint64_t elapsed_count = (uint64_t)UINT32_MAX - remaining;
    uint64_t denominator =
        (uint64_t)PIT_CALIBRATION_DIVISOR *
        PIT_CALIBRATION_PERIODS * target_hz;
    uint64_t calibrated =
        elapsed_count * PIT_BASE_FREQUENCY / denominator;
    if (calibrated == 0U || calibrated > UINT32_MAX)
        return 0;

    *count_per_tick = (uint32_t)calibrated;
    return 1;
}

int apic_initialize_cpu(uint32_t expected_apic_id)
{
    if (!enabled || !cpu_has_apic())
        return 0;

    uint64_t base = read_msr(IA32_APIC_BASE_MSR);
    if (x2apic_mode)
    {
        if (!cpu_has_x2apic())
            return 0;
        base |= APIC_BASE_ENABLE | APIC_BASE_X2APIC;
        write_msr(IA32_APIC_BASE_MSR, base);
        base = read_msr(IA32_APIC_BASE_MSR);
        if ((base & (APIC_BASE_ENABLE | APIC_BASE_X2APIC)) !=
            (APIC_BASE_ENABLE | APIC_BASE_X2APIC))
            return 0;
    }
    else
    {
        if (platform_info.local_apic_address >= 0x100000000ULL ||
            (base & APIC_BASE_X2APIC))
            return 0;
        if (base & APIC_BASE_ENABLE)
        {
            base &= ~APIC_BASE_ENABLE;
            write_msr(IA32_APIC_BASE_MSR, base);
        }
        base &= ~0x0000000FFFFFF000ULL;
        base |= (platform_info.local_apic_address & 0x0000000FFFFF000ULL) |
                APIC_BASE_ENABLE;
        write_msr(IA32_APIC_BASE_MSR, base);
    }

    lapic_write(LAPIC_TPR, 0);
    lapic_write(LAPIC_SVR, (1U << 8) | LAPIC_SPURIOUS_VECTOR);
    lapic_write(LAPIC_LVT_LINT0, 1U << 16);
    lapic_write(LAPIC_LVT_LINT1, 1U << 16);
    return lapic_id() == expected_apic_id;
}

static void apic_delay(volatile uint32_t iterations)
{
    while (iterations--)
        __asm__ volatile ("pause");
}

static int apic_send_ipi(uint32_t destination, uint32_t command)
{
    if (!local_apic_ready() || (!x2apic_mode && destination > 255U))
        return 0;

    /* The ICR is local to the sending CPU; preserve the caller's IF state. */
    uint64_t saved_flags;
    __asm__ volatile ("pushfq; popq %0; cli"
                      : "=r"(saved_flags) :: "memory", "cc");
    uint32_t timeout = 1000000U;

    if (x2apic_mode)
    {
        while ((read_msr(APIC_X2APIC_MSR_ICR) & (1ULL << 12U)) != 0U &&
               timeout-- != 0U)
            __asm__ volatile ("pause" ::: "memory");
        if ((read_msr(APIC_X2APIC_MSR_ICR) & (1ULL << 12U)) != 0U)
        {
            __asm__ volatile ("pushq %0; popfq"
                              :: "r"(saved_flags) : "memory", "cc");
            return 0;
        }
        __asm__ volatile ("mfence" ::: "memory");
        write_msr(APIC_X2APIC_MSR_ICR,
                  apic_x2apic_encode_icr(destination, command));
        timeout = 1000000U;
        while ((read_msr(APIC_X2APIC_MSR_ICR) & (1ULL << 12U)) != 0U &&
               timeout-- != 0U)
            __asm__ volatile ("pause" ::: "memory");
        int delivered =
            (read_msr(APIC_X2APIC_MSR_ICR) & (1ULL << 12U)) == 0U;
        __asm__ volatile ("pushq %0; popfq"
                          :: "r"(saved_flags) : "memory", "cc");
        return delivered;
    }

    while ((lapic_read(0x300U) & (1U << 12)) != 0U && timeout > 0U)
    {
        timeout--;
        __asm__ volatile ("pause" ::: "memory");
    }
    if ((lapic_read(0x300U) & (1U << 12)) != 0U)
    {
        __asm__ volatile ("pushq %0; popfq"
                          :: "r"(saved_flags) : "memory", "cc");
        return 0;
    }
    lapic_write(0x310U, destination << 24U);
    __asm__ volatile ("" ::: "memory");
    lapic_write(0x300U, command);
    timeout = 1000000U;
    while ((lapic_read(0x300U) & (1U << 12)) != 0U && timeout > 0U)
    {
        timeout--;
        __asm__ volatile ("pause" ::: "memory");
    }
    int delivered = (lapic_read(0x300U) & (1U << 12)) == 0U;
    __asm__ volatile ("pushq %0; popfq"
                      :: "r"(saved_flags) : "memory", "cc");
    return delivered;
}

int apic_send_nmi_all(void)
{
    /* Fixed APIC ICR: NMI delivery, shorthand = all excluding self. */
    return apic_send_ipi(0U, 0x000C0400U);
}

uint32_t apic_get_bsp_id(void)
{
    return local_apic_ready() ? lapic_id() : 0U;
}

int apic_send_init_all(void)
{
    if (!local_apic_ready())
        return 0;

    /*
     * Initialize all other processors as a batch. The INIT IPI destination
     * shorthand is "all excluding self"; waiting once avoids multiplying the
     * mandatory INIT delays by the number of APs under slow QEMU TCG.
     */
    if (!apic_send_ipi(0U, 0x000CC500U))
        return 0;
    apic_delay(10000000U);
    if (!apic_send_ipi(0U, 0x000C8500U))
        return 0;
    apic_delay(10000000U);
    return 1;
}

int apic_send_sipi(uint32_t apic_id, uint8_t startup_vector)
{
    if (!local_apic_ready() || startup_vector == 0 ||
        (!x2apic_mode && apic_id > 255U))
        return 0;

    if (!apic_send_ipi(apic_id, 0x00000600U | startup_vector))
        return 0;
    apic_delay(200000U);
    return apic_send_ipi(apic_id, 0x00000600U | startup_vector);
}

int apic_send_fixed_ipi(uint32_t apic_id, uint8_t vector)
{
    if (vector < 0x10U || vector == 0xFFU)
        return 0;
    return apic_send_ipi(apic_id, vector);
}

int apic_send_reschedule_ipi(uint32_t apic_id)
{
    return apic_send_fixed_ipi(apic_id, APIC_RESCHEDULE_VECTOR);
}

int apic_is_enabled(void)
{
    return enabled;
}

int apic_is_x2apic(void)
{
    return enabled && x2apic_mode;
}

int apic_uses_legacy_pic(void)
{
    return enabled && legacy_pic_routing;
}

void apic_send_eoi(void)
{
    if (local_apic_ready())
        lapic_write(LAPIC_EOI, 0);
}

const acpi_platform_info_t* apic_get_platform_info(void)
{
    return platform_info.cpu_count ? &platform_info : NULL;
}
