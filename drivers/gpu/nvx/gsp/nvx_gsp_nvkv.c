/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_nvkv.h"

#include <string.h>
#include <stdint.h>
#include <limits.h>

#define NVKV_OPCODE_IMM32  0U
#define NVKV_OPCODE_SEQ32  1U
#define NVKV_OPCODE_SEQ64  2U
#define NVKV_OPCODE_ARRAY8 3U
#define NVKV_OPCODE_ARRAY32 4U
#define NVKV_OPCODE_ARRAY64 5U
#define NVKV_MAX_COUNT UINT32_MAX

static void store_le32(uint8_t *p, uint32_t v)
{
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8U);
    p[2]=(uint8_t)(v>>16U); p[3]=(uint8_t)(v>>24U);
}

static uint32_t load_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8U) |
           ((uint32_t)p[2]<<16U) | ((uint32_t)p[3]<<24U);
}

static void store_le64(uint8_t *p, uint64_t v)
{
    store_le32(p,(uint32_t)v);
    store_le32(p+4U,(uint32_t)(v>>32U));
}

static uint64_t load_le64(const uint8_t *p)
{
    return (uint64_t)load_le32(p) | ((uint64_t)load_le32(p+4U)<<32U);
}

static int writer_valid(const nvx_gsp_nvkv_writer_t *w)
{
    return w && w->initialized && w->buffer &&
           w->words_written <= w->capacity / sizeof(uint64_t);
}

static int add_words(size_t a, size_t b, size_t *out)
{
    if (a > SIZE_MAX-b) return 0;
    *out=a+b;
    return 1;
}

static size_t words_for_bytes(size_t bytes)
{
    return bytes / sizeof(uint64_t) +
           (bytes % sizeof(uint64_t) != 0U ? 1U : 0U);
}

static nvx_gsp_nvkv_status_t reserve_words(
    nvx_gsp_nvkv_writer_t *w, size_t additional, size_t *start)
{
    size_t end;
    if (!writer_valid(w)) return NVX_GSP_NVKV_INVALID_ARGUMENT;
    if (!add_words(w->words_written,additional,&end) ||
        end > w->capacity / sizeof(uint64_t))
        return NVX_GSP_NVKV_NO_SPACE;
    *start=w->words_written;
    w->words_written=end;
    return NVX_GSP_NVKV_OK;
}

static uint64_t instruction(uint32_t opcode, uint16_t index,
                            uint16_t key, uint32_t count_or_value)
{
    return ((uint64_t)count_or_value<<32U) |
           ((uint64_t)opcode<<28U) |
           ((uint64_t)index<<16U) | (uint64_t)key;
}

nvx_gsp_nvkv_status_t nvx_gsp_nvkv_begin(
    nvx_gsp_nvkv_writer_t *w, void *buffer, size_t capacity)
{
    if (!w || !buffer || capacity < sizeof(uint64_t) ||
        (capacity / sizeof(uint64_t)) == 0U)
        return NVX_GSP_NVKV_INVALID_ARGUMENT;
    memset(buffer,0,capacity);
    w->buffer=(uint8_t *)buffer;
    w->capacity=capacity;
    w->words_written=0U;
    w->initialized=1U;
    return NVX_GSP_NVKV_OK;
}

nvx_gsp_nvkv_status_t nvx_gsp_nvkv_put_imm32(
    nvx_gsp_nvkv_writer_t *w, uint16_t index, uint16_t key, uint32_t value)
{
    size_t start;
    nvx_gsp_nvkv_status_t s;
    if (index > 0x0FFFU) return NVX_GSP_NVKV_INVALID_ARGUMENT;
    s=reserve_words(w,1U,&start);
    if (s!=NVX_GSP_NVKV_OK) return s;
    store_le64(w->buffer+start*8U,instruction(NVKV_OPCODE_IMM32,index,key,value));
    return NVX_GSP_NVKV_OK;
}

nvx_gsp_nvkv_status_t nvx_gsp_nvkv_put_seq32(
    nvx_gsp_nvkv_writer_t *w, uint16_t index, uint16_t first_key,
    const uint32_t *values, size_t count)
{
    size_t payload_words, total, start;
    nvx_gsp_nvkv_status_t s;
    if (index>0x0FFFU || !values || count==0U || count>NVKV_MAX_COUNT ||
        count>(size_t)UINT16_MAX+1U-(size_t)first_key)
        return NVX_GSP_NVKV_INVALID_COUNT;
    payload_words=words_for_bytes(count*sizeof(uint32_t));
    if (!add_words(1U,payload_words,&total)) return NVX_GSP_NVKV_INVALID_COUNT;
    s=reserve_words(w,total,&start);
    if (s!=NVX_GSP_NVKV_OK) return s;
    store_le64(w->buffer+start*8U,instruction(NVKV_OPCODE_SEQ32,index,first_key,(uint32_t)count));
    for (size_t i=0U;i<count;i++)
        store_le32(w->buffer+start*8U+8U+i*4U,values[i]);
    return NVX_GSP_NVKV_OK;
}

nvx_gsp_nvkv_status_t nvx_gsp_nvkv_put_seq64(
    nvx_gsp_nvkv_writer_t *w, uint16_t index, uint16_t first_key,
    const uint64_t *values, size_t count)
{
    size_t total, start;
    nvx_gsp_nvkv_status_t s;
    if (index>0x0FFFU || !values || count==0U || count>NVKV_MAX_COUNT ||
        count>(size_t)UINT16_MAX+1U-(size_t)first_key ||
        !add_words(1U,count,&total))
        return NVX_GSP_NVKV_INVALID_COUNT;
    s=reserve_words(w,total,&start);
    if (s!=NVX_GSP_NVKV_OK) return s;
    store_le64(w->buffer+start*8U,instruction(NVKV_OPCODE_SEQ64,index,first_key,(uint32_t)count));
    for (size_t i=0U;i<count;i++)
        store_le64(w->buffer+(start+1U+i)*8U,values[i]);
    return NVX_GSP_NVKV_OK;
}

static nvx_gsp_nvkv_status_t put_array(
    nvx_gsp_nvkv_writer_t *w, uint16_t index, uint16_t key,
    const void *values, size_t count, uint32_t opcode, size_t element_size)
{
    size_t bytes, payload_words, total, start;
    nvx_gsp_nvkv_status_t s;
    if (index>0x0FFFU || (count && !values) || count>NVKV_MAX_COUNT ||
        (element_size && count>SIZE_MAX/element_size))
        return NVX_GSP_NVKV_INVALID_COUNT;
    bytes=count*element_size;
    payload_words=words_for_bytes(bytes);
    if (!add_words(1U,payload_words,&total)) return NVX_GSP_NVKV_INVALID_COUNT;
    s=reserve_words(w,total,&start);
    if (s!=NVX_GSP_NVKV_OK) return s;
    store_le64(w->buffer+start*8U,instruction(opcode,index,key,(uint32_t)count));
    if (bytes) {
        if (opcode==NVKV_OPCODE_ARRAY8) {
            memcpy(w->buffer+(start+1U)*8U,values,bytes);
        } else if (opcode==NVKV_OPCODE_ARRAY32) {
            const uint32_t *src=(const uint32_t *)values;
            for (size_t i=0U;i<count;i++)
                store_le32(w->buffer+(start+1U)*8U+i*4U,src[i]);
        } else {
            const uint64_t *src=(const uint64_t *)values;
            for (size_t i=0U;i<count;i++)
                store_le64(w->buffer+(start+1U+i)*8U,src[i]);
        }
    }
    return NVX_GSP_NVKV_OK;
}

nvx_gsp_nvkv_status_t nvx_gsp_nvkv_put_array8(
    nvx_gsp_nvkv_writer_t *w, uint16_t index, uint16_t key,
    const uint8_t *values, size_t count)
{
    return put_array(w,index,key,values,count,NVKV_OPCODE_ARRAY8,sizeof(uint8_t));
}

nvx_gsp_nvkv_status_t nvx_gsp_nvkv_put_array32(
    nvx_gsp_nvkv_writer_t *w, uint16_t index, uint16_t key,
    const uint32_t *values, size_t count)
{
    return put_array(w,index,key,values,count,NVKV_OPCODE_ARRAY32,sizeof(uint32_t));
}

nvx_gsp_nvkv_status_t nvx_gsp_nvkv_put_array64(
    nvx_gsp_nvkv_writer_t *w, uint16_t index, uint16_t key,
    const uint64_t *values, size_t count)
{
    return put_array(w,index,key,values,count,NVKV_OPCODE_ARRAY64,sizeof(uint64_t));
}

size_t nvx_gsp_nvkv_size_bytes(const nvx_gsp_nvkv_writer_t *w)
{
    return writer_valid(w) ? w->words_written*sizeof(uint64_t) : 0U;
}

nvx_gsp_nvkv_status_t nvx_gsp_nvkv_validate(const void *buffer, size_t size_bytes)
{
    const uint8_t *p=(const uint8_t *)buffer;
    size_t words,index=0U;
    if (!buffer || size_bytes==0U || (size_bytes%sizeof(uint64_t))!=0U)
        return NVX_GSP_NVKV_INVALID_ARGUMENT;
    words=size_bytes/sizeof(uint64_t);
    while (index<words) {
        uint64_t inst=load_le64(p+index*8U);
        uint32_t op=(uint32_t)((inst>>28U)&0xFU);
        uint32_t count=(uint32_t)(inst>>32U);
        size_t payload=0U;
        ++index;
        switch (op) {
        case NVKV_OPCODE_IMM32: continue;
        case NVKV_OPCODE_SEQ32:
        case NVKV_OPCODE_ARRAY32: payload=words_for_bytes((size_t)count*4U); break;
        case NVKV_OPCODE_SEQ64:
        case NVKV_OPCODE_ARRAY64: payload=(size_t)count; break;
        case NVKV_OPCODE_ARRAY8: payload=words_for_bytes((size_t)count); break;
        default: return NVX_GSP_NVKV_BAD_STREAM;
        }
        if (payload>words-index) return NVX_GSP_NVKV_BAD_STREAM;
        index+=payload;
    }
    return index==words ? NVX_GSP_NVKV_OK : NVX_GSP_NVKV_BAD_STREAM;
}
