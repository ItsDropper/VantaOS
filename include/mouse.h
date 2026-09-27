#ifndef MOUSE_H
#define MOUSE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Initialize the PS/2 mouse and enable wheel support
 * when the hardware supports the IntelliMouse protocol.
 */
void mouse_initialize(void);

/*
 * Called by IRQ12 when the mouse sends a byte.
 */
void mouse_handle_interrupt(void);

/*
 * Check whether the mouse has accumulated wheel movement.
 */
bool mouse_has_event(void);
bool mouse_has_wheel_event(void);
bool mouse_has_click_event(void);
bool mouse_has_move_event(void);
void mouse_clear_event_flags(void);
void mouse_set_resolution_scale(uint32_t width, uint32_t height);

/*
 * Get and consume accumulated wheel movement.
 *
 * Positive values mean scroll up.
 * Negative values mean scroll down.
 */
int mouse_get_wheel_delta(void);

#endif