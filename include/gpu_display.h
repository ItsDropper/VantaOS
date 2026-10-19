/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_GPU_DISPLAY_H
#define VANTA_GPU_DISPLAY_H
#include <stddef.h>
#include <stdint.h>
typedef struct gpu_display_mode {
    uint32_t width, height, refresh_millihz, pixel_format, pitch_bytes;
} gpu_display_mode_t;
typedef struct gpu_display_info {
    gpu_display_mode_t active_mode;
    uint64_t framebuffer_physical_address;
    size_t framebuffer_size;
    uint8_t framebuffer_is_write_combining;
    uint8_t hardware_cursor_available;
} gpu_display_info_t;
/* Scanout and presentation are independent of 3D command submission. */
typedef struct gpu_display_ops {
    int (*query_info)(gpu_display_info_t *out_info);
    int (*set_mode)(const gpu_display_mode_t *mode);
    int (*present)(const void *pixels, size_t length, uint32_t pitch_bytes);
} gpu_display_ops_t;
#endif
