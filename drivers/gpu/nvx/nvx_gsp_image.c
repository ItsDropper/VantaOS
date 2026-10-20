/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_image.h"
#include <string.h>
#define BIN_HEADER_SIZE 24U
#define HS_HEADER_V2_SIZE 36U
#define LOAD_HEADER_V2_SIZE 20U
#define LOAD_APP_V2_SIZE 16U
static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8U) |
           ((uint32_t)p[2]<<16U) | ((uint32_t)p[3]<<24U);
}
static int range_ok(size_t total,uint64_t off,uint64_t len)
{
    return off<=total && len<=(uint64_t)total-off;
}
static int read_u32(const uint8_t *data,size_t total,uint32_t off,uint32_t *out)
{
    if(!out||!range_ok(total,off,4U)) return 0;
    *out=read_le32(data+off); return 1;
}
static int image_range_ok(uint32_t total,uint32_t off,uint32_t len)
{
    return off<=total && len<=total-off;
}
static int overlaps(const void *a,size_t an,const void *b,size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(!an||!bn)return 0;
    if(x>UINTPTR_MAX-an||y>UINTPTR_MAX-bn)return 1;
    return x<y+bn&&y<x+an;
}
nvx_gsp_image_status_t nvx_gsp_image_parse(
    const nvx_gsp_component_t *component,nvx_gsp_image_info_t *out)
{
    const uint8_t *data; size_t size;
    uint32_t hs_off,meta_off,meta_size,patch_loc_off,patch_sig_off;
    uint32_t sig_count_off,load_off,load_size,sig_prod_off,sig_prod_size;
    uint32_t loc,sig,apps,count;
    nvx_gsp_image_info_t info={0};
    if(!component||!out)
        return NVX_GSP_IMAGE_INVALID_ARGUMENT;
    if(overlaps(out,sizeof(*out),component,sizeof(*component)) ||
       (component->blob.data &&
        overlaps(out,sizeof(*out),component->blob.data,component->blob.size)))
        return NVX_GSP_IMAGE_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(!component->acquired||!component->blob.data)
        return NVX_GSP_IMAGE_INVALID_ARGUMENT;
    if(component->kind!=NVX_GSP_BOOTER_LOAD &&
       component->kind!=NVX_GSP_BOOTER_UNLOAD)
        return NVX_GSP_IMAGE_UNSUPPORTED_LAYOUT;
    data=component->blob.data; size=component->blob.size;
    if(size<BIN_HEADER_SIZE) return NVX_GSP_IMAGE_TRUNCATED;
    info.binary_magic=read_le32(data);
    if(info.binary_magic!=0x10DEU)return NVX_GSP_IMAGE_BAD_MAGIC;
    info.binary_version=read_le32(data+4U);
    info.binary_size=read_le32(data+8U);
    hs_off=read_le32(data+12U);
    info.data_offset=read_le32(data+16U);
    info.data_size=read_le32(data+20U);
    if(info.binary_size<BIN_HEADER_SIZE||info.binary_size>size)
        return NVX_GSP_IMAGE_HEADER_INVALID;
    if(!range_ok(info.binary_size,info.data_offset,info.data_size))
        return NVX_GSP_IMAGE_RANGE_INVALID;
    if(!range_ok(info.binary_size,hs_off,HS_HEADER_V2_SIZE))
        return NVX_GSP_IMAGE_HEADER_INVALID;
    sig_prod_off=read_le32(data+hs_off);
    sig_prod_size=read_le32(data+hs_off+4U);
    patch_loc_off=read_le32(data+hs_off+8U);
    patch_sig_off=read_le32(data+hs_off+12U);
    meta_off=read_le32(data+hs_off+16U);
    meta_size=read_le32(data+hs_off+20U);
    sig_count_off=read_le32(data+hs_off+24U);
    load_off=read_le32(data+hs_off+28U);
    load_size=read_le32(data+hs_off+32U);
    if(!range_ok(info.binary_size,meta_off,meta_size)||meta_size<12U)
        return NVX_GSP_IMAGE_METADATA_INVALID;
    if(!read_u32(data,info.binary_size,patch_loc_off,&loc)||
       !read_u32(data,info.binary_size,patch_sig_off,&sig)||
       !read_u32(data,info.binary_size,sig_count_off,&count))
        return NVX_GSP_IMAGE_HEADER_INVALID;
    /*
     * The published GA10x booter format requires patch_sig == 0. Signatures
     * are a packed array and the selected signature is indexed by fuse version;
     * patch_sig is not an offset into that array.
     */
    if(sig!=0U||!count||count>NVX_GSP_IMAGE_MAX_SIGNATURES||!sig_prod_size||
       sig_prod_size%count||!range_ok(info.binary_size,sig_prod_off,sig_prod_size))
        return NVX_GSP_IMAGE_SIGNATURE_INVALID;
    info.signature_count=count;
    info.signature_size=sig_prod_size/count;
    info.signature_offset=sig_prod_off;
    info.patch_location=loc; info.patch_signature=sig;
    info.fuse_version=read_le32(data+meta_off);
    info.engine_id=read_le32(data+meta_off+4U);
    info.ucode_id=read_le32(data+meta_off+8U);
    if(load_size<LOAD_HEADER_V2_SIZE||!range_ok(info.binary_size,load_off,load_size))
        return NVX_GSP_IMAGE_HEADER_INVALID;
    info.load_header_offset=load_off; info.load_header_size=load_size;
    info.os_code_offset=read_le32(data+load_off);
    info.os_code_size=read_le32(data+load_off+4U);
    info.os_data_offset=read_le32(data+load_off+8U);
    info.os_data_size=read_le32(data+load_off+12U);
    apps=read_le32(data+load_off+16U);
    if(!apps||apps>NVX_GSP_IMAGE_MAX_APPS||
       apps>(load_size-LOAD_HEADER_V2_SIZE)/LOAD_APP_V2_SIZE)
        return NVX_GSP_IMAGE_APP_INVALID;
    if(!image_range_ok(info.data_size,info.os_code_offset,info.os_code_size)||
       !image_range_ok(info.data_size,info.os_data_offset,info.os_data_size))
        return NVX_GSP_IMAGE_RANGE_INVALID;
    info.app_count=apps;
    for(uint32_t i=0;i<apps;i++) {
        const uint8_t *app=data+load_off+LOAD_HEADER_V2_SIZE+
                           (size_t)i*LOAD_APP_V2_SIZE;
        uint32_t co=read_le32(app),cs=read_le32(app+4U);
        uint32_t doff=read_le32(app+8U),ds=read_le32(app+12U);
        if(!image_range_ok(info.data_size,co,cs)||
           !image_range_ok(info.data_size,doff,ds))
            return NVX_GSP_IMAGE_APP_INVALID;
        if(i==0U) { info.app0_offset=co; info.app0_size=cs;
            info.app0_data_offset=doff; info.app0_data_size=ds; }
    }
    if(loc<info.os_data_offset||
       !image_range_ok(info.os_data_size,loc-info.os_data_offset,info.signature_size)||
       !image_range_ok(info.data_size,loc,info.signature_size))
        return NVX_GSP_IMAGE_SIGNATURE_INVALID;
    *out=info; return NVX_GSP_IMAGE_OK;
}

nvx_gsp_image_status_t nvx_gsp_image_prepare(
    const nvx_gsp_component_t *component, uint32_t fuse_version,
    uint8_t *staging, size_t staging_size)
{
    nvx_gsp_image_info_t info;
    nvx_gsp_image_status_t status;
    uint32_t signature_index;
    uint64_t signature_source;

    if (!component || !staging)
        return NVX_GSP_IMAGE_INVALID_ARGUMENT;

    status = nvx_gsp_image_parse(component, &info);
    if (status != NVX_GSP_IMAGE_OK)
        return status;
    if (staging_size < info.data_size)
        return NVX_GSP_IMAGE_STAGING_TOO_SMALL;
    if (overlaps(staging, info.data_size, component, sizeof(*component)) ||
        overlaps(staging, info.data_size, component->blob.data,
                 component->blob.size))
        return NVX_GSP_IMAGE_INVALID_ARGUMENT;

    if (info.signature_count > 1U) {
        if (fuse_version >= info.signature_count)
            return NVX_GSP_IMAGE_FUSE_VERSION_UNSUPPORTED;
        signature_index = info.signature_count - 1U - fuse_version;
    } else {
        signature_index = 0U;
    }

    signature_source = (uint64_t)info.signature_offset +
        (uint64_t)signature_index * info.signature_size;
    if (!range_ok(info.binary_size, signature_source, info.signature_size))
        return NVX_GSP_IMAGE_SIGNATURE_INVALID;

    /*
     * The .bin container holds metadata/signatures before the actual Falcon
     * payload. Hardware loaders consume the payload only, with the selected
     * fuse-version signature patched at the payload-relative patch location.
     * Never mutate the provider's verified source blob.
     */
    memcpy(staging, component->blob.data + info.data_offset, info.data_size);
    memcpy(staging + info.patch_location,
           component->blob.data + (size_t)signature_source,
           info.signature_size);
    return NVX_GSP_IMAGE_OK;
}
