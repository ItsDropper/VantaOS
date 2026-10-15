#include "keyboard.h"

#include <stdint.h>

#define KEYBOARD_DATA_PORT 0x60

#define KEYBOARD_BUFFER_SIZE 256
#define KEYBOARD_EVENT_BUFFER_SIZE 32

#define SCANCODE_LEFT_SHIFT        0x2A
#define SCANCODE_RIGHT_SHIFT       0x36
#define SCANCODE_CAPS_LOCK         0x3A
#define SCANCODE_LEFT_CTRL         0x1D
#define SCANCODE_LEFT_ALT          0x38

#define SCANCODE_EXTENDED          0xE0

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
    buffer_read = 0;
    buffer_write = 0;

    event_read = 0;
    event_write = 0;

    left_shift_pressed = false;
    right_shift_pressed = false;

    ctrl_pressed = false;
    alt_pressed = false;

    caps_lock = false;
    extended_scancode = false;
}

void keyboard_handle_interrupt(void)
{
    uint8_t scancode =
        keyboard_read_data();

    /*
     * 0xE0 starts an extended Set 1 scancode.
     */
    if (scancode == SCANCODE_EXTENDED)
    {
        extended_scancode = true;
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

        handle_key_release(
            released_scancode
        );

        extended_scancode = false;

        return;
    }

    handle_key_press(scancode);
}

bool keyboard_has_char(void)
{
    return buffer_read != buffer_write;
}

char keyboard_get_char(void)
{
    if (!keyboard_has_char())
        return 0;

    char c =
        keyboard_buffer[buffer_read];

    buffer_read =
        (buffer_read + 1) % KEYBOARD_BUFFER_SIZE;

    return c;
}

bool keyboard_has_event(void)
{
    return event_read != event_write;
}

keyboard_event_t keyboard_get_event(void)
{
    if (!keyboard_has_event())
        return KEY_EVENT_NONE;

    keyboard_event_t event =
        event_buffer[event_read];

    event_read =
        (event_read + 1) % KEYBOARD_EVENT_BUFFER_SIZE;

    return event;
}