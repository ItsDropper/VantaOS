#include "input.h"

#include "spinlock.h"

typedef struct
{
    bool occupied;
    input_device_info_t info;
} input_device_slot_t;

static input_device_slot_t devices[INPUT_MAX_DEVICES];
static input_event_t events[INPUT_EVENT_QUEUE_CAPACITY];
static uint32_t next_device_id = 1;
static uint32_t device_count;
static uint32_t event_read;
static uint32_t event_write;
static uint32_t event_count;
static uint32_t dropped_events;
static bool initialized;
static spinlock_t input_lock = SPINLOCK_INITIALIZER;

static bool input_names_equal(const char *left, const char *right)
{
    if (!left || !right)
        return false;

    while (*left && *right && *left == *right)
    {
        ++left;
        ++right;
    }

    return *left == *right;
}

static bool input_descriptor_valid(const input_device_descriptor_t *descriptor)
{
    const uint32_t keyboard_caps =
        INPUT_CAP_KEYBOARD_TEXT | INPUT_CAP_KEYBOARD_KEYS;
    const uint32_t mouse_caps =
        INPUT_CAP_MOUSE_RELATIVE | INPUT_CAP_MOUSE_BUTTONS | INPUT_CAP_MOUSE_WHEEL;
    const uint32_t known_caps = keyboard_caps | mouse_caps;

    if (!descriptor || !descriptor->name || descriptor->name[0] == '\0')
        return false;
    if (descriptor->bus != INPUT_BUS_PS2 && descriptor->bus != INPUT_BUS_USB)
        return false;
    if (descriptor->capabilities == 0 ||
        (descriptor->capabilities & ~known_caps) != 0)
        return false;

    if (descriptor->kind == INPUT_DEVICE_KEYBOARD)
        return (descriptor->capabilities & ~keyboard_caps) == 0;
    if (descriptor->kind == INPUT_DEVICE_MOUSE)
        return (descriptor->capabilities & ~mouse_caps) == 0;

    return false;
}

static uint32_t input_required_capability(input_event_type_t type)
{
    switch (type)
    {
        case INPUT_EVENT_KEY:
            return INPUT_CAP_KEYBOARD_KEYS;
        case INPUT_EVENT_TEXT:
            return INPUT_CAP_KEYBOARD_TEXT;
        case INPUT_EVENT_RELATIVE_MOTION:
            return INPUT_CAP_MOUSE_RELATIVE;
        case INPUT_EVENT_BUTTON:
            return INPUT_CAP_MOUSE_BUTTONS;
        case INPUT_EVENT_WHEEL:
            return INPUT_CAP_MOUSE_WHEEL;
        default:
            return 0;
    }
}

void input_initialize(void)
{
    uint64_t flags = spinlock_lock_irqsave(&input_lock);

    for (uint32_t i = 0; i < INPUT_MAX_DEVICES; ++i)
    {
        devices[i].occupied = false;
        devices[i].info.id = 0;
        devices[i].info.name = 0;
        devices[i].info.bus = INPUT_BUS_NONE;
        devices[i].info.kind = INPUT_DEVICE_NONE;
        devices[i].info.capabilities = 0;
        devices[i].info.online = false;
    }

    next_device_id = 1;
    device_count = 0;
    event_read = 0;
    event_write = 0;
    event_count = 0;
    dropped_events = 0;
    initialized = true;

    spinlock_unlock_irqrestore(&input_lock, flags);
}

bool input_register_device(const input_device_descriptor_t *descriptor,
                           uint32_t *device_id_out)
{
    if (!input_descriptor_valid(descriptor))
        return false;

    uint64_t flags = spinlock_lock_irqsave(&input_lock);
    if (!initialized)
    {
        spinlock_unlock_irqrestore(&input_lock, flags);
        return false;
    }

    uint32_t free_slot = INPUT_MAX_DEVICES;
    for (uint32_t i = 0; i < INPUT_MAX_DEVICES; ++i)
    {
        if (!devices[i].occupied)
        {
            if (free_slot == INPUT_MAX_DEVICES)
                free_slot = i;
            continue;
        }

        const input_device_info_t *current = &devices[i].info;
        if (current->bus == descriptor->bus &&
            current->kind == descriptor->kind &&
            input_names_equal(current->name, descriptor->name))
        {
            if (device_id_out)
                *device_id_out = current->id;
            spinlock_unlock_irqrestore(&input_lock, flags);
            return false;
        }
    }

    if (free_slot == INPUT_MAX_DEVICES || next_device_id == 0)
    {
        spinlock_unlock_irqrestore(&input_lock, flags);
        return false;
    }

    input_device_info_t *info = &devices[free_slot].info;
    info->id = next_device_id++;
    info->name = descriptor->name;
    info->bus = descriptor->bus;
    info->kind = descriptor->kind;
    info->capabilities = descriptor->capabilities;
    info->online = true;
    devices[free_slot].occupied = true;
    ++device_count;

    if (device_id_out)
        *device_id_out = info->id;

    spinlock_unlock_irqrestore(&input_lock, flags);
    return true;
}

bool input_unregister_device(uint32_t device_id)
{
    if (device_id == 0)
        return false;

    uint64_t flags = spinlock_lock_irqsave(&input_lock);
    uint32_t slot = INPUT_MAX_DEVICES;

    for (uint32_t i = 0; i < INPUT_MAX_DEVICES; ++i)
    {
        if (devices[i].occupied && devices[i].info.id == device_id)
        {
            slot = i;
            break;
        }
    }

    if (slot == INPUT_MAX_DEVICES)
    {
        spinlock_unlock_irqrestore(&input_lock, flags);
        return false;
    }

    devices[slot].occupied = false;
    devices[slot].info.online = false;
    --device_count;

    /* Do not deliver queued events from a device after it is removed. */
    input_event_t retained[INPUT_EVENT_QUEUE_CAPACITY];
    uint32_t retained_count = 0;

    for (uint32_t i = 0; i < event_count; ++i)
    {
        input_event_t event =
            events[(event_read + i) % INPUT_EVENT_QUEUE_CAPACITY];
        if (event.device_id != device_id)
            retained[retained_count++] = event;
    }

    for (uint32_t i = 0; i < retained_count; ++i)
        events[i] = retained[i];

    event_read = 0;
    event_write = retained_count % INPUT_EVENT_QUEUE_CAPACITY;
    event_count = retained_count;

    spinlock_unlock_irqrestore(&input_lock, flags);
    return true;
}

bool input_set_device_online(uint32_t device_id, bool online)
{
    uint64_t flags = spinlock_lock_irqsave(&input_lock);

    for (uint32_t i = 0; i < INPUT_MAX_DEVICES; ++i)
    {
        if (devices[i].occupied && devices[i].info.id == device_id)
        {
            devices[i].info.online = online;
            spinlock_unlock_irqrestore(&input_lock, flags);
            return true;
        }
    }

    spinlock_unlock_irqrestore(&input_lock, flags);
    return false;
}

uint32_t input_device_count(void)
{
    uint64_t flags = spinlock_lock_irqsave(&input_lock);
    uint32_t count = device_count;
    spinlock_unlock_irqrestore(&input_lock, flags);
    return count;
}

bool input_get_device(uint32_t index, input_device_info_t *out)
{
    if (!out)
        return false;

    uint64_t flags = spinlock_lock_irqsave(&input_lock);
    uint32_t current = 0;

    for (uint32_t i = 0; i < INPUT_MAX_DEVICES; ++i)
    {
        if (!devices[i].occupied)
            continue;

        if (current++ == index)
        {
            *out = devices[i].info;
            spinlock_unlock_irqrestore(&input_lock, flags);
            return true;
        }
    }

    spinlock_unlock_irqrestore(&input_lock, flags);
    return false;
}

bool input_report_event(const input_event_t *event)
{
    if (!event || event->type <= INPUT_EVENT_NONE ||
        event->type > INPUT_EVENT_WHEEL || event->device_id == 0)
        return false;

    uint32_t required_capability = input_required_capability(event->type);
    if (required_capability == 0)
        return false;

    uint64_t flags = spinlock_lock_irqsave(&input_lock);
    bool source_valid = false;

    for (uint32_t i = 0; i < INPUT_MAX_DEVICES; ++i)
    {
        if (!devices[i].occupied || devices[i].info.id != event->device_id)
            continue;

        source_valid = devices[i].info.online &&
            (devices[i].info.capabilities & required_capability) != 0;
        break;
    }

    if (!source_valid)
    {
        spinlock_unlock_irqrestore(&input_lock, flags);
        return false;
    }

    if (event_count == INPUT_EVENT_QUEUE_CAPACITY)
    {
        ++dropped_events;
        spinlock_unlock_irqrestore(&input_lock, flags);
        return false;
    }

    events[event_write] = *event;
    event_write = (event_write + 1) % INPUT_EVENT_QUEUE_CAPACITY;
    ++event_count;

    spinlock_unlock_irqrestore(&input_lock, flags);
    return true;
}

bool input_next_event(input_event_t *out)
{
    if (!out)
        return false;

    uint64_t flags = spinlock_lock_irqsave(&input_lock);
    if (event_count == 0)
    {
        spinlock_unlock_irqrestore(&input_lock, flags);
        return false;
    }

    *out = events[event_read];
    event_read = (event_read + 1) % INPUT_EVENT_QUEUE_CAPACITY;
    --event_count;

    spinlock_unlock_irqrestore(&input_lock, flags);
    return true;
}

uint32_t input_dropped_event_count(void)
{
    uint64_t flags = spinlock_lock_irqsave(&input_lock);
    uint32_t count = dropped_events;
    spinlock_unlock_irqrestore(&input_lock, flags);
    return count;
}
