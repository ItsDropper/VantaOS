/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 *
 * Host integration tests use strict mocks for hardware-facing services. They
 * exercise orchestration, failure unwind, and the post-start DMA lifetime rule;
 * they do not simulate or claim a successful physical GSP boot.
 */
#include "nvx_gsp_platform.h"
#include "nvx_gsp_vbios_prom.h"
#include "nvx_gsp_init_args.h"
#include "pci.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static uint8_t fake_bar[64];
static unsigned allocation_calls;
static unsigned live_allocations;
static unsigned fail_allocation_at;
static int fail_bundle;
static int fail_prom;
static int fail_bus_master;
static int fail_startup;
static int quiesce_result;
static int bus_master_enabled;
static unsigned attach_calls;
static unsigned detach_calls;

static int provider_request(const char *name, nvx_firmware_blob_t *blob,
                            void *context)
{ (void)name; (void)blob; (void)context; return 0; }
static int provider_verify(const char *name, const nvx_firmware_blob_t *blob,
                           void *context)
{ (void)name; (void)blob; (void)context; return 0; }
static void provider_release(nvx_firmware_blob_t *blob, void *context)
{ (void)blob; (void)context; }
static void delay_ms(uint32_t ms, void *context)
{ (void)ms; (void)context; }

static void reset_mocks(void)
{
    allocation_calls = live_allocations = fail_allocation_at = 0U;
    fail_bundle = fail_prom = fail_bus_master = fail_startup = 0;
    quiesce_result = 1;
    bus_master_enabled = 0;
    attach_calls = detach_calls = 0U;
}

int nvx_match_pci_device(const struct pci_device *device)
{
    return device && device->vendor_id == NVX_PCI_VENDOR_NVIDIA &&
           device->class_code == 0x03U;
}
int nvx_identify_pci_device(uint16_t id, nvx_generation_family_t *family,
                            nvx_device_profile_t *profile)
{
    if (!family || !profile) return 0;
    if (id != 0x2503U) {
        *family = NVX_FAMILY_UNKNOWN;
        *profile = NVX_PROFILE_UNKNOWN;
        return 0;
    }
    *family = NVX_FAMILY_AMPERE;
    *profile = NVX_PROFILE_GEFORCE_RTX_3060;
    return 1;
}
nvx_resource_status_t nvx_bar_resource_validate(const nvx_bar_resource_t *bar)
{
    return bar && bar->base && bar->size >= 0x100000U &&
        bar->size_known && !bar->is_io ? NVX_RESOURCE_OK :
        NVX_RESOURCE_INVALID_ARGUMENT;
}
nvx_attach_status_t nvx_device_attach(const struct pci_device *pci,
    const nvx_bar_resource_t *bar, nvx_attached_device_t *out)
{
    assert(pci && bar && out);
    ++attach_calls;
    out->pci_device = pci;
    out->bar0 = *bar;
    out->bar0_mapping = fake_bar;
    out->lifecycle.state = NVX_STATE_PROBED;
    out->attached = 1U;
    return NVX_ATTACH_OK;
}
nvx_attach_status_t nvx_device_detach(nvx_attached_device_t *device)
{
    assert(device && device->attached);
    ++detach_calls;
    device->attached = 0U;
    device->bar0_mapping = 0;
    device->lifecycle.state = NVX_STATE_STOPPED;
    return NVX_ATTACH_OK;
}
int nvx_lifecycle_transition(nvx_lifecycle_t *l, nvx_lifecycle_state_t next)
{
    if (!l) return 0;
    if ((l->state == NVX_STATE_PROBED && next == NVX_STATE_INITIALIZING) ||
        (l->state == NVX_STATE_INITIALIZING && next == NVX_STATE_INITIALIZED) ||
        ((l->state == NVX_STATE_INITIALIZING || l->state == NVX_STATE_FAILED ||
          l->state == NVX_STATE_PROBED || l->state == NVX_STATE_INITIALIZED) &&
         next == NVX_STATE_QUIESCING) ||
        (l->state == NVX_STATE_QUIESCING && next == NVX_STATE_STOPPED)) {
        l->state = next;
        return 1;
    }
    return 0;
}
int nvx_lifecycle_fail(nvx_lifecycle_t *l, nvx_lifecycle_error_t error)
{
    if (!l || error == NVX_ERROR_NONE) return 0;
    l->state = NVX_STATE_FAILED;
    l->error = error;
    l->verified_capabilities = 0U;
    return 1;
}
int pci_enable_bus_mastering(const struct pci_device *device)
{
    assert(device);
    if (fail_bus_master) return 0;
    bus_master_enabled = 1;
    return 1;
}
int pci_disable_bus_mastering(const struct pci_device *device)
{
    assert(device);
    bus_master_enabled = 0;
    return 1;
}

nvx_dma_status_t nvx_dma_alloc_identity_mapped(size_t size,
                                                nvx_dma_buffer_t *out)
{
    if (!out || !size) return NVX_DMA_INVALID_ARGUMENT;
    ++allocation_calls;
    if (fail_allocation_at == allocation_calls)
        return NVX_DMA_ALLOCATION_FAILED;
    size_t rounded = (size + 4095U) & ~(size_t)4095U;
    void *memory = aligned_alloc(4096U, rounded);
    if (!memory) return NVX_DMA_ALLOCATION_FAILED;
    memset(memory, 0, rounded);
    out->cpu_address = memory;
    out->gpu_address = (uint64_t)(uintptr_t)memory;
    out->size = size;
    out->allocated_size = rounded;
    out->acquired = 1U;
    ++live_allocations;
    return NVX_DMA_OK;
}
void nvx_dma_release(nvx_dma_buffer_t *buffer)
{
    if (!buffer || !buffer->acquired) return;
    free(buffer->cpu_address);
    memset(buffer, 0, sizeof(*buffer));
    assert(live_allocations > 0U);
    --live_allocations;
}
nvx_dma_sync_status_t nvx_dma_sync_init(nvx_dma_sync_t *sync,
                                        nvx_dma_buffer_t *buffer)
{
    if (!sync || !buffer || !buffer->acquired) return NVX_DMA_SYNC_INVALID_ARGUMENT;
    memset(sync, 0, sizeof(*sync));
    sync->buffer = buffer;
    sync->cpu_address = (uintptr_t)buffer->cpu_address;
    sync->gpu_address = buffer->gpu_address;
    sync->size = buffer->size;
    sync->owner = NVX_DMA_OWNER_CPU;
    sync->initialized = 1U;
    return NVX_DMA_SYNC_OK;
}
nvx_dma_sync_status_t nvx_dma_sync_destroy(nvx_dma_sync_t *sync)
{
    if (!sync || !sync->initialized) return NVX_DMA_SYNC_INVALID_ARGUMENT;
    if (sync->owner != NVX_DMA_OWNER_CPU) return NVX_DMA_SYNC_WRONG_OWNER;
    memset(sync, 0, sizeof(*sync));
    return NVX_DMA_SYNC_OK;
}
nvx_dma_sync_status_t nvx_dma_sync_for_cpu(nvx_dma_sync_t *sync,
                                           size_t offset, size_t length)
{
    if (!sync || !sync->initialized || offset > sync->size ||
        length > sync->size - offset) return NVX_DMA_SYNC_INVALID_ARGUMENT;
    sync->owner = NVX_DMA_OWNER_CPU;
    return NVX_DMA_SYNC_OK;
}

nvx_gsp_vbios_prom_status_t nvx_gsp_vbios_read_from_prom(
    const nvx_mmio_region_t *bar, void *buffer, size_t capacity,
    size_t *out_size, size_t *out_expansion)
{
    assert(bar && buffer && out_size && out_expansion);
    if (fail_prom) return NVX_VBIOS_PROM_MMIO_FAILED;
    memset(buffer, 0x5A, capacity);
    *out_size = 4096U;
    *out_expansion = 0U;
    return NVX_VBIOS_PROM_OK;
}
nvx_gsp_vbios_status_t nvx_gsp_vbios_find_ga10x_fwsec(
    const void *vbios, size_t size, size_t expansion,
    nvx_gsp_vbios_fwsec_info_t *out)
{
    assert(vbios && size && out);
    (void)expansion;
    memset(out, 0, sizeof(*out));
    out->engine_id_mask = NVX_GSP_GA10X_ENGINE_MASK_SEC2;
    out->ucode_id = 1U;
    return NVX_GSP_VBIOS_OK;
}
nvx_gsp_bundle_status_t nvx_gsp_bundle_acquire(
    const nvx_firmware_provider_t *provider, nvx_generation_family_t family,
    const char *chip, const char *version, nvx_gsp_bundle_t *out)
{
    assert(provider && family == NVX_FAMILY_AMPERE && chip && version && out);
    if (fail_bundle) return NVX_GSP_BUNDLE_COMPONENT_NOT_FOUND;
    out->provider = *provider;
    out->acquired = 1U;
    return NVX_GSP_BUNDLE_OK;
}
void nvx_gsp_bundle_release(nvx_gsp_bundle_t *bundle)
{ if (bundle) memset(bundle, 0, sizeof(*bundle)); }
nvx_gsp_boot_memory_status_t nvx_gsp_boot_memory_acquire(
    const nvx_gsp_bundle_t *bundle, nvx_gsp_boot_memory_t *memory)
{
    assert(bundle && bundle->acquired && memory);
    memory->acquired = 1U;
    memory->radix3_elf.size = 4096U;
    memory->bootloader_payload.size = 4096U;
    memory->signature.size = 384U;
    memory->monitor_code_offset = 0U;
    memory->monitor_data_offset = 16U;
    memory->manifest_offset = 32U;
    return NVX_GSP_BOOT_MEMORY_OK;
}
nvx_gsp_boot_memory_status_t nvx_gsp_boot_memory_stage_booters_from_bar0(
    nvx_gsp_boot_memory_t *memory, const nvx_gsp_bundle_t *bundle,
    const nvx_mmio_region_t *bar)
{
    assert(memory && bundle && bar);
    memory->booters_staged = 1U;
    return NVX_GSP_BOOT_MEMORY_OK;
}
nvx_gsp_boot_memory_status_t nvx_gsp_boot_memory_prepare_wpr_metadata(
    nvx_gsp_boot_memory_t *memory, const nvx_gsp_wpr_layout_t *layout,
    uint64_t *out_phys)
{
    assert(memory && layout && out_phys);
    memory->wpr_metadata_prepared = 1U;
    *out_phys = UINT64_C(0x100000);
    return NVX_GSP_BOOT_MEMORY_OK;
}
void nvx_gsp_boot_memory_release(nvx_gsp_boot_memory_t *memory)
{ if (memory) memset(memory, 0, sizeof(*memory)); }

nvx_gsp_ga10x_status_t nvx_gsp_ga10x_read_usable_fb_size(
    const nvx_mmio_region_t *bar, uint64_t *out)
{
    assert(bar && out);
    *out = UINT64_C(12) << 30U;
    return NVX_GSP_GA10X_OK;
}
nvx_gsp_wpr_policy_status_t nvx_gsp_ga10x_fb_facts_read(
    const nvx_mmio_region_t *bar, nvx_gsp_ga10x_fb_facts_t *out)
{
    assert(bar && out);
    memset(out, 0, sizeof(*out));
    return NVX_GSP_WPR_POLICY_OK;
}
static int heap_policy(void *context, uint64_t pre, uint64_t post,
                       uint64_t *out)
{
    (void)context; (void)pre; (void)post;
    *out = UINT64_C(64) << 20U;
    return 1;
}
nvx_gsp_wpr_policy_status_t nvx_gsp_ga10x_wpr_plan_config(
    const nvx_gsp_ga10x_fb_facts_t *facts,
    nvx_gsp_wpr_plan_config_t *out)
{
    assert(facts && out);
    memset(out, 0, sizeof(*out));
    out->framebuffer_size = facts->usable_fb_bytes;
    out->vga_workspace_offset = facts->usable_fb_bytes - (UINT64_C(1) << 20U);
    out->frts_size = UINT64_C(1) << 20U;
    out->nonwpr_heap_size = UINT64_C(1) << 20U;
    out->compute_wpr_heap_size = heap_policy;
    return NVX_GSP_WPR_POLICY_OK;
}
nvx_gsp_wpr_status_t nvx_gsp_wpr_layout_plan(
    const nvx_gsp_wpr_layout_t *resources,
    const nvx_gsp_wpr_plan_config_t *config,
    nvx_gsp_wpr_layout_t *out)
{
    assert(resources && config && out);
    *out = *resources;
    out->frts_offset = UINT64_C(0x10000000);
    return NVX_GSP_WPR_OK;
}
nvx_gsp_ga10x_status_t nvx_gsp_ga10x_read_ucode_fuse_version(
    const nvx_mmio_region_t *bar, uint32_t engine, uint32_t id,
    uint32_t *out)
{
    assert(bar && engine == NVX_GSP_GA10X_ENGINE_MASK_SEC2 && id == 1U);
    *out = 4U;
    return NVX_GSP_GA10X_OK;
}

nvx_gsp_init_args_status_t nvx_gsp_modern_shared_queue_required_size(
    uint64_t command, uint64_t status, uint64_t *required,
    uint64_t *pte_size, uint32_t *pte_count)
{
    if (!required || !pte_size || !pte_count ||
        command < 8192U || status < 8192U ||
        (command & 4095U) || (status & 4095U))
        return NVX_GSP_INIT_ARGS_BAD_QUEUE_SIZE;
    uint64_t pages = (command + status) / 4096U;
    *pte_count = (uint32_t)pages + 1U;
    *pte_size = 4096U;
    *required = command + status + 4096U;
    return NVX_GSP_INIT_ARGS_OK;
}
nvx_gsp_init_args_status_t nvx_gsp_modern_shared_queue_plan(
    const nvx_gsp_modern_shared_queue_config_t *config,
    nvx_gsp_modern_shared_queue_layout_t *out)
{
    assert(config && out);
    memset(out, 0, sizeof(*out));
    out->shared_mem_phys_addr = config->shared_mem_phys_addr;
    out->allocation_size = config->allocation_size;
    out->required_size = config->command_queue_size +
                         config->status_queue_size + 4096U;
    out->page_table_size = 4096U;
    out->page_table_entry_count = (uint32_t)
        ((config->command_queue_size + config->status_queue_size) / 4096U) + 1U;
    out->command_queue_offset = 4096U;
    out->command_queue_size = config->command_queue_size;
    out->status_queue_offset = 4096U + config->command_queue_size;
    out->status_queue_size = config->status_queue_size;
    out->queue_element_header_size = config->queue_element_header_size;
    out->queue_element_size_min = config->queue_element_size_min;
    out->queue_element_size_max = config->queue_element_size_max;
    out->queue_header_alignment = config->queue_header_alignment;
    out->queue_element_alignment = config->queue_element_alignment;
    return NVX_GSP_INIT_ARGS_OK;
}
nvx_gsp_init_args_status_t nvx_gsp_modern_shared_queue_build_pte_table(
    const nvx_gsp_modern_shared_queue_layout_t *layout, void *memory,
    size_t capacity)
{ assert(layout && memory && capacity >= layout->allocation_size); return NVX_GSP_INIT_ARGS_OK; }
nvx_gsp_init_args_status_t nvx_gsp_arguments_cached_modern_build(
    const nvx_gsp_modern_shared_queue_layout_t *layout, uint32_t instance,
    uint64_t flags, nvx_gsp_arguments_cached_modern_t *out)
{ assert(layout && out && flags == 0U); (void)instance; memset(out, 0, sizeof(*out)); return NVX_GSP_INIT_ARGS_OK; }
nvx_gsp_init_args_status_t nvx_gsp_arguments_cached_modern_materialize(
    const nvx_gsp_arguments_cached_modern_t *args, void *page, size_t capacity)
{ assert(args && page && capacity >= NVX_GSP_INIT_ARGS_PAGE_SIZE); return NVX_GSP_INIT_ARGS_OK; }

nvx_gsp_startup_status_t nvx_gsp_ga10x_start_and_init_rm_from_pci(
    const nvx_gsp_startup_platform_info_t *platform,
    nvx_dma_buffer_t *vbios, nvx_dma_sync_t *vbios_sync,
    const nvx_gsp_vbios_fwsec_info_t *fwsec, uint32_t fuse,
    nvx_gsp_boot_memory_t *memory, nvx_dma_buffer_t *init_args,
    nvx_dma_sync_t *init_sync, nvx_dma_buffer_t *queue,
    nvx_dma_sync_t *queue_sync,
    const nvx_gsp_modern_shared_queue_layout_t *layout,
    const nvx_mmio_region_t *bar, uint64_t frts,
    void *static_info, size_t static_capacity, size_t *out_static_size,
    void *scratch, size_t scratch_size, uint32_t timeout, uint32_t poll,
    nvx_gsp_delay_ms_fn delay, void *context,
    nvx_gsp_msgq_ga10x_t *runtime, nvx_gsp_gmcapi_transport_t *gmcapi,
    nvx_gsp_msgq_rpc_transport_t *rpc_transport,
    nvx_gsp_rpc_manager_t *rpc_manager,
    nvx_gsp_startup_result_t *result)
{
    assert(platform && vbios && vbios_sync && fwsec && fuse && memory);
    assert(init_args && init_sync && queue && queue_sync && layout && bar && frts);
    assert(static_info && static_capacity && out_static_size);
    assert(scratch && scratch_size && timeout && poll && delay);
    assert(runtime && gmcapi && rpc_transport && rpc_manager && result);
    if (fail_startup) return NVX_GSP_STARTUP_GSP_INIT_FAILED;
    *out_static_size = 128U;
    result->static_info_size = 128U;
    result->gsp_rm_initialized = 1U;
    result->rpc_initialized = 1U;
    return NVX_GSP_STARTUP_OK;
}

static int quiesce(nvx_attached_device_t *device,
                   const nvx_gsp_startup_result_t *result, void *context)
{
    assert(device && device->attached && result);
    (void)context;
    return quiesce_result;
}

static nvx_gsp_platform_config_t make_config(
    struct pci_device *pci, nvx_bar_resource_t *bar,
    nvx_firmware_provider_t *provider, nvx_gsp_system_info_t *facts)
{
    nvx_gsp_platform_config_t c = {0};
    c.pci_device = pci;
    c.trusted_bar0 = bar;
    c.firmware_provider = provider;
    c.firmware_chip = "ga106";
    c.firmware_version = "535.129.03";
    c.trusted_gpu_facts = facts;
    c.command_queue_bytes = 64U * 1024U;
    c.status_queue_bytes = 64U * 1024U;
    c.timeout_ms = 5000U;
    c.poll_interval_ms = 10U;
    c.delay_ms = delay_ms;
    return c;
}

int main(void)
{
    struct pci_device pci = {
        .vendor_id = NVX_PCI_VENDOR_NVIDIA, .device_id = 0x2503U,
        .class_code = 0x03U
    };
    nvx_bar_resource_t bar = {
        .base = UINT64_C(0x80000000), .size = UINT64_C(0x01000000),
        .is_io = 0U, .is_64bit = 1U, .size_known = 1U
    };
    nvx_firmware_provider_t provider = {
        .request = provider_request, .verify = provider_verify,
        .release = provider_release
    };
    nvx_gsp_system_info_t facts = {0};
    nvx_gsp_platform_config_t config = make_config(&pci, &bar, &provider, &facts);
    static nvx_gsp_platform_session_t session;

    reset_mocks();
    assert(nvx_gsp_platform_start(&config, &session) == NVX_GSP_PLATFORM_OK);
    assert(session.attached && session.bus_master_enabled &&
           session.startup_attempted && session.gsp_rm_initialized);
    assert(session.device.lifecycle.state == NVX_STATE_INITIALIZED);
    assert(session.fwsec_info.ucode_id == 1U && session.fuse_version == 4U);
    assert(session.wpr_metadata_phys == UINT64_C(0x100000));
    assert(session.static_info_size == 128U && live_allocations == 3U);
    quiesce_result = 0;
    assert(nvx_gsp_platform_shutdown(&session, quiesce, 0) ==
           NVX_GSP_PLATFORM_QUIESCE_FAILED);
    assert(live_allocations == 3U && bus_master_enabled);
    quiesce_result = 1;
    assert(nvx_gsp_platform_shutdown(&session, quiesce, 0) == NVX_GSP_PLATFORM_OK);
    assert(live_allocations == 0U && !bus_master_enabled && !session.attached);
    assert(attach_calls == 1U && detach_calls == 1U);

    reset_mocks();
    fail_startup = 1;
    assert(nvx_gsp_platform_start(&config, &session) ==
           NVX_GSP_PLATFORM_STARTUP_FAILED);
    assert(session.recovery_required && session.startup_attempted &&
           session.bus_master_enabled && live_allocations == 3U);
    assert(nvx_gsp_platform_shutdown(&session, 0, 0) ==
           NVX_GSP_PLATFORM_QUIESCE_FAILED);
    quiesce_result = 0;
    assert(nvx_gsp_platform_shutdown(&session, quiesce, 0) ==
           NVX_GSP_PLATFORM_QUIESCE_FAILED);
    quiesce_result = 1;
    assert(nvx_gsp_platform_shutdown(&session, quiesce, 0) ==
           NVX_GSP_PLATFORM_OK);
    assert(live_allocations == 0U && !bus_master_enabled);

    reset_mocks();
    fail_prom = 1;
    assert(nvx_gsp_platform_start(&config, &session) ==
           NVX_GSP_PLATFORM_VBIOS_FAILED);
    assert(!session.attached && live_allocations == 0U &&
           !bus_master_enabled && detach_calls == 1U);

    reset_mocks();
    fail_bundle = 1;
    assert(nvx_gsp_platform_start(&config, &session) ==
           NVX_GSP_PLATFORM_FIRMWARE_FAILED);
    assert(!session.attached && live_allocations == 0U &&
           !bus_master_enabled && detach_calls == 1U);

    reset_mocks();
    fail_allocation_at = 2U; /* VBIOS succeeds; shared queue allocation fails. */
    assert(nvx_gsp_platform_start(&config, &session) ==
           NVX_GSP_PLATFORM_QUEUE_FAILED);
    assert(!session.attached && live_allocations == 0U &&
           !bus_master_enabled && detach_calls == 1U);

    reset_mocks();
    fail_bus_master = 1;
    assert(nvx_gsp_platform_start(&config, &session) ==
           NVX_GSP_PLATFORM_BUS_MASTER_FAILED);
    assert(!session.attached && live_allocations == 0U &&
           !bus_master_enabled && detach_calls == 1U);
    return 0;
}
