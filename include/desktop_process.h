#ifndef DESKTOP_PROCESS_H
#define DESKTOP_PROCESS_H

#include <stdint.h>

void desktop_process_initialize(void);
void desktop_process_main(void);
int desktop_process_start(uint32_t parent_pid);

#endif
