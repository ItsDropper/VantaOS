/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_MEMORY_H
#define VANTA_NVX_MEMORY_H
#include <stdint.h>
typedef enum nvx_memory_domain {
    NVX_MEMORY_SYSTEM = 0, NVX_MEMORY_DEVICE_LOCAL, NVX_MEMORY_HOST_VISIBLE,
    NVX_MEMORY_HOST_COHERENT
} nvx_memory_domain_t;
typedef enum nvx_memory_access {
    NVX_MEMORY_CPU_READ = 1u << 0, NVX_MEMORY_CPU_WRITE = 1u << 1,
    NVX_MEMORY_GPU_READ = 1u << 2, NVX_MEMORY_GPU_WRITE = 1u << 3
} nvx_memory_access_t;
typedef struct nvx_memory_requirements {
    uint64_t size, alignment;
    uint32_t allowed_domains, required_access;
} nvx_memory_requirements_t;
typedef struct nvx_memory_object nvx_memory_object_t;
/* CPU virtual, system physical, and GPU virtual addresses are never interchangeable. */
typedef struct nvx_memory_mapping {
    void *cpu_virtual_address;
    uint64_t system_physical_address, gpu_virtual_address, size;
} nvx_memory_mapping_t;
#endif
