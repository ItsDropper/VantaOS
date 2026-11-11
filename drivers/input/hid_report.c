/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "hid_report.h"

#include <string.h>

#define HID_MAX_DESCRIPTOR_BYTES 4096U
#define HID_GLOBAL_STACK_DEPTH 8U
#define HID_COLLECTION_DEPTH 16U
#define HID_MAX_REPORT_BITS (HID_REPORT_MAX_BYTES * 8U)

typedef struct
{
    uint32_t report_size;
    uint32_t report_count;
    uint16_t usage_page;
    int32_t logical_min;
    uint8_t report_id;
} hid_global_state_t;

typedef struct
{
    uint16_t usage;
    uint8_t valid;
    uint16_t minimum;
    uint16_t maximum;
    uint8_t range_valid;
} hid_local_state_t;

static uint32_t read_item_value(const uint8_t* bytes, uint32_t size)
{
    uint32_t value=0U;
    for(uint32_t i=0U;i<size;++i) value|=(uint32_t)bytes[i]<<(i*8U);
    return value;
}

static int32_t read_signed_item(const uint8_t* bytes, uint32_t size)
{
    uint32_t value=read_item_value(bytes,size);
    if(size==1U) return (int8_t)value;
    if(size==2U) return (int16_t)value;
    return (int32_t)value;
}

static uint16_t local_usage(uint32_t value, uint16_t current_page)
{
    return (uint16_t)(value > 0xFFFFU ? value & 0xFFFFU : value);
}

static int add_input_bits(uint32_t bits[256], uint8_t seen[256],
                          uint8_t report_id, uint64_t additional)
{
    if(additional==0U || additional>HID_MAX_REPORT_BITS ||
       additional>HID_MAX_REPORT_BITS-bits[report_id]) return 0;
    bits[report_id]+=(uint32_t)additional;
    seen[report_id]=1U;
    return 1;
}

int hid_report_parse(const uint8_t* descriptor,size_t length,
                    hid_report_layout_t* layout)
{
    if(!descriptor||!layout||length==0U||length>HID_MAX_DESCRIPTOR_BYTES)
        return 0;

    uint32_t input_bits[256]={0};
    uint8_t input_seen[256]={0};
    hid_global_state_t globals={0U,0U,0U,0,0U};
    hid_global_state_t global_stack[HID_GLOBAL_STACK_DEPTH];
    hid_local_state_t local={0U,0U,0U,0U,0U};
    uint32_t stack_depth=0U,collection_depth=0U;
    int numbered_input=0,unnumbered_input=0;
    hid_report_layout_t parsed;
    memset(&parsed,0,sizeof(parsed));

    size_t cursor=0U;
    while(cursor<length)
    {
        uint8_t prefix=descriptor[cursor++];
        if(prefix==0xFEU)
        {
            if(length-cursor<2U) return 0;
            uint32_t long_length=descriptor[cursor++];
            ++cursor;
            if(long_length>length-cursor) return 0;
            cursor+=long_length;
            continue;
        }
        uint32_t size_code=prefix&3U;
        uint32_t item_size=size_code==3U?4U:size_code;
        uint32_t item_type=(prefix>>2U)&3U;
        uint32_t item_tag=(prefix>>4U)&0x0FU;
        if(item_size>length-cursor) return 0;
        uint32_t value=read_item_value(descriptor+cursor,item_size);
        int32_t signed_value=read_signed_item(descriptor+cursor,item_size);
        cursor+=item_size;

        if(item_type==1U)
        {
            switch(item_tag)
            {
                case 0U:
                    if(value>UINT16_MAX) return 0;
                    globals.usage_page=(uint16_t)value;
                    break;
                case 1U: globals.logical_min=signed_value; break;
                case 7U:
                    if(value==0U||value>32U) return 0;
                    globals.report_size=value;
                    break;
                case 8U:
                    if(item_size!=1U||value==0U||value>255U) return 0;
                    globals.report_id=(uint8_t)value;
                    break;
                case 9U:
                    if(value==0U||value>1024U) return 0;
                    globals.report_count=value;
                    break;
                case 10U:
                    if(item_size!=0U||stack_depth>=HID_GLOBAL_STACK_DEPTH)
                        return 0;
                    global_stack[stack_depth++]=globals;
                    break;
                case 11U:
                    if(item_size!=0U||stack_depth==0U) return 0;
                    globals=global_stack[--stack_depth];
                    break;
                default: break;
            }
        }
        else if(item_type==2U)
        {
            switch(item_tag)
            {
                case 0U:
                    local.usage=local_usage(value,globals.usage_page);
                    local.valid=1U;
                    break;
                case 1U:
                    local.minimum=local_usage(value,globals.usage_page);
                    local.range_valid=1U;
                    break;
                case 2U:
                    local.maximum=local_usage(value,globals.usage_page);
                    local.range_valid=1U;
                    break;
                default: break;
            }
        }
        else if(item_type==0U)
        {
            if(item_tag==10U)
            {
                if(item_size==0U||collection_depth>=HID_COLLECTION_DEPTH)
                    return 0;
                if(collection_depth==0U&&local.valid)
                {
                    if(parsed.top_usage==0U)
                    {
                        parsed.top_usage_page=globals.usage_page;
                        parsed.top_usage=local.usage;
                    }
                    if(globals.usage_page==0x01U&&local.usage==0x06U)
                        parsed.has_keyboard=1U;
                    if(globals.usage_page==0x01U&&local.usage==0x02U)
                        parsed.has_mouse=1U;
                    if(globals.usage_page==0x0DU&&
                       (local.usage==0x04U||local.usage==0x05U))
                        parsed.has_touchpad=1U;
                }
                ++collection_depth;
                memset(&local,0,sizeof(local));
            }
            else if(item_tag==12U)
            {
                if(item_size!=0U||collection_depth==0U) return 0;
                --collection_depth;
                memset(&local,0,sizeof(local));
            }
            else if(item_tag==8U||item_tag==9U||item_tag==11U)
            {
                if(item_size==0U) return 0;
                if(globals.report_size==0U||globals.report_count==0U)
                    return 0;
                uint64_t bits=(uint64_t)globals.report_size*globals.report_count;
                if(item_tag==8U)
                {
                    uint32_t offset=input_bits[globals.report_id];
                    if(offset>UINT16_MAX||globals.report_count>UINT16_MAX||
                       parsed.field_count>=HID_REPORT_MAX_FIELDS)
                        return 0;
                    if((value&1U)==0U)
                    {
                        hid_input_field_t* field=&parsed.fields[parsed.field_count++];
                        field->report_id=globals.report_id;
                        field->usage_page=globals.usage_page;
                        field->usage_min=local.range_valid?local.minimum:
                                         local.valid?local.usage:0U;
                        field->usage_max=local.range_valid?local.maximum:
                                         local.valid?local.usage:0U;
                        field->bit_offset=(uint16_t)offset;
                        field->bit_size=(uint8_t)globals.report_size;
                        field->report_count=(uint16_t)globals.report_count;
                        field->flags=(uint8_t)value;
                        field->logical_min=globals.logical_min;
                    }
                    if(globals.report_id==0U) unnumbered_input=1;
                    else numbered_input=1;
                    if(numbered_input&&unnumbered_input) return 0;
                    if(!add_input_bits(input_bits,input_seen,globals.report_id,bits))
                        return 0;
                }
                memset(&local,0,sizeof(local));
            }
            else
                memset(&local,0,sizeof(local));
        }
    }

    if(collection_depth!=0U||stack_depth!=0U) return 0;
    for(uint32_t id=0U;id<256U;++id)
    {
        if(!input_seen[id]) continue;
        uint32_t bytes=(input_bits[id]+7U)/8U;
        if(id!=0U) ++bytes;
        if(bytes==0U||bytes>HID_REPORT_MAX_BYTES||
           parsed.report_count>=HID_REPORT_MAX_IDS) return 0;
        hid_input_report_t* report=&parsed.reports[parsed.report_count++];
        report->report_id=(uint8_t)id;
        report->input_bytes=(uint16_t)bytes;
    }
    if(parsed.report_count==0U) return 0;
    *layout=parsed;
    return 1;
}
