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
/* Caller holds process_table_lock; detach children and reap orphan zombies. */
void process_reparent_children_locked(uint32_t parent_pid);
int process_stack_is_valid_locked(const process_t* process);
int process_release_user_resources(process_t* process);
void process_release_unpublished_user_image(
    paging_address_space_t* space, void* code_frame, void* stack_frame);
int process_install_user_image(const char* name, uint32_t parent_pid,
                               paging_address_space_t* space,
                               uintptr_t entry, uintptr_t stack_base,
                               uintptr_t stack_top, uintptr_t initial_stack,
                               size_t image_size,
                               const process_user_page_t* pages,
                               uint32_t page_count);

#endif
