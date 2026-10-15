#ifndef OS_H
#define OS_H

#include "vanta_boot_info.h"

void os_initialize(const vanta_boot_info_t* boot_info);
void os_run(void);

#endif
