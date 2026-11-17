/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_SYSCALL_VM_H
#define VANTA_SYSCALL_VM_H

#include <stdint.h>

#include "paging.h"

/*
 * Logic behind the mmap/munmap/brk syscalls. Each function takes the calling
 * process's own address space and the raw register arguments, and returns the
 * value to place in RAX: a non-negative result or a negative errno. They are
 * kept free of exception-frame types so host tests can drive them.
 *
 * Only a process may change its own address space; callers must pass the
 * address space the current CPU is running.
 */
int64_t syscall_vm_mmap(paging_address_space_t* space, uint64_t length,
                        uint64_t prot, uint64_t flags);
int64_t syscall_vm_munmap(paging_address_space_t* space, uint64_t address,
                          uint64_t length);
int64_t syscall_vm_brk(paging_address_space_t* space, uint64_t requested);

#endif
