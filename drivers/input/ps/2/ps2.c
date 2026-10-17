#include "input.h"

/* Static names outlive their registrations. "Online" means initialized in
 * the kernel; the current legacy drivers do not perform presence probing. */
static uint32_t keyboard_device_id;
static uint32_t mouse_device_id;

static const input_device_descriptor_t ps2_keyboard =
    .name = "PS/2 keyboard", .bus = INPUT_BUS_PS2, .kind = INPUT_DEVICE_KEYBOARD,
    .capabilities = INPUT_CAP_KEYBOARD_TEXT | INPUT_CAP_KEYBOARD_KEYS
};
static const input_device_descriptor_t ps2_mouse = {
    .name = "PS/2 mouse", .bus = INPUT_BUS_PS2, .kind = INPUT_DEVICE_MOUSE,
    .capabilities = INPUT_CAP_MOUSE_RELATIVE | INPUT_CAP_MOUSE_BUTTONS | INPUT_CAP_MOUSE_WHEEL
};

void input_register_ps2_devices(void)
{
    if (!input_register_device(&ps2_keyboard, &keyboard_device_id))
    {
        input_device_info_t existing;
        keyboard_device_id = 0;
        for (uint32_t i = 0; i < input_device_count(); ++i)
            if (input_get_device(i, &existing) && existing.bus == INPUT_BUS_PS2 &&
                existing.kind == INPUT_DEVICE_KEYBOARD)
                keyboard_device_id = existing.id;
    }
    if (!input_register_device(&ps2_mouse, &mouse_device_id))
    {
        input_device_info_t existing;
        mouse_device_id = 0;
        for (uint32_t i = 0; i < input_device_count(); ++i)
            if (input_get_device(i, &existing) && existing.bus == INPUT_BUS_PS2 &&
                existing.kind == INPUT_DEVICE_MOUSE)
                mouse_device_id = existing.id;
    }
}

uint32_t ps2_input_keyboard_id(void) { return keyboard_device_id; }
uint32_t ps2_input_mouse_id(void) { return mouse_device_id; }
