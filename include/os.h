#ifndef OS_H
#define OS_H

#include "multiboot.h"

void os_initialize(multiboot_info_t* mbd);
void os_run(void);

#endif
