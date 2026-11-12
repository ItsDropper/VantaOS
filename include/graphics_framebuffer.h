/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_GRAPHICS_FRAMEBUFFER_H
#define VANTA_GRAPHICS_FRAMEBUFFER_H

#include <stdint.h>

#define GRAPHICS_FRAMEBUFFER_PAGE_SIZE 4096U
#define GRAPHICS_FRAMEBUFFER_PHYSICAL_LIMIT 0x0010000000000000ULL

typedef struct
{
    uint64_t page_base;
    uint64_t byte_length;
    uint32_t page_offset;
    uint32_t page_count;
} graphics_framebuffer_layout_t;

/*
 * Validate a packed 32-bit RGB framebuffer and compute its page mapping.
 * max_span includes the first page offset, so the resulting mapping cannot
 * overlap the next virtual allocation. The output is committed only on
 * success; malformed firmware values leave it untouched.
 */
static inline int graphics_framebuffer_layout_compute(
    uint64_t address,uint32_t pitch,uint32_t width,uint32_t height,
    uint8_t bits_per_pixel,uint8_t framebuffer_type,
    uint64_t max_span,uint32_t max_pages,
    graphics_framebuffer_layout_t *out_layout)
{
    if(!out_layout||address==0U||pitch==0U||width==0U||height==0U||
       bits_per_pixel!=32U||framebuffer_type!=1U||max_span==0U||
       max_pages==0U)
        return 0;

    uint64_t minimum_pitch=(uint64_t)width*4U;
    if(minimum_pitch>UINT32_MAX||(uint64_t)pitch<minimum_pitch||
       (pitch&3U)!=0U)
        return 0;

    if((uint64_t)height>UINT64_MAX/(uint64_t)pitch)
        return 0;
    uint64_t bytes=(uint64_t)pitch*(uint64_t)height;
    if(bytes==0U||address>=GRAPHICS_FRAMEBUFFER_PHYSICAL_LIMIT||
       bytes>GRAPHICS_FRAMEBUFFER_PHYSICAL_LIMIT-address)
        return 0;

    uint32_t offset=(uint32_t)(address&(GRAPHICS_FRAMEBUFFER_PAGE_SIZE-1U));
    if(bytes>max_span||(uint64_t)offset>max_span-bytes)
        return 0;
    uint64_t span=(uint64_t)offset+bytes;
    if(span>UINT64_MAX-(GRAPHICS_FRAMEBUFFER_PAGE_SIZE-1U))
        return 0;
    uint64_t pages=(span+GRAPHICS_FRAMEBUFFER_PAGE_SIZE-1U)/
                   GRAPHICS_FRAMEBUFFER_PAGE_SIZE;
    if(pages==0U||pages>max_pages||pages>UINT32_MAX)
        return 0;

    graphics_framebuffer_layout_t candidate={
        .page_base=address-offset,
        .byte_length=bytes,
        .page_offset=offset,
        .page_count=(uint32_t)pages
    };
    *out_layout=candidate;
    return 1;
}

#endif
