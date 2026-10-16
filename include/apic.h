#ifndef APIC_H
#define APIC_H

#include "acpi.h"

int apic_initialize(void);
/* Enables and validates the local APIC on the calling CPU. */
int apic_initialize_cpu(uint32_t expected_apic_id);
/* Starts a periodic local timer on the calling CPU. */
int apic_timer_initialize(uint8_t vector, uint32_t initial_count);
/* Calibrates LAPIC decrements per reference PIT tick on the calling CPU. */
int apic_timer_calibrate(uint8_t vector, uint32_t target_hz,
                         uint32_t* count_per_tick);
int apic_is_enabled(void);
void apic_send_eoi(void);
uint32_t apic_get_bsp_id(void);
int apic_send_init_all(void);
int apic_send_sipi(uint32_t apic_id, uint8_t startup_vector);
int apic_send_fixed_ipi(uint32_t apic_id, uint8_t vector);
/* Broadcasts an NMI to all other CPUs for system-wide fatal-stop handling. */
int apic_send_nmi_all(void);
/* Sends the dedicated reschedule-request vector; does not switch contexts. */
int apic_send_reschedule_ipi(uint32_t apic_id);
const acpi_platform_info_t* apic_get_platform_info(void);

#endif
