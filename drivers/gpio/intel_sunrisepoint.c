/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "intel_sunrisepoint.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const intel_sptlp_community_t communities[INTEL_SPTLP_COMMUNITY_COUNT]={
    {0U,47U,0U},
    {48U,119U,1U},
    {120U,151U,2U}
};

static int id_equal(const char *left,const char *right)
{
    if(!left||!right) return 0;
    while(*left&&*right)
    {
        char a=*left++,b=*right++;
        if(a>='a'&&a<='z') a=(char)(a-'a'+'A');
        if(b>='a'&&b<='z') b=(char)(b-'a'+'A');
        if(a!=b) return 0;
    }
    return *left=='\0'&&*right=='\0';
}

static int path_equal(const char *left,const char *right)
{
    if(!left||!right) return 0;
    while(*left=='\\') ++left;
    while(*right=='\\') ++right;
    for(;;)
    {
        const char *left_end=left,*right_end=right;
        while(*left_end&&*left_end!='.') ++left_end;
        while(*right_end&&*right_end!='.') ++right_end;
        while(left_end>left&&left_end[-1]=='_') --left_end;
        while(right_end>right&&right_end[-1]=='_') --right_end;
        size_t left_length=(size_t)(left_end-left);
        size_t right_length=(size_t)(right_end-right);
        if(left_length!=right_length) return 0;
        for(size_t i=0U;i<left_length;++i)
        {
            char a=left[i],b=right[i];
            if(a>='a'&&a<='z') a=(char)(a-'a'+'A');
            if(b>='a'&&b<='z') b=(char)(b-'a'+'A');
            if(a!=b) return 0;
        }
        int left_done=*left_end=='\0';
        int right_done=*right_end=='\0';
        if(left_done||right_done) return left_done&&right_done;
        left=left_end+1;
        right=right_end+1;
    }
}


static int intel_sptlp_memory_resource_valid(
    const acpi_aml_resource_t *resource)
{
    if(!resource||resource->resource_type!=1U||
       resource->address==0U||resource->minimum!=resource->maximum||
       resource->address!=resource->minimum||
       resource->length<INTEL_SPTLP_MMIO_MIN_LENGTH||
       resource->length>INTEL_SPTLP_MMIO_MAX_LENGTH||
       resource->length>SIZE_MAX||
       (resource->address&3U)!=0U||
       resource->length>UINT64_MAX-resource->address)
        return 0;
    return 1;
}

static int intel_sptlp_ranges_overlap(
    const acpi_aml_resource_t *left,
    const acpi_aml_resource_t *right)
{
    uint64_t left_end=left->address+left->length;
    uint64_t right_end=right->address+right->length;
    return left->address<right_end&&right->address<left_end;
}

int intel_sptlp_probe_controller(const acpi_aml_device_t *controller,
                                 const intel_sptlp_mmio_ops_t *ops,
                                 intel_sptlp_snapshot_t *snapshot)
{
    if(!controller||!ops||!snapshot||controller->invalid||
       controller->sta_dynamic||
       (controller->sta_valid&&
        ((controller->sta&0x03U)!=0x03U||(controller->sta&0x08U)==0U))||
       !id_equal(controller->hid,"INT344B")||
       !ops->map||!ops->unmap||!ops->read32||
       controller->resource_count>ACPI_AML_MAX_RESOURCES)
        return 0;

    const acpi_aml_resource_t *windows[INTEL_SPTLP_COMMUNITY_COUNT]={0};
    uint32_t window_count=0U;
    for(uint32_t i=0U;i<controller->resource_count;++i)
    {
        const acpi_aml_resource_t *resource=&controller->resources[i];
        if(resource->resource_type!=1U) continue;
        if(window_count>=INTEL_SPTLP_COMMUNITY_COUNT||
           !intel_sptlp_memory_resource_valid(resource))
            return 0;
        for(uint32_t prior=0U;prior<window_count;++prior)
            if(intel_sptlp_ranges_overlap(windows[prior],resource))
                return 0;
        windows[window_count++]=resource;
    }
    if(window_count!=INTEL_SPTLP_COMMUNITY_COUNT) return 0;

    void *mappings[INTEL_SPTLP_COMMUNITY_COUNT]={0};
    intel_sptlp_snapshot_t candidate={0};
    uint32_t mapped=0U;
    for(;mapped<INTEL_SPTLP_COMMUNITY_COUNT;++mapped)
    {
        const acpi_aml_resource_t *resource=windows[mapped];
        size_t length=(size_t)resource->length;
        mappings[mapped]=ops->map(ops->context,resource->address,length);
        if(!mappings[mapped]) break;

        volatile uint8_t *base=(volatile uint8_t*)mappings[mapped];
        candidate.communities[mapped].base=resource->address;
        candidate.communities[mapped].length=resource->length;
        candidate.communities[mapped].pad_ownership=
            ops->read32(ops->context,base,INTEL_SPTLP_PAD_OWN_OFFSET);
        candidate.communities[mapped].pad_config_lock=
            ops->read32(ops->context,base,INTEL_SPTLP_PADCFGLOCK_OFFSET);
        candidate.communities[mapped].host_software_ownership=
            ops->read32(ops->context,base,INTEL_SPTLP_HOSTSW_OWN_OFFSET);
        candidate.communities[mapped].interrupt_enable=
            ops->read32(ops->context,base,INTEL_SPTLP_GPI_IE_OFFSET);
    }

    int success=mapped==INTEL_SPTLP_COMMUNITY_COUNT;
    while(mapped!=0U)
    {
        --mapped;
        if(!ops->unmap(ops->context,mappings[mapped],
                       (size_t)windows[mapped]->length))
            success=0;
    }
    if(!success) return 0;
    *snapshot=candidate;
    return 1;
}

int intel_sptlp_pin_community(uint16_t pin,uint8_t *community)
{
    if(!community||pin>=INTEL_SPTLP_PIN_COUNT) return 0;
    for(uint32_t i=0U;i<INTEL_SPTLP_COMMUNITY_COUNT;++i)
    {
        if(pin<communities[i].first_pin||pin>communities[i].last_pin)
            continue;
        *community=communities[i].index;
        return 1;
    }
    return 0;
}

int intel_sptlp_validate_gpio_resource(
    const acpi_aml_device_t *controller,
    const acpi_aml_resource_t *resource)
{
    if(!controller||!resource||controller->invalid||controller->sta_dynamic||
       (controller->sta_valid&&
        ((controller->sta&0x03U)!=0x03U||(controller->sta&0x08U)==0U))||
       !id_equal(controller->hid,"INT344B")||
       resource->resource_type!=5U||
       resource->gpio_connection_type>1U||
       resource->gpio_pin_count==0U||
       resource->gpio_pin_count>ACPI_AML_MAX_GPIO_PINS||
       !path_equal(resource->resource_source,controller->path))
        return 0;

    for(uint32_t i=0U;i<resource->gpio_pin_count;++i)
    {
        uint8_t community=0U;
        if(!intel_sptlp_pin_community(resource->gpio_pins[i],&community))
            return 0;
    }
    return 1;
}
