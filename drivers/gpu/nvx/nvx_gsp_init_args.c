/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_init_args.h"

#include <string.h>

_Static_assert(sizeof(nvx_gsp_message_queue_init_args_t) == 64U,
               "GSP message queue init ABI size changed");
_Static_assert(sizeof(nvx_gsp_sr_init_args_t) == 16U,
               "GSP SR init ABI size changed");
_Static_assert(offsetof(nvx_gsp_arguments_cached_t, flags) == 8U,
               "GSP arguments flags offset changed");
_Static_assert(offsetof(nvx_gsp_arguments_cached_t, message_queue) == 16U,
               "GSP message queue arguments offset changed");
_Static_assert(offsetof(nvx_gsp_arguments_cached_t, sr) == 80U,
               "GSP SR arguments offset changed");
_Static_assert(offsetof(nvx_gsp_arguments_cached_t, gpu_instance) == 100U,
               "GSP GPU instance offset changed");
_Static_assert(sizeof(nvx_gsp_arguments_cached_t) == 184U,
               "GSP cached arguments ABI size changed");
_Static_assert(sizeof(nvx_gsp_arguments_cached_t) <= NVX_GSP_INIT_ARGS_PAGE_SIZE,
               "GSP arguments must fit in one page");

static int add_overflow_u64(uint64_t a, uint64_t b, uint64_t *out)
{
    if (!out || a > UINT64_MAX - b)
        return 1;
    *out = a + b;
    return 0;
}

static int ranges_overlap(const void *a, size_t a_size,
                          const void *b, size_t b_size)
{
    uintptr_t a_start = (uintptr_t)a;
    uintptr_t b_start = (uintptr_t)b;

    if (!a_size || !b_size)
        return 0;
    if (a_start > UINTPTR_MAX - a_size ||
        b_start > UINTPTR_MAX - b_size)
        return 1;
    return a_start < b_start + b_size && b_start < a_start + a_size;
}

static int range_valid(uint64_t start, uint64_t size, uint64_t limit)
{
    uint64_t end;
    return !add_overflow_u64(start, size, &end) && end <= limit;
}

static int bytes_are_zero(const uint8_t *bytes, size_t size)
{
    for (size_t i = 0U; i < size; ++i)
        if (bytes[i] != 0U)
            return 0;
    return 1;
}

static void write_le64(uint8_t *destination, uint64_t value)
{
    for (unsigned i = 0U; i < 8U; ++i)
        destination[i] = (uint8_t)(value >> (i * 8U));
}

static int queue_profile_valid(uint32_t header_size, uint32_t min_size,
                               uint32_t max_size, uint32_t header_alignment,
                               uint32_t element_alignment)
{
    /*
     * First supported profile is GA10x without confidential-compute queue
     * encryption. NVIDIA documents encryption as an unstable queue ABI.
     */
    return header_size == NVX_GSP_QUEUE_HEADER_SIZE_PLAIN &&
           min_size == NVX_GSP_QUEUE_ELEMENT_MIN_SIZE &&
           max_size == NVX_GSP_QUEUE_ELEMENT_MAX_SIZE &&
           header_alignment == NVX_GSP_QUEUE_HEADER_ALIGNMENT &&
           element_alignment == NVX_GSP_QUEUE_ELEMENT_ALIGNMENT;
}

nvx_gsp_init_args_status_t nvx_gsp_shared_queue_required_size(
    uint64_t command_size, uint64_t status_size, uint64_t *out_required,
    uint64_t *out_page_table_size, uint32_t *out_pte_count)
{
    uint64_t queue_bytes, queue_pages, pte_bytes, pte_pages;
    uint64_t pte_count, page_table_size, total_size;

    if (!out_required || !out_page_table_size || !out_pte_count ||
        ranges_overlap(out_required, sizeof(*out_required),
                       out_page_table_size, sizeof(*out_page_table_size)) ||
        ranges_overlap(out_required, sizeof(*out_required),
                       out_pte_count, sizeof(*out_pte_count)) ||
        ranges_overlap(out_page_table_size, sizeof(*out_page_table_size),
                       out_pte_count, sizeof(*out_pte_count)))
        return NVX_GSP_INIT_ARGS_INVALID_ARGUMENT;
    *out_required = 0U;
    *out_page_table_size = 0U;
    *out_pte_count = 0U;

    if (command_size < 2U * NVX_GSP_PAGE_SIZE ||
        status_size < 2U * NVX_GSP_PAGE_SIZE ||
        (command_size & (NVX_GSP_PAGE_SIZE - 1U)) != 0U ||
        (status_size & (NVX_GSP_PAGE_SIZE - 1U)) != 0U)
        return NVX_GSP_INIT_ARGS_BAD_QUEUE_SIZE;
    if (add_overflow_u64(command_size, status_size, &queue_bytes))
        return NVX_GSP_INIT_ARGS_SIZE_OVERFLOW;

    queue_pages = queue_bytes / NVX_GSP_PAGE_SIZE;
    if (queue_pages > UINT32_MAX)
        return NVX_GSP_INIT_ARGS_SIZE_OVERFLOW;
    if (queue_pages > (UINT64_MAX / sizeof(uint64_t)))
        return NVX_GSP_INIT_ARGS_SIZE_OVERFLOW;

    pte_bytes = queue_pages * sizeof(uint64_t);
    if (add_overflow_u64(pte_bytes, NVX_GSP_PAGE_SIZE - 1U, &pte_pages))
        return NVX_GSP_INIT_ARGS_SIZE_OVERFLOW;
    pte_pages /= NVX_GSP_PAGE_SIZE;
    if (add_overflow_u64(queue_pages, pte_pages, &pte_count) ||
        pte_count > UINT32_MAX ||
        pte_count > UINT64_MAX / sizeof(uint64_t))
        return NVX_GSP_INIT_ARGS_SIZE_OVERFLOW;

    pte_bytes = pte_count * sizeof(uint64_t);
    if (add_overflow_u64(pte_bytes, NVX_GSP_PAGE_SIZE - 1U,
                         &page_table_size))
        return NVX_GSP_INIT_ARGS_SIZE_OVERFLOW;
    page_table_size &= ~((uint64_t)NVX_GSP_PAGE_SIZE - 1U);
    if (add_overflow_u64(page_table_size, queue_bytes, &total_size))
        return NVX_GSP_INIT_ARGS_SIZE_OVERFLOW;

    *out_required = total_size;
    *out_page_table_size = page_table_size;
    *out_pte_count = (uint32_t)pte_count;
    return NVX_GSP_INIT_ARGS_OK;
}

nvx_gsp_init_args_status_t nvx_gsp_shared_queue_plan(
    const nvx_gsp_shared_queue_config_t *config,
    nvx_gsp_shared_queue_layout_t *out)
{
    nvx_gsp_shared_queue_layout_t layout = {0};
    nvx_gsp_init_args_status_t status;
    uint64_t status_offset, status_end;

    if (!out || !config ||
        ranges_overlap(out, sizeof(*out), config, sizeof(*config)))
        return NVX_GSP_INIT_ARGS_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!queue_profile_valid(config->queue_element_header_size,
            config->queue_element_size_min, config->queue_element_size_max,
            config->queue_header_alignment, config->queue_element_alignment))
        return NVX_GSP_INIT_ARGS_UNSUPPORTED_CONFIGURATION;
    if (config->shared_mem_phys_addr == 0U ||
        (config->shared_mem_phys_addr & (NVX_GSP_PAGE_SIZE - 1U)) != 0U ||
        (config->allocation_size & (NVX_GSP_PAGE_SIZE - 1U)) != 0U)
        return NVX_GSP_INIT_ARGS_BAD_ALIGNMENT;
    if (config->shared_mem_phys_addr > UINT64_MAX - config->allocation_size)
        return NVX_GSP_INIT_ARGS_SIZE_OVERFLOW;

    status = nvx_gsp_shared_queue_required_size(config->command_queue_size,
        config->status_queue_size, &layout.required_size,
        &layout.page_table_size, &layout.page_table_entry_count);
    if (status != NVX_GSP_INIT_ARGS_OK)
        return status;
    if (config->allocation_size < layout.required_size)
        return NVX_GSP_INIT_ARGS_INSUFFICIENT_MEMORY;
    /*
     * This first implementation supports a single self-mapping PTE page.
     * That covers NVIDIA's normal 256 KiB queues and up to 511 combined queue
     * pages; larger layouts need a verified multi-page PTE bootstrap.
     */
    if (layout.page_table_size != NVX_GSP_PAGE_SIZE ||
        layout.page_table_entry_count !=
            layout.required_size / NVX_GSP_PAGE_SIZE)
        return NVX_GSP_INIT_ARGS_UNSUPPORTED_CONFIGURATION;

    layout.command_queue_offset = layout.page_table_size;
    if (add_overflow_u64(layout.command_queue_offset,
                         config->command_queue_size, &status_offset) ||
        add_overflow_u64(status_offset, config->status_queue_size, &status_end))
        return NVX_GSP_INIT_ARGS_SIZE_OVERFLOW;
    if (status_end > layout.required_size ||
        !range_valid(layout.command_queue_offset, config->command_queue_size,
                     layout.required_size) ||
        !range_valid(status_offset, config->status_queue_size,
                     layout.required_size))
        return NVX_GSP_INIT_ARGS_BAD_LAYOUT;

    layout.shared_mem_phys_addr = config->shared_mem_phys_addr;
    layout.allocation_size = config->allocation_size;
    layout.command_queue_size = config->command_queue_size;
    layout.status_queue_offset = status_offset;
    layout.status_queue_size = config->status_queue_size;
    layout.queue_element_header_size = config->queue_element_header_size;
    layout.queue_element_size_min = config->queue_element_size_min;
    layout.queue_element_size_max = config->queue_element_size_max;
    layout.queue_header_alignment = config->queue_header_alignment;
    layout.queue_element_alignment = config->queue_element_alignment;
    *out = layout;
    return NVX_GSP_INIT_ARGS_OK;
}

nvx_gsp_init_args_status_t nvx_gsp_shared_queue_validate(
    const nvx_gsp_shared_queue_layout_t *layout)
{
    nvx_gsp_shared_queue_config_t config;
    nvx_gsp_shared_queue_layout_t expected;
    nvx_gsp_init_args_status_t status;

    if (!layout)
        return NVX_GSP_INIT_ARGS_INVALID_ARGUMENT;
    config.shared_mem_phys_addr = layout->shared_mem_phys_addr;
    config.allocation_size = layout->allocation_size;
    config.command_queue_size = layout->command_queue_size;
    config.status_queue_size = layout->status_queue_size;
    config.queue_element_header_size = layout->queue_element_header_size;
    config.queue_element_size_min = layout->queue_element_size_min;
    config.queue_element_size_max = layout->queue_element_size_max;
    config.queue_header_alignment = layout->queue_header_alignment;
    config.queue_element_alignment = layout->queue_element_alignment;

    status = nvx_gsp_shared_queue_plan(&config, &expected);
    if (status != NVX_GSP_INIT_ARGS_OK)
        return status;
    if (layout->required_size != expected.required_size ||
        layout->page_table_size != expected.page_table_size ||
        layout->command_queue_offset != expected.command_queue_offset ||
        layout->status_queue_offset != expected.status_queue_offset ||
        layout->page_table_entry_count != expected.page_table_entry_count)
        return NVX_GSP_INIT_ARGS_BAD_LAYOUT;
    return NVX_GSP_INIT_ARGS_OK;
}

nvx_gsp_init_args_status_t nvx_gsp_shared_queue_build_pte_table(
    const nvx_gsp_shared_queue_layout_t *layout, void *shared_mem_cpu,
    size_t capacity)
{
    nvx_gsp_init_args_status_t status;
    uint8_t *table = (uint8_t *)shared_mem_cpu;

    if (!layout || !shared_mem_cpu)
        return NVX_GSP_INIT_ARGS_INVALID_ARGUMENT;
    if (ranges_overlap(layout, sizeof(*layout), shared_mem_cpu, capacity))
        return NVX_GSP_INIT_ARGS_INVALID_ARGUMENT;
    status = nvx_gsp_shared_queue_validate(layout);
    if (status != NVX_GSP_INIT_ARGS_OK)
        return status;
    if (capacity < layout->allocation_size)
        return NVX_GSP_INIT_ARGS_INSUFFICIENT_MEMORY;
    if (layout->page_table_size != NVX_GSP_PAGE_SIZE ||
        layout->page_table_entry_count >
            NVX_GSP_PAGE_SIZE / sizeof(uint64_t) ||
        layout->page_table_entry_count !=
            layout->required_size / NVX_GSP_PAGE_SIZE)
        return NVX_GSP_INIT_ARGS_UNSUPPORTED_CONFIGURATION;

    memset(table, 0, (size_t)layout->page_table_size);
    for (uint32_t i = 0U; i < layout->page_table_entry_count; ++i) {
        uint64_t offset = (uint64_t)i * NVX_GSP_PAGE_SIZE;
        uint64_t physical_address;
        if (add_overflow_u64(layout->shared_mem_phys_addr, offset,
                             &physical_address))
            return NVX_GSP_INIT_ARGS_SIZE_OVERFLOW;
        write_le64(table + (size_t)i * sizeof(uint64_t), physical_address);
    }
    __atomic_thread_fence(__ATOMIC_RELEASE);
    return NVX_GSP_INIT_ARGS_OK;
}

nvx_gsp_init_args_status_t nvx_gsp_arguments_cached_build(
    const nvx_gsp_shared_queue_layout_t *layout, uint32_t gpu_instance,
    uint64_t flags, nvx_gsp_arguments_cached_t *out)
{
    nvx_gsp_arguments_cached_t args = {0};
    nvx_gsp_init_args_status_t status;

    if (!out || !layout)
        return NVX_GSP_INIT_ARGS_INVALID_ARGUMENT;
    if (ranges_overlap(out, sizeof(*out), layout, sizeof(*layout)))
        return NVX_GSP_INIT_ARGS_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if ((flags & ~NVX_GSP_INIT_ARGS_FLAG_STACK_IN_DMEM) != 0U)
        return NVX_GSP_INIT_ARGS_UNSUPPORTED_CONFIGURATION;
    status = nvx_gsp_shared_queue_validate(layout);
    if (status != NVX_GSP_INIT_ARGS_OK)
        return status;

    args.magic = NVX_GSP_INIT_ARGS_MAGIC;
    args.size = (uint16_t)sizeof(args);
    args.flags = flags;
    args.message_queue.shared_mem_phys_addr = layout->shared_mem_phys_addr;
    args.message_queue.command_queue_offset = layout->command_queue_offset;
    args.message_queue.status_queue_offset = layout->status_queue_offset;
    args.message_queue.queue_element_header_size =
        layout->queue_element_header_size;
    args.message_queue.queue_element_size_min = layout->queue_element_size_min;
    args.message_queue.queue_element_size_max = layout->queue_element_size_max;
    args.message_queue.queue_header_alignment = layout->queue_header_alignment;
    args.message_queue.queue_element_alignment = layout->queue_element_alignment;
    args.message_queue.page_table_entry_count = layout->page_table_entry_count;
    args.gpu_instance = gpu_instance;
    *out = args;
    return NVX_GSP_INIT_ARGS_OK;
}

nvx_gsp_init_args_status_t nvx_gsp_arguments_cached_materialize(
    const nvx_gsp_arguments_cached_t *args, void *page, size_t capacity)
{
    if (!args || !page)
        return NVX_GSP_INIT_ARGS_INVALID_ARGUMENT;
    if (capacity < NVX_GSP_INIT_ARGS_PAGE_SIZE)
        return NVX_GSP_INIT_ARGS_INSUFFICIENT_MEMORY;
    if (ranges_overlap(args, sizeof(*args), page, NVX_GSP_INIT_ARGS_PAGE_SIZE))
        return NVX_GSP_INIT_ARGS_INVALID_ARGUMENT;
    if (args->magic != NVX_GSP_INIT_ARGS_MAGIC ||
        args->size != sizeof(*args) ||
        (args->flags & ~NVX_GSP_INIT_ARGS_FLAG_STACK_IN_DMEM) != 0U ||
        args->message_queue.flags != 0U ||
        args->sr.old_level != 0U || args->sr.flags != 0U ||
        !bytes_are_zero(args->reserved, sizeof(args->reserved)) ||
        !bytes_are_zero(args->message_queue.reserved,
                        sizeof(args->message_queue.reserved)) ||
        !bytes_are_zero(args->sr.reserved, sizeof(args->sr.reserved)) ||
        !bytes_are_zero(args->reserved2, sizeof(args->reserved2)) ||
        args->message_queue.shared_mem_phys_addr == 0U ||
        (args->message_queue.shared_mem_phys_addr &
         (NVX_GSP_PAGE_SIZE - 1U)) != 0U ||
        args->message_queue.command_queue_offset == 0U ||
        (args->message_queue.command_queue_offset &
         (NVX_GSP_PAGE_SIZE - 1U)) != 0U ||
        (args->message_queue.status_queue_offset &
         (NVX_GSP_PAGE_SIZE - 1U)) != 0U ||
        args->message_queue.page_table_entry_count == 0U ||
        !queue_profile_valid(args->message_queue.queue_element_header_size,
            args->message_queue.queue_element_size_min,
            args->message_queue.queue_element_size_max,
            args->message_queue.queue_header_alignment,
            args->message_queue.queue_element_alignment))
        return NVX_GSP_INIT_ARGS_BAD_ARGUMENTS;

    memset(page, 0, NVX_GSP_INIT_ARGS_PAGE_SIZE);
    memcpy(page, args, sizeof(*args));
    return NVX_GSP_INIT_ARGS_OK;
}
