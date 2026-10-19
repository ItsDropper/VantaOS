/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

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
    KEY_EVENT_DELETE
} keyboard_event_t;

/*
 * Initialize the keyboard driver and reset its state.
 */
void keyboard_initialize(void);

/*
 * Called by IRQ1 when the keyboard sends a scancode.
 */
void keyboard_handle_interrupt(void);

/* Inject a normalized USB HID Keyboard/Keypad usage transition. */
void keyboard_inject_hid_usage(uint16_t usage, bool pressed);

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
bool keyboard_take_super_event(void);
bool keyboard_ctrl_pressed(void);

/*
 * Get the next special keyboard event.
 *
 * Returns KEY_EVENT_NONE if the event queue is empty.
 */
keyboard_event_t keyboard_get_event(void);

#endif