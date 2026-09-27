#ifndef TERMINAL_PROCESS_H
#define TERMINAL_PROCESS_H

#include <stdint.h>

void terminal_process_initialize(void);

int terminal_process_start(uint32_t parent_pid);
void terminal_process_request_exit(void);
int terminal_process_is_running(void);
int terminal_process_consume_redraw(void);
void terminal_process_poll_input(void);
uint32_t terminal_process_pid(void);

#endif
