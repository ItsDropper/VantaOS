/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "watchdog.h"

#include "spinlock.h"
#include "timer.h"
#include "timer_math.h"

#define WATCHDOG_SLOT_BITS 8U
#define WATCHDOG_SLOT_MASK 0xFFU
#define WATCHDOG_GENERATION_MASK 0x00FFFFFFFFFFFFFFULL

typedef struct
{
    uint64_t generation;
    uint32_t timeout_ticks;
    uint32_t deadline;
    kernel_watchdog_status_t status;
} kernel_watchdog_slot_t;

static kernel_watchdog_slot_t slots[KERNEL_WATCHDOG_MAX_CLIENTS];
static spinlock_t watchdog_lock = SPINLOCK_INITIALIZER;

static kernel_watchdog_slot_t* watchdog_find_locked(
    kernel_watchdog_handle_t handle)
{
    uint32_t encoded_slot = handle & WATCHDOG_SLOT_MASK;
    uint64_t generation = handle >> WATCHDOG_SLOT_BITS;
    if (encoded_slot == 0U || encoded_slot > KERNEL_WATCHDOG_MAX_CLIENTS ||
        generation == 0U)
        return 0;

    kernel_watchdog_slot_t* slot = &slots[encoded_slot - 1U];
    if (slot->generation != generation ||
        slot->status == KERNEL_WATCHDOG_INVALID)
        return 0;
    return slot;
}

kernel_watchdog_handle_t kernel_watchdog_register(uint32_t timeout_ticks)
{
    if (timeout_ticks == 0U || timeout_ticks > VANTA_TIMER_MAX_WAIT_TICKS)
        return 0U;

    uint64_t flags = spinlock_lock_irqsave(&watchdog_lock);
    for (uint32_t i = 0U; i < KERNEL_WATCHDOG_MAX_CLIENTS; ++i)
    {
        kernel_watchdog_slot_t* slot = &slots[i];
        if (slot->status != KERNEL_WATCHDOG_INVALID)
            continue;

        uint64_t generation =
            (slot->generation + 1U) & WATCHDOG_GENERATION_MASK;
        if (generation == 0U)
            generation = 1U;

        slot->generation = generation;
        slot->timeout_ticks = timeout_ticks;
        slot->deadline = timer_get_ticks() + timeout_ticks;
        slot->status = KERNEL_WATCHDOG_ACTIVE;
        kernel_watchdog_handle_t handle =
            (generation << WATCHDOG_SLOT_BITS) | (i + 1U);
        spinlock_unlock_irqrestore(&watchdog_lock, flags);
        return handle;
    }
    spinlock_unlock_irqrestore(&watchdog_lock, flags);
    return 0U;
}

int kernel_watchdog_kick(kernel_watchdog_handle_t handle)
{
    uint64_t flags = spinlock_lock_irqsave(&watchdog_lock);
    kernel_watchdog_slot_t* slot = watchdog_find_locked(handle);
    if (slot == 0 || slot->status != KERNEL_WATCHDOG_ACTIVE)
    {
        spinlock_unlock_irqrestore(&watchdog_lock, flags);
        return 0;
    }

    slot->deadline = timer_get_ticks() + slot->timeout_ticks;
    spinlock_unlock_irqrestore(&watchdog_lock, flags);
    return 1;
}

int kernel_watchdog_unregister(kernel_watchdog_handle_t handle)
{
    uint64_t flags = spinlock_lock_irqsave(&watchdog_lock);
    kernel_watchdog_slot_t* slot = watchdog_find_locked(handle);
    if (slot == 0)
    {
        spinlock_unlock_irqrestore(&watchdog_lock, flags);
        return 0;
    }

    slot->status = KERNEL_WATCHDOG_INVALID;
    slot->timeout_ticks = 0U;
    slot->deadline = 0U;
    spinlock_unlock_irqrestore(&watchdog_lock, flags);
    return 1;
}

kernel_watchdog_status_t kernel_watchdog_status(
    kernel_watchdog_handle_t handle)
{
    uint64_t flags = spinlock_lock_irqsave(&watchdog_lock);
    kernel_watchdog_slot_t* slot = watchdog_find_locked(handle);
    kernel_watchdog_status_t status = slot == 0 ?
        KERNEL_WATCHDOG_INVALID : slot->status;
    spinlock_unlock_irqrestore(&watchdog_lock, flags);
    return status;
}

void kernel_watchdog_timer_tick(uint32_t now)
{
    /* Never spin in interrupt context: a busy lock only delays this scan. */
    if (!spinlock_try_lock(&watchdog_lock))
        return;

    for (uint32_t i = 0U; i < KERNEL_WATCHDOG_MAX_CLIENTS; ++i)
    {
        kernel_watchdog_slot_t* slot = &slots[i];
        if (slot->status == KERNEL_WATCHDOG_ACTIVE &&
            vanta_timer_deadline_reached(now, slot->deadline))
            slot->status = KERNEL_WATCHDOG_EXPIRED;
    }
    spinlock_unlock(&watchdog_lock);
}
