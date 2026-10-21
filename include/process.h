/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stddef.h>
#include "paging.h"

#define PROCESS_MAX 128
#define PROCESS_NAME_MAX 31
#define PROCESS_STACK_SIZE 16384U
#define PROCESS_USER_MAX_ARGS 16U
#define PROCESS_USER_ARG_BYTES_MAX 512U
#define PROCESS_USER_MAX_PAGES 64U
#define PROCESS_FD_BASE 3
#define PROCESS_FD_COUNT 8U
#define PROCESS_USER_PAGE_WRITE 0x01U
#define PROCESS_USER_PAGE_EXEC  0x02U
#define PROCESS_CPU_UNOWNED UINT32_MAX
#define PROCESS_CPU_ANY PROCESS_CPU_UNOWNED

typedef void (*process_entry_t)(void);

typedef enum
{
    PROCESS_UNUSED = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_SLEEPING,
    PROCESS_TERMINATED,
    PROCESS_ZOMBIE,
    PROCESS_REAPING
} process_state_t;

typedef struct process_context
{
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rsi, rdi, rbp, rdx, rcx, rbx, rax;
    uint64_t rsp, rip, rflags, cs, ss;
} process_context_t;

typedef struct process_user_page
{
    uintptr_t virtual_address;
    void* physical_frame;
    uint32_t flags;
} process_user_page_t;

typedef struct
{
    uint32_t pid;
    uint32_t parent_pid;
    /* PROCESS_CPU_UNOWNED unless this process is executing on a CPU. */
    uint32_t owner_cpu;
    uint32_t affinity_cpu;
    process_state_t state;
    int32_t exit_status;
    char name[PROCESS_NAME_MAX + 1];
    uint64_t stack_pointer;
    process_entry_t entry;
    void* kernel_stack;
    paging_address_space_t* address_space;
    process_context_t context;
    /* User-image ownership; frames are reclaimed only after leaving its CR3. */
    uint32_t is_user;
    uint32_t user_page_count;
    uintptr_t user_entry;
    uintptr_t user_stack_base;
    uintptr_t user_stack_top;
    uint64_t user_image_size;
    process_user_page_t user_pages[PROCESS_USER_MAX_PAGES];
    int32_t file_descriptors[PROCESS_FD_COUNT];
} process_t;

void process_initialize(void);
int process_is_initialized(void);
int process_create_kernel(const char* name, uint32_t parent_pid, process_entry_t entry);
/* Copies a single-page flat x86-64 image into an isolated ring-3 process. */
int process_create_user(const char* name, uint32_t parent_pid,
                        const void* image, size_t image_size);
/* Load a validated static ELF64 executable into a private address space. */
int process_create_user_elf(const char* name, uint32_t parent_pid,
                            const void* image, size_t image_size);
int process_create_user_elf_args(const char* name, uint32_t parent_pid,
                                 const void* image, size_t image_size,
                                 uint32_t argc, const char* const argv[]);
/* Load a read-only ELF64 file from the VFS and publish it as a child. */
int process_spawn_user_elf_file(const char* path, uint32_t parent_pid,
                                uint32_t argc, const char* const argv[]);
int process_create_kernel_any(const char* name, uint32_t parent_pid,
                              process_entry_t entry);
int process_create_kernel_on_cpu(const char* name, uint32_t parent_pid,
                                  process_entry_t entry, uint32_t cpu_id);
int process_set_affinity(uint32_t pid, uint32_t cpu_id);
int process_create_idle_for_cpu(uint32_t cpu_id, const char* name,
                                process_entry_t entry);
int process_attach_current(const char* name, uint32_t parent_pid);
int process_set_running(uint32_t pid);
int process_mark_ready(uint32_t pid);
int process_wake(uint32_t pid);
int process_terminate(uint32_t pid);
void process_block_current(void);
void process_terminate_current(void);
void process_terminate_current_with_status(int32_t exit_status);
/* Non-blocking termination for exception paths; failure must fail closed. */
int process_terminate_current_try_with_status(int32_t exit_status);
/* Returns child PID when reaped, 0 when the parent was put to sleep, -1 if not a child. */
int process_wait_child(uint32_t parent_pid, uint32_t child_pid,
                       int32_t* exit_status_out);
void process_save_stack(uint32_t pid, uint64_t stack_pointer);
uint64_t process_get_stack(uint32_t pid);
int process_stack_is_valid(uint32_t pid);
int process_switch_to(uint32_t pid);
/* Atomically releases current_pid and claims a READY process for cpu_id. */
int process_claim_for_cpu(uint32_t next_pid, uint32_t current_pid,
                          uint32_t cpu_id);
/* Claims a process and returns its validated context under the same lock. */
int process_claim_for_cpu_context(uint32_t next_pid, uint32_t current_pid,
                                  uint32_t cpu_id, uint64_t* stack_pointer_out,
                                  paging_address_space_t** address_space_out);
/* Scheduler transition APIs; finish must run after switching off the old stack. */
int process_prepare_context_switch(uint32_t next_pid, uint32_t current_pid,
                                   uint32_t cpu_id, uint64_t* next_stack_out,
                                   paging_address_space_t** next_address_space_out);
int process_finish_context_switch(uint32_t outgoing_pid, uint32_t cpu_id);
int process_reap(uint32_t pid);
int process_fd_install(uint32_t pid, int vfs_handle);
int process_fd_get(uint32_t pid, int descriptor);
int process_fd_remove(uint32_t pid, int descriptor, int* vfs_handle_out);
uint64_t process_schedule(uint64_t current_stack);
/* Copies a process record while holding the process-table lock. */
int process_snapshot(uint32_t pid, process_t* snapshot);
/* Non-blocking snapshot for exception/fault paths that must not deadlock. */
int process_snapshot_try(uint32_t pid, process_t* snapshot);
unsigned int process_count(void);
uint32_t process_current_pid(void);
uint32_t process_current_cpu_id(void);

#endif
