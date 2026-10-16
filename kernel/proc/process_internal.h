#ifndef PROCESS_INTERNAL_H
#define PROCESS_INTERNAL_H

#include <stdint.h>
#include "process.h"
#include "cpu_local.h"
#include "spinlock.h"

/* Private process-table contract shared by lifecycle implementation units. */
extern process_t process_table_records[PROCESS_MAX];
extern uint8_t process_stack_storage[PROCESS_MAX][PROCESS_STACK_SIZE];
extern int process_initialized_state;
extern uint32_t process_legacy_current_pid;
extern spinlock_t process_table_lock;

uint64_t process_table_lock_acquire(void);
void process_table_lock_release(uint64_t flags);
cpu_local_t* process_local_state(void);
uint32_t process_current_slot(void);
void process_set_current_slot(uint32_t pid);
int process_is_idle_locked(uint32_t pid);
void process_reset_record(process_t* process);
int process_stack_is_valid_locked(const process_t* process);
int process_release_user_resources(process_t* process);

#endif
