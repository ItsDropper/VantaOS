/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_preflight.h"
#include <string.h>
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
    nvx_gsp_elf_status_t es;size_t expected_len=0U;
    if(!b||!out||overlaps(out,sizeof(*out),b,sizeof(*b)))return NVX_GSP_PREFLIGHT_INVALID_ARGUMENT;
    for(size_t i=0;i<NVX_GSP_COMPONENT_COUNT;i++)
        if(b->components[i].blob.data&&overlaps(out,sizeof(*out),b->components[i].blob.data,b->components[i].blob.size))
            return NVX_GSP_PREFLIGHT_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(!b->acquired||b->family!=NVX_FAMILY_AMPERE||!b->provider.verify)
        return NVX_GSP_PREFLIGHT_BUNDLE_NOT_READY;
    const nvx_gsp_component_t *runtime=&b->components[0],*ucodes=&b->components[1];
    if(!runtime->acquired||runtime->kind!=NVX_GSP_RUNTIME||!runtime->blob.data||!runtime->blob.size||
       !ucodes->acquired||ucodes->kind!=NVX_GSP_UCODE_ARCHIVE||!ucodes->blob.data||!ucodes->blob.size)
        return NVX_GSP_PREFLIGHT_COMPONENT_INVALID;
    for(size_t i=2U;i<NVX_GSP_COMPONENT_COUNT;i++)
        if(!b->components[i].acquired||b->components[i].kind!=(nvx_gsp_component_kind_t)i||
           !b->components[i].blob.data||!b->components[i].blob.size)
            return NVX_GSP_PREFLIGHT_COMPONENT_INVALID;
    es=nvx_gsp_elf_parse(runtime,NVX_GSP_GA10X_SIGNATURE_SECTION,&info);
    if(es!=NVX_GSP_ELF_OK)return NVX_GSP_PREFLIGHT_RUNTIME_INVALID;
    if(nvx_gsp_elf_find_section(runtime,".fwimage",&image)!=NVX_GSP_ELF_OK||!image.size||
       nvx_gsp_elf_find_section(runtime,".fwversion",&version)!=NVX_GSP_ELF_OK||
       !version.size||version.size>=sizeof(out->version)||version.data[version.size-1U]!=0U)
        return NVX_GSP_PREFLIGHT_CONTAINER_INVALID;
    while(expected_len<sizeof(b->version)&&b->version[expected_len])++expected_len;
    if(expected_len==0U||expected_len==sizeof(b->version)||version.size!=expected_len+1U||
       memcmp(version.data,b->version,expected_len)!=0)return NVX_GSP_PREFLIGHT_VERSION_MISMATCH;
    out->runtime=info;out->firmware_image=image;out->firmware_version=version;
    memcpy(out->version,b->version,expected_len+1U);out->runtime_bytes=runtime->blob.size;
    out->ucode_archive_bytes=ucodes->blob.size;out->booter_load_bytes=b->components[2].blob.size;
    out->booter_unload_bytes=b->components[3].blob.size;out->bootloader_bytes=b->components[4].blob.size;
    out->runtime_verified=1U;out->ucode_archive_verified=1U;out->boot_assets_present=1U;
    return NVX_GSP_PREFLIGHT_OK;
}
