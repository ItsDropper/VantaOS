#include "smp.h"

#include "acpi.h"
#include "apic.h"
#include "serial.h"

static unsigned int discovered_cpu_count;

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
    serial_write("VANTAOS_SMP_CPU_COUNT=");
    serial_write_hex(discovered_cpu_count);
    serial_write("\n");

    if (discovered_cpu_count > 1U)
        serial_write("VANTAOS_SMP_SECONDARIES_PENDING\n");
    else
        serial_write("VANTAOS_SMP_SINGLE_CPU_FIRMWARE\n");

    return 1;
}

unsigned int smp_cpu_count(void)
{
    return discovered_cpu_count;
}
