/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_platform.h"

#include "nvx_gsp_init_args.h"
#include "nvx_gsp_vbios.h"
#include "nvx_gsp_vbios_prom.h"
#include "nvx_gsp_wpr_meta.h"
#include "nvx_gsp_wpr_policy.h"
#include "pci.h"

#include <stdint.h>
#include <string.h>

static int text_equals(const char *left, const char *right)
{
    if (!left || !right) return 0;
    while (*left && *right && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static int session_has_resources(const nvx_gsp_platform_session_t *s)
{
    return s->attached || s->device.attached || s->firmware.acquired ||
        s->boot_memory.acquired || s->staged_vbios.acquired ||
        s->init_args.acquired || s->shared_queue.acquired ||
        s->bus_master_enabled || s->startup_attempted ||
        s->recovery_required;
}

static int valid_dma_sync(const nvx_dma_sync_t *sync)
{
    return sync && sync->initialized && sync->owner == NVX_DMA_OWNER_CPU;
}

static int release_dma_pair(nvx_dma_buffer_t *buffer, nvx_dma_sync_t *sync)
{
    if (!buffer || !sync)
        return 0;
    if (sync->initialized) {
        if (sync->owner != NVX_DMA_OWNER_CPU ||
            nvx_dma_sync_destroy(sync) != NVX_DMA_SYNC_OK)
            return 0;
    }
    if (buffer->acquired)
        nvx_dma_release(buffer);
    return !buffer->acquired;
}

static int release_boot_memory(nvx_gsp_boot_memory_t *memory)
{
    if (!memory)
        return 0;
    nvx_gsp_boot_memory_release(memory);
    return !memory->acquired && !memory->radix3_elf.acquired &&
        !memory->bootloader_payload.acquired && !memory->signature.acquired &&
        !memory->booter_load.acquired && !memory->booter_unload.acquired &&
        !memory->wpr_metadata.acquired;
}

static int reclaim_sync(nvx_dma_sync_t *sync)
{
    if (!sync || !sync->initialized)
        return 1;
    if (sync->owner == NVX_DMA_OWNER_CPU)
        return 1;
    return nvx_dma_sync_for_cpu(sync, 0U, sync->size) == NVX_DMA_SYNC_OK;
}

static int reclaim_boot_memory(nvx_gsp_boot_memory_t *m)
{
    return reclaim_sync(&m->radix3_elf_sync) &&
        reclaim_sync(&m->bootloader_payload_sync) &&
        reclaim_sync(&m->signature_sync) &&
        reclaim_sync(&m->booter_load_sync) &&
        reclaim_sync(&m->booter_unload_sync) &&
        reclaim_sync(&m->wpr_metadata_sync);
}

static int release_unstarted_session(nvx_gsp_platform_session_t *s)
{
    if (!s || s->startup_attempted || s->bus_master_enabled)
        return 0;
    if (!valid_dma_sync(&s->staged_vbios_sync) && s->staged_vbios_sync.initialized)
        return 0;
    if (!valid_dma_sync(&s->init_args_sync) && s->init_args_sync.initialized)
        return 0;
    if (!valid_dma_sync(&s->shared_queue_sync) && s->shared_queue_sync.initialized)
        return 0;
    if (!release_boot_memory(&s->boot_memory))
        return 0;
    if (s->firmware.acquired)
        nvx_gsp_bundle_release(&s->firmware);
    if (!release_dma_pair(&s->staged_vbios, &s->staged_vbios_sync) ||
        !release_dma_pair(&s->init_args, &s->init_args_sync) ||
        !release_dma_pair(&s->shared_queue, &s->shared_queue_sync))
        return 0;
    if (s->attached && s->device.attached) {
        if (nvx_device_detach(&s->device) != NVX_ATTACH_OK)
            return 0;
    }
    memset(s, 0, sizeof(*s));
    return 1;
}

nvx_gsp_platform_status_t nvx_gsp_platform_validate_config(
    const nvx_gsp_platform_config_t *c)
{
    nvx_generation_family_t family = NVX_FAMILY_UNKNOWN;
    nvx_device_profile_t profile = NVX_PROFILE_UNKNOWN;
    uint64_t required = 0U, pte_bytes = 0U;
    uint32_t pte_count = 0U;
    const uint32_t known_facts = (UINT32_C(1) << 18U) - 1U;

    if (!c || !c->pci_device || !c->trusted_bar0 ||
        !c->firmware_provider || !c->firmware_chip ||
        !c->firmware_version || !c->expected_extended_chip_id ||
        c->expected_extended_chip_id > 0x03FFU || !c->trusted_gpu_facts ||
        !c->delay_ms || !c->timeout_ms || !c->poll_interval_ms ||
        c->poll_interval_ms > c->timeout_ms)
        return NVX_GSP_PLATFORM_INVALID_ARGUMENT;

    if (!nvx_match_pci_device(c->pci_device) ||
        !nvx_identify_pci_device(c->pci_device->device_id, &family, &profile) ||
        family != NVX_FAMILY_AMPERE ||
        profile != NVX_PROFILE_GEFORCE_RTX_3060 ||
        !text_equals(c->firmware_chip, "ga106"))
        return NVX_GSP_PLATFORM_UNSUPPORTED_DEVICE;

    if (nvx_bar_resource_validate(c->trusted_bar0) != NVX_RESOURCE_OK ||
        c->trusted_bar0->size < NVX_GSP_GA10X_REQUIRED_BAR_BYTES)
        return NVX_GSP_PLATFORM_BAR_INVALID;

    if (!c->firmware_provider->request || !c->firmware_provider->verify ||
        !c->firmware_provider->release || !c->firmware_version[0] ||
        (c->trusted_gpu_facts->present & ~known_facts) != 0U ||
        (c->trusted_gpu_facts->present &
            (NVX_GSP_SI_PCI_IDENTITY | NVX_GSP_SI_HOST_IDENTITY |
             NVX_GSP_SI_HOST_PAGE_SIZE)) != 0U)
        return NVX_GSP_PLATFORM_INVALID_ARGUMENT;

    if (nvx_gsp_modern_shared_queue_required_size(
            c->command_queue_bytes, c->status_queue_bytes,
            &required, &pte_bytes, &pte_count) != NVX_GSP_INIT_ARGS_OK ||
        required > NVX_DMA_MAX_BYTES ||
        pte_bytes != NVX_GSP_PAGE_SIZE ||
        pte_count > NVX_GSP_PAGE_SIZE / sizeof(uint64_t))
        return NVX_GSP_PLATFORM_QUEUE_FAILED;

    return NVX_GSP_PLATFORM_OK;
}

static nvx_gsp_platform_status_t prepare_firmware_and_wpr(
    const nvx_gsp_platform_config_t *c, nvx_gsp_platform_session_t *s,
    const nvx_mmio_region_t *bar0)
{
    nvx_gsp_bundle_status_t bundle_status;
    nvx_gsp_boot_memory_status_t memory_status;
    nvx_gsp_ga10x_status_t ga10x_status;
    nvx_gsp_wpr_plan_config_t plan_config;
    nvx_gsp_wpr_layout_t resources = {0};
    uint64_t usable_fb_bytes = 0U;

    bundle_status = nvx_gsp_bundle_acquire(c->firmware_provider,
        NVX_FAMILY_AMPERE, c->firmware_chip, c->firmware_version,
        &s->firmware);
    if (bundle_status != NVX_GSP_BUNDLE_OK)
        return NVX_GSP_PLATFORM_FIRMWARE_FAILED;

    memory_status = nvx_gsp_boot_memory_acquire(&s->firmware,
                                                &s->boot_memory);
    if (memory_status != NVX_GSP_BOOT_MEMORY_OK)
        return NVX_GSP_PLATFORM_BOOT_MEMORY_FAILED;

    memory_status = nvx_gsp_boot_memory_stage_booters_from_bar0(
        &s->boot_memory, &s->firmware, bar0);
    if (memory_status != NVX_GSP_BOOT_MEMORY_OK)
        return NVX_GSP_PLATFORM_BOOT_MEMORY_FAILED;

    ga10x_status = nvx_gsp_ga10x_read_usable_fb_size(bar0,
                                                     &usable_fb_bytes);
    if (ga10x_status != NVX_GSP_GA10X_OK || !usable_fb_bytes)
        return NVX_GSP_PLATFORM_WPR_FAILED;

    if (nvx_gsp_ga10x_fb_facts_read(bar0, &s->framebuffer_facts) !=
        NVX_GSP_WPR_POLICY_OK)
        return NVX_GSP_PLATFORM_WPR_FAILED;
    s->framebuffer_facts.usable_fb_bytes = usable_fb_bytes;

    if (nvx_gsp_ga10x_wpr_plan_config(&s->framebuffer_facts,
                                      &plan_config) != NVX_GSP_WPR_POLICY_OK)
        return NVX_GSP_PLATFORM_WPR_FAILED;

    /*
     * The planner receives artifact extents from the acquired firmware, not
     * constants. It computes framebuffer offsets from live GA10x registers
     * and the pinned GA10x heap policy; no BAR length is treated as VRAM size.
     */
    resources.radix3_size = s->boot_memory.radix3_elf.size;
    resources.bootloader_size = s->boot_memory.bootloader_payload.size;
    resources.signature_size = s->boot_memory.signature.size;
    resources.bootloader_code_offset = s->boot_memory.monitor_code_offset;
    resources.bootloader_data_offset = s->boot_memory.monitor_data_offset;
    resources.bootloader_manifest_offset = s->boot_memory.manifest_offset;

    if (nvx_gsp_wpr_layout_plan(&resources, &plan_config,
                                &s->wpr_layout) != NVX_GSP_WPR_OK)
        return NVX_GSP_PLATFORM_WPR_FAILED;

    memory_status = nvx_gsp_boot_memory_prepare_wpr_metadata(
        &s->boot_memory, &s->wpr_layout, &s->wpr_metadata_phys);
    if (memory_status != NVX_GSP_BOOT_MEMORY_OK ||
        !s->boot_memory.wpr_metadata_prepared)
        return NVX_GSP_PLATFORM_WPR_FAILED;

    ga10x_status = nvx_gsp_ga10x_read_ucode_fuse_version(
        bar0, s->fwsec_info.engine_id_mask, s->fwsec_info.ucode_id,
        &s->fuse_version);
    if (ga10x_status != NVX_GSP_GA10X_OK)
        return NVX_GSP_PLATFORM_FUSE_FAILED;

    return NVX_GSP_PLATFORM_OK;
}

static nvx_gsp_platform_status_t prepare_dma_and_queues(
    const nvx_gsp_platform_config_t *c, nvx_gsp_platform_session_t *s)
{
    nvx_gsp_modern_shared_queue_config_t queue_config = {0};
    nvx_gsp_arguments_cached_modern_t arguments = {0};
    uint64_t required_size = 0U, page_table_size = 0U;
    uint32_t pte_count = 0U;

    if (nvx_dma_alloc_identity_mapped(NVX_VBIOS_PROM_MAX_SIZE,
                                     &s->staged_vbios) != NVX_DMA_OK ||
        nvx_dma_sync_init(&s->staged_vbios_sync,
                          &s->staged_vbios) != NVX_DMA_SYNC_OK)
        return NVX_GSP_PLATFORM_VBIOS_FAILED;

    size_t vbios_size = 0U, expansion_offset = 0U;
    nvx_mmio_region_t bar0 = {
        (volatile uint8_t *)s->device.bar0_mapping,
        (size_t)s->device.bar0.size
    };
    nvx_gsp_vbios_prom_status_t prom_status = nvx_gsp_vbios_read_from_prom(
        &bar0, s->staged_vbios.cpu_address, s->staged_vbios.size,
        &vbios_size, &expansion_offset);
    if (prom_status != NVX_VBIOS_PROM_OK || !vbios_size ||
        nvx_gsp_vbios_find_ga10x_fwsec(s->staged_vbios.cpu_address,
            vbios_size, expansion_offset, &s->fwsec_info) != NVX_GSP_VBIOS_OK)
        return NVX_GSP_PLATFORM_VBIOS_FAILED;

    nvx_gsp_platform_status_t status =
        prepare_firmware_and_wpr(c, s, &bar0);
    if (status != NVX_GSP_PLATFORM_OK)
        return status;

    if (nvx_gsp_modern_shared_queue_required_size(
            c->command_queue_bytes, c->status_queue_bytes,
            &required_size, &page_table_size, &pte_count) !=
        NVX_GSP_INIT_ARGS_OK ||
        required_size > SIZE_MAX ||
        nvx_dma_alloc_identity_mapped((size_t)required_size,
                                      &s->shared_queue) != NVX_DMA_OK ||
        nvx_dma_sync_init(&s->shared_queue_sync,
                          &s->shared_queue) != NVX_DMA_SYNC_OK)
        return NVX_GSP_PLATFORM_QUEUE_FAILED;

    queue_config.shared_mem_phys_addr = s->shared_queue.gpu_address;
    queue_config.allocation_size = s->shared_queue.size;
    queue_config.command_queue_size = c->command_queue_bytes;
    queue_config.status_queue_size = c->status_queue_bytes;
    queue_config.queue_element_header_size = NVX_GSP_QUEUE_HEADER_SIZE_PLAIN;
    queue_config.queue_element_size_min = NVX_GSP_QUEUE_ELEMENT_MIN_SIZE;
    queue_config.queue_element_size_max = NVX_GSP_QUEUE_ELEMENT_MAX_SIZE;
    queue_config.queue_header_alignment = NVX_GSP_QUEUE_HEADER_ALIGNMENT;
    queue_config.queue_element_alignment = NVX_GSP_QUEUE_ELEMENT_ALIGNMENT;

    if (nvx_gsp_modern_shared_queue_plan(&queue_config,
                                         &s->queue_layout) !=
            NVX_GSP_INIT_ARGS_OK ||
        s->queue_layout.required_size != required_size ||
        nvx_gsp_modern_shared_queue_build_pte_table(&s->queue_layout,
            s->shared_queue.cpu_address, s->shared_queue.size) !=
            NVX_GSP_INIT_ARGS_OK)
        return NVX_GSP_PLATFORM_QUEUE_FAILED;

    if (nvx_dma_alloc_identity_mapped(NVX_GSP_INIT_ARGS_PAGE_SIZE,
                                      &s->init_args) != NVX_DMA_OK ||
        nvx_dma_sync_init(&s->init_args_sync,
                          &s->init_args) != NVX_DMA_SYNC_OK)
        return NVX_GSP_PLATFORM_INIT_ARGS_FAILED;

    if (nvx_gsp_arguments_cached_modern_build(&s->queue_layout,
            c->gpu_instance, 0U, &arguments) != NVX_GSP_INIT_ARGS_OK ||
        nvx_gsp_arguments_cached_modern_materialize(&arguments,
            s->init_args.cpu_address, s->init_args.size) !=
            NVX_GSP_INIT_ARGS_OK)
        return NVX_GSP_PLATFORM_INIT_ARGS_FAILED;

    return NVX_GSP_PLATFORM_OK;
}

nvx_gsp_platform_status_t nvx_gsp_platform_start(
    const nvx_gsp_platform_config_t *config,
    nvx_gsp_platform_session_t *session)
{
    nvx_gsp_platform_status_t status;
    nvx_gsp_startup_platform_info_t startup_platform;
    nvx_mmio_region_t bar0;

    if (!session)
        return NVX_GSP_PLATFORM_INVALID_ARGUMENT;
    status = nvx_gsp_platform_validate_config(config);
    if (status != NVX_GSP_PLATFORM_OK)
        return status;
    if (session_has_resources(session))
        return NVX_GSP_PLATFORM_BUSY;

    memset(session, 0, sizeof(*session));
    if (nvx_device_attach(config->pci_device, config->trusted_bar0,
                          &session->device) != NVX_ATTACH_OK)
        return NVX_GSP_PLATFORM_ATTACH_FAILED;
    session->attached = 1U;

    /*
     * PCI IDs only select a broad GA10x candidate. Never trust a caller's
     * "ga106" string alone: require the extended PMC chip ID read from BAR0
     * to match the platform's independently sourced exact chip profile.
     */
    if (!session->device.registers.extended_revision_valid ||
        session->device.registers.identity_mismatch ||
        session->device.registers.identified_family != NVX_FAMILY_AMPERE ||
        session->device.registers.extended_chip_id !=
            config->expected_extended_chip_id) {
        if (nvx_device_detach(&session->device) != NVX_ATTACH_OK)
            return NVX_GSP_PLATFORM_BUSY;
        memset(session, 0, sizeof(*session));
        return NVX_GSP_PLATFORM_CHIP_MISMATCH;
    }

    bar0.base = (volatile uint8_t *)session->device.bar0_mapping;
    bar0.length = (size_t)session->device.bar0.size;
    status = prepare_dma_and_queues(config, session);
    if (status != NVX_GSP_PLATFORM_OK) {
        if (!release_unstarted_session(session))
            return NVX_GSP_PLATFORM_BUSY;
        return status;
    }

    startup_platform.pci_device = config->pci_device;
    startup_platform.trusted_gpu_facts = config->trusted_gpu_facts;
    startup_platform.encoded_system_info_buffer = session->encoded_system_info;
    startup_platform.encoded_system_info_capacity =
        sizeof(session->encoded_system_info);

    if (!nvx_lifecycle_transition(&session->device.lifecycle,
                                  NVX_STATE_INITIALIZING)) {
        if (!release_unstarted_session(session))
            return NVX_GSP_PLATFORM_BUSY;
        return NVX_GSP_PLATFORM_ATTACH_FAILED;
    }

    /*
     * All firmware, DMA buffers, PTEs and init arguments are ready before
     * enabling PCI bus mastering. This is the first point at which the GPU
     * is permitted to issue host-memory transactions.
     */
    if (!pci_enable_bus_mastering(config->pci_device)) {
        /*
         * The config write may have succeeded even if readback failed. Prove
         * bus mastering is disabled before freeing any DMA allocations.
         */
        if (!pci_disable_bus_mastering(config->pci_device)) {
            session->bus_master_enabled = 1U;
            session->recovery_required = 1U;
            return NVX_GSP_PLATFORM_BUSY;
        }
        if (!release_unstarted_session(session))
            return NVX_GSP_PLATFORM_BUSY;
        return NVX_GSP_PLATFORM_BUS_MASTER_FAILED;
    }
    session->bus_master_enabled = 1U;
    session->startup_attempted = 1U;

    status = NVX_GSP_PLATFORM_STARTUP_FAILED;
    nvx_gsp_startup_status_t startup_status =
        nvx_gsp_ga10x_start_and_init_rm_from_pci(
            &startup_platform, &session->staged_vbios,
            &session->staged_vbios_sync, &session->fwsec_info,
            session->fuse_version, &session->boot_memory,
            &session->init_args, &session->init_args_sync,
            &session->shared_queue, &session->shared_queue_sync,
            &session->queue_layout, &bar0, session->wpr_layout.frts_offset,
            session->static_info, sizeof(session->static_info),
            &session->static_info_size,
            session->scratch, sizeof(session->scratch),
            config->timeout_ms, config->poll_interval_ms,
            config->delay_ms, config->delay_context,
            &session->queue_runtime, &session->gmcapi,
            &session->rpc_transport, &session->rpc_manager,
            &session->startup_result);

    if (startup_status != NVX_GSP_STARTUP_OK) {
        session->recovery_required = 1U;
        (void)nvx_lifecycle_fail(&session->device.lifecycle,
                                 NVX_ERROR_INITIALIZATION);
        return status;
    }

    if (!session->startup_result.gsp_rm_initialized ||
        !session->startup_result.rpc_initialized ||
        !nvx_lifecycle_transition(&session->device.lifecycle,
                                  NVX_STATE_INITIALIZED)) {
        session->recovery_required = 1U;
        (void)nvx_lifecycle_fail(&session->device.lifecycle,
                                 NVX_ERROR_INITIALIZATION);
        return NVX_GSP_PLATFORM_STARTUP_FAILED;
    }

    session->gsp_rm_initialized = 1U;
    session->recovery_required = 0U;
    return NVX_GSP_PLATFORM_OK;
}

nvx_gsp_platform_status_t nvx_gsp_platform_shutdown(
    nvx_gsp_platform_session_t *session,
    nvx_gsp_platform_quiesce_fn quiesce, void *quiesce_context)
{
    if (!session || !session->attached || !session->device.attached)
        return NVX_GSP_PLATFORM_INVALID_ARGUMENT;

    if (session->startup_attempted && !session->quiesced) {
        if (!quiesce)
            return NVX_GSP_PLATFORM_QUIESCE_FAILED;
        if (!quiesce(&session->device, &session->startup_result,
                     quiesce_context))
            return NVX_GSP_PLATFORM_QUIESCE_FAILED;
        session->quiesced = 1U;
    }

    /*
     * Only the platform quiesce callback can establish that hardware has
     * stopped. Ownership is reclaimed after that proof, never on timeout.
     */
    if (!reclaim_boot_memory(&session->boot_memory) ||
        !reclaim_sync(&session->staged_vbios_sync) ||
        !reclaim_sync(&session->init_args_sync) ||
        !reclaim_sync(&session->shared_queue_sync))
        return NVX_GSP_PLATFORM_DMA_RECLAIM_FAILED;

    if (session->bus_master_enabled) {
        if (!pci_disable_bus_mastering(session->device.pci_device))
            return NVX_GSP_PLATFORM_SHUTDOWN_FAILED;
        session->bus_master_enabled = 0U;
    }

    if (!release_boot_memory(&session->boot_memory))
        return NVX_GSP_PLATFORM_BUSY;
    if (session->firmware.acquired)
        nvx_gsp_bundle_release(&session->firmware);
    if (!release_dma_pair(&session->staged_vbios, &session->staged_vbios_sync) ||
        !release_dma_pair(&session->init_args, &session->init_args_sync) ||
        !release_dma_pair(&session->shared_queue, &session->shared_queue_sync))
        return NVX_GSP_PLATFORM_BUSY;

    if (nvx_device_detach(&session->device) != NVX_ATTACH_OK)
        return NVX_GSP_PLATFORM_SHUTDOWN_FAILED;

    memset(session, 0, sizeof(*session));
    return NVX_GSP_PLATFORM_OK;
}
