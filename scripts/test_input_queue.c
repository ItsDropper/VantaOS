/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "input.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    input_initialize();

    input_device_descriptor_t mouse = {
        .name = "test mouse",
        .bus = INPUT_BUS_PS2,
        .kind = INPUT_DEVICE_MOUSE,
        .capabilities = INPUT_CAP_MOUSE_RELATIVE |
                        INPUT_CAP_MOUSE_BUTTONS | INPUT_CAP_MOUSE_WHEEL
    };
    uint32_t mouse_id = 0;
    assert(input_register_device(&mouse, &mouse_id));
    assert(mouse_id != 0U);

    input_event_t motion = {
        .device_id = mouse_id,
        .type = INPUT_EVENT_RELATIVE_MOTION,
        .value = 12,
        .value2 = -7
    };
    assert(input_report_event(&motion));
    motion.value = -5;
    motion.value2 = 3;
    assert(input_report_event(&motion));

    input_event_t event;
    assert(input_next_event(&event));
    assert(event.type == INPUT_EVENT_RELATIVE_MOTION);
    assert(event.device_id == mouse_id);
    assert(event.value == 7 && event.value2 == -4);
    assert(!input_next_event(&event));

    input_event_t button = {
        .device_id = mouse_id,
        .type = INPUT_EVENT_BUTTON,
        .code = 0,
        .value = 1
    };
    assert(input_report_event(&motion));
    assert(input_report_event(&button));
    motion.value = 2;
    motion.value2 = 1;
    assert(input_report_event(&motion));

    assert(input_next_event(&event));
    assert(event.type == INPUT_EVENT_RELATIVE_MOTION);
    assert(event.value == -5 && event.value2 == 3);
    assert(input_next_event(&event));
    assert(event.type == INPUT_EVENT_BUTTON && event.value == 1);
    assert(input_next_event(&event));
    assert(event.type == INPUT_EVENT_RELATIVE_MOTION);
    assert(event.value == 2 && event.value2 == 1);
    assert(!input_next_event(&event));

    motion.value = INT32_MAX;
    motion.value2 = INT32_MIN;
    assert(input_report_event(&motion));
    motion.value = 1;
    motion.value2 = -1;
    assert(input_report_event(&motion));
    assert(input_next_event(&event));
    assert(event.value == INT32_MAX && event.value2 == INT32_MIN);

    /* Discrete transitions displace stale motion rather than being lost. */
    for (uint32_t i = 0; i + 1U < INPUT_EVENT_QUEUE_CAPACITY; ++i)
    {
        button.value = (int32_t)(i & 1U);
        assert(input_report_event(&button));
    }
    motion.value = 9;
    motion.value2 = -4;
    assert(input_report_event(&motion));
    button.value = 1;
    assert(input_report_event(&button));
    uint32_t retained = 0U;
    while (input_next_event(&event))
    {
        assert(event.type == INPUT_EVENT_BUTTON);
        retained++;
    }
    assert(retained == INPUT_EVENT_QUEUE_CAPACITY);
    assert(input_dropped_event_count() == 0U);

    for (uint32_t i = 0; i < INPUT_EVENT_QUEUE_CAPACITY; ++i)
    {
        button.value = (int32_t)(i & 1U);
        assert(input_report_event(&button));
    }
    assert(!input_report_event(&button));
    assert(input_dropped_event_count() == 1U);

    assert(input_unregister_device(mouse_id));
    assert(!input_next_event(&event));
    puts("normalized input queue tests passed");
    return 0;
}
