/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTA_IPC_H
#define VANTA_IPC_H

#include <stddef.h>
#include <stdint.h>

#define VANTA_IPC_PAYLOAD_MAX 128U
#define VANTA_IPC_QUEUE_DEPTH 8U
#define VANTA_IPC_ANY_SENDER 0U

typedef struct
{
    uint32_t sender_pid;
    uint32_t length;
    uint8_t payload[VANTA_IPC_PAYLOAD_MAX];
} vanta_ipc_message_t;

/* Non-blocking, bounded, per-process message queues. */
void ipc_initialize(void);
void ipc_process_reset(uint32_t pid);
/* Returns payload length on success or a negative errno-style result. */
int ipc_send(uint32_t sender_pid, uint32_t target_pid,
             const void *payload, size_t length);
/* Returns 1 when a message is received, 0 when no matching message exists. */
int ipc_receive(uint32_t receiver_pid, uint32_t sender_filter,
                vanta_ipc_message_t *message);
/* Deterministic queue invariants probe used by the kernel self-test build. */
int ipc_selftest(void);

#endif
