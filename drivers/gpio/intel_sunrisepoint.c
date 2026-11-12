/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "intel_sunrisepoint.h"

#include <stddef.h>
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
