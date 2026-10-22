/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_registers.h"

#include <limits.h>

gpu_register_status_t gpu_register_read(const gpu_mmio_region_t *region,
                                        size_t offset, uint32_t *value)
{
    if (region == NULL || value == NULL)
        return GPU_REGISTER_INVALID;
    return gpu_mmio_read32(region, offset, value) == GPU_MMIO_OK
               ? GPU_REGISTER_OK : GPU_REGISTER_IO_ERROR;
}

gpu_register_status_t gpu_register_write(const gpu_mmio_region_t *region,
                                         size_t offset, uint32_t value)
{
    if (region == NULL)
        return GPU_REGISTER_INVALID;
    return gpu_mmio_write32(region, offset, value) == GPU_MMIO_OK
               ? GPU_REGISTER_OK : GPU_REGISTER_IO_ERROR;
}

gpu_register_status_t gpu_register_update(const gpu_mmio_region_t *region,
                                          size_t offset, uint32_t clear_mask,
                                          uint32_t set_mask,
                                          uint32_t *out_value)
{
    uint32_t old_value, new_value;
    gpu_register_status_t status;

    if ((set_mask & clear_mask) != 0U)
        return GPU_REGISTER_INVALID;
    status = gpu_register_read(region, offset, &old_value);
    if (status != GPU_REGISTER_OK)
        return status;
    new_value = (old_value & ~clear_mask) | set_mask;
    status = gpu_register_write(region, offset, new_value);
    if (status != GPU_REGISTER_OK)
        return status;
    if (out_value != NULL)
        *out_value = new_value;
    return GPU_REGISTER_OK;
}

gpu_register_status_t gpu_register_poll(const gpu_mmio_region_t *region,
                                        size_t offset, uint32_t mask,
                                        uint32_t expected,
                                        uint32_t max_reads,
                                        uint32_t *last_value)
{
    uint32_t value = 0U;

    if (region == NULL || mask == 0U || max_reads == 0U ||
        (expected & ~mask) != 0U)
        return GPU_REGISTER_INVALID;
    if (last_value != NULL)
        *last_value = 0U;
    for (uint32_t i = 0U; i < max_reads; ++i) {
        gpu_register_status_t status =
            gpu_register_read(region, offset, &value);
        if (status != GPU_REGISTER_OK)
            return status;
        if ((value & mask) == expected) {
            if (last_value != NULL)
                *last_value = value;
            return GPU_REGISTER_OK;
        }
    }
    if (last_value != NULL)
        *last_value = value;
    return GPU_REGISTER_TIMEOUT;
}

gpu_register_status_t gpu_register_field_encode(gpu_register_field_t field,
                                                uint32_t value,
                                                uint32_t *encoded)
{
    uint32_t shifted;

    if (encoded == NULL || field.mask == 0U || field.shift >= 32U)
        return GPU_REGISTER_INVALID;
    if ((field.mask & ((1U << field.shift) - 1U)) != 0U)
        return GPU_REGISTER_INVALID;
    if (value > (field.mask >> field.shift))
        return GPU_REGISTER_INVALID;
    shifted = value << field.shift;
    if ((shifted & ~field.mask) != 0U)
        return GPU_REGISTER_INVALID;
    *encoded = shifted;
    return GPU_REGISTER_OK;
}

uint32_t gpu_register_field_decode(gpu_register_field_t field,
                                   uint32_t register_value)
{
    if (field.mask == 0U || field.shift >= 32U)
        return 0U;
    return (register_value & field.mask) >> field.shift;
}

gpu_register_status_t gpu_register_apply_sequence(
    const gpu_mmio_region_t *region, const gpu_register_write_t *writes,
    size_t count, uint32_t max_reads_per_verify, size_t *failed_index)
{
    if (failed_index != NULL)
        *failed_index = SIZE_MAX;
    if (region == NULL || (count != 0U && writes == NULL))
        return GPU_REGISTER_INVALID;

    for (size_t i = 0U; i < count; ++i) {
        uint32_t observed = 0U;
        gpu_register_status_t status;
        if ((writes[i].offset & 3U) != 0U)
            status = GPU_REGISTER_INVALID;
        else
            status = gpu_register_write(region, writes[i].offset,
                                        writes[i].value);
        if (status == GPU_REGISTER_OK && writes[i].verify_mask != 0U) {
            if (max_reads_per_verify == 0U)
                status = GPU_REGISTER_INVALID;
            else
                status = gpu_register_poll(region, writes[i].offset,
                    writes[i].verify_mask,
                    writes[i].value & writes[i].verify_mask,
                    max_reads_per_verify, &observed);
            if (status == GPU_REGISTER_TIMEOUT)
                status = GPU_REGISTER_VERIFY_FAILED;
        }
        if (status != GPU_REGISTER_OK) {
            if (failed_index != NULL)
                *failed_index = i;
            return status;
        }
    }
    return GPU_REGISTER_OK;
}
