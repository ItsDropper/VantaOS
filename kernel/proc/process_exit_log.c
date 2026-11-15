/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "process_exit_log.h"
#include "spinlock.h"

static spinlock_t exit_log_lock = SPINLOCK_INITIALIZER;
static process_exit_record_t exit_log[PROCESS_EXIT_LOG_ENTRIES];
static uint64_t exit_log_next_sequence = 1U;

void process_exit_log_record(const process_exit_record_t* record)
{
    if (!record)
        return;
    uint64_t flags = spinlock_lock_irqsave(&exit_log_lock);
    process_exit_record_t* slot =
        &exit_log[exit_log_next_sequence % PROCESS_EXIT_LOG_ENTRIES];
    *slot = *record;
    slot->sequence = exit_log_next_sequence++;
    spinlock_unlock_irqrestore(&exit_log_lock, flags);
}

uint64_t process_exit_log_sequence(void)
{
    uint64_t flags = spinlock_lock_irqsave(&exit_log_lock);
    uint64_t sequence = exit_log_next_sequence - 1U;
    spinlock_unlock_irqrestore(&exit_log_lock, flags);
    return sequence;
}

static int names_match(const char* wanted, const char* actual)
{
    if (!wanted)
        return 1;
    for (unsigned int i = 0U; i <= PROCESS_NAME_MAX; i++)
    {
        if (wanted[i] != actual[i])
            return 0;
        if (wanted[i] == '\0')
            return 1;
    }
    return 1;
}

int process_exit_log_find(uint32_t pid, uint64_t after_sequence,
                          const char* name, process_exit_record_t* out)
{
    int found = 0;
    uint64_t flags = spinlock_lock_irqsave(&exit_log_lock);
    for (uint64_t seq = exit_log_next_sequence - 1U;
         seq > after_sequence && seq != 0U &&
         exit_log_next_sequence - seq <= PROCESS_EXIT_LOG_ENTRIES; seq--)
    {
        const process_exit_record_t* slot =
            &exit_log[seq % PROCESS_EXIT_LOG_ENTRIES];
        if (slot->sequence == seq && slot->pid == pid &&
            names_match(name, slot->name))
        {
            if (out)
                *out = *slot;
            found = 1;
            break;
        }
    }
    spinlock_unlock_irqrestore(&exit_log_lock, flags);
    return found;
}
