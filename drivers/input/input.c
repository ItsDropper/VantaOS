#include "input.h"
#include "spinlock.h"

typedef struct { bool occupied; input_device_info_t info; } input_device_slot_t;
static input_device_slot_t devices[INPUT_MAX_DEVICES];
static input_event_t events[INPUT_EVENT_QUEUE_CAPACITY];
static uint32_t next_device_id = 1, device_count = 0;
static uint32_t event_read = 0, event_write = 0, event_count = 0, dropped_events = 0;
static bool initialized = false;
static spinlock_t input_lock = SPINLOCK_INITIALIZER;

static bool descriptor_valid(const input_device_descriptor_t *d)
{
    if (!d || !d->name || d->name[0] == '\0') return false;
    if (d->bus != INPUT_BUS_PS2 && d->bus != INPUT_BUS_USB) return false;
    if (d->kind != INPUT_DEVICE_KEYBOARD && d->kind != INPUT_DEVICE_MOUSE) return false;
    return d->capabilities != 0;
}

void input_initialize(void)
{
    uint64_t flags = spinlock_lock_irqsave(&input_lock);
    for (uint32_t i = 0; i < INPUT_MAX_DEVICES; ++i) {
        devices[i].occupied = false;
        devices[i].info.id = 0;
        devices[i].info.name = 0;
        devices[i].info.bus = INPUT_BUS_NONE;
        devices[i].info.kind = INPUT_DEVICE_NONE;
        devices[i].info.capabilities = 0;
        devices[i].info.online = false;
    }
    next_device_id = 1; device_count = 0;
    event_read = 0; event_write = 0; event_count = 0; dropped_events = 0;
    initialized = true;
    spinlock_unlock_irqrestore(&input_lock, flags);
}

bool input_register_device(const input_device_descriptor_t *d, uint32_t *id_out)
{
    if (!descriptor_valid(d)) return false;
    uint64_t flags = spinlock_lock_irqsave(&input_lock);
    if (!initialized) { spinlock_unlock_irqrestore(&input_lock, flags); return false; }
    uint32_t free_slot = INPUT_MAX_DEVICES;
    for (uint32_t i = 0; i < INPUT_MAX_DEVICES; ++i) {
        if (!devices[i].occupied) { if (free_slot == INPUT_MAX_DEVICES) free_slot = i; continue; }
        const input_device_info_t *current = &devices[i].info;
        if (current->bus == d->bus && current->kind == d->kind && current->name == d->name) {
            if (id_out) *id_out = current->id;
            spinlock_unlock_irqrestore(&input_lock, flags);
            return false;
        }
    }
    if (free_slot == INPUT_MAX_DEVICES || next_device_id == 0) {
        spinlock_unlock_irqrestore(&input_lock, flags); return false;
    }
    input_device_info_t *info = &devices[free_slot].info;
    info->id = next_device_id++; info->name = d->name; info->bus = d->bus;
    info->kind = d->kind; info->capabilities = d->capabilities; info->online = true;
    devices[free_slot].occupied = true; ++device_count;
    if (id_out) *id_out = info->id;
    spinlock_unlock_irqrestore(&input_lock, flags);
    return true;
}

bool input_unregister_device(uint32_t id)
{
    if (!id) return false;
    uint64_t flags = spinlock_lock_irqsave(&input_lock);
    uint32_t slot = INPUT_MAX_DEVICES;
    for (uint32_t i = 0; i < INPUT_MAX_DEVICES; ++i)
        if (devices[i].occupied && devices[i].info.id == id) { slot = i; break; }
    if (slot == INPUT_MAX_DEVICES) { spinlock_unlock_irqrestore(&input_lock, flags); return false; }
    devices[slot].occupied = false; devices[slot].info.online = false; --device_count;
    input_event_t retained[INPUT_EVENT_QUEUE_CAPACITY];
    uint32_t retained_count = 0;
    for (uint32_t i = 0; i < event_count; ++i) {
        input_event_t e = events[(event_read + i) % INPUT_EVENT_QUEUE_CAPACITY];
        if (e.device_id != id) retained[retained_count++] = e;
    }
    for (uint32_t i = 0; i < retained_count; ++i) events[i] = retained[i];
    event_read = 0; event_write = retained_count % INPUT_EVENT_QUEUE_CAPACITY;
    event_count = retained_count;
    spinlock_unlock_irqrestore(&input_lock, flags);
    return true;
}

bool input_set_device_online(uint32_t id, bool online)
{
    uint64_t flags = spinlock_lock_irqsave(&input_lock);
    for (uint32_t i = 0; i < INPUT_MAX_DEVICES; ++i) {
        if (devices[i].occupied && devices[i].info.id == id) {
            devices[i].info.online = online;
            spinlock_unlock_irqrestore(&input_lock, flags); return true;
        }
    }
    spinlock_unlock_irqrestore(&input_lock, flags); return false;
}

uint32_t input_device_count(void)
{
    uint64_t flags = spinlock_lock_irqsave(&input_lock);
    uint32_t count = device_count;
    spinlock_unlock_irqrestore(&input_lock, flags); return count;
}

bool input_get_device(uint32_t index, input_device_info_t *out)
{
    if (!out) return false;
    uint64_t flags = spinlock_lock_irqsave(&input_lock);
    uint32_t current = 0;
    for (uint32_t i = 0; i < INPUT_MAX_DEVICES; ++i) {
        if (!devices[i].occupied) continue;
        if (current++ == index) { *out = devices[i].info; spinlock_unlock_irqrestore(&input_lock, flags); return true; }
    }
    spinlock_unlock_irqrestore(&input_lock, flags); return false;
}

bool input_report_event(const input_event_t *event)
{
    if (!event || event->type <= INPUT_EVENT_NONE || event->type > INPUT_EVENT_WHEEL || !event->device_id)
        return false;
    uint64_t flags = spinlock_lock_irqsave(&input_lock);
    bool online = false;
    for (uint32_t i = 0; i < INPUT_MAX_DEVICES; ++i)
        if (devices[i].occupied && devices[i].info.id == event->device_id) { online = devices[i].info.online; break; }
    if (!online) { spinlock_unlock_irqrestore(&input_lock, flags); return false; }
    if (event_count == INPUT_EVENT_QUEUE_CAPACITY) {
        ++dropped_events; spinlock_unlock_irqrestore(&input_lock, flags); return false;
    }
    events[event_write] = *event;
    event_write = (event_write + 1) % INPUT_EVENT_QUEUE_CAPACITY; ++event_count;
    spinlock_unlock_irqrestore(&input_lock, flags); return true;
}

bool input_next_event(input_event_t *out)
{
    if (!out) return false;
    uint64_t flags = spinlock_lock_irqsave(&input_lock);
    if (!event_count) { spinlock_unlock_irqrestore(&input_lock, flags); return false; }
    *out = events[event_read]; event_read = (event_read + 1) % INPUT_EVENT_QUEUE_CAPACITY; --event_count;
    spinlock_unlock_irqrestore(&input_lock, flags); return true;
}

uint32_t input_dropped_event_count(void)
{
    uint64_t flags = spinlock_lock_irqsave(&input_lock);
    uint32_t count = dropped_events;
    spinlock_unlock_irqrestore(&input_lock, flags); return count;
}
