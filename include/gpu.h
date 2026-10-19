/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_GPU_H
#define VANTA_GPU_H
#include <stdint.h>
struct pci_device;
typedef enum gpu_device_state {
    GPU_DEVICE_DISCOVERED = 0, GPU_DEVICE_PROBING, GPU_DEVICE_INITIALIZED,
    GPU_DEVICE_RUNNING, GPU_DEVICE_QUIESCING, GPU_DEVICE_FAILED
} gpu_device_state_t;
enum gpu_capability {
    GPU_CAP_DISPLAY_SCANOUT = 1ULL << 0, GPU_CAP_2D_ACCELERATION = 1ULL << 1,
    GPU_CAP_3D_ACCELERATION = 1ULL << 2, GPU_CAP_VIDEO_DECODE = 1ULL << 3,
    GPU_CAP_VIDEO_ENCODE = 1ULL << 4, GPU_CAP_HARDWARE_CURSOR = 1ULL << 5,
    GPU_CAP_DMA = 1ULL << 6
};
typedef struct gpu_device_identity {
    uint16_t vendor_id, device_id, subsystem_vendor_id, subsystem_device_id;
    uint8_t revision_id, class_code, subclass, programming_interface;
} gpu_device_identity_t;
typedef struct gpu_device gpu_device_t;
typedef struct gpu_driver_ops {
    int (*probe)(gpu_device_t *device);
    int (*initialize)(gpu_device_t *device);
    int (*quiesce)(gpu_device_t *device);
    void (*shutdown)(gpu_device_t *device);
} gpu_driver_ops_t;
#endif
