/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "mouse.h"
#include "input.h"
#include "ps2.h"
#include "graphics.h"
#include "terminal.h"

#define PS2_DATA_PORT       0x60
#define PS2_STATUS_PORT     0x64
#define PS2_COMMAND_PORT    0x64

#define PS2_STATUS_OUTPUT_FULL 0x01
#define PS2_STATUS_INPUT_FULL  0x02

#define PS2_COMMAND_ENABLE_AUX 0xA8
#define PS2_COMMAND_READ_CONFIG 0x20
#define PS2_COMMAND_WRITE_CONFIG 0x60
#define PS2_COMMAND_WRITE_AUX   0xD4

#define MOUSE_CMD_SET_SAMPLE_RATE 0xF3
#define MOUSE_CMD_GET_ID          0xF2
#define MOUSE_CMD_ENABLE_REPORTING 0xF4

static unsigned char mouse_packet[4];
static unsigned int mouse_packet_index = 0;
static unsigned int mouse_packet_size = 3;

static volatile int mouse_wheel_delta = 0;
static volatile int mouse_moved = 0;
static volatile int mouse_clicked = 0;
static volatile int mouse_pending_dx = 0;
static volatile int mouse_pending_dy = 0;
static volatile int mouse_pending_press = 0;
static volatile int mouse_pending_release = 0;
static volatile int mouse_pending_right_press = 0;
static unsigned char mouse_left_down = 0;
static unsigned char mouse_reported_buttons = 0;
static unsigned char usb_mouse_buttons;
static unsigned char mouse_right_down = 0;
static uint32_t mouse_scale_x = 1024;
static uint32_t mouse_scale_y = 768;

static inline unsigned char ps2_read_status(void)
{
    unsigned char value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"((unsigned short)PS2_STATUS_PORT)
    );

    return value;
}

static inline unsigned char ps2_read_data(void)
{
    unsigned char value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"((unsigned short)PS2_DATA_PORT)
    );

    return value;
}

static inline void ps2_write_command(unsigned char command)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(command),
          "Nd"((unsigned short)PS2_COMMAND_PORT)
    );
}

static inline void ps2_write_data(unsigned char data)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(data),
          "Nd"((unsigned short)PS2_DATA_PORT)
    );
}

static void ps2_wait_input_clear(void)
{
    unsigned int timeout = 100000;

    while ((ps2_read_status() & PS2_STATUS_INPUT_FULL) && timeout--)
        __asm__ volatile ("pause");
}

static int ps2_wait_output_full(void)
{
    unsigned int timeout = 100000;

    while (!(ps2_read_status() & PS2_STATUS_OUTPUT_FULL) && timeout--)
        __asm__ volatile ("pause");

    return timeout != 0;
}

static void ps2_flush_output(void)
{
    unsigned int timeout = 32;

    while ((ps2_read_status() & PS2_STATUS_OUTPUT_FULL) && timeout--)
        (void)ps2_read_data();
}

static void mouse_write(unsigned char value)
{
    ps2_wait_input_clear();

    ps2_write_command(PS2_COMMAND_WRITE_AUX);

    ps2_wait_input_clear();

    ps2_write_data(value);
}

static unsigned char mouse_read_response(void)
{
    if (!ps2_wait_output_full())
        return 0xFF;

    return ps2_read_data();
}

static void mouse_set_sample_rate(unsigned char rate)
{
    mouse_write(MOUSE_CMD_SET_SAMPLE_RATE);
    (void)mouse_read_response();

    mouse_write(rate);
    (void)mouse_read_response();
}

static unsigned char mouse_get_id(void)
{
    mouse_write(MOUSE_CMD_GET_ID);

    (void)mouse_read_response();

    return mouse_read_response();
}

static void mouse_enable_reporting(void)
{
    mouse_write(MOUSE_CMD_ENABLE_REPORTING);
    (void)mouse_read_response();
}

static void mouse_enable_aux_irq(void)
{
    unsigned char config;

    ps2_wait_input_clear();
    ps2_write_command(PS2_COMMAND_ENABLE_AUX);

    ps2_wait_input_clear();
    ps2_write_command(PS2_COMMAND_READ_CONFIG);

    config = mouse_read_response();

    /*
     * Bit 1 = enable IRQ12.
     * Bit 5 = disable mouse clock.
     */
    config |= 0x02;
    config &= (unsigned char)~0x20;

    ps2_wait_input_clear();
    ps2_write_command(PS2_COMMAND_WRITE_CONFIG);

    ps2_wait_input_clear();
    ps2_write_data(config);
}

static unsigned char mouse_enable_wheel(void)
{
    /*
     * IntelliMouse detection sequence.
     *
     * 200 -> 100 -> 80 changes the mouse ID to 3
     * on standard PS/2 wheel mice.
     */
    mouse_set_sample_rate(200);
    mouse_set_sample_rate(100);
    mouse_set_sample_rate(80);

    return mouse_get_id();
}

void mouse_initialize(void)
{
    mouse_packet_index = 0;
    mouse_packet_size = 3;
    mouse_wheel_delta = 0;
    mouse_moved = 0;
    mouse_clicked = 0;
    mouse_pending_dx = 0;
    mouse_pending_dy = 0;
    mouse_pending_press = 0;
    mouse_pending_release = 0;
    mouse_pending_right_press = 0;
    mouse_left_down = 0;
    mouse_right_down = 0;
    mouse_reported_buttons = 0;
    usb_mouse_buttons = 0;
    mouse_scale_x = 1024;
    mouse_scale_y = 768;

    /*
     * Remove stale controller data before configuring
     * the mouse so it cannot interfere with the keyboard.
     */
    ps2_flush_output();

    mouse_enable_aux_irq();

    unsigned char mouse_id = mouse_enable_wheel();


    /*
     * ID 3 = IntelliMouse / wheel mouse.
     * ID 0 = standard 3-byte PS/2 mouse.
     *
     * QEMU's default PS/2 mouse is commonly ID 0, so do not
     * disable the mouse just because wheel support is absent.
     */
    mouse_packet_size = (mouse_id == 3) ? 4 : 3;
    mouse_enable_reporting();
}

void mouse_handle_interrupt(void)
{
    unsigned char status =
        ps2_read_status();

    if (!(status & PS2_STATUS_OUTPUT_FULL))
        return;

    /*
     * Status bit 5 means the byte came from
     * the auxiliary PS/2 device rather than
     * the keyboard.
     */
    if (!(status & 0x20))
        return;

    unsigned char value =
        ps2_read_data();

    /*
     * The first byte of a PS/2 mouse packet
     * always has bit 3 set.
     */
    if (mouse_packet_index == 0)
    {
        if (!(value & 0x08))
            return;
    }

    mouse_packet[mouse_packet_index] = value;
    mouse_packet_index++;

    if (mouse_packet_index < mouse_packet_size)
        return;

    mouse_packet_index = 0;

    /*
     * IntelliMouse wheel is a signed 4-bit value.
     *
     * 0x01 = +1
     * 0x0F = -1
     * 0x0E = -2
     * etc.
     */
    /*
     * PS/2 X/Y movement is a signed 9-bit value. The high sign
     * bits live in the first packet byte, so treating bytes 1/2
     * as signed 8-bit values causes large jumps and cursor drift.
     */
    int delta_x = (int)mouse_packet[1];
    int delta_y = (int)mouse_packet[2];

    if (mouse_packet[0] & 0x10)
        delta_x -= 0x100;

    if (mouse_packet[0] & 0x20)
        delta_y -= 0x100;

    if (!(mouse_packet[0] & 0x40) &&
        !(mouse_packet[0] & 0x80))
    {
        if (delta_x != 0 || delta_y != 0)
        {
            int scaled_x = (delta_x * (int)mouse_scale_x) / 1024;
            int scaled_y = (delta_y * (int)mouse_scale_y) / 768;

            if (scaled_x == 0 && delta_x != 0) scaled_x = delta_x > 0 ? 1 : -1;
            if (scaled_y == 0 && delta_y != 0) scaled_y = delta_y > 0 ? 1 : -1;

            __atomic_fetch_add(&mouse_pending_dx, scaled_x, __ATOMIC_RELAXED);
            __atomic_fetch_add(&mouse_pending_dy, scaled_y, __ATOMIC_RELAXED);
            __atomic_store_n(&mouse_moved, 1, __ATOMIC_RELEASE);
        }
    }

    unsigned char left_down =
        mouse_packet[0] & 0x01;

    if (left_down && !mouse_left_down)
    {
        __atomic_store_n(&mouse_pending_press, 1, __ATOMIC_RELEASE);
        __atomic_store_n(&mouse_clicked, 1, __ATOMIC_RELEASE);
    }

    if (!left_down && mouse_left_down)
        __atomic_store_n(&mouse_pending_release, 1, __ATOMIC_RELEASE);
    mouse_left_down = left_down;

    unsigned char right_down = (mouse_packet[0] & 0x02U) != 0U;
    if (right_down && !mouse_right_down)
    {
        __atomic_store_n(&mouse_pending_right_press, 1, __ATOMIC_RELEASE);
        __atomic_store_n(&mouse_clicked, 1, __ATOMIC_RELEASE);
    }
    mouse_right_down = right_down;

    unsigned char buttons = (unsigned char)(mouse_packet[0] & 0x07U);
    unsigned char changed_buttons = (unsigned char)(buttons ^ mouse_reported_buttons);
    for (int button = 0; button < 3; ++button)
    {
        unsigned char mask = (unsigned char)(1U << button);
        if (!(changed_buttons & mask))
            continue;
        input_event_t button_event = {
            .device_id = ps2_input_mouse_id(),
            .type = INPUT_EVENT_BUTTON,
            .code = button,
            .value = (buttons & mask) ? 1 : 0,
            .value2 = 0
        };
        (void)input_report_event(&button_event);
    }
    mouse_reported_buttons = buttons;

    if (delta_x != 0 || delta_y != 0)
    {
        input_event_t motion_event = {
            .device_id = ps2_input_mouse_id(),
            .type = INPUT_EVENT_RELATIVE_MOTION,
            .code = 0,
            .value = delta_x,
            .value2 = delta_y
        };
        (void)input_report_event(&motion_event);
    }

    if (mouse_packet_size == 4)
    {
        int wheel = (int)(mouse_packet[3] & 0x0F);
        if (wheel & 0x08)
            wheel -= 16;
        if (wheel != 0)
        {
            __atomic_fetch_add(&mouse_wheel_delta, wheel, __ATOMIC_RELAXED);
            input_event_t wheel_event = {
                .device_id = ps2_input_mouse_id(),
                .type = INPUT_EVENT_WHEEL,
                .code = 0,
                .value = wheel,
                .value2 = 0
            };
            (void)input_report_event(&wheel_event);
        }
    }
}


void mouse_inject_usb_report(int delta_x, int delta_y,
                             unsigned char buttons, int wheel)
{
    int scaled_x = (delta_x * (int)mouse_scale_x) / 1024;
    int scaled_y = (delta_y * (int)mouse_scale_y) / 768;
    if (scaled_x == 0 && delta_x != 0)
        scaled_x = delta_x > 0 ? 1 : -1;
    if (scaled_y == 0 && delta_y != 0)
        scaled_y = delta_y > 0 ? 1 : -1;
    if (scaled_x != 0 || scaled_y != 0) {
        __atomic_fetch_add(&mouse_pending_dx, scaled_x, __ATOMIC_RELAXED);
        __atomic_fetch_add(&mouse_pending_dy, scaled_y, __ATOMIC_RELAXED);
        __atomic_store_n(&mouse_moved, 1, __ATOMIC_RELEASE);
    }

    unsigned char left = buttons & 1U;
    unsigned char right = buttons & 2U;
    if (left && !(usb_mouse_buttons & 1U)) {
        __atomic_store_n(&mouse_pending_press, 1, __ATOMIC_RELEASE);
        __atomic_store_n(&mouse_clicked, 1, __ATOMIC_RELEASE);
    } else if (!left && (usb_mouse_buttons & 1U)) {
        __atomic_store_n(&mouse_pending_release, 1, __ATOMIC_RELEASE);
    }
    if (right && !(usb_mouse_buttons & 2U)) {
        __atomic_store_n(&mouse_pending_right_press, 1, __ATOMIC_RELEASE);
        __atomic_store_n(&mouse_clicked, 1, __ATOMIC_RELEASE);
    }
    usb_mouse_buttons = buttons;
    if (wheel != 0)
        __atomic_fetch_add(&mouse_wheel_delta, wheel, __ATOMIC_RELAXED);
}

bool mouse_has_event(void)
{
    return __atomic_load_n(&mouse_wheel_delta, __ATOMIC_ACQUIRE) != 0 ||
           __atomic_load_n(&mouse_moved, __ATOMIC_ACQUIRE) != 0 ||
           __atomic_load_n(&mouse_clicked, __ATOMIC_ACQUIRE) != 0;
}

bool mouse_has_click_event(void)
{
    return __atomic_load_n(&mouse_clicked, __ATOMIC_ACQUIRE) != 0;
}

bool mouse_has_move_event(void)
{
    return __atomic_load_n(&mouse_moved, __ATOMIC_ACQUIRE) != 0;
}

void mouse_clear_event_flags(void)
{
    __atomic_store_n(&mouse_moved, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&mouse_clicked, 0, __ATOMIC_RELEASE);
}

bool mouse_has_wheel_event(void)
{
    return __atomic_load_n(&mouse_wheel_delta, __ATOMIC_ACQUIRE) != 0;
}

int mouse_get_wheel_delta(void)
{
    return __atomic_exchange_n(&mouse_wheel_delta, 0, __ATOMIC_ACQ_REL);
}

void mouse_process_events(void)
{
    /*
     * The PS/2 IRQ producer and desktop consumer may run on different CPUs.
     * Atomic exchanges prevent lost deltas and button edges while draining
     * the event queue, which otherwise makes pointer motion feel sticky.
     */
    int dx = __atomic_exchange_n(&mouse_pending_dx, 0, __ATOMIC_ACQ_REL);
    int dy = __atomic_exchange_n(&mouse_pending_dy, 0, __ATOMIC_ACQ_REL);
    int press = __atomic_exchange_n(&mouse_pending_press, 0, __ATOMIC_ACQ_REL);
    int release = __atomic_exchange_n(&mouse_pending_release, 0, __ATOMIC_ACQ_REL);
    int right_press = __atomic_exchange_n(&mouse_pending_right_press, 0, __ATOMIC_ACQ_REL);

    if (dx != 0 || dy != 0)
        graphics_mouse_move(dx, dy);

    if (press)
        graphics_mouse_click(1);
    if (right_press)
        graphics_mouse_click(3);

    if (release)
        graphics_mouse_release(1);
}

void mouse_set_resolution_scale(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return;

    mouse_scale_x = width;
    mouse_scale_y = height;
}
