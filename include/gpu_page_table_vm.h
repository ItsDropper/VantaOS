/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_GPU_PAGE_TABLE_VM_H
#define VANTA_GPU_PAGE_TABLE_VM_H

#include "gpu_page_table.h"
#include "gpu_vm.h"

/* Adapt the vendor page-table walker to gpu_vm's transactional map interface. */
int gpu_page_table_make_vm_ops(gpu_page_table_t *table, gpu_vm_ops_t *out_ops);

#endif
