#include "keyboard.h"
#include "input.h"
#include "ps2.h"

#include <stdint.h>
#include "spinlock.h"

#define KEYBOARD_DATA_PORT 0x60

#define KEYBOARD_BUFFER_SIZE 256
#define KEYBOARD_EVENT_BUFFER_SIZE 32

#define SCANCODE_LEFT_SHIFT        0x2A
#define SCANCODE_RIGHT_SHIFT       0x36
#define SCANCODE_CAPS_LOCK         0x3A
#define SCANCODE_LEFT_CTRL         0x1D
#define SCANCODE_LEFT_ALT          0x38

#define SCANCODE_EXTENDED          0xE0
#define SCANCODE_LEFT_SUPER        0x5B
#define SCANCODE_RIGHT_SUPER       0x5C

#define SCANCODE_PAGE_UP           0x49
#define SCANCODE_PAGE_DOWN         0x51

#define SCANCODE_UP                0x48
#define SCANCODE_DOWN              0x50
#define SCANCODE_LEFT              0x4B
#define SCANCODE_RIGHT             0x4D

#define SCANCODE_HOME              0x47
#define SCANCODE_END               0x4F
#define SCANCODE_DELETE            0x53

static const char keyboard_map_lowercase[] =
{
    0,
    27,

    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '-', '=', '\b', '\t',

    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    'o', 'p', '[', ']', '\n',

    0,

    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l',
    ';', '\'', '`',

    0,

    '\\',

    'z', 'x', 'c', 'v', 'b', 'n', 'm',
    ',', '.', '/',

    0,

    '*',

    0,

    ' '
};

static const char keyboard_map_shifted[] =
{
    0,
    27,

    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
    '_', '+', '\b', '\t',

    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O', 'P', '{', '}', '\n',

    0,

    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L',
    ':', '"', '~',

    0,

    '|',

    'Z', 'X', 'C', 'V', 'B', 'N', 'M',
    '<', '>', '?',

    0,

    '*',

    0,

    ' '
};

static volatile char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static spinlock_t keyboard_lock = SPINLOCK_INITIALIZER;

static volatile unsigned int buffer_read = 0;
static volatile unsigned int buffer_write = 0;

static volatile keyboard_event_t event_buffer[
    KEYBOARD_EVENT_BUFFER_SIZE
];

static volatile unsigned int event_read = 0;
static volatile unsigned int event_write = 0;

static bool left_shift_pressed = false;
static bool right_shift_pressed = false;
static bool ctrl_pressed = false;
static bool alt_pressed = false;
static bool left_super_pressed = false;
static bool right_super_pressed = false;
static bool super_event_pending = false;
static bool caps_lock = false;

static bool extended_scancode = false;

static inline uint8_t keyboard_read_data(void)
{
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"((uint16_t)KEYBOARD_DATA_PORT)
    );

    return value;
}

static bool shift_pressed(void)
{
    return left_shift_pressed || right_shift_pressed;
}

static void keyboard_buffer_push(char c)
{
    unsigned int next =
        (buffer_write + 1) % KEYBOARD_BUFFER_SIZE;

    if (next == buffer_read)
        return;

    keyboard_buffer[buffer_write] = c;
    buffer_write = next;
}

static void keyboard_event_push(keyboard_event_t event)
{
    unsigned int next =
        (event_write + 1) % KEYBOARD_EVENT_BUFFER_SIZE;

    if (next == event_read)
        return;

    event_buffer[event_write] = event;
    event_write = next;
}

static char apply_ctrl(char c)
{
    if (c >= 'a' && c <= 'z')
        return (char)(c - 'a' + 1);

    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 1);

    return c;
}

static void handle_key_press(uint8_t scancode)
{
    if (scancode == SCANCODE_LEFT_SHIFT)
    {
        left_shift_pressed = true;
        return;
    }

    if (scancode == SCANCODE_RIGHT_SHIFT)
    {
        right_shift_pressed = true;
        return;
    }

    if (scancode == SCANCODE_LEFT_CTRL)
    {
        ctrl_pressed = true;
        return;
    }

    if (scancode == SCANCODE_LEFT_ALT)
    {
        alt_pressed = true;
        return;
    }

    if (scancode == SCANCODE_CAPS_LOCK)
    {
        caps_lock = !caps_lock;
        return;
    }

    /*
     * Extended Set 1 keys.
     */
    if (extended_scancode)
    {
        extended_scancode = false;

        switch (scancode)
        {
            case SCANCODE_LEFT_SUPER:
                if (!left_super_pressed)
                    super_event_pending = true;
                left_super_pressed = true;
                return;

            case SCANCODE_RIGHT_SUPER:
                if (!right_super_pressed)
                    super_event_pending = true;
                right_super_pressed = true;
                return;

            case SCANCODE_PAGE_UP:
                keyboard_event_push(KEY_EVENT_PAGE_UP);
                return;

            case SCANCODE_PAGE_DOWN:
                keyboard_event_push(KEY_EVENT_PAGE_DOWN);
                return;

            case SCANCODE_UP:
                keyboard_event_push(KEY_EVENT_UP);
                return;

            case SCANCODE_DOWN:
                keyboard_event_push(KEY_EVENT_DOWN);
                return;

            case SCANCODE_LEFT:
                keyboard_event_push(KEY_EVENT_LEFT);
                return;

            case SCANCODE_RIGHT:
                keyboard_event_push(KEY_EVENT_RIGHT);
                return;

            case SCANCODE_HOME:
                keyboard_event_push(KEY_EVENT_HOME);
                return;

            case SCANCODE_END:
                keyboard_event_push(KEY_EVENT_END);
                return;

            case SCANCODE_DELETE:
                keyboard_event_push(KEY_EVENT_DELETE);
                return;

            default:
                return;
        }
    }

    if (scancode >= sizeof(keyboard_map_lowercase))
        return;

    bool shift = shift_pressed();

    char normal =
        keyboard_map_lowercase[scancode];

    char shifted =
        keyboard_map_shifted[scancode];

    if (normal == 0)
        return;

    char c = normal;

    /*
     * Letters:
     *
     * Shift XOR Caps Lock determines capitalization.
     */
    if (normal >= 'a' && normal <= 'z')
    {
        if (shift != caps_lock)
            c = shifted;
    }
    else
    {
        /*
         * Numbers and punctuation only depend on Shift.
         */
        if (shift)
            c = shifted;
    }

    if (ctrl_pressed)
        c = apply_ctrl(c);

    (void)alt_pressed;

    keyboard_buffer_push(c);

    input_event_t text_event = {
        .device_id = ps2_input_keyboard_id(),
        .type = INPUT_EVENT_TEXT,
        .code = (unsigned char)c,
        .value = (unsigned char)c,
        .value2 = 0
    };
    (void)input_report_event(&text_event);
}

/* Canonical key codes are USB HID Keyboard/Keypad usage IDs on every bus. */
static uint16_t keyboard_scancode_to_usage(uint8_t code, bool extended)
{
    if (extended)
    {
        switch (code)
        {
            case 0x1C: return 0x58; /* Keypad Enter */
            case 0x1D: return 0xE4; /* Right Control */
            case 0x35: return 0x54; /* Keypad Divide */
            case 0x38: return 0xE6; /* Right Alt */
            case 0x47: return 0x4A; /* Home */
            case 0x48: return 0x52; /* Up */
            case 0x49: return 0x4B; /* Page Up */
            case 0x4B: return 0x50; /* Left */
            case 0x4D: return 0x4F; /* Right */
            case 0x4F: return 0x4D; /* End */
            case 0x50: return 0x51; /* Down */
            case 0x51: return 0x4E; /* Page Down */
            case 0x52: return 0x49; /* Insert */
            case 0x53: return 0x4C; /* Delete */
            case 0x5B: return 0xE3; /* Left GUI/Super */
            case 0x5C: return 0xE7; /* Right GUI/Super */
            default: return 0;
        }
    }

    switch (code)
    {
        case 0x01: return 0x29; /* Escape */
        case 0x02: return 0x1E; case 0x03: return 0x1F;
        case 0x04: return 0x20; case 0x05: return 0x21;
        case 0x06: return 0x22; case 0x07: return 0x23;
        case 0x08: return 0x24; case 0x09: return 0x25;
        case 0x0A: return 0x26; case 0x0B: return 0x27;
        case 0x0C: return 0x2D; case 0x0D: return 0x2E;
        case 0x0E: return 0x2A; case 0x0F: return 0x2B;
        case 0x10: return 0x14; case 0x11: return 0x1A;
        case 0x12: return 0x08; case 0x13: return 0x15;
        case 0x14: return 0x17; case 0x15: return 0x1C;
        case 0x16: return 0x18; case 0x17: return 0x0C;
        case 0x18: return 0x12; case 0x19: return 0x13;
        case 0x1A: return 0x2F; case 0x1B: return 0x30;
        case 0x1C: return 0x28; case 0x1D: return 0xE0;
        case 0x1E: return 0x04; case 0x1F: return 0x16;
        case 0x20: return 0x07; case 0x21: return 0x09;
        case 0x22: return 0x0A; case 0x23: return 0x0B;
        case 0x24: return 0x0D; case 0x25: return 0x0E;
        case 0x26: return 0x0F; case 0x27: return 0x33;
        case 0x28: return 0x34; case 0x29: return 0x35;
        case 0x2A: return 0xE1; case 0x2B: return 0x31;
        case 0x2C: return 0x1D; case 0x2D: return 0x1B;
        case 0x2E: return 0x06; case 0x2F: return 0x19;
        case 0x30: return 0x05; case 0x31: return 0x11;
        case 0x32: return 0x10; case 0x33: return 0x36;
        case 0x34: return 0x37; case 0x35: return 0x38;
        case 0x36: return 0xE5; case 0x37: return 0x55;
        case 0x38: return 0xE2; case 0x39: return 0x2C;
        case 0x3A: return 0x39;
        case 0x3B: return 0x3A; case 0x3C: return 0x3B;
        case 0x3D: return 0x3C; case 0x3E: return 0x3D;
        case 0x3F: return 0x3E; case 0x40: return 0x3F;
        case 0x41: return 0x40; case 0x42: return 0x41;
        case 0x43: return 0x42; case 0x44: return 0x43;
        case 0x57: return 0x44; case 0x58: return 0x45;
        case 0x45: return 0x53; case 0x46: return 0x47;
        case 0x47: return 0x5F; case 0x48: return 0x60;
        case 0x49: return 0x61; case 0x4A: return 0x56;
        case 0x4B: return 0x5C; case 0x4C: return 0x5D;
        case 0x4D: return 0x5E; case 0x4E: return 0x57;
        case 0x4F: return 0x59; case 0x50: return 0x5A;
        case 0x51: return 0x5B; case 0x52: return 0x62;
        case 0x53: return 0x63;
        default: return 0;
    }
}

static void handle_key_release(uint8_t scancode)
{
    switch (scancode)
    {
        case SCANCODE_LEFT_SHIFT:
            left_shift_pressed = false;
            break;

        case SCANCODE_RIGHT_SHIFT:
            right_shift_pressed = false;
            break;

        case SCANCODE_LEFT_CTRL:
            ctrl_pressed = false;
            break;

        case SCANCODE_LEFT_ALT:
            alt_pressed = false;
            break;

        default:
            break;
    }
}

void keyboard_initialize(void)
{
    spinlock_init(&keyboard_lock);
    buffer_read = 0;
    buffer_write = 0;

    event_read = 0;
    event_write = 0;

    left_shift_pressed = false;
    right_shift_pressed = false;

    ctrl_pressed = false;
    alt_pressed = false;
    left_super_pressed = false;
    right_super_pressed = false;
    super_event_pending = false;

    caps_lock = false;
    extended_scancode = false;
}

void keyboard_handle_interrupt(void)
{
    uint64_t flags = spinlock_lock_irqsave(&keyboard_lock);
    uint8_t scancode =
        keyboard_read_data();

    /*
     * 0xE0 starts an extended Set 1 scancode.
     */
    if (scancode == SCANCODE_EXTENDED)
    {
        extended_scancode = true;
        spinlock_unlock_irqrestore(&keyboard_lock, flags);
        return;
    }

    /*
     * Bit 7 indicates key release for
     * normal Set 1 scancodes.
     */
    if (scancode & 0x80)
    {
        uint8_t released_scancode =
            scancode & 0x7F;

        if (extended_scancode)
        {
            if (released_scancode == SCANCODE_LEFT_SUPER)
                left_super_pressed = false;
            else if (released_scancode == SCANCODE_RIGHT_SUPER)
                right_super_pressed = false;
        }
        else
        {
            handle_key_release(released_scancode);
        }

        uint16_t usage = keyboard_scancode_to_usage(released_scancode, extended_scancode);
        if (usage != 0)
        {
            input_event_t key_event = {
                .device_id = ps2_input_keyboard_id(),
                .type = INPUT_EVENT_KEY,
                .code = usage,
                .value = 0,
                .value2 = 0
            };
            (void)input_report_event(&key_event);
        }
        extended_scancode = false;
        spinlock_unlock_irqrestore(&keyboard_lock, flags);
        return;
    }

    bool was_extended = extended_scancode;
    handle_key_press(scancode);
    uint16_t usage = keyboard_scancode_to_usage(scancode, was_extended);
    if (usage != 0)
    {
        input_event_t key_event = {
            .device_id = ps2_input_keyboard_id(),
            .type = INPUT_EVENT_KEY,
            .code = usage,
            .value = 1,
            .value2 = 0
        };
        (void)input_report_event(&key_event);
    }
    spinlock_unlock_irqrestore(&keyboard_lock, flags);
}

bool keyboard_has_char(void)
{
    uint64_t flags = spinlock_lock_irqsave(&keyboard_lock);
    bool available = buffer_read != buffer_write;
    spinlock_unlock_irqrestore(&keyboard_lock, flags);
    return available;
}

char keyboard_get_char(void)
{
    uint64_t flags = spinlock_lock_irqsave(&keyboard_lock);
    if (buffer_read == buffer_write)
    {
        spinlock_unlock_irqrestore(&keyboard_lock, flags);
        return 0;
    }

    char c = keyboard_buffer[buffer_read];
    buffer_read = (buffer_read + 1) % KEYBOARD_BUFFER_SIZE;
    spinlock_unlock_irqrestore(&keyboard_lock, flags);
    return c;
}

bool keyboard_has_event(void)
{
    uint64_t flags = spinlock_lock_irqsave(&keyboard_lock);
    bool available = event_read != event_write;
    spinlock_unlock_irqrestore(&keyboard_lock, flags);
    return available;
}

keyboard_event_t keyboard_get_event(void)
{
    uint64_t flags = spinlock_lock_irqsave(&keyboard_lock);
    if (event_read == event_write)
    {
        spinlock_unlock_irqrestore(&keyboard_lock, flags);
        return KEY_EVENT_NONE;
    }

    keyboard_event_t event = event_buffer[event_read];
    event_read = (event_read + 1) % KEYBOARD_EVENT_BUFFER_SIZE;
    spinlock_unlock_irqrestore(&keyboard_lock, flags);
    return event;
}
bool keyboard_take_super_event(void)
{
    uint64_t flags = spinlock_lock_irqsave(&keyboard_lock);
    bool pending = super_event_pending;
    super_event_pending = false;
    spinlock_unlock_irqrestore(&keyboard_lock, flags);
    return pending;
}

bool keyboard_ctrl_pressed(void)
{
    uint64_t flags = spinlock_lock_irqsave(&keyboard_lock);
    bool pressed = ctrl_pressed;
    spinlock_unlock_irqrestore(&keyboard_lock, flags);
    return pressed;
}
