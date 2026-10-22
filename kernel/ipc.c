/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "ipc.h"

#include "crypto_rng.h"
#include "spinlock.h"

#include <stddef.h>
#include <stdint.h>

#define IPC_ENDPOINTS_MAX 32U

typedef struct
{
    uint32_t sender_pid;
    uint32_t length;
    uint8_t bytes[IPC_MESSAGE_MAX];
} ipc_message_t;

typedef struct
{
    uint64_t token;
    uint32_t owner_pid;
    uint32_t head;
    uint32_t count;
    uint32_t active;
    ipc_message_t queue[IPC_QUEUE_DEPTH];
} ipc_endpoint_t;

static ipc_endpoint_t endpoints[IPC_ENDPOINTS_MAX];
static spinlock_t ipc_lock = SPINLOCK_INITIALIZER;

/* Caller must hold ipc_lock. */
static int ipc_token_exists_locked(uint64_t token)
{
    for (uint32_t i = 0; i < IPC_ENDPOINTS_MAX; ++i)
    {
        if (endpoints[i].active && endpoints[i].token == token)
            return 1;
    }
    return 0;
}

/* Caller must hold ipc_lock; erase queued payloads before slot reuse. */
static void ipc_endpoint_clear_locked(ipc_endpoint_t *endpoint)
{
    endpoint->active = 0U;
    endpoint->token = 0U;
    endpoint->owner_pid = 0U;
    endpoint->head = 0U;
    endpoint->count = 0U;
    for (uint32_t i = 0; i < IPC_QUEUE_DEPTH; ++i)
    {
        endpoint->queue[i].sender_pid = 0U;
        endpoint->queue[i].length = 0U;
        for (size_t j = 0; j < IPC_MESSAGE_MAX; ++j)
            endpoint->queue[i].bytes[j] = 0U;
    }
}

int64_t ipc_endpoint_create(uint32_t owner_pid)
{
    if (owner_pid == 0U)
        return -22;

    uint64_t token = 0U;
    /* Capability creation fails closed when the CPU has no working RDRAND. */
    if (!crypto_rng_fill(&token, sizeof(token)))
        return -19;

    /* Keep the token positive so successful capabilities are not error returns. */
    token &= 0x7FFFFFFFFFFFFFFFULL;
    if (token == 0U)
        return -19;

    uint64_t flags = spinlock_lock_irqsave(&ipc_lock);
    uint32_t slot = IPC_ENDPOINTS_MAX;
    for (uint32_t i = 0; i < IPC_ENDPOINTS_MAX; ++i)
    {
        if (!endpoints[i].active)
        {
            slot = i;
            break;
        }
    }

    if (slot == IPC_ENDPOINTS_MAX || ipc_token_exists_locked(token))
    {
        spinlock_unlock_irqrestore(&ipc_lock, flags);
        return slot == IPC_ENDPOINTS_MAX ? -12 : -11;
    }

    ipc_endpoint_t *endpoint = &endpoints[slot];
    endpoint->token = token;
    endpoint->owner_pid = owner_pid;
    endpoint->head = 0U;
    endpoint->count = 0U;
    endpoint->active = 1U;
    for (uint32_t i = 0; i < IPC_QUEUE_DEPTH; ++i)
    {
        endpoint->queue[i].sender_pid = 0U;
        endpoint->queue[i].length = 0U;
        for (size_t j = 0; j < IPC_MESSAGE_MAX; ++j)
            endpoint->queue[i].bytes[j] = 0U;
    }

    spinlock_unlock_irqrestore(&ipc_lock, flags);
    return (int64_t)token;
}

int ipc_message_send(uint64_t token, uint32_t sender_pid,
                     const void *message, size_t length)
{
    if (token == 0U || sender_pid == 0U || message == NULL ||
        length == 0U || length > IPC_MESSAGE_MAX)
        return -22;

    uint64_t flags = spinlock_lock_irqsave(&ipc_lock);
    ipc_endpoint_t *endpoint = NULL;
    for (uint32_t i = 0; i < IPC_ENDPOINTS_MAX; ++i)
    {
        if (endpoints[i].active && endpoints[i].token == token)
        {
            endpoint = &endpoints[i];
            break;
        }
    }

    if (endpoint == NULL)
    {
        spinlock_unlock_irqrestore(&ipc_lock, flags);
        return -9;
    }
    if (endpoint->count == IPC_QUEUE_DEPTH)
    {
        spinlock_unlock_irqrestore(&ipc_lock, flags);
        return -11;
    }

    uint32_t tail = (endpoint->head + endpoint->count) % IPC_QUEUE_DEPTH;
    ipc_message_t *destination = &endpoint->queue[tail];
    const uint8_t *source = (const uint8_t *)message;
    destination->sender_pid = sender_pid;
    destination->length = (uint32_t)length;
    for (size_t i = 0; i < length; ++i)
        destination->bytes[i] = source[i];
    ++endpoint->count;

    spinlock_unlock_irqrestore(&ipc_lock, flags);
    return 0;
}

int ipc_message_receive(uint64_t token, uint32_t receiver_pid,
                        void *message, size_t capacity)
{
    if (token == 0U || receiver_pid == 0U || message == NULL ||
        capacity == 0U || capacity > IPC_MESSAGE_MAX)
        return -22;

    uint64_t flags = spinlock_lock_irqsave(&ipc_lock);
    ipc_endpoint_t *endpoint = NULL;
    for (uint32_t i = 0; i < IPC_ENDPOINTS_MAX; ++i)
    {
        if (endpoints[i].active && endpoints[i].token == token)
        {
            endpoint = &endpoints[i];
            break;
        }
    }

    if (endpoint == NULL)
    {
        spinlock_unlock_irqrestore(&ipc_lock, flags);
        return -9;
    }
    if (endpoint->owner_pid != receiver_pid)
    {
        spinlock_unlock_irqrestore(&ipc_lock, flags);
        return -1;
    }
    if (endpoint->count == 0U)
    {
        spinlock_unlock_irqrestore(&ipc_lock, flags);
        return -11;
    }

    ipc_message_t *source = &endpoint->queue[endpoint->head];
    if (source->length > capacity)
    {
        spinlock_unlock_irqrestore(&ipc_lock, flags);
        return -90;
    }

    uint8_t *destination = (uint8_t *)message;
    for (uint32_t i = 0; i < source->length; ++i)
        destination[i] = source->bytes[i];
    int result = (int)source->length;

    source->sender_pid = 0U;
    source->length = 0U;
    for (size_t i = 0; i < IPC_MESSAGE_MAX; ++i)
        source->bytes[i] = 0U;
    endpoint->head = (endpoint->head + 1U) % IPC_QUEUE_DEPTH;
    --endpoint->count;

    spinlock_unlock_irqrestore(&ipc_lock, flags);
    return result;
}

int ipc_endpoint_close(uint64_t token, uint32_t owner_pid)
{
    if (token == 0U || owner_pid == 0U)
        return -22;

    uint64_t flags = spinlock_lock_irqsave(&ipc_lock);
    for (uint32_t i = 0; i < IPC_ENDPOINTS_MAX; ++i)
    {
        ipc_endpoint_t *endpoint = &endpoints[i];
        if (!endpoint->active || endpoint->token != token)
            continue;
        if (endpoint->owner_pid != owner_pid)
        {
            spinlock_unlock_irqrestore(&ipc_lock, flags);
            return -1;
        }

        ipc_endpoint_clear_locked(endpoint);
        spinlock_unlock_irqrestore(&ipc_lock, flags);
        return 0;
    }

    spinlock_unlock_irqrestore(&ipc_lock, flags);
    return -9;
}

void ipc_process_cleanup(uint32_t pid)
{
    if (pid == 0U)
        return;

    uint64_t flags = spinlock_lock_irqsave(&ipc_lock);
    for (uint32_t i = 0; i < IPC_ENDPOINTS_MAX; ++i)
    {
        ipc_endpoint_t *endpoint = &endpoints[i];
        if (endpoint->active && endpoint->owner_pid == pid)
            ipc_endpoint_clear_locked(endpoint);
    }
    spinlock_unlock_irqrestore(&ipc_lock, flags);
}
