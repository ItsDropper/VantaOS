/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_INTEL_GEN9_VM_H
#define VANTA_INTEL_GEN9_VM_H

#include "gpu_page_table.h"

#define INTEL_GEN9_VM_READ  1U
#define INTEL_GEN9_VM_WRITE 2U

typedef struct intel_gen9_vm_backend {
    void *context;
    int (*allocate_table)(void *context, size_t entry_count,
                          gpu_page_table_page_t *out_page);
    void (*free_page)(void *context, gpu_page_table_page_t *page);
    int (*invalidate_tlb)(void *context, uint64_t root_physical);
} intel_gen9_vm_backend_t;

/* Configure Gen9 4-level PPGTT encoding over platform-owned DMA/TLB callbacks. */
const gpu_page_table_config_t *intel_gen9_vm_config(void);
int intel_gen9_vm_make_ops(intel_gen9_vm_backend_t *backend,
                           gpu_page_table_ops_t *out_ops);

#endif
