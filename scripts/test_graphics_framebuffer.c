/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "graphics_framebuffer.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#define TEST_SPAN (16ULL*1024ULL*1024ULL)
static void test_full_hd_high_address(void)
{
    graphics_framebuffer_layout_t layout={0};
    assert(graphics_framebuffer_layout_compute(
        0x0000001234500000ULL,7680U,1920U,1080U,32U,1U,
        TEST_SPAN,4096U,&layout));
    assert(layout.page_base==0x0000001234500000ULL);
    assert(layout.page_offset==0U);
    assert(layout.byte_length==8294400ULL);
    assert(layout.page_count==2025U);
}
static void test_unaligned_scanout(void)
{
    graphics_framebuffer_layout_t layout={0};
    assert(graphics_framebuffer_layout_compute(
        0x0000000123450123ULL,3200U,800U,600U,32U,1U,
        TEST_SPAN,4096U,&layout));
    assert(layout.page_base==0x0000000123450000ULL);
    assert(layout.page_offset==0x123U);
    assert(layout.byte_length==1920000ULL);
    assert(layout.page_count==
           (uint32_t)((0x123ULL+1920000ULL+4095ULL)/4096ULL));
}
static void test_physical_limit(void)
{
    graphics_framebuffer_layout_t layout={0};
    const uint64_t limit=GRAPHICS_FRAMEBUFFER_PHYSICAL_LIMIT;
    assert(graphics_framebuffer_layout_compute(
        limit-4096ULL,4U,1U,1U,32U,1U,TEST_SPAN,4096U,&layout));
    assert(layout.page_base==limit-4096ULL);
    assert(!graphics_framebuffer_layout_compute(
        limit-3ULL,4U,1U,1U,32U,1U,TEST_SPAN,4096U,&layout));
    assert(!graphics_framebuffer_layout_compute(
        limit,4U,1U,1U,32U,1U,TEST_SPAN,4096U,&layout));
}
static void test_invalid_layouts_preserve_output(void)
{
    graphics_framebuffer_layout_t layout={
        .page_base=0x1122334455667788ULL,
        .byte_length=0x8877665544332211ULL,
        .page_offset=0xA5A5U,
        .page_count=0x5A5AU
    };
    const graphics_framebuffer_layout_t expected=layout;
#define REJECT(address,pitch,width,height,bpp,type,span,pages) \
    do { \
        assert(!graphics_framebuffer_layout_compute( \
            (address),(pitch),(width),(height),(bpp),(type), \
            (span),(pages),&layout)); \
        assert(layout.page_base==expected.page_base); \
        assert(layout.byte_length==expected.byte_length); \
        assert(layout.page_offset==expected.page_offset); \
        assert(layout.page_count==expected.page_count); \
    } while(0)
    REJECT(0U,3200U,800U,600U,32U,1U,TEST_SPAN,4096U);
    REJECT(0x100000U,3199U,800U,600U,32U,1U,TEST_SPAN,4096U);
    REJECT(0x100000U,3200U,800U,600U,24U,1U,TEST_SPAN,4096U);
    REJECT(0x100000U,3200U,800U,600U,32U,2U,TEST_SPAN,4096U);
    REJECT(0x100000U,3200U,800U,600U,32U,1U,1024U,4096U);
    REJECT(0x100000U,3200U,800U,600U,32U,1U,TEST_SPAN,1U);
    REJECT(0x100000U,UINT32_MAX-3U,UINT32_MAX,1U,32U,1U,TEST_SPAN,4096U);
    REJECT(0x100000U,0U,800U,600U,32U,1U,TEST_SPAN,4096U);
    assert(!graphics_framebuffer_layout_compute(
        0x100000U,3200U,800U,600U,32U,1U,TEST_SPAN,4096U,NULL));
#undef REJECT
}
int main(void)
{
    test_full_hd_high_address();
    test_unaligned_scanout();
    test_physical_limit();
    test_invalid_layouts_preserve_output();
    puts("Graphics framebuffer layout tests passed");
    return 0;
}
