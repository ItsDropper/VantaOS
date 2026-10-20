/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_preflight.h"
#include "nvx_gsp_ga10x.h"
#include <string.h>

static const char *const component_names[NVX_GSP_COMPONENT_COUNT] = {
    "gsp", "ucodes", "booter_load", "booter_unload", "bootloader"
};

static int valid_version_token(const char *s)
{
    size_t n = 0U;
    int previous_dot = 1;
    if (!s || !s[0])
        return 0;
    while (n < NVX_GSP_VERSION_MAX + 1U && s[n] != '\\0') {
        char c = s[n];
        if (c >= '0' && c <= '9')
            previous_dot = 0;
        else if (c == '.' && !previous_dot)
            previous_dot = 1;
        else
            return 0;
        ++n;
    }
    return n > 0U && n <= NVX_GSP_VERSION_MAX && !previous_dot && s[n] == '\\0';
}

static int append_path_text(char *dst, size_t capacity, size_t *length,
                            const char *src)
{
    size_t n = 0U;
    while (src[n] != '\\0')
        ++n;
    if (*length >= capacity || n >= capacity - *length)
        return 0;
    memcpy(dst + *length, src, n);
    *length += n;
    dst[*length] = '\\0';
    return 1;
}

static int verify_component_again(const nvx_gsp_bundle_t *b, size_t index)
{
    char path[128];
    size_t length = 0U;
    path[0] = '\\0';
    if (index >= NVX_GSP_COMPONENT_COUNT || !valid_version_token(b->version))
        return 0;
    if (!append_path_text(path, sizeof(path), &length, "nvidia/") ||
        !append_path_text(path, sizeof(path), &length, b->chip) ||
        !append_path_text(path, sizeof(path), &length, "/gsp/") ||
        !append_path_text(path, sizeof(path), &length, component_names[index]) ||
        !append_path_text(path, sizeof(path), &length, "-") ||
        !append_path_text(path, sizeof(path), &length, b->version) ||
        !append_path_text(path, sizeof(path), &length, ".bin"))
        return 0;
    return b->provider.verify(path, &b->components[index].blob,
                              b->provider.context) != 0;
}
static int string_equals_literal(const char *a, const char *b)
{
    size_t i = 0U;
    if (!a || !b)
        return 0;
    while (b[i] != '\0') {
        if (a[i] != b[i])
            return 0;
        ++i;
    }
    return a[i] == '\0';
}

static int supported_ga10x_chip(const char *chip)
{
    static const char *const chips[] = {
        "ga102", "ga103", "ga104", "ga106", "ga107"
    };
    if (!chip)
        return 0;
    for (size_t i = 0U; i < sizeof(chips) / sizeof(chips[0]); ++i)
        if (string_equals_literal(chip, chips[i]))
            return 1;
    return 0;
}
static int overlaps(const void *a,size_t an,const void *b,size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(!an||!bn)return 0;
    if(x>UINTPTR_MAX-an||y>UINTPTR_MAX-bn)return 1;
    return x<y+bn&&y<x+an;
}
nvx_gsp_preflight_status_t nvx_gsp_preflight_validate(
    const nvx_gsp_bundle_t *b,nvx_gsp_preflight_report_t *out)
{
    nvx_gsp_elf_info_t info;nvx_gsp_elf_section_t image,version;
    nvx_gsp_image_info_t booter_load,booter_unload;
    nvx_gsp_bootloader_info_t gsp_bootloader;
    nvx_gsp_elf_status_t es;nvx_gsp_image_status_t is;
    nvx_gsp_bootloader_status_t bs;size_t expected_len=0U;
    if(!b||!out||overlaps(out,sizeof(*out),b,sizeof(*b)))return NVX_GSP_PREFLIGHT_INVALID_ARGUMENT;
    for(size_t i=0;i<NVX_GSP_COMPONENT_COUNT;i++)
        if(b->components[i].blob.data&&overlaps(out,sizeof(*out),b->components[i].blob.data,b->components[i].blob.size))
            return NVX_GSP_PREFLIGHT_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(!b->acquired||b->family!=NVX_FAMILY_AMPERE||!b->provider.verify)
        return NVX_GSP_PREFLIGHT_BUNDLE_NOT_READY;
    if(!supported_ga10x_chip(b->chip))
        return NVX_GSP_PREFLIGHT_UNSUPPORTED_CHIP;
    const nvx_gsp_component_t *runtime=&b->components[0],*ucodes=&b->components[1];
    if(!runtime->acquired||runtime->kind!=NVX_GSP_RUNTIME||!runtime->blob.data||!runtime->blob.size||
       !ucodes->acquired||ucodes->kind!=NVX_GSP_UCODE_ARCHIVE||!ucodes->blob.data||!ucodes->blob.size)
        return NVX_GSP_PREFLIGHT_COMPONENT_INVALID;
    for(size_t i=2U;i<NVX_GSP_COMPONENT_COUNT;i++)
        if(!b->components[i].acquired||b->components[i].kind!=(nvx_gsp_component_kind_t)i||
           !b->components[i].blob.data||!b->components[i].blob.size)
            return NVX_GSP_PREFLIGHT_COMPONENT_INVALID;

    /*
     * Acquisition verifies each blob, but the bundle exposes its backing bytes
     * to later parsing/staging. Revalidate at this trust boundary so a changed
     * or substituted blob cannot inherit the acquisition-time verified claim.
     */
    if (!valid_version_token(b->version))
        return NVX_GSP_PREFLIGHT_VERSION_MISMATCH;
    for (size_t i = 0U; i < NVX_GSP_COMPONENT_COUNT; ++i)
        if (!verify_component_again(b, i))
            return NVX_GSP_PREFLIGHT_VERIFICATION_FAILED;

    es=nvx_gsp_elf_parse(runtime,NVX_GSP_GA10X_SIGNATURE_SECTION,&info);
    if(es!=NVX_GSP_ELF_OK)return NVX_GSP_PREFLIGHT_RUNTIME_INVALID;
    is=nvx_gsp_image_parse(&b->components[2],&booter_load);
    if(is!=NVX_GSP_IMAGE_OK)return NVX_GSP_PREFLIGHT_CONTAINER_INVALID;
    is=nvx_gsp_image_parse(&b->components[3],&booter_unload);
    if(is!=NVX_GSP_IMAGE_OK)return NVX_GSP_PREFLIGHT_CONTAINER_INVALID;
    bs=nvx_gsp_bootloader_parse(&b->components[4],&gsp_bootloader);
    if(bs!=NVX_GSP_BOOTLOADER_OK)return NVX_GSP_PREFLIGHT_CONTAINER_INVALID;
    if(nvx_gsp_elf_find_section(runtime,".fwimage",&image)!=NVX_GSP_ELF_OK||!image.size||
       nvx_gsp_elf_find_section(runtime,".fwversion",&version)!=NVX_GSP_ELF_OK||
       !version.size||version.size>=sizeof(out->version)||version.data[version.size-1U]!=0U)
        return NVX_GSP_PREFLIGHT_CONTAINER_INVALID;
    while(expected_len<sizeof(b->version)&&b->version[expected_len])++expected_len;
    if(expected_len==0U||expected_len==sizeof(b->version)||version.size!=expected_len+1U||
       memcmp(version.data,b->version,expected_len)!=0)return NVX_GSP_PREFLIGHT_VERSION_MISMATCH;
    out->runtime=info;out->booter_load=booter_load;out->booter_unload=booter_unload;
    out->gsp_bootloader=gsp_bootloader;
    out->firmware_image=image;out->firmware_version=version;
    memcpy(out->version,b->version,expected_len+1U);out->runtime_bytes=runtime->blob.size;
    out->ucode_archive_bytes=ucodes->blob.size;out->booter_load_bytes=b->components[2].blob.size;
    out->booter_unload_bytes=b->components[3].blob.size;out->bootloader_bytes=b->components[4].blob.size;
    out->runtime_verified=1U;out->ucode_archive_verified=1U;out->boot_assets_present=1U;
    return NVX_GSP_PREFLIGHT_OK;
}

nvx_gsp_preflight_status_t nvx_gsp_preflight_stage_booters(
    const nvx_gsp_bundle_t *b,
    uint32_t load_fuse_version, uint8_t *load_staging, size_t load_capacity,
    uint32_t unload_fuse_version, uint8_t *unload_staging, size_t unload_capacity,
    nvx_gsp_image_info_t *out_load_info, nvx_gsp_image_info_t *out_unload_info)
{
    nvx_gsp_preflight_report_t report;
    nvx_gsp_image_status_t image_status;

    if (!b || !load_staging || !unload_staging ||
        !out_load_info || !out_unload_info)
        return NVX_GSP_PREFLIGHT_INVALID_ARGUMENT;

    if (overlaps(out_load_info, sizeof(*out_load_info),
                 out_unload_info, sizeof(*out_unload_info)) ||
        overlaps(out_load_info, sizeof(*out_load_info), b, sizeof(*b)) ||
        overlaps(out_unload_info, sizeof(*out_unload_info), b, sizeof(*b)) ||
        overlaps(load_staging, load_capacity, b, sizeof(*b)) ||
        overlaps(unload_staging, unload_capacity, b, sizeof(*b)) ||
        overlaps(load_staging, load_capacity, unload_staging, unload_capacity) ||
        overlaps(load_staging, load_capacity, out_load_info, sizeof(*out_load_info)) ||
        overlaps(load_staging, load_capacity, out_unload_info, sizeof(*out_unload_info)) ||
        overlaps(unload_staging, unload_capacity, out_load_info, sizeof(*out_load_info)) ||
        overlaps(unload_staging, unload_capacity, out_unload_info, sizeof(*out_unload_info)))
        return NVX_GSP_PREFLIGHT_STAGING_ALIAS;

    for (size_t i = 0U; i < NVX_GSP_COMPONENT_COUNT; ++i) {
        const nvx_gsp_component_t *component = &b->components[i];
        if (!component->blob.data || !component->blob.size)
            continue;
        if (overlaps(load_staging, load_capacity,
                     component->blob.data, component->blob.size) ||
            overlaps(unload_staging, unload_capacity,
                     component->blob.data, component->blob.size) ||
            overlaps(out_load_info, sizeof(*out_load_info),
                     component->blob.data, component->blob.size) ||
            overlaps(out_unload_info, sizeof(*out_unload_info),
                     component->blob.data, component->blob.size))
            return NVX_GSP_PREFLIGHT_STAGING_ALIAS;
    }

    nvx_gsp_preflight_status_t preflight_status =
        nvx_gsp_preflight_validate(b, &report);
    if (preflight_status != NVX_GSP_PREFLIGHT_OK)
        return preflight_status;

    if (load_capacity < report.booter_load.data_size ||
        unload_capacity < report.booter_unload.data_size)
        return NVX_GSP_PREFLIGHT_STAGING_TOO_SMALL;
    /*
     * FUSE in the booter descriptor is the firmware's signature baseline;
     * the BAR0 value is the hardware fuse version. Zero is a special case
     * meaning "use the final signature", as in the public GA102+ loader.
     * Validate the relative index before either staging buffer is modified.
     */
    if ((load_fuse_version != 0U &&
         (load_fuse_version > report.booter_load.fuse_version ||
          report.booter_load.fuse_version - load_fuse_version >=
              report.booter_load.signature_count)) ||
        (unload_fuse_version != 0U &&
         (unload_fuse_version > report.booter_unload.fuse_version ||
          report.booter_unload.fuse_version - unload_fuse_version >=
              report.booter_unload.signature_count)))
        return NVX_GSP_PREFLIGHT_FUSE_VERSION_UNSUPPORTED;

    image_status = nvx_gsp_image_prepare(&b->components[2], load_fuse_version,
                                         load_staging, load_capacity);
    if (image_status != NVX_GSP_IMAGE_OK) {
        memset(load_staging, 0, report.booter_load.data_size);
        return NVX_GSP_PREFLIGHT_CONTAINER_INVALID;
    }

    image_status = nvx_gsp_image_prepare(&b->components[3], unload_fuse_version,
                                         unload_staging, unload_capacity);
    if (image_status != NVX_GSP_IMAGE_OK) {
        memset(load_staging, 0, report.booter_load.data_size);
        memset(unload_staging, 0, report.booter_unload.data_size);
        return NVX_GSP_PREFLIGHT_CONTAINER_INVALID;
    }

    *out_load_info = report.booter_load;
    *out_unload_info = report.booter_unload;
    return NVX_GSP_PREFLIGHT_OK;
}

nvx_gsp_preflight_status_t nvx_gsp_preflight_stage_gsp_bootloader(
    const nvx_gsp_bundle_t *b, uint8_t *staging, size_t staging_capacity,
    nvx_gsp_bootloader_info_t *out_info)
{
    nvx_gsp_preflight_report_t report;
    nvx_gsp_preflight_status_t preflight_status;
    nvx_gsp_bootloader_status_t boot_status;

    if (!b || !staging || !out_info)
        return NVX_GSP_PREFLIGHT_INVALID_ARGUMENT;
    if (overlaps(staging, staging_capacity, b, sizeof(*b)) ||
        overlaps(out_info, sizeof(*out_info), b, sizeof(*b)) ||
        overlaps(staging, staging_capacity, out_info, sizeof(*out_info)))
        return NVX_GSP_PREFLIGHT_STAGING_ALIAS;
    for (size_t i = 0U; i < NVX_GSP_COMPONENT_COUNT; ++i) {
        const nvx_gsp_component_t *component = &b->components[i];
        if (!component->blob.data || !component->blob.size)
            continue;
        if (overlaps(staging, staging_capacity,
                     component->blob.data, component->blob.size) ||
            overlaps(out_info, sizeof(*out_info),
                     component->blob.data, component->blob.size))
            return NVX_GSP_PREFLIGHT_STAGING_ALIAS;
    }

    preflight_status = nvx_gsp_preflight_validate(b, &report);
    if (preflight_status != NVX_GSP_PREFLIGHT_OK)
        return preflight_status;
    if (staging_capacity < report.gsp_bootloader.payload_size)
        return NVX_GSP_PREFLIGHT_STAGING_TOO_SMALL;

    boot_status = nvx_gsp_bootloader_prepare(&b->components[4], staging,
                                              staging_capacity, out_info);
    if (boot_status == NVX_GSP_BOOTLOADER_OK)
        return NVX_GSP_PREFLIGHT_OK;
    if (boot_status == NVX_GSP_BOOTLOADER_BUFFER_TOO_SMALL)
        return NVX_GSP_PREFLIGHT_STAGING_TOO_SMALL;
    if (boot_status == NVX_GSP_BOOTLOADER_INVALID_ARGUMENT)
        return NVX_GSP_PREFLIGHT_STAGING_ALIAS;
    return NVX_GSP_PREFLIGHT_CONTAINER_INVALID;
}

nvx_gsp_preflight_status_t nvx_gsp_preflight_stage_booters_from_bar0(
    const nvx_gsp_bundle_t *b, const nvx_mmio_region_t *bar0,
    uint8_t *load_staging, size_t load_capacity,
    uint8_t *unload_staging, size_t unload_capacity,
    nvx_gsp_image_info_t *out_load_info, nvx_gsp_image_info_t *out_unload_info)
{
    nvx_gsp_preflight_report_t report;
    uint32_t load_fuse_version = 0U;
    uint32_t unload_fuse_version = 0U;
    nvx_gsp_ga10x_status_t fuse_status;
    nvx_gsp_preflight_status_t status;

    if (!b || !bar0 || !bar0->base || !load_staging || !unload_staging ||
        !out_load_info || !out_unload_info)
        return NVX_GSP_PREFLIGHT_INVALID_ARGUMENT;

    status = nvx_gsp_preflight_validate(b, &report);
    if (status != NVX_GSP_PREFLIGHT_OK)
        return status;

    /* Engine mask and ucode ID come from the validated signed booter metadata. */
    fuse_status = nvx_gsp_ga10x_read_ucode_fuse_version(
        bar0, report.booter_load.engine_id, report.booter_load.ucode_id,
        &load_fuse_version);
    if (fuse_status != NVX_GSP_GA10X_OK)
        return NVX_GSP_PREFLIGHT_FUSE_READ_FAILED;

    fuse_status = nvx_gsp_ga10x_read_ucode_fuse_version(
        bar0, report.booter_unload.engine_id, report.booter_unload.ucode_id,
        &unload_fuse_version);
    if (fuse_status != NVX_GSP_GA10X_OK)
        return NVX_GSP_PREFLIGHT_FUSE_READ_FAILED;

    return nvx_gsp_preflight_stage_booters(
        b, load_fuse_version, load_staging, load_capacity,
        unload_fuse_version, unload_staging, unload_capacity,
        out_load_info, out_unload_info);
}

nvx_gsp_preflight_status_t nvx_gsp_preflight_load_runtime(
    const nvx_gsp_bundle_t *bundle, nvx_gsp_elf_map_fn map,
    void *map_context, uint64_t *out_entry)
{
    nvx_gsp_preflight_report_t report;
    nvx_gsp_preflight_status_t status;
    nvx_gsp_elf_load_status_t load_status;

    if (!bundle || !map || !out_entry)
        return NVX_GSP_PREFLIGHT_INVALID_ARGUMENT;
    *out_entry = 0U;

    status = nvx_gsp_preflight_validate(bundle, &report);
    if (status != NVX_GSP_PREFLIGHT_OK)
        return status;

    load_status = nvx_gsp_elf_load_segments(
        &bundle->components[0], map, map_context, out_entry);
    if (load_status == NVX_GSP_ELF_LOAD_OK)
        return NVX_GSP_PREFLIGHT_OK;
    *out_entry = 0U;
    return NVX_GSP_PREFLIGHT_RUNTIME_LOAD_FAILED;
}
