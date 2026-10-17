#ifndef INPUT_H
#define INPUT_H
#include <stdbool.h>
#include <stdint.h>
#define INPUT_MAX_DEVICES 16
#define INPUT_EVENT_QUEUE_CAPACITY 128
typedef enum { INPUT_BUS_NONE = 0, INPUT_BUS_PS2, INPUT_BUS_USB } input_bus_t;
typedef enum { INPUT_DEVICE_NONE = 0, INPUT_DEVICE_KEYBOARD, INPUT_DEVICE_MOUSE } input_device_kind_t;
enum {
    INPUT_CAP_KEYBOARD_TEXT = 1U << 0, INPUT_CAP_KEYBOARD_KEYS = 1U << 1,
    INPUT_CAP_MOUSE_RELATIVE = 1U << 2, INPUT_CAP_MOUSE_BUTTONS = 1U << 3,
    INPUT_CAP_MOUSE_WHEEL = 1U << 4
};
typedef struct {
    const char *name;
    input_bus_t bus;
    input_device_kind_t kind;
    uint32_t capabilities;
} input_device_descriptor_t;
typedef struct {
    uint32_t id;
    const char *name;
    input_bus_t bus;
    input_device_kind_t kind;
    uint32_t capabilities;
    bool online;
} input_device_info_t;
typedef enum {
    INPUT_EVENT_NONE = 0, INPUT_EVENT_KEY, INPUT_EVENT_TEXT,
    INPUT_EVENT_RELATIVE_MOTION, INPUT_EVENT_BUTTON, INPUT_EVENT_WHEEL
} input_event_type_t;
typedef struct {
    uint32_t device_id;
    input_event_type_t type;
    int32_t code;
    int32_t value;
    int32_t value2;
} input_event_t;
void input_initialize(void);
bool input_register_device(const input_device_descriptor_t *descriptor, uint32_t *device_id_out);
bool input_unregister_device(uint32_t device_id);
bool input_set_device_online(uint32_t device_id, bool online);
uint32_t input_device_count(void);
bool input_get_device(uint32_t index, input_device_info_t *out);
bool input_report_event(const input_event_t *event);
bool input_next_event(input_event_t *out);
uint32_t input_dropped_event_count(void);
#endif
