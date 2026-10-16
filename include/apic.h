#ifndef APIC_H
#define APIC_H

#include "acpi.h"

int apic_initialize(void);
/* Enables and validates the local APIC on the calling CPU. */
int apic_initialize_cpu(uint32_t expected_apic_id);
int apic_is_enabled(void);
void apic_send_eoi(void);
uint32_t apic_get_bsp_id(void);
int apic_send_init_all(void);
int apic_send_sipi(uint32_t apic_id, uint8_t startup_vector);
int apic_send_fixed_ipi(uint32_t apic_id, uint8_t vector);
const acpi_platform_info_t* apic_get_platform_info(void);

#endif
