/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef PROCESS_EXIT_LOG_H
#define PROCESS_EXIT_LOG_H

#include <stdint.h>
#include "process.h"

/*
 * Bounded log of recently reclaimed user processes. A kernel observer cannot
 * wait() on a user child (zombies are kept only for user parents), and the
 * process record is reset on reap, so this is the one place the exit status
 * and the memory accounting of a finished process remain inspectable.
 */
#define PROCESS_EXIT_LOG_ENTRIES 64U

typedef struct process_exit_record
{
    uint64_t sequence;           /* strictly increasing, never reused */
    uint32_t pid;
    int32_t exit_status;
    uint32_t demand_faults;      /* demand pages the process faulted in */
    uint32_t resident_at_exit;   /* demand pages still mapped at exit */
    uint32_t frames_reclaimed;   /* demand frames returned to the PMM */
    uint32_t fault_events;       /* user page faults seen (all outcomes) */
    uint32_t last_fault_class;   /* vm_fault_class_t */
    uint64_t last_fault_address;
    uint64_t last_fault_error;
    char name[PROCESS_NAME_MAX + 1];
} process_exit_record_t;

void process_exit_log_record(const process_exit_record_t* record);
/* Sequence of the most recent record (0 if none); use as a "before" mark. */
uint64_t process_exit_log_sequence(void);
/*
 * Find a record for pid newer than after_sequence. A non-NULL name must also
 * match: a PID is reused as soon as its slot is freed, so an unrelated
 * process that exited just before the caller's spawn can carry the same PID.
 * Returns 1 when found.
 */
int process_exit_log_find(uint32_t pid, uint64_t after_sequence,
                          const char* name, process_exit_record_t* out);

#endif
