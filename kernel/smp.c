#include "smp.h"

#include "acpi.h"
#include "apic.h"
#include "paging.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define AP_TRAMPOLINE_PHYSICAL 0x8000U
#define AP_MAILBOX_PHYSICAL 0x7000U
#define AP_STARTUP_VECTOR 0x08U
#define AP_STACK_SIZE 16384U
#define AP_STARTUP_SPIN_LIMIT 50000000U

typedef struct
{
    volatile uint64_t cr3;
    volatile uint64_t entry;
    volatile uint64_t stack_top;
} ap_startup_mailbox_t;

extern uint8_t ap_trampoline_blob_start[];
extern uint8_t ap_trampoline_blob_end[];
extern _Noreturn void smp_ap_entry(uint32_t apic_id);

static unsigned int discovered_cpu_count;
static uint8_t ap_stacks[ACPI_MAX_CPUS][AP_STACK_SIZE]
    __attribute__((aligned(16)));
static volatile uint32_t ap_online[ACPI_MAX_CPUS];

static int find_cpu_index(const acpi_platform_info_t* info, uint32_t apic_id)
{
    for (uint32_t i = 0; i < info->cpu_count; i++)
        if (info->cpu_apic_ids[i] == apic_id)
            return (int)i;
    return -1;
}

_Noreturn void smp_ap_entry(uint32_t apic_id)
{
    const acpi_platform_info_t* info = apic_get_platform_info();
    int index = info ? find_cpu_index(info, apic_id) : -1;

    if (index >= 0)
        __atomic_store_n(&ap_online[index], 1U, __ATOMIC_RELEASE);

    /* APs remain safely parked until per-CPU scheduling is implemented. */
    for (;;)
        __asm__ volatile ("cli; hlt" ::: "memory");
}

static int start_secondary_cpus(const acpi_platform_info_t* info)
{
    if (!apic_is_enabled())
        return 0;

    size_t trampoline_size =
        (size_t)(ap_trampoline_blob_end - ap_trampoline_blob_start);
    if (trampoline_size == 0 || trampoline_size > PAGE_SIZE)
    {
        serial_write("VANTAOS_SMP_TRAMPOLINE_SIZE_FAIL\n");
        return 0;
    }

    volatile uint8_t* destination =
        (volatile uint8_t*)(uintptr_t)AP_TRAMPOLINE_PHYSICAL;
    for (size_t i = 0; i < trampoline_size; i++)
        destination[i] = ap_trampoline_blob_start[i];

    ap_startup_mailbox_t* mailbox =
        (ap_startup_mailbox_t*)(uintptr_t)AP_MAILBOX_PHYSICAL;
    mailbox->cr3 = paging_get_kernel_address_space()->directory_physical;
    mailbox->entry = (uintptr_t)&smp_ap_entry;

    uint32_t bsp_id = apic_get_bsp_id();
    unsigned int online_count = 1U;

    for (uint32_t i = 0; i < info->cpu_count; i++)
    {
        uint32_t apic_id = info->cpu_apic_ids[i];
        if (apic_id == bsp_id)
            continue;

        /* xAPIC's destination field is eight bits; x2APIC startup is separate. */
        if (apic_id > 255U)
        {
            serial_write("VANTAOS_SMP_AP_START_FAIL\n");
            return 0;
        }

        __atomic_store_n(&ap_online[i], 0U, __ATOMIC_RELAXED);
        mailbox->stack_top =
            (uintptr_t)&ap_stacks[i][AP_STACK_SIZE];
        __asm__ volatile ("mfence" ::: "memory");

        if (!apic_send_init_sipi(apic_id, AP_STARTUP_VECTOR))
        {
            serial_write("VANTAOS_SMP_AP_START_FAIL\n");
            return 0;
        }

        uint32_t spin = 0;
        while (__atomic_load_n(&ap_online[i], __ATOMIC_ACQUIRE) == 0U &&
               spin++ < AP_STARTUP_SPIN_LIMIT)
            __asm__ volatile ("pause");

        if (__atomic_load_n(&ap_online[i], __ATOMIC_ACQUIRE) == 0U)
        {
            serial_write("VANTAOS_SMP_AP_START_FAIL\n");
            return 0;
        }

        online_count++;
    }

    if (online_count != info->cpu_count)
    {
        serial_write("VANTAOS_SMP_AP_START_FAIL\n");
        return 0;
    }

    serial_write("VANTAOS_SMP_AP_START_OK\n");
    return 1;
}

int smp_initialize(void)
{
    const acpi_platform_info_t* info = apic_get_platform_info();
    if (!info)
    {
        serial_write("VANTAOS_SMP_TOPOLOGY_UNAVAILABLE\n");
        return 0;
    }

    discovered_cpu_count = info->cpu_count;
    serial_write("VANTAOS_SMP_TOPOLOGY_OK\n");

    if (!start_secondary_cpus(info))
    {
        if (!apic_is_enabled())
            serial_write("VANTAOS_SMP_AP_START_SKIPPED\n");
        return 0;
    }

    return 1;
}

unsigned int smp_cpu_count(void)
{
    return discovered_cpu_count;
}
