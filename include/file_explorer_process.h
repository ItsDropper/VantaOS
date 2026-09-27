#ifndef FILE_EXPLORER_PROCESS_H
#define FILE_EXPLORER_PROCESS_H

#include <stdint.h>

void file_explorer_process_initialize(void);
int file_explorer_process_start(uint32_t parent_pid);
void file_explorer_process_step(void);
uint32_t file_explorer_process_pid(void);

#endif
