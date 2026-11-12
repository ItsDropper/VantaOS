/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_boot_package.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static uint64_t next_phys = 0x10000000ULL;
static uint64_t next_id = 1U;
static unsigned allocation_calls;
static unsigned release_calls;
static unsigned fail_allocation_at;
static int fail_booter_stage;

nvx_dma_status_t nvx_dma_alloc(size_t size, unsigned address_bits,
                               nvx_dma_buffer_t *out)
{
    size_t allocated;
    (void)address_bits;
    ++allocation_calls;
    if (!out || size == 0U || allocation_calls == fail_allocation_at)
        return NVX_DMA_ALLOCATION_FAILED;
    allocated = (size + NVX_DMA_PAGE_SIZE - 1U) &
                ~((size_t)NVX_DMA_PAGE_SIZE - 1U);
    out->cpu_address = calloc(1U, allocated);
    out->pages = calloc(1U, sizeof(*out->pages));
    if (!out->cpu_address || !out->pages) {
        free(out->cpu_address);
        free(out->pages);
        memset(out, 0, sizeof(*out));
        return NVX_DMA_ALLOCATION_FAILED;
    }
    out->gpu_address = next_phys;
    out->size = size;
    out->allocated_size = allocated;
    out->page_count = allocated / NVX_DMA_PAGE_SIZE;
    out->allocation_id = next_id++;
    out->acquired = 1U;
    next_phys += allocated;
    return NVX_DMA_OK;
}

void nvx_dma_release(nvx_dma_buffer_t *buffer)
{
    if (!buffer)
        return;
    if (buffer->acquired)
        ++release_calls;
    free(buffer->cpu_address);
    free(buffer->pages);
    memset(buffer, 0, sizeof(*buffer));
}

nvx_gsp_preflight_status_t nvx_gsp_preflight_validate(
    const nvx_gsp_bundle_t *bundle, nvx_gsp_preflight_report_t *out_report)
{
    if (!bundle || !out_report || !bundle->acquired)
        return NVX_GSP_PREFLIGHT_BUNDLE_NOT_READY;
    memset(out_report, 0, sizeof(*out_report));
    out_report->booter_load.data_size = 256U;
    out_report->booter_unload.data_size = 192U;
    return NVX_GSP_PREFLIGHT_OK;
}


nvx_gsp_preflight_status_t nvx_gsp_preflight_stage_booters_from_bar0(
    const nvx_gsp_bundle_t *bundle, const nvx_mmio_region_t *bar0,
    uint8_t *load_staging, size_t load_capacity,
    uint8_t *unload_staging, size_t unload_capacity,
    nvx_gsp_image_info_t *out_load_info, nvx_gsp_image_info_t *out_unload_info)
{
    if (!bundle || !bar0 || !bar0->base || !load_staging || !unload_staging ||
        !out_load_info || !out_unload_info)
        return NVX_GSP_PREFLIGHT_INVALID_ARGUMENT;
    if (fail_booter_stage)
        return NVX_GSP_PREFLIGHT_FUSE_READ_FAILED;
    if (load_capacity < 256U || unload_capacity < 192U)
        return NVX_GSP_PREFLIGHT_STAGING_TOO_SMALL;
    memcpy(load_staging, bundle->components[2].blob.data, 256U);
    memcpy(unload_staging, bundle->components[3].blob.data, 192U);
    *out_load_info = (nvx_gsp_image_info_t){ .data_size = 256U, .engine_id = 1U, .ucode_id = 2U };
    *out_unload_info = (nvx_gsp_image_info_t){ .data_size = 192U, .engine_id = 1U, .ucode_id = 3U };
    return NVX_GSP_PREFLIGHT_OK;
}

nvx_gsp_elf_status_t nvx_gsp_elf_find_section(
    const nvx_gsp_component_t *component, const char *name,
    nvx_gsp_elf_section_t *out_section)
{
    if (!component || !name || !out_section ||
        strcmp(name, NVX_GSP_GA10X_SIGNATURE_SECTION) != 0 ||
        !component->blob.data || component->blob.size < 64U)
        return NVX_GSP_ELF_SECTION_NOT_FOUND;
    out_section->data = component->blob.data + 32U;
    out_section->offset = 32U;
    out_section->size = 32U;
    out_section->alignment = 16U;
    out_section->type = 1U;
    out_section->flags = 0U;
    out_section->index = 1U;
    return NVX_GSP_ELF_OK;
}

nvx_gsp_bootloader_status_t nvx_gsp_bootloader_parse(
    const nvx_gsp_component_t *component, nvx_gsp_bootloader_info_t *out_info)
{
    if (!component || !out_info || !component->blob.data ||
        component->blob.size < 256U)
        return NVX_GSP_BOOTLOADER_RANGE_INVALID;
    *out_info = (nvx_gsp_bootloader_info_t){
        .binary_size = 256U, .descriptor_offset = 24U,
        .descriptor_size = 84U, .payload_offset = 108U,
        .payload_size = 128U, .descriptor_version = 4U,
        .manifest_offset = 8U, .manifest_size = 16U,
        .monitor_data_offset = 32U, .monitor_data_size = 32U,
        .monitor_code_offset = 64U, .monitor_code_size = 64U
    };
    return NVX_GSP_BOOTLOADER_OK;
}

nvx_gsp_bootloader_status_t nvx_gsp_bootloader_prepare(
    const nvx_gsp_component_t *component, uint8_t *staging,
    size_t staging_capacity, nvx_gsp_bootloader_info_t *out_info)
{
    nvx_gsp_bootloader_info_t info;
    if (!component || !staging || !out_info ||
        nvx_gsp_bootloader_parse(component, &info) != NVX_GSP_BOOTLOADER_OK)
        return NVX_GSP_BOOTLOADER_INVALID_ARGUMENT;
    if (staging_capacity < info.payload_size)
        return NVX_GSP_BOOTLOADER_BUFFER_TOO_SMALL;
    memcpy(staging, component->blob.data + info.payload_offset,
           info.payload_size);
    *out_info = info;
    return NVX_GSP_BOOTLOADER_OK;
}


static unsigned provider_release_calls;
static int provider_request(const char *name, nvx_firmware_blob_t *out,
                            void *context)
{
    (void)context;
    if (!name || !out)
        return 0;
    if (strstr(name, "/gsp/gsp-") != 0) {
        out->data = runtime;
        out->size = sizeof(runtime);
    } else if (strstr(name, "/gsp/ucodes-") != 0) {
        out->data = bootloader_blob;
        out->size = sizeof(bootloader_blob);
    } else if (strstr(name, "/gsp/booter_load-") != 0) {
        out->data = booter_load_blob;
        out->size = sizeof(booter_load_blob);
    } else if (strstr(name, "/gsp/booter_unload-") != 0) {
        out->data = booter_unload_blob;
        out->size = sizeof(booter_unload_blob);
    } else if (strstr(name, "/gsp/bootloader-") != 0) {
        out->data = bootloader_blob;
        out->size = sizeof(bootloader_blob);
    } else {
        return 0;
    }
    out->release_context = 0;
    return 1;
}

static int provider_verify(const char *name,
                          const nvx_firmware_blob_t *blob, void *context)
{
    (void)context;
    return name != 0 && blob != 0 && blob->data != 0 && blob->size != 0U;
}

static void provider_release(nvx_firmware_blob_t *blob, void *context)
{
    (void)context;
    if (blob) {
        ++provider_release_calls;
        memset(blob, 0, sizeof(*blob));
    }
}

static nvx_gsp_wpr_layout_t planned_layout(void)
{
    return (nvx_gsp_wpr_layout_t){
        .radix3_size = 1024U,
        .bootloader_size = 128U,
        .signature_size = 32U,
        .bootloader_code_offset = 64U,
        .bootloader_data_offset = 32U,
        .bootloader_manifest_offset = 8U,
        .reserved_start = 0x38020000ULL,
        .nonwpr_heap_offset = 0x38020000ULL,
        .nonwpr_heap_size = 0x01000000ULL,
        .wpr_start = 0x30000000ULL,
        .wpr_heap_offset = 0x30001000ULL,
        .wpr_heap_size = 0x00100000ULL,
        .firmware_offset = 0x30101000ULL,
        .boot_bin_offset = 0x30301000ULL,
        .frts_offset = 0x30311000ULL,
        .frts_size = 0x00010000ULL,
        .wpr_end = 0x38000000ULL,
        .framebuffer_size = 0x100000000ULL,
        .vga_workspace_offset = 0xFFF00000ULL,
        .vga_workspace_size = 0x00100000ULL
    };
}

int main(void)
{
    uint8_t runtime[1024];
    uint8_t bootloader_blob[256];
    uint8_t booter_load_blob[256];
    uint8_t booter_unload_blob[192];
    uint32_t fake_bar[8] = {0};
    nvx_mmio_region_t bar0 = { .base = (volatile uint8_t *)(void *)fake_bar, .length = sizeof(fake_bar) };
    nvx_gsp_bundle_t bundle = {0};
    nvx_gsp_boot_memory_t memory = {0};
    nvx_gsp_wpr_layout_t layout = planned_layout();
    uint64_t metadata_phys = 0U;

    for (size_t i = 0U; i < sizeof(runtime); ++i)
        runtime[i] = (uint8_t)(i ^ 0x5AU);
    for (size_t i = 0U; i < sizeof(bootloader_blob); ++i)
        bootloader_blob[i] = (uint8_t)(i ^ 0xA5U);
    bundle.acquired = 1U;
    bundle.family = NVX_FAMILY_AMPERE;
    bundle.components[0].kind = NVX_GSP_RUNTIME;
    bundle.components[0].acquired = 1U;
    bundle.components[0].blob.data = runtime;
    bundle.components[0].blob.size = sizeof(runtime);
    bundle.components[2].kind = NVX_GSP_BOOTER_LOAD;
    bundle.components[2].acquired = 1U;
    bundle.components[2].blob.data = booter_load_blob;
    bundle.components[2].blob.size = sizeof(booter_load_blob);
    bundle.components[3].kind = NVX_GSP_BOOTER_UNLOAD;
    bundle.components[3].acquired = 1U;
    bundle.components[3].blob.data = booter_unload_blob;
    bundle.components[3].blob.size = sizeof(booter_unload_blob);
    bundle.components[4].kind = NVX_GSP_BOOTLOADER;
    bundle.components[4].acquired = 1U;
    bundle.components[4].blob.data = bootloader_blob;
    bundle.components[4].blob.size = sizeof(bootloader_blob);

    assert(nvx_gsp_boot_memory_acquire(&bundle, &memory) ==
           NVX_GSP_BOOT_MEMORY_OK);
    assert(memory.acquired == 1U);
    assert(memory.wpr_metadata.acquired == 1U);
    assert(memory.wpr_metadata.allocated_size == NVX_DMA_PAGE_SIZE);
    assert(memcmp(memory.radix3_elf.cpu_address, runtime, sizeof(runtime)) == 0);
    assert(memcmp(memory.signature.cpu_address, runtime + 32U, 32U) == 0);
    assert(memcmp(memory.bootloader_payload.cpu_address,
                  bootloader_blob + 108U, 128U) == 0);
    assert(memory.booter_load.acquired && memory.booter_unload.acquired);
    assert(memory.booter_load.size == sizeof(booter_load_blob));
    assert(memory.booter_unload.size == sizeof(booter_unload_blob));
    fail_booter_stage = 1;
    assert(nvx_gsp_boot_memory_stage_booters_from_bar0(&memory, &bundle, &bar0) ==
           NVX_GSP_BOOT_MEMORY_BOOTER_STAGE_FAILED);
    assert(memory.booters_staged == 0U);
    for (size_t i = 0U; i < memory.booter_load.allocated_size; ++i)
        assert(((const uint8_t *)memory.booter_load.cpu_address)[i] == 0U);
    fail_booter_stage = 0;
    assert(nvx_gsp_boot_memory_stage_booters_from_bar0(&memory, &bundle, &bar0) ==
           NVX_GSP_BOOT_MEMORY_OK);
    assert(memory.booters_staged == 1U);
    assert(memory.booter_load_info.engine_id == 1U && memory.booter_load_info.ucode_id == 2U);
    assert(memory.booter_unload_info.engine_id == 1U && memory.booter_unload_info.ucode_id == 3U);
    assert(memcmp(memory.booter_load.cpu_address, booter_load_blob, sizeof(booter_load_blob)) == 0);
    assert(nvx_gsp_boot_memory_prepare_wpr_metadata(
        &memory, &layout, &metadata_phys) == NVX_GSP_BOOT_MEMORY_OK);
    assert(metadata_phys == memory.wpr_metadata.gpu_address);
    assert((metadata_phys & (NVX_DMA_PAGE_SIZE - 1U)) == 0U);
    assert(metadata_phys != memory.radix3_elf.gpu_address);
    assert(metadata_phys != memory.bootloader_payload.gpu_address);
    assert(metadata_phys != memory.signature.gpu_address);
    assert(nvx_gsp_wpr_meta_validate(
        (const nvx_gsp_wpr_meta_v1_t *)memory.wpr_metadata.cpu_address) ==
        NVX_GSP_WPR_OK);
    assert(((const nvx_gsp_wpr_meta_v1_t *)memory.wpr_metadata.cpu_address)
               ->sysmem_addr_of_radix3_elf == memory.radix3_elf.gpu_address);
    assert(memory.radix3_elf_sync.initialized &&
           memory.bootloader_payload_sync.initialized &&
           memory.signature_sync.initialized && memory.booter_load_sync.initialized &&
           memory.booter_unload_sync.initialized && memory.wpr_metadata_sync.initialized);
    /* Release must refuse while any one of the six hardware-visible buffers is owned by a device. */
    assert(nvx_dma_sync_for_device(&memory.radix3_elf_sync, 0U, memory.radix3_elf.size) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_for_device(&memory.bootloader_payload_sync, 0U, memory.bootloader_payload.size) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_for_device(&memory.signature_sync, 0U, memory.signature.size) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_for_device(&memory.booter_load_sync, 0U, memory.booter_load.size) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_for_device(&memory.booter_unload_sync, 0U, memory.booter_unload.size) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_for_device(&memory.wpr_metadata_sync, 0U, NVX_DMA_PAGE_SIZE) == NVX_DMA_SYNC_OK);
    nvx_gsp_boot_memory_release(&memory);
    assert(memory.acquired == 1U && release_calls == 0U);
    assert(nvx_dma_sync_for_cpu(&memory.radix3_elf_sync, 0U, memory.radix3_elf.size) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_for_cpu(&memory.bootloader_payload_sync, 0U, memory.bootloader_payload.size) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_for_cpu(&memory.signature_sync, 0U, memory.signature.size) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_for_cpu(&memory.booter_load_sync, 0U, memory.booter_load.size) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_for_cpu(&memory.booter_unload_sync, 0U, memory.booter_unload.size) == NVX_DMA_SYNC_OK);
    assert(nvx_dma_sync_for_cpu(&memory.wpr_metadata_sync, 0U, NVX_DMA_PAGE_SIZE) == NVX_DMA_SYNC_OK);
    nvx_gsp_boot_memory_release(&memory);
    assert(memory.acquired == 0U);
    assert(release_calls == 6U);

    allocation_calls = 0U;
    release_calls = 0U;
    fail_allocation_at = 6U;
    assert(nvx_gsp_boot_memory_acquire(&bundle, &memory) ==
           NVX_GSP_BOOT_MEMORY_ALLOCATION_FAILED);
    assert(memory.acquired == 0U);
    assert(!memory.radix3_elf.acquired && !memory.bootloader_payload.acquired &&
           !memory.signature.acquired && !memory.wpr_metadata.acquired);
    assert(release_calls == 5U);

    /* One owner composes provider acquisition, SYSRAM staging and WPR metadata. */
    {
        nvx_firmware_provider_t provider = {
            .request = provider_request,
            .verify = provider_verify,
            .release = provider_release
        };
        nvx_gsp_boot_package_t package = {0};
        nvx_gsp_wpr_layout_t platform_layout = planned_layout();
        nvx_gsp_wpr_layout_t prepared_layout = {0};
        nvx_mmio_region_t package_bar0 = {
            .base = (volatile uint8_t *)(void *)fake_bar,
            .length = sizeof(fake_bar)
        };

        allocation_calls = 0U;
        release_calls = 0U;
        provider_release_calls = 0U;
        fail_allocation_at = 0U;
        assert(nvx_gsp_boot_package_prepare(&provider, "ga102", "1.0",
            &package_bar0, &platform_layout, &package, &prepared_layout) ==
            NVX_GSP_BOOT_PACKAGE_OK);
        assert(package.prepared == 1U && package.firmware.acquired == 1U);
        assert(package.memory.acquired == 1U && package.memory.booters_staged == 1U);
        assert(package.wpr_metadata_phys == package.memory.wpr_metadata.gpu_address);
        assert(prepared_layout.sysmem_radix3_addr ==
               package.memory.radix3_elf.gpu_address);
        assert(prepared_layout.sysmem_bootloader_addr ==
               package.memory.bootloader_payload.gpu_address);
        assert(nvx_gsp_wpr_meta_validate((const nvx_gsp_wpr_meta_v1_t *)
            package.memory.wpr_metadata.cpu_address) == NVX_GSP_WPR_OK);

        /* Uncertain device ownership retains every package allocation and firmware lease. */
        assert(nvx_dma_sync_for_device(&package.memory.radix3_elf_sync, 0U,
            package.memory.radix3_elf.size) == NVX_DMA_SYNC_OK);
        assert(nvx_gsp_boot_package_release(&package) == NVX_GSP_BOOT_PACKAGE_BUSY);
        assert(package.prepared == 1U && package.firmware.acquired == 1U);
        assert(provider_release_calls == 0U && release_calls == 0U);
        assert(nvx_dma_sync_for_cpu(&package.memory.radix3_elf_sync, 0U,
            package.memory.radix3_elf.size) == NVX_DMA_SYNC_OK);
        assert(nvx_gsp_boot_package_release(&package) == NVX_GSP_BOOT_PACKAGE_OK);
        assert(package.prepared == 0U && package.firmware.acquired == 0U);
        assert(provider_release_calls == NVX_GSP_COMPONENT_COUNT);
        assert(release_calls == 6U);
    }

    /* Invalid platform inputs fail before requesting any firmware component. */
    {
        nvx_firmware_provider_t provider = {
            .request = provider_request,
            .verify = provider_verify,
            .release = provider_release
        };
        nvx_gsp_boot_package_t package = {0};
        nvx_gsp_wpr_layout_t platform_layout = planned_layout();
        nvx_gsp_wpr_layout_t prepared_layout = {0};
        unsigned before = provider_release_calls;
        assert(nvx_gsp_boot_package_prepare(&provider, "ga102", "1.0",
            &bar0, &platform_layout, &package, &platform_layout) ==
            NVX_GSP_BOOT_PACKAGE_INVALID_ARGUMENT);
        assert(provider_release_calls == before && !package.prepared);
    }
    return 0;
}
