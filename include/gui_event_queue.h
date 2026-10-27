/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_GUI_EVENT_QUEUE_H
#define VANTA_GUI_EVENT_QUEUE_H
#include "gui_abi.h"
#include <stdint.h>
static inline int gui_event_queue_push(vanta_gui_event_t* events,
                                       uint32_t capacity, uint32_t* head,
                                       uint32_t* count,
                                       const vanta_gui_event_t* event)
{
    if (!events || capacity == 0U || !head || !count || !event ||
        *head >= capacity || *count > capacity)
        return 0;
    if (event->type == VANTA_GUI_EVENT_POINTER_MOVE && *count != 0U)
    {
        uint32_t last = (*head + *count - 1U) % capacity;
        if (events[last].type == VANTA_GUI_EVENT_POINTER_MOVE)
        { events[last] = *event; return 1; }
    }
    if (*count == capacity)
    {
        uint32_t remove_offset = capacity;
        for (uint32_t i = 0U; i < *count; i++)
            if (events[(*head + i) % capacity].type == VANTA_GUI_EVENT_POINTER_MOVE)
            { remove_offset = i; break; }
        if (remove_offset == capacity)
        {
            if (event->type == VANTA_GUI_EVENT_POINTER_MOVE) return 0;
            remove_offset = 0U;
        }
        for (uint32_t i = remove_offset; i + 1U < *count; i++)
            events[(*head + i) % capacity] =
                events[(*head + i + 1U) % capacity];
        --(*count);
    }
    events[(*head + *count) % capacity] = *event;
    ++(*count);
    return 1;
}
#endif
