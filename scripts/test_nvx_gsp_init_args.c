/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_init_args.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define QUEUE_SIZE UINT64_C(0x40000)
static uint8_t args_page[NVX_GSP_INIT_ARGS_PAGE_SIZE]
    __attribute__((aligned(NVX_GSP_INIT_ARGS_PAGE_SIZE)));

static nvx_gsp_shared_queue_config_t config_for(uint64_t physical,
                                                uint64_t allocation_size)
{
    nvx_gsp_shared_queue_config_t config = {0};
    config.shared_mem_phys_addr = physical;
    config.allocation_size = allocation_size;
    config.command_queue_size = QUEUE_SIZE;
    config.status_queue_size = QUEUE_SIZE;
    config.queue_element_header_size = NVX_GSP_QUEUE_HEADER_SIZE_PLAIN;
    config.queue_element_size_min = NVX_GSP_QUEUE_ELEMENT_MIN_SIZE;
    config.queue_element_size_max = NVX_GSP_QUEUE_ELEMENT_MAX_SIZE;
    config.queue_header_alignment = NVX_GSP_QUEUE_HEADER_ALIGNMENT;
    config.queue_element_alignment = NVX_GSP_QUEUE_ELEMENT_ALIGNMENT;
    return config;
}

int main(void)
{
    uint64_t required = 0U, page_table = 0U;
    uint32_t pte_count = 0U;
    nvx_gsp_shared_queue_config_t config;
    nvx_gsp_shared_queue_layout_t layout = {0}, bad_layout;
    nvx_gsp_arguments_cached_t args = {0}, bad_args;
    nvx_gsp_init_args_status_t status;

    assert(sizeof(nvx_gsp_arguments_cached_t) == 184U);
    assert(nvx_gsp_shared_queue_required_size(QUEUE_SIZE, QUEUE_SIZE,
        &required, &page_table, &pte_count) == NVX_GSP_INIT_ARGS_OK);
    assert(required == UINT64_C(528384));
    assert(page_table == NVX_GSP_PAGE_SIZE);
    assert(pte_count == 129U);

    config = config_for(UINT64_C(0x10000000), required);
    assert(nvx_gsp_shared_queue_plan(&config, &layout) == NVX_GSP_INIT_ARGS_OK);
    assert(layout.command_queue_offset == NVX_GSP_PAGE_SIZE);
    assert(layout.status_queue_offset == NVX_GSP_PAGE_SIZE + QUEUE_SIZE);
    assert(layout.status_queue_offset + layout.status_queue_size == required);
    assert(nvx_gsp_shared_queue_validate(&layout) == NVX_GSP_INIT_ARGS_OK);

    assert(nvx_gsp_arguments_cached_build(&layout, 0U,
        NVX_GSP_INIT_ARGS_FLAG_STACK_IN_DMEM, &args) == NVX_GSP_INIT_ARGS_OK);
    assert(args.magic == NVX_GSP_INIT_ARGS_MAGIC);
    assert(args.size == sizeof(args));
    assert(args.gpu_instance == 0U);
    assert(args.flags == NVX_GSP_INIT_ARGS_FLAG_STACK_IN_DMEM);
    assert(args.message_queue.shared_mem_phys_addr == config.shared_mem_phys_addr);
    assert(args.message_queue.page_table_entry_count == 129U);
    assert(args.message_queue.command_queue_offset == NVX_GSP_PAGE_SIZE);
    assert(args.message_queue.status_queue_offset == NVX_GSP_PAGE_SIZE + QUEUE_SIZE);

    memset(args_page, 0xA5, sizeof(args_page));
    assert(nvx_gsp_arguments_cached_materialize(&args, args_page,
        sizeof(args_page)) == NVX_GSP_INIT_ARGS_OK);
    assert(memcmp(args_page, &args, sizeof(args)) == 0);
    for (size_t i = sizeof(args); i < sizeof(args_page); ++i)
        assert(args_page[i] == 0U);

    assert(nvx_gsp_arguments_cached_materialize(&args, args_page,
        sizeof(args_page) - 1U) == NVX_GSP_INIT_ARGS_INSUFFICIENT_MEMORY);
    assert(nvx_gsp_arguments_cached_build(&layout, 0U,
        NVX_GSP_INIT_ARGS_FLAG_IN_PM_TRANSITION, &bad_args) ==
        NVX_GSP_INIT_ARGS_UNSUPPORTED_CONFIGURATION);

    bad_layout = layout;
    bad_layout.status_queue_offset += NVX_GSP_PAGE_SIZE;
    assert(nvx_gsp_shared_queue_validate(&bad_layout) == NVX_GSP_INIT_ARGS_BAD_LAYOUT);

    config = config_for(UINT64_C(0x10000000), required - NVX_GSP_PAGE_SIZE);
    assert(nvx_gsp_shared_queue_plan(&config, &bad_layout) ==
        NVX_GSP_INIT_ARGS_INSUFFICIENT_MEMORY);
    config = config_for(UINT64_C(0x10000001), required);
    assert(nvx_gsp_shared_queue_plan(&config, &bad_layout) ==
        NVX_GSP_INIT_ARGS_BAD_ALIGNMENT);
    config = config_for(UINT64_MAX - NVX_GSP_PAGE_SIZE + 1U, required);
    assert(nvx_gsp_shared_queue_plan(&config, &bad_layout) ==
        NVX_GSP_INIT_ARGS_SIZE_OVERFLOW);
    config = config_for(UINT64_C(0x10000000), required);
    config.queue_element_header_size = 40U; /* CC/encrypted ABI not implemented. */
    assert(nvx_gsp_shared_queue_plan(&config, &bad_layout) ==
        NVX_GSP_INIT_ARGS_UNSUPPORTED_CONFIGURATION);
    assert(nvx_gsp_shared_queue_required_size(UINT64_MAX, QUEUE_SIZE,
        &required, &page_table, &pte_count) ==
        NVX_GSP_INIT_ARGS_BAD_QUEUE_SIZE);

    status = nvx_gsp_arguments_cached_build(&layout, 0U, 0U, &bad_args);
    assert(status == NVX_GSP_INIT_ARGS_OK);
    bad_args.message_queue.shared_mem_phys_addr = 0U;
    assert(nvx_gsp_arguments_cached_materialize(&bad_args, args_page,
        sizeof(args_page)) == NVX_GSP_INIT_ARGS_BAD_ARGUMENTS);
    return 0;
}
