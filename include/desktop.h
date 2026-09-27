#ifndef DESKTOP_H
#define DESKTOP_H

#include "multiboot.h"

void desktop_initialize(multiboot_info_t* mbd);
void desktop_present(void);
void desktop_update(void);

#endif
