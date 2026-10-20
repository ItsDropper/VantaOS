/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_submit.h"

#include <stdint.h>

static int nvx_submit_add_overflow(uint64_t a, uint64_t b, uint64_t *out)
{
    if (out == 0 || a > UINT64_MAX - b)
        return 1;
    *out = a + b;
    return 0;
}

static int nvx_submit_valid_access(uint8_t access)
{
    return access == NVX_RELOC_READ || access == NVX_RELOC_WRITE ||
           access == NVX_RELOC_READ_WRITE;
}

nvx_submit_status_t nvx_submit_validate_batch(
    nvx_memory_manager_t *memory,
    const nvx_memory_handle_t *command_buffer,
    size_t command_bytes,
    const nvx_relocation_t *relocations,
    size_t relocation_count,
    nvx_submit_report_t *out_report)
{
    void *command_cpu = 0;
    nvx_memory_info_t command_info;
    nvx_memory_status_t memory_status;
    nvx_submit_report_t report = {0};
    uint64_t command_end;

    if (out_report != 0)
        *out_report = report;
    if (memory == 0 || command_buffer == 0 || out_report == 0 ||
        (relocation_count != 0U && relocations == 0))
        return NVX_SUBMIT_INVALID_ARGUMENT;
    if (command_bytes == 0U || command_bytes > NVX_SUBMIT_MAX_COMMAND_BYTES ||
        (command_bytes & 3U) != 0U || command_bytes > command_buffer->size ||
        nvx_submit_add_overflow(command_buffer->gpu_virtual_address,
                                command_bytes, &command_end))
        return NVX_SUBMIT_INVALID_COMMAND_RANGE;
    if (relocation_count > NVX_SUBMIT_MAX_RELOCATIONS)
        return NVX_SUBMIT_TOO_MANY_RELOCATIONS;

    memory_status = nvx_memory_get_info(memory, command_buffer, &command_info);
    if (memory_status != NVX_MEMORY_OK || command_info.cpu_address == 0)
        return NVX_SUBMIT_MEMORY_ERROR;
    if (command_info.kind != NVX_MEMORY_COMMAND_STORAGE)
        return NVX_SUBMIT_INVALID_COMMAND_RANGE;
    command_cpu = command_info.cpu_address;
    (void)command_cpu;

    report.command_gpu_address = command_info.gpu_virtual_address;
    report.command_bytes = command_bytes;
    report.relocation_count = (uint32_t)relocation_count;

    uint64_t previous_end = 0U;
    for (size_t i = 0; i < relocation_count; ++i) {
        const nvx_relocation_t *reloc = &relocations[i];
        uint64_t target_address;
        uint64_t target_end;
        uint64_t field_end;
        void *target_cpu = 0;
        size_t width = reloc->width;

        if ((width != NVX_RELOC_ADDRESS32 && width != NVX_RELOC_ADDRESS64) ||
            !nvx_submit_valid_access(reloc->access) ||
            reloc->target_span == 0U)
            return NVX_SUBMIT_INVALID_RELOCATION;
        if ((reloc->command_offset & (width - 1U)) != 0U ||
            nvx_submit_add_overflow(reloc->command_offset, width, &field_end) ||
            field_end > command_bytes)
            return NVX_SUBMIT_INVALID_RELOCATION;
        if (i != 0U && reloc->command_offset < previous_end)
            return NVX_SUBMIT_RELOCATION_OVERLAP;
        previous_end = field_end;

        if ((uint64_t)reloc->target_offset + reloc->target_span >
            reloc->target.size)
            return NVX_SUBMIT_TARGET_INVALID;
        if (nvx_submit_add_overflow(reloc->target.gpu_virtual_address,
                                    reloc->target_offset, &target_address) ||
            nvx_submit_add_overflow(target_address, reloc->target_span,
                                    &target_end))
            return NVX_SUBMIT_ADDRESS_OVERFLOW;
        if (width == NVX_RELOC_ADDRESS32 &&
            (target_address > UINT32_MAX ||
             (uint64_t)reloc->target_span - 1U >
                 UINT32_MAX - target_address))
            return NVX_SUBMIT_ADDRESS_WIDTH;
        if ((target_address & (width - 1U)) != 0U)
            return NVX_SUBMIT_INVALID_RELOCATION;

        memory_status = nvx_memory_cpu_address(memory, &reloc->target,
                                               &target_cpu);
        if (memory_status != NVX_MEMORY_OK || target_cpu == 0)
            return NVX_SUBMIT_TARGET_INVALID;
        (void)target_end;

        if ((reloc->access & NVX_RELOC_READ) != 0U)
            ++report.read_relocation_count;
        if ((reloc->access & NVX_RELOC_WRITE) != 0U)
            ++report.write_relocation_count;
        if (width == NVX_RELOC_ADDRESS32)
            ++report.address32_count;
        else
            ++report.address64_count;
    }

    *out_report = report;
    return NVX_SUBMIT_OK;
}
