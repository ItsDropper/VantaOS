#ifndef APIC_H
#define APIC_H

#include "acpi.h"

int apic_initialize(void);
int apic_is_enabled(void);
void apic_send_eoi(void);
uint32_t apic_get_bsp_id(void);
int apic_send_init_sipi(uint32_t apic_id, uint8_t startup_vector);
const acpi_platform_info_t* apic_get_platform_info(void);

#endif
