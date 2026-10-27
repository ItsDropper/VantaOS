/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTA_USER_IPC_H
#define VANTA_USER_IPC_H

#include <stddef.h>
#include <stdint.h>
#include <vanta/syscall.h>

/*
 * Stable VantaOS message envelope layered over capability endpoints.
 * The ABI is little-endian x86-64. The kernel transports bytes and does not
 * interpret this envelope; both peers must agree on the message type/payload.
 */
#define VANTA_IPC_MAGIC 0x43495056U /* "VPIC" in little-endian memory */
#define VANTA_IPC_VERSION 1U
#define VANTA_IPC_HEADER_SIZE 16U
#define VANTA_IPC_PAYLOAD_MAX (VANTA_IPC_MESSAGE_MAX - VANTA_IPC_HEADER_SIZE)

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t request_id;
    uint32_t payload_length;
    uint8_t payload[VANTA_IPC_PAYLOAD_MAX];
} vanta_ipc_message_t;

static inline int vanta_ipc_message_init(vanta_ipc_message_t* message,
                                         uint16_t type, uint32_t request_id,
                                         const void* payload,
                                         size_t payload_length)
{
    if (!message || type == 0U ||
        (payload_length != 0U && !payload))
        return -VANTA_EINVAL;
    if (payload_length > VANTA_IPC_PAYLOAD_MAX)
        return -VANTA_EMSGSIZE;

    uint8_t* bytes = (uint8_t*)message;
    for (size_t i = 0; i < sizeof(*message); i++)
        bytes[i] = 0U;
    message->magic = VANTA_IPC_MAGIC;
    message->version = VANTA_IPC_VERSION;
    message->type = type;
    message->request_id = request_id;
    message->payload_length = (uint32_t)payload_length;
    const uint8_t* source = (const uint8_t*)payload;
    for (size_t i = 0; i < payload_length; i++)
        message->payload[i] = source[i];
    return 0;
}

/* Validate the exact byte count returned by vanta_ipc_receive(). */
static inline int vanta_ipc_message_validate(
    const vanta_ipc_message_t* message, size_t received_length)
{
    if (!message)
        return -VANTA_EINVAL;
    if (received_length < VANTA_IPC_HEADER_SIZE ||
        received_length > VANTA_IPC_MESSAGE_MAX ||
        message->magic != VANTA_IPC_MAGIC ||
        message->version != VANTA_IPC_VERSION ||
        message->type == 0U ||
        message->payload_length > VANTA_IPC_PAYLOAD_MAX ||
        received_length != VANTA_IPC_HEADER_SIZE + message->payload_length)
        return -VANTA_EPROTO;
    return 0;
}

static inline long vanta_ipc_send_message(uint64_t endpoint, uint16_t type,
                                          uint32_t request_id,
                                          const void* payload,
                                          size_t payload_length)
{
    vanta_ipc_message_t message;
    int result = vanta_ipc_message_init(&message, type, request_id,
                                        payload, payload_length);
    if (result != 0)
        return result;
    return vanta_ipc_send(endpoint, &message,
                          VANTA_IPC_HEADER_SIZE + payload_length);
}

/*
 * Receives one complete envelope. Returns payload byte count on success.
 * type_out and request_id_out may be NULL. If capacity is too small, the
 * message has already been dequeued; callers should size buffers accordingly.
 */
static inline long vanta_ipc_receive_message(uint64_t endpoint,
                                             uint16_t* type_out,
                                             uint32_t* request_id_out,
                                             void* payload,
                                             size_t capacity)
{
    vanta_ipc_message_t message;
    long received = vanta_ipc_receive(endpoint, &message, sizeof(message));
    if (received < 0)
        return received;
    int valid = vanta_ipc_message_validate(&message, (size_t)received);
    if (valid != 0)
        return valid;
    if (message.payload_length > capacity ||
        (message.payload_length != 0U && !payload))
        return -VANTA_EMSGSIZE;

    uint8_t* destination = (uint8_t*)payload;
    for (size_t i = 0; i < message.payload_length; i++)
        destination[i] = message.payload[i];
    if (type_out)
        *type_out = message.type;
    if (request_id_out)
        *request_id_out = message.request_id;
    return (long)message.payload_length;
}

#endif
