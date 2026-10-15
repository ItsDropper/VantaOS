#ifndef APIC_H
#define APIC_H

#include "acpi.h"

int apic_initialize(void);
int apic_is_enabled(void);
void apic_send_eoi(void);
const acpi_platform_info_t* apic_get_platform_info(void);

#endif
