/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_GPU_CORE_H
#define VANTA_GPU_CORE_H

#include "gpu.h"
#include "gpu_vendor.h"
#include <stddef.h>

typedef struct gpu_device {
    gpu_device_identity_t identity;
    gpu_vendor_t vendor;
    gpu_architecture_t architecture;
    gpu_device_state_t state;
    uint64_t verified_capabilities;
    const struct pci_device *pci_device;
    const gpu_driver_ops_t *ops;
    void *driver_private;
} gpu_device_t;

typedef struct gpu_driver_descriptor {
    const char *name;
    uint16_t vendor_id;
    int (*match)(const struct pci_device *device);
    const gpu_driver_ops_t *ops;
} gpu_driver_descriptor_t;

#define GPU_CAPABILITY_MASK ((uint64_t)(GPU_CAP_DISPLAY_SCANOUT | \
    GPU_CAP_2D_ACCELERATION | GPU_CAP_3D_ACCELERATION | GPU_CAP_VIDEO_DECODE | \
    GPU_CAP_VIDEO_ENCODE | GPU_CAP_HARDWARE_CURSOR | GPU_CAP_DMA))

/* Enumerate and report supported display functions; no acceleration is implied. */
void gpu_initialize(void);

int gpu_device_construct(gpu_device_t *device,
                         const struct pci_device *pci_device,
                         const gpu_pci_match_t *match);
int gpu_device_transition(gpu_device_t *device, gpu_device_state_t next_state);
int gpu_device_publish_capabilities(gpu_device_t *device,
                                    uint64_t capabilities);
void gpu_device_clear_capabilities(gpu_device_t *device);
/* Read-only inventory; returns 0 without partial writes when capacity is too small. */
int gpu_discover_pci(gpu_device_t *out_devices, size_t capacity,
                     size_t *out_count);
int gpu_driver_matches(const gpu_driver_descriptor_t *driver,
                       const struct pci_device *device);
/* Run the backend probe/initialize state machine; failed backends fail closed. */
int gpu_driver_start(gpu_device_t *device,
                     const gpu_driver_descriptor_t *driver);
/* Quiesce a running backend and release its owned resources. */
int gpu_driver_stop(gpu_device_t *device);

#endif
