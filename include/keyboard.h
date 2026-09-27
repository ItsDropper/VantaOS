#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdbool.h>

typedef enum
{
    KEY_EVENT_NONE = 0,

    KEY_EVENT_PAGE_UP,
    KEY_EVENT_PAGE_DOWN,

    KEY_EVENT_UP,
    KEY_EVENT_DOWN,
    KEY_EVENT_LEFT,
    KEY_EVENT_RIGHT,

    KEY_EVENT_HOME,
    KEY_EVENT_END,
    KEY_EVENT_DELETE,
    KEY_EVENT_TERMINAL
} keyboard_event_t;

/*
 * Initialize the keyboard driver and reset its state.
 */
void keyboard_initialize(void);

/*
 * Called by IRQ1 when the keyboard sends a scancode.
 */
void keyboard_handle_interrupt(void);

/*
 * Check whether a character is waiting in the keyboard buffer.
 */
bool keyboard_has_char(void);

/*
 * Get the next character from the keyboard buffer.
 *
 * Returns 0 if the buffer is empty.
 */
char keyboard_get_char(void);

/*
 * Check whether a special keyboard event is waiting.
 */
bool keyboard_has_event(void);

/*
 * Get the next special keyboard event.
 *
 * Returns KEY_EVENT_NONE if the event queue is empty.
 */
keyboard_event_t keyboard_get_event(void);

#endif