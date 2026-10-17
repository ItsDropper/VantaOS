#include "hid.h"
#include "input.h"
#include <stdbool.h>

/*
 * USB HID boot-protocol decoders. The xHCI/USB transport is responsible for
 * enumeration, selecting boot protocol, and delivering complete reports.
 * This module owns HID report state and publishes normalized input events.
 */
#define USB_HID_MAX_KEYBOARDS 8
#define USB_HID_MAX_MICE 8
#define USB_HID_KEYBOARD_REPORT_SIZE 8

typedef struct
{
    bool active;
    uint32_t device_id;
    uint8_t modifiers;
    uint8_t keys[6];
} usb_hid_keyboard_t;

typedef struct
{
    bool active;
    uint32_t device_id;
    uint8_t buttons;
} usb_hid_mouse_t;

static usb_hid_keyboard_t keyboards[USB_HID_MAX_KEYBOARDS];
static usb_hid_mouse_t mice[USB_HID_MAX_MICE];

static const char *const keyboard_names[USB_HID_MAX_KEYBOARDS] = {
    "USB HID keyboard 0", "USB HID keyboard 1",
    "USB HID keyboard 2", "USB HID keyboard 3",
    "USB HID keyboard 4", "USB HID keyboard 5",
    "USB HID keyboard 6", "USB HID keyboard 7"
};
static const char *const mouse_names[USB_HID_MAX_MICE] = {
    "USB HID mouse 0", "USB HID mouse 1",
    "USB HID mouse 2", "USB HID mouse 3",
    "USB HID mouse 4", "USB HID mouse 5",
    "USB HID mouse 6", "USB HID mouse 7"
};

static bool report_key(uint32_t device_id, uint16_t usage, bool pressed)
{
    input_event_t event = {
        .device_id = device_id,
        .type = INPUT_EVENT_KEY,
        .code = usage,
        .value = pressed ? 1 : 0,
        .value2 = 0
    };
    return input_report_event(&event);
}

int usb_hid_keyboard_attach(uint32_t *handle_out)
{
    for (uint32_t i = 0; i < USB_HID_MAX_KEYBOARDS; ++i)
    {
        if (keyboards[i].active)
            continue;

        input_device_descriptor_t descriptor = {
            .name = keyboard_names[i],
            .bus = INPUT_BUS_USB,
            .kind = INPUT_DEVICE_KEYBOARD,
            .capabilities = INPUT_CAP_KEYBOARD_KEYS
        };
        uint32_t id = 0;
        if (!input_register_device(&descriptor, &id))
            return 0;

        keyboards[i].active = true;
        keyboards[i].device_id = id;
        keyboards[i].modifiers = 0;
        for (uint32_t key = 0; key < 6; ++key)
            keyboards[i].keys[key] = 0;
        if (handle_out)
            *handle_out = i + 1;
        return 1;
    }
    return 0;
}

void usb_hid_keyboard_detach(uint32_t handle)
{
    if (handle == 0 || handle > USB_HID_MAX_KEYBOARDS)
        return;
    usb_hid_keyboard_t *keyboard = &keyboards[handle - 1];
    if (!keyboard->active)
        return;
    (void)input_unregister_device(keyboard->device_id);
    keyboard->active = false;
    keyboard->device_id = 0;
    keyboard->modifiers = 0;
}

void usb_hid_keyboard_report(uint32_t handle, const uint8_t *report, uint32_t length)
{
    if (handle == 0 || handle > USB_HID_MAX_KEYBOARDS ||
        !report || length < USB_HID_KEYBOARD_REPORT_SIZE)
        return;

    usb_hid_keyboard_t *keyboard = &keyboards[handle - 1];
    if (!keyboard->active)
        return;

    /* HID usages 1..3 indicate rollover/error, not actual key presses. */
    for (uint32_t key = 2; key < USB_HID_KEYBOARD_REPORT_SIZE; ++key)
        if (report[key] >= 1 && report[key] <= 3)
            return;

    uint8_t modifiers = report[0];
    for (uint32_t bit = 0; bit < 8; ++bit)
    {
        uint8_t mask = (uint8_t)(1U << bit);
        if ((modifiers & mask) != (keyboard->modifiers & mask))
            (void)report_key(keyboard->device_id, (uint16_t)(0xE0 + bit),
                             (modifiers & mask) != 0);
    }

    for (uint32_t old_index = 0; old_index < 6; ++old_index)
    {
        uint8_t usage = keyboard->keys[old_index];
        if (!usage)
            continue;
        bool still_down = false;
        for (uint32_t new_index = 2; new_index < 8; ++new_index)
            if (report[new_index] == usage)
                still_down = true;
        if (!still_down)
            (void)report_key(keyboard->device_id, usage, false);
    }

    for (uint32_t new_index = 2; new_index < 8; ++new_index)
    {
        uint8_t usage = report[new_index];
        if (!usage)
            continue;
        bool already_down = false;
        for (uint32_t old_index = 0; old_index < 6; ++old_index)
            if (keyboard->keys[old_index] == usage)
                already_down = true;
        if (!already_down)
            (void)report_key(keyboard->device_id, usage, true);
    }

    keyboard->modifiers = modifiers;
    for (uint32_t key = 0; key < 6; ++key)
        keyboard->keys[key] = report[key + 2];
}

int usb_hid_mouse_attach(uint32_t *handle_out)
{
    for (uint32_t i = 0; i < USB_HID_MAX_MICE; ++i)
    {
        if (mice[i].active)
            continue;

        input_device_descriptor_t descriptor = {
            .name = mouse_names[i],
            .bus = INPUT_BUS_USB,
            .kind = INPUT_DEVICE_MOUSE,
            .capabilities = INPUT_CAP_MOUSE_RELATIVE |
                            INPUT_CAP_MOUSE_BUTTONS |
                            INPUT_CAP_MOUSE_WHEEL
        };
        uint32_t id = 0;
        if (!input_register_device(&descriptor, &id))
            return 0;

        mice[i].active = true;
        mice[i].device_id = id;
        mice[i].buttons = 0;
        if (handle_out)
            *handle_out = i + 1;
        return 1;
    }
    return 0;
}

void usb_hid_mouse_detach(uint32_t handle)
{
    if (handle == 0 || handle > USB_HID_MAX_MICE)
        return;
    usb_hid_mouse_t *mouse = &mice[handle - 1];
    if (!mouse->active)
        return;
    (void)input_unregister_device(mouse->device_id);
    mouse->active = false;
    mouse->device_id = 0;
    mouse->buttons = 0;
}

void usb_hid_mouse_report(uint32_t handle, const uint8_t *report, uint32_t length)
{
    if (handle == 0 || handle > USB_HID_MAX_MICE ||
        !report || length < 3)
        return;

    usb_hid_mouse_t *mouse = &mice[handle - 1];
    if (!mouse->active)
        return;

    uint8_t buttons = report[0] & 0x1FU;
    for (uint32_t bit = 0; bit < 5; ++bit)
    {
        uint8_t mask = (uint8_t)(1U << bit);
        if ((buttons & mask) == (mouse->buttons & mask))
            continue;
        input_event_t event = {
            .device_id = mouse->device_id,
            .type = INPUT_EVENT_BUTTON,
            .code = (int32_t)bit,
            .value = (buttons & mask) ? 1 : 0,
            .value2 = 0
        };
        (void)input_report_event(&event);
    }
    mouse->buttons = buttons;

    int32_t dx = (int8_t)report[1];
    int32_t dy = (int8_t)report[2];
    if (dx || dy)
    {
        input_event_t motion = {
            .device_id = mouse->device_id,
            .type = INPUT_EVENT_RELATIVE_MOTION,
            .code = 0,
            .value = dx,
            .value2 = dy
        };
        (void)input_report_event(&motion);
    }

    if (length >= 4 && (int8_t)report[3] != 0)
    {
        input_event_t wheel = {
            .device_id = mouse->device_id,
            .type = INPUT_EVENT_WHEEL,
            .code = 0,
            .value = (int8_t)report[3],
            .value2 = 0
        };
        (void)input_report_event(&wheel);
    }
}
