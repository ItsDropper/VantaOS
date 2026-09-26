#ifndef KEYBOARD_H
#define KEYBOARD_H

void keyboard_initialize(void);
void keyboard_handle_interrupt(void);
char keyboard_get_char(void);

#endif