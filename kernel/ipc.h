/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTAOS_IPC_H
#define VANTAOS_IPC_H
#include <stddef.h>
#include <stdint.h>
#define IPC_MESSAGE_MAX 256U
#define IPC_QUEUE_DEPTH 8U
int64_t ipc_endpoint_create(uint32_t owner_pid);
int ipc_message_send(uint64_t endpoint, uint32_t sender_pid, const void *message, size_t length);
int ipc_message_receive(uint64_t endpoint, uint32_t receiver_pid, void *message, size_t capacity);
int ipc_endpoint_close(uint64_t endpoint, uint32_t owner_pid);
void ipc_process_cleanup(uint32_t pid);
#endif
