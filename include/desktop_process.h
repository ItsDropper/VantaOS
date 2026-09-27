#ifndef DESKTOP_PROCESS_H
#define DESKTOP_PROCESS_H

#include <stdint.h>

void desktop_process_initialize(void);
int desktop_process_start(uint32_t parent_pid);
void desktop_process_run(void);

#endif
