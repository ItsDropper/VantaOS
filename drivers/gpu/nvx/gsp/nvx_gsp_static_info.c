/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_static_info.h"

#include <string.h>
#include <stdint.h>
#include <limits.h>

#define OP_IMM32 0U
#define OP_SEQ32 1U
#define OP_SEQ64 2U
#define OP_ARRAY8 3U
#define OP_ARRAY32 4U
#define OP_ARRAY64 5U

/* Public NVIDIA gmcapi_gsp_config.h static-info keys. */
#define KEY_BOARD_ID 0x0001U
#define KEY_SKU_CONFIG_VERSION 0x0002U
#define KEY_SKU_BUSINESS_CYCLE 0x0003U
#define KEY_CHIP_SUB_REV 0x0004U
#define KEY_FB_REGION_COUNT 0x0010U
#define KEY_FB_REGION_PERFORMANCE 0x0011U
#define KEY_FB_REGION_FLAGS 0x0012U
#define KEY_FB_REGION_TAG 0x0013U
#define KEY_BRANDING 0x1000U
#define KEY_MISC_FEATURES 0x1001U
#define KEY_POWER_FEATURES 0x1002U
#define KEY_FB_LENGTH 0x1010U
#define KEY_FB_REGION_BASE 0x1011U
#define KEY_FB_REGION_LIMIT 0x1012U
#define KEY_FB_REGION_RESERVED 0x1013U
#define KEY_BAR1_PDE_BASE 0x1020U
#define KEY_BAR2_PDE_BASE 0x1021U
#define KEY_WPR_HEAP_OFFSET 0x1080U
#define KEY_WPR_FRTS_OFFSET 0x1081U
#define KEY_GPU_NAME 0x2000U
#define KEY_GPU_SHORT_NAME 0x2001U

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0]|((uint32_t)p[1]<<8U)|
           ((uint32_t)p[2]<<16U)|((uint32_t)p[3]<<24U);
}
static uint64_t get64(const uint8_t *p)
{
    return (uint64_t)get32(p)|((uint64_t)get32(p+4U)<<32U);
}
static size_t words_for_bytes(size_t n)
{
    return n/8U+(n%8U!=0U?1U:0U);
}
static int overlaps(const void *a,size_t an,const void *b,size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(!an||!bn)return 0;
    if(!a||!b||x>UINTPTR_MAX-an||y>UINTPTR_MAX-bn)return 1;
    return x<y+bn&&y<x+an;
}

static nvx_gsp_nvkv_status_t set32(nvx_gsp_static_info_t *o,
                                    uint16_t key,uint16_t idx,uint32_t v)
{
    if(key==KEY_FB_REGION_PERFORMANCE||key==KEY_FB_REGION_FLAGS||key==KEY_FB_REGION_TAG) {
        nvx_gsp_static_fb_region_t *r;
        if(idx>=NVX_GSP_STATIC_MAX_FB_REGIONS)return NVX_GSP_NVKV_BAD_STREAM;
        r=&o->fb_regions[idx];
        if(key==KEY_FB_REGION_PERFORMANCE){r->performance=v;r->has_performance=1U;}
        else if(key==KEY_FB_REGION_FLAGS){r->flags=v;r->has_flags=1U;}
        else {r->tag=v;r->has_tag=1U;}
        return NVX_GSP_NVKV_OK;
    }
    if(idx!=0U)return NVX_GSP_NVKV_BAD_STREAM;
    switch(key) {
    case KEY_BOARD_ID:o->board_id=v;o->present|=NVX_GSP_SC_BOARD_ID;break;
    case KEY_SKU_CONFIG_VERSION:o->sku_config_version=v;o->present|=NVX_GSP_SC_SKU_CONFIG_VERSION;break;
    case KEY_SKU_BUSINESS_CYCLE:o->sku_business_cycle=v;o->present|=NVX_GSP_SC_SKU_BUSINESS_CYCLE;break;
    case KEY_CHIP_SUB_REV:o->chip_sub_revision=v;o->present|=NVX_GSP_SC_CHIP_SUB_REV;break;
    case KEY_FB_REGION_COUNT:
        if(v>NVX_GSP_STATIC_MAX_FB_REGIONS)return NVX_GSP_NVKV_BAD_STREAM;
        o->fb_region_count=v;o->present|=NVX_GSP_SC_FB_REGION_COUNT;break;
    default:break; /* Unknown well-formed key. */
    }
    return NVX_GSP_NVKV_OK;
}

static nvx_gsp_nvkv_status_t set64(nvx_gsp_static_info_t *o,
                                    uint16_t key,uint16_t idx,uint64_t v)
{
    if(key==KEY_FB_REGION_BASE||key==KEY_FB_REGION_LIMIT||key==KEY_FB_REGION_RESERVED) {
        nvx_gsp_static_fb_region_t *r;
        if(idx>=NVX_GSP_STATIC_MAX_FB_REGIONS)return NVX_GSP_NVKV_BAD_STREAM;
        r=&o->fb_regions[idx];
        if(key==KEY_FB_REGION_BASE){r->base=v;r->has_base=1U;}
        else if(key==KEY_FB_REGION_LIMIT){r->limit=v;r->has_limit=1U;}
        else {r->reserved=v;r->has_reserved=1U;}
        return NVX_GSP_NVKV_OK;
    }
    if(idx!=0U)return NVX_GSP_NVKV_BAD_STREAM;
    switch(key) {
    case KEY_BRANDING:o->branding=v;o->present|=NVX_GSP_SC_BRANDING;break;
    case KEY_MISC_FEATURES:o->misc_features=v;o->present|=NVX_GSP_SC_MISC_FEATURES;break;
    case KEY_POWER_FEATURES:o->power_features=v;o->present|=NVX_GSP_SC_POWER_FEATURES;break;
    case KEY_FB_LENGTH:o->fb_length=v;o->present|=NVX_GSP_SC_FB_LENGTH;break;
    case KEY_BAR1_PDE_BASE:o->bar1_pde_base=v;o->present|=NVX_GSP_SC_BAR1_PDE_BASE;break;
    case KEY_BAR2_PDE_BASE:o->bar2_pde_base=v;o->present|=NVX_GSP_SC_BAR2_PDE_BASE;break;
    case KEY_WPR_HEAP_OFFSET:o->wpr_non_wpr_heap_offset=v;o->present|=NVX_GSP_SC_WPR_HEAP_OFFSET;break;
    case KEY_WPR_FRTS_OFFSET:o->wpr_frts_offset=v;o->present|=NVX_GSP_SC_WPR_FRTS_OFFSET;break;
    default:break;
    }
    return NVX_GSP_NVKV_OK;
}

static nvx_gsp_nvkv_status_t set_array8(nvx_gsp_static_info_t *o,
                                        uint16_t key,uint16_t idx,
                                        const uint8_t *data,size_t count)
{
    char *dst=NULL;size_t cap=0U;
    if(idx!=0U)return NVX_GSP_NVKV_BAD_STREAM;
    if(key==KEY_GPU_NAME){dst=o->gpu_name;cap=sizeof(o->gpu_name);o->present|=NVX_GSP_SC_GPU_NAME;}
    else if(key==KEY_GPU_SHORT_NAME){dst=o->gpu_short_name;cap=sizeof(o->gpu_short_name);o->present|=NVX_GSP_SC_GPU_SHORT_NAME;}
    else return NVX_GSP_NVKV_OK;
    if(count>=cap)return NVX_GSP_NVKV_BAD_STREAM;
    if(count)memcpy(dst,data,count);
    dst[count]='\0';
    return NVX_GSP_NVKV_OK;
}

nvx_gsp_nvkv_status_t nvx_gsp_static_info_decode(
    const void *buffer,size_t size_bytes,nvx_gsp_static_info_t *out)
{
    const uint8_t *p=(const uint8_t *)buffer;
    size_t words,pos=0U;
    nvx_gsp_static_info_t decoded={0};
    if(!buffer||!out||!size_bytes||(size_bytes&7U)!=0U||
       overlaps(buffer,size_bytes,out,sizeof(*out)))
        return NVX_GSP_NVKV_INVALID_ARGUMENT;
    if(nvx_gsp_nvkv_validate(buffer,size_bytes)!=NVX_GSP_NVKV_OK)
        return NVX_GSP_NVKV_BAD_STREAM;
    words=size_bytes/8U;
    while(pos<words) {
        uint64_t ins=get64(p+pos*8U);
        uint32_t op=(uint32_t)((ins>>28U)&0xFU);
        uint16_t idx=(uint16_t)((ins>>16U)&0x0FFFU);
        uint16_t key=(uint16_t)ins;
        uint32_t count=(uint32_t)(ins>>32U);
        size_t payload_words=0U;
        const uint8_t *payload;
        nvx_gsp_nvkv_status_t s=NVX_GSP_NVKV_OK;
        ++pos;payload=p+pos*8U;
        switch(op) {
        case OP_IMM32:
            s=set32(&decoded,key,idx,count);
            break;
        case OP_SEQ32:
            payload_words=words_for_bytes((size_t)count*4U);
            for(uint32_t i=0U;i<count;i++) {
                if((uint32_t)key+i>UINT16_MAX)return NVX_GSP_NVKV_BAD_STREAM;
                s=set32(&decoded,(uint16_t)(key+i),idx,get32(payload+(size_t)i*4U));
                if(s!=NVX_GSP_NVKV_OK)return s;
            }
            break;
        case OP_SEQ64:
            payload_words=(size_t)count;
            for(uint32_t i=0U;i<count;i++) {
                if((uint32_t)key+i>UINT16_MAX)return NVX_GSP_NVKV_BAD_STREAM;
                s=set64(&decoded,(uint16_t)(key+i),idx,get64(payload+(size_t)i*8U));
                if(s!=NVX_GSP_NVKV_OK)return s;
            }
            break;
        case OP_ARRAY8:
            payload_words=words_for_bytes((size_t)count);
            s=set_array8(&decoded,key,idx,payload,(size_t)count);
            break;
        case OP_ARRAY32:
            payload_words=words_for_bytes((size_t)count*4U);
            break;
        case OP_ARRAY64:
            payload_words=(size_t)count;
            break;
        default:return NVX_GSP_NVKV_BAD_STREAM;
        }
        if(s!=NVX_GSP_NVKV_OK)return s;
        if(payload_words>words-pos)return NVX_GSP_NVKV_BAD_STREAM;
        pos+=payload_words;
    }
    *out=decoded;
    return NVX_GSP_NVKV_OK;
}
