/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "ipc.h"
#include "../proc/process_internal.h"
#include "spinlock.h"

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint32_t count;
    vanta_ipc_message_t messages[VANTA_IPC_QUEUE_DEPTH];
} ipc_mailbox_t;

static ipc_mailbox_t mailboxes[PROCESS_MAX];
static spinlock_t ipc_lock = SPINLOCK_INITIALIZER;

static int process_can_use_ipc(uint32_t pid)
{
    process_t process;
    if (pid == 0U || pid >= PROCESS_MAX ||
        !process_snapshot(pid, &process) || !process.is_user)
        return 0;

    return process.state != PROCESS_UNUSED &&
           process.state != PROCESS_TERMINATED &&
           process.state != PROCESS_ZOMBIE &&
           process.state != PROCESS_REAPING;
}

static void message_clear(vanta_ipc_message_t *message)
{
    message->sender_pid = 0U;
    message->length = 0U;
    for (size_t i = 0U; i < VANTA_IPC_PAYLOAD_MAX; ++i)
        message->payload[i] = 0U;
}

static int queue_push(ipc_mailbox_t *mailbox,
                      const vanta_ipc_message_t *message)
{
    if (!mailbox || !message || mailbox->count >= VANTA_IPC_QUEUE_DEPTH)
        return 0;

    mailbox->messages[mailbox->count] = *message;
    mailbox->count++;
    return 1;
}

static int queue_receive(ipc_mailbox_t *mailbox, uint32_t sender_filter,
                         vanta_ipc_message_t *message)
{
    if (!mailbox || !message)
        return 0;

    uint32_t found = mailbox->count;
    for (uint32_t i = 0U; i < mailbox->count; ++i)
    {
        if (sender_filter == VANTA_IPC_ANY_SENDER ||
            mailbox->messages[i].sender_pid == sender_filter)
        {
            found = i;
            break;
        }
    }
    if (found == mailbox->count)
        return 0;

    *message = mailbox->messages[found];
    for (uint32_t i = found + 1U; i < mailbox->count; ++i)
        mailbox->messages[i - 1U] = mailbox->messages[i];
    mailbox->count--;
    message_clear(&mailbox->messages[mailbox->count]);
    return 1;
}

void ipc_initialize(void)
{
    uint64_t flags = spinlock_lock_irqsave(&ipc_lock);
    for (uint32_t pid = 0U; pid < PROCESS_MAX; ++pid)
    {
        mailboxes[pid].count = 0U;
        for (uint32_t i = 0U; i < VANTA_IPC_QUEUE_DEPTH; ++i)
            message_clear(&mailboxes[pid].messages[i]);
    }
    spinlock_unlock_irqrestore(&ipc_lock, flags);
}

void ipc_process_reset(uint32_t pid)
{
    if (pid >= PROCESS_MAX)
        return;

    uint64_t flags = spinlock_lock_irqsave(&ipc_lock);
    mailboxes[pid].count = 0U;
    for (uint32_t i = 0U; i < VANTA_IPC_QUEUE_DEPTH; ++i)
        message_clear(&mailboxes[pid].messages[i]);

    /* Drop queued messages from a process when its PID is being recycled. */
    for (uint32_t receiver = 0U; receiver < PROCESS_MAX; ++receiver)
    {
        ipc_mailbox_t *mailbox = &mailboxes[receiver];
        uint32_t write_index = 0U;
        for (uint32_t read_index = 0U; read_index < mailbox->count;
             ++read_index)
        {
            if (mailbox->messages[read_index].sender_pid == pid)
                continue;
            if (write_index != read_index)
                mailbox->messages[write_index] = mailbox->messages[read_index];
            write_index++;
        }
        while (mailbox->count > write_index)
        {
            mailbox->count--;
            message_clear(&mailbox->messages[mailbox->count]);
        }
    }
    spinlock_unlock_irqrestore(&ipc_lock, flags);
}

static int process_live_user_locked(uint32_t pid)
{
    if (pid == 0U || pid >= PROCESS_MAX)
        return 0;
    const process_t *process = &process_table_records[pid];
    return process->is_user &&
           process->state != PROCESS_UNUSED &&
           process->state != PROCESS_TERMINATED &&
           process->state != PROCESS_ZOMBIE &&
           process->state != PROCESS_REAPING;
}

int ipc_send(uint32_t sender_pid, uint32_t target_pid,
             const void *payload, size_t length)
{
    if (length > VANTA_IPC_PAYLOAD_MAX || (length != 0U && !payload))
        return -90;

    vanta_ipc_message_t message;
    message_clear(&message);
    message.sender_pid = sender_pid;
    message.length = (uint32_t)length;
    const uint8_t *source = (const uint8_t *)payload;
    for (size_t i = 0U; i < length; ++i)
        message.payload[i] = source[i];

    /*
     * Hold process-table then IPC lock, matching process_reset_record's lock
     * order. This closes the target-PID reuse window between validation and
     * enqueue, without ever taking the process-table lock while holding IPC.
     */
    uint64_t process_flags = process_table_lock_acquire();
    if (!process_initialized_state ||
        !process_live_user_locked(sender_pid) ||
        !process_live_user_locked(target_pid))
    {
        process_table_lock_release(process_flags);
        return -3;
    }

    uint64_t ipc_flags = spinlock_lock_irqsave(&ipc_lock);
    int ok = queue_push(&mailboxes[target_pid], &message);
    spinlock_unlock_irqrestore(&ipc_lock, ipc_flags);
    process_table_lock_release(process_flags);
    return ok ? (int)length : -11;
}

int ipc_receive(uint32_t receiver_pid, uint32_t sender_filter,
                vanta_ipc_message_t *message)
{
    if (!message || receiver_pid == 0U || receiver_pid >= PROCESS_MAX ||
        sender_filter >= PROCESS_MAX)
        return -22;
    if (!process_can_use_ipc(receiver_pid))
        return -3;

    uint64_t flags = spinlock_lock_irqsave(&ipc_lock);
    int received = queue_receive(&mailboxes[receiver_pid], sender_filter,
                                 message);
    spinlock_unlock_irqrestore(&ipc_lock, flags);
    return received;
}

int ipc_selftest(void)
{
    ipc_mailbox_t mailbox = {0};
    vanta_ipc_message_t input;
    vanta_ipc_message_t output;
    message_clear(&input);
    input.sender_pid = 7U;
    input.length = 3U;
    input.payload[0] = 'i';
    input.payload[1] = 'p';
    input.payload[2] = 'c';

    if (!queue_push(&mailbox, &input))
        return 0;
    input.sender_pid = 9U;
    input.length = 1U;
    input.payload[0] = '!';
    if (!queue_push(&mailbox, &input) ||
        mailbox.count != 2U ||
        !queue_receive(&mailbox, 9U, &output) ||
        output.sender_pid != 9U || output.length != 1U ||
        output.payload[0] != '!' || mailbox.count != 1U ||
        !queue_receive(&mailbox, VANTA_IPC_ANY_SENDER, &output) ||
        output.sender_pid != 7U || output.length != 3U ||
        output.payload[0] != 'i' || output.payload[1] != 'p' ||
        output.payload[2] != 'c' || mailbox.count != 0U)
        return 0;

    for (uint32_t i = 0U; i < VANTA_IPC_QUEUE_DEPTH; ++i)
    {
        input.sender_pid = i + 1U;
        if (!queue_push(&mailbox, &input))
            return 0;
    }
    if (queue_push(&mailbox, &input) ||
        queue_receive(&mailbox, PROCESS_MAX, &output) ||
        mailbox.count != VANTA_IPC_QUEUE_DEPTH)
        return 0;

    return 1;
}
