/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_preflight.h"
#include <string.h>
nvx_gsp_preflight_status_t nvx_gsp_preflight_validate(
    const nvx_gsp_bundle_t *b,nvx_gsp_preflight_report_t *out)
{
    nvx_gsp_elf_info_t info;
    nvx_gsp_elf_status_t es;
    if(out)memset(out,0,sizeof(*out));
    if(!b||!out)return NVX_GSP_PREFLIGHT_INVALID_ARGUMENT;
    if(!b->acquired||b->family!=NVX_FAMILY_AMPERE||
       !b->provider.verify)
        return NVX_GSP_PREFLIGHT_BUNDLE_NOT_READY;
    const nvx_gsp_component_t *runtime=&b->components[0];
    const nvx_gsp_component_t *ucodes=&b->components[1];
    const nvx_gsp_component_t *log=&b->components[2];
    if(!runtime->acquired||runtime->kind!=NVX_GSP_RUNTIME||
       !runtime->blob.data||!runtime->blob.size||
       !ucodes->acquired||ucodes->kind!=NVX_GSP_UCODE_ARCHIVE||
       !ucodes->blob.data||!ucodes->blob.size)
        return NVX_GSP_PREFLIGHT_COMPONENT_INVALID;
    es=nvx_gsp_elf_parse(runtime,NVX_GSP_GA10X_SIGNATURE_SECTION,&info);
    if(es!=NVX_GSP_ELF_OK)return NVX_GSP_PREFLIGHT_RUNTIME_INVALID;
    out->runtime=info;
    out->runtime_bytes=runtime->blob.size;
    out->ucode_archive_bytes=ucodes->blob.size;
    out->runtime_verified=1U;
    out->ucode_archive_verified=1U;
    if(log->acquired) {
        if(log->kind!=NVX_GSP_LOG||!log->blob.data||!log->blob.size) {
            memset(out,0,sizeof(*out));
            return NVX_GSP_PREFLIGHT_COMPONENT_INVALID;
        }
        out->optional_log_present=1U;
        out->log_bytes=log->blob.size;
    }
    return NVX_GSP_PREFLIGHT_OK;
}
