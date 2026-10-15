#ifndef SHELL_H
#define SHELL_H

#include "keyboard.h"
#include "vanta_boot_info.h"

void shell_initialize(void);

void shell_handle_char(char c);

void shell_handle_event(keyboard_event_t event);

void shell_show_prompt(void);

void shell_set_boot_info(const vanta_boot_info_t* boot_info);

#endif