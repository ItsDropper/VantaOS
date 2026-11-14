/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "input.h"
#include "ps2.h"
#include "keyboard.h"
#include "mouse.h"
#include "serial.h"
#include "spinlock.h"

#include <stdbool.h>
#include <stdint.h>

#define PS2_DATA_PORT       0x60U
#define PS2_STATUS_PORT     0x64U
#define PS2_COMMAND_PORT    0x64U
#define PS2_STATUS_OUTPUT   0x01U
#define PS2_STATUS_INPUT    0x02U
#define PS2_STATUS_AUX      0x20U
#define PS2_COMMAND_DISABLE_KEYBOARD 0xADU
#define PS2_COMMAND_DISABLE_AUX      0xA7U
#define PS2_COMMAND_ENABLE_KEYBOARD  0xAEU
#define PS2_COMMAND_READ_CONFIG      0x20U
#define PS2_COMMAND_WRITE_CONFIG     0x60U
#define PS2_WAIT_LIMIT               100000U
#define PS2_KEYBOARD_ACK             0xFAU
#define PS2_KEYBOARD_RESEND          0xFEU
#define PS2_KEYBOARD_SET_DEFAULTS    0xF6U
#define PS2_KEYBOARD_ENABLE_SCANNING 0xF4U
#define PS2_POLL_BUDGET              16U

static uint32_t keyboard_device_id;
static uint32_t mouse_device_id;
static bool controller_initialized;
static bool controller_ready;
static bool keyboard_scanning;

/*
 * Serializes the status-check/data-read pair. The 8042 has a single output
 * buffer shared by the keyboard and auxiliary device, so an IRQ1 handler and
 * a polling caller (possibly on another CPU) must not interleave.
 */
spinlock_t ps2_io_lock = SPINLOCK_INITIALIZER;

static const input_device_descriptor_t ps2_keyboard = {
    .name = "PS/2 keyboard", .bus = INPUT_BUS_PS2,
    .kind = INPUT_DEVICE_KEYBOARD,
    .capabilities = INPUT_CAP_KEYBOARD_TEXT | INPUT_CAP_KEYBOARD_KEYS
};
static const input_device_descriptor_t ps2_mouse = {
    .name = "PS/2 mouse", .bus = INPUT_BUS_PS2,
    .kind = INPUT_DEVICE_MOUSE,
    .capabilities = INPUT_CAP_MOUSE_RELATIVE | INPUT_CAP_MOUSE_BUTTONS |
                    INPUT_CAP_MOUSE_WHEEL
};

static uint8_t ps2_in8(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void ps2_out8(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static bool ps2_wait_input_empty(void)
{
    for (uint32_t spin = 0U; spin < PS2_WAIT_LIMIT; ++spin)
    {
        uint8_t status = ps2_in8(PS2_STATUS_PORT);
        if (status == 0xFFU)
            return false;
        if ((status & PS2_STATUS_INPUT) == 0U)
            return true;
        __asm__ volatile ("pause" ::: "memory");
    }
    return false;
}

static bool ps2_wait_output_full(void)
{
    for (uint32_t spin = 0U; spin < PS2_WAIT_LIMIT; ++spin)
    {
        uint8_t status = ps2_in8(PS2_STATUS_PORT);
        if (status == 0xFFU)
            return false;
        if ((status & PS2_STATUS_OUTPUT) != 0U)
            return true;
        __asm__ volatile ("pause" ::: "memory");
    }
    return false;
}

static bool ps2_command(uint8_t command)
{
    if (!ps2_wait_input_empty())
        return false;
    ps2_out8(PS2_COMMAND_PORT, command);
    return true;
}

static bool ps2_write_config(uint8_t config)
{
    if (!ps2_command(PS2_COMMAND_WRITE_CONFIG) ||
        !ps2_wait_input_empty())
        return false;
    ps2_out8(PS2_DATA_PORT, config);
    return true;
}

/* Send one byte to the keyboard and wait for its ACK, retrying on RESEND. */
static bool ps2_keyboard_command(uint8_t value)
{
    for (uint32_t attempt = 0U; attempt < 3U; ++attempt)
    {
        if (!ps2_wait_input_empty())
            return false;
        ps2_out8(PS2_DATA_PORT, value);
        for (uint32_t reads = 0U; reads < 8U; ++reads)
        {
            if (!ps2_wait_output_full())
                return false;
            uint8_t reply = ps2_in8(PS2_DATA_PORT);
            if (reply == PS2_KEYBOARD_ACK)
                return true;
            if (reply == PS2_KEYBOARD_RESEND)
                break;
            /* Anything else (for example a late self-test 0xAA) is skipped. */
        }
    }
    return false;
}

bool ps2_controller_initialize(void)
{
    if (controller_initialized)
        return controller_ready;
    controller_initialized = true;
    controller_ready = false;

    uint8_t status = ps2_in8(PS2_STATUS_PORT);
    if (status == 0xFFU)
    {
        serial_write("VANTAOS_PS2_CONTROLLER_ABSENT\n");
        return false;
    }

    /* Stop both ports while changing the shared controller configuration. */
    if (!ps2_command(PS2_COMMAND_DISABLE_KEYBOARD) ||
        !ps2_command(PS2_COMMAND_DISABLE_AUX))
        goto timeout;

    /* Drain stale bytes, but never let a broken controller spin forever. */
    for (uint32_t count = 0U; count < 32U; ++count)
    {
        status = ps2_in8(PS2_STATUS_PORT);
        if (status == 0xFFU)
            goto timeout;
        if ((status & PS2_STATUS_OUTPUT) == 0U)
            break;
        (void)ps2_in8(PS2_DATA_PORT);
    }

    if (!ps2_command(PS2_COMMAND_READ_CONFIG) ||
        !ps2_wait_output_full())
        goto timeout;

    uint8_t config = ps2_in8(PS2_DATA_PORT);

    /*
     * IRQ1 enabled, IRQ12 and the auxiliary clock left disabled until the mouse
     * driver requests them. Enable the first-port clock and translation: the keyboard
     * decoder consumes translated Set-1 scancodes.
     */
    config = (uint8_t)((config | 0x61U) & (uint8_t)~0x12U);
    if (!ps2_write_config((uint8_t)(config & (uint8_t)~0x01U)))
        goto timeout;
    if (!ps2_command(PS2_COMMAND_ENABLE_KEYBOARD))
        goto timeout;

    /*
     * Firmware or a previous OS may have left the keyboard with scanning
     * disabled or in a different scancode set; enabling the port alone then
     * produces no key events. IRQ1 is still disabled here, so replies are read
     * by polling without racing an interrupt handler.
     */
    for (uint32_t count = 0U; count < 32U; ++count)
    {
        status = ps2_in8(PS2_STATUS_PORT);
        if (status == 0xFFU || (status & PS2_STATUS_OUTPUT) == 0U)
            break;
        (void)ps2_in8(PS2_DATA_PORT);
    }
    keyboard_scanning = ps2_keyboard_command(PS2_KEYBOARD_SET_DEFAULTS) &&
                        ps2_keyboard_command(PS2_KEYBOARD_ENABLE_SCANNING);
    serial_write(keyboard_scanning ?
                 "VANTAOS_PS2_KEYBOARD_SCANNING_ENABLED\n" :
                 "VANTAOS_PS2_KEYBOARD_NO_ACK\n");

    if (!ps2_write_config(config))
        goto timeout;

    status = ps2_in8(PS2_STATUS_PORT);
    if (status == 0xFFU)
        goto timeout;

    controller_ready = true;
    serial_write("VANTAOS_PS2_CONTROLLER_READY keyboard_clock=on irq1=on translation=set1\n");
    return true;

timeout:
    serial_write("VANTAOS_PS2_CONTROLLER_INIT_TIMEOUT\n");
    return false;
}

bool ps2_controller_is_ready(void)
{
    return controller_ready;
}

bool ps2_keyboard_is_scanning(void)
{
    return keyboard_scanning;
}

static uint32_t poll_serviced_bytes;

uint32_t ps2_poll_serviced_bytes(void)
{
    return __atomic_load_n(&poll_serviced_bytes, __ATOMIC_RELAXED);
}

/*
 * Drain pending 8042 output without relying on IRQ1/IRQ12 delivery. The
 * desktop loop calls this every pass, so input keeps working if the
 * interrupt route is wrong on a particular machine, and a stuck output byte
 * can never block the keyboard behind an unserviced auxiliary byte.
 */
void ps2_poll_input(void)
{
    if (!controller_ready)
        return;
    for (uint32_t i = 0U; i < PS2_POLL_BUDGET; ++i)
    {
        uint8_t status = ps2_in8(PS2_STATUS_PORT);
        if (status == 0xFFU || (status & PS2_STATUS_OUTPUT) == 0U)
            return;
        __atomic_add_fetch(&poll_serviced_bytes, 1U, __ATOMIC_RELAXED);
        if ((status & PS2_STATUS_AUX) != 0U)
            mouse_handle_interrupt();
        else
            keyboard_handle_interrupt();
    }
}

/* Static names outlive their registrations. Only expose devices when the
 * controller has passed its bounded initialization sequence. */
void input_register_ps2_devices(void)
{
    if (!controller_ready)
    {
        serial_write("VANTAOS_PS2_INPUT_UNAVAILABLE\n");
        return;
    }

    if (!input_register_device(&ps2_keyboard, &keyboard_device_id))
    {
        input_device_info_t existing;
        keyboard_device_id = 0U;
        for (uint32_t i = 0U; i < input_device_count(); ++i)
            if (input_get_device(i, &existing) &&
                existing.bus == INPUT_BUS_PS2 &&
                existing.kind == INPUT_DEVICE_KEYBOARD)
                keyboard_device_id = existing.id;
    }
    if (!input_register_device(&ps2_mouse, &mouse_device_id))
    {
        input_device_info_t existing;
        mouse_device_id = 0U;
        for (uint32_t i = 0U; i < input_device_count(); ++i)
            if (input_get_device(i, &existing) &&
                existing.bus == INPUT_BUS_PS2 &&
                existing.kind == INPUT_DEVICE_MOUSE)
                mouse_device_id = existing.id;
    }

    serial_write(keyboard_device_id != 0U ?
                 "VANTAOS_PS2_KEYBOARD_REGISTERED\n" :
                 "VANTAOS_PS2_KEYBOARD_REGISTER_FAILED\n");
}

uint32_t ps2_input_keyboard_id(void) { return keyboard_device_id; }
uint32_t ps2_input_mouse_id(void) { return mouse_device_id; }
