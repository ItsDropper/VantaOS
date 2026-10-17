#include "input.h"

/* Static names outlive their registrations. "Online" means initialized in
 * the kernel; the current legacy drivers do not perform presence probing. */
static const input_device_descriptor_t ps2_keyboard = {
    .name = "PS/2 keyboard", .bus = INPUT_BUS_PS2, .kind = INPUT_DEVICE_KEYBOARD,
    .capabilities = INPUT_CAP_KEYBOARD_TEXT | INPUT_CAP_KEYBOARD_KEYS
};
static const input_device_descriptor_t ps2_mouse = {
    .name = "PS/2 mouse", .bus = INPUT_BUS_PS2, .kind = INPUT_DEVICE_MOUSE,
    .capabilities = INPUT_CAP_MOUSE_RELATIVE | INPUT_CAP_MOUSE_BUTTONS | INPUT_CAP_MOUSE_WHEEL
};

void input_register_ps2_devices(void)
{
    (void)input_register_device(&ps2_keyboard, 0);
    (void)input_register_device(&ps2_mouse, 0);
}
