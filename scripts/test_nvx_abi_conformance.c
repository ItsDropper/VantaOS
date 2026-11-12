/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

/*
 * NVX hardware-ABI conformance pins.
 *
 * Every constant below was taken from NVIDIA's public open-gpu-kernel-modules
 * headers (git e6a2e01, src/common/sdk/nvidia/inc) and from a compile of
 * NVIDIA's own struct definition, NOT from NVX source. The test exists because
 * these values cannot be validated by a synthetic-MMIO test: a wrong class ID,
 * control command, struct offset, or bitfield position only fails on real
 * hardware. Each check names the header that defines it.
 *
 * This is software evidence that NVX matches the published ABI. It does not
 * show that any hardware accepted the value.
 */
#include "nvx_gsp_ce.h"
#include "nvx_gsp_gpfifo.h"
#include "nvx_gsp_rmapi.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#define OFF(field) offsetof(nvx_gsp_channel_alloc_params_t, field)

static void test_class_and_command_ids(void)
{
    /* class/cl0000.h, cl0080.h, cl0070.h */
    assert(NVX_GSP_RM_CLASS_ROOT == UINT32_C(0x00));
    assert(NVX_GSP_RM_CLASS_DEVICE_0 == UINT32_C(0x80));
    assert(NVX_GSP_RM_CLASS_MEMORY_VIRTUAL == UINT32_C(0x70));
    /* class/clc46f.h, clc56f.h */
    assert(NVX_GSP_RM_CLASS_TURING_CHANNEL_GPFIFO_A == UINT32_C(0xC46F));
    assert(NVX_GSP_RM_CLASS_AMPERE_CHANNEL_GPFIFO_A == UINT32_C(0xC56F));
    /* class/cl2080_notification.h: NV2080_ENGINE_TYPE_COPY0 */
    assert(NVX_GSP_RM_ENGINE_TYPE_COPY0 == UINT32_C(0x09));
    /* ctrl/ctrla06f/ctrla06fgpfifo.h: NVA06F_CTRL_CMD_GPFIFO_SCHEDULE / _BIND */
    assert(NVX_GSP_RM_CTRL_GPFIFO_SCHEDULE == UINT32_C(0xA06F0103));
    assert(NVX_GSP_RM_CTRL_BIND_CHANNEL == UINT32_C(0xA06F0104));
    /* class/clc5b5.h, clc6b5.h, clc7b5.h. src/nvidia/generated/g_gpu_class_list.c
     * lists AMPERE_DMA_COPY_B for ENG_CE(0..4) on GA106 (RTX 3060). */
    assert(NVX_GSP_CE_TURING_DMA_COPY_A_CLASS == UINT32_C(0xC5B5));
    assert(NVX_GSP_CE_AMPERE_DMA_COPY_A_CLASS == UINT32_C(0xC6B5));
    assert(NVX_GSP_CE_AMPERE_DMA_COPY_B_CLASS == UINT32_C(0xC7B5));
    assert(NVX_GSP_CE_GA10X_COPY_CLASS == UINT32_C(0xC7B5));
    /* class/clc5b5.h NVC5B5_LAUNCH_DMA: DATA_TRANSFER_TYPE(1:0)=NON_PIPELINED(2),
     * FLUSH_ENABLE(2)=1, SEMAPHORE_TYPE(4:3)=RELEASE_ONE_WORD(1),
     * SRC_MEMORY_LAYOUT(7)=PITCH, DST_MEMORY_LAYOUT(8)=PITCH. */
    assert(NVX_GSP_CE_LAUNCH_DMA_FLAGS ==
           ((UINT32_C(2) << 0) | (UINT32_C(1) << 2) | (UINT32_C(1) << 3) |
            (UINT32_C(1) << 7) | (UINT32_C(1) << 8)));
}

static void test_channel_alloc_layout(void)
{
    /* alloc/alloc_channel.h NV_CHANNEL_ALLOC_PARAMS compiled for x86-64 with
     * NV_MAX_SUBDEVICES = 8, CC_CHAN_ALLOC_IV_SIZE_DWORD = 3 and
     * CC_CHAN_ALLOC_NONCE_SIZE_DWORD = 8. */
    assert(sizeof(nvx_gsp_channel_alloc_params_t) == 376U);
    assert(OFF(h_object_error) == 0U);
    assert(OFF(h_object_buffer) == 4U);
    assert(OFF(gpfifo_offset) == 8U);
    assert(OFF(gpfifo_entries) == 16U);
    assert(OFF(flags) == 20U);
    assert(OFF(h_context_share) == 24U);
    assert(OFF(h_vaspace) == 28U);
    assert(OFF(h_handle_vaspace) == 32U);
    assert(OFF(h_userd_memory) == 36U);
    assert(OFF(userd_offset) == 72U);
    assert(OFF(engine_type) == 136U);
    assert(OFF(cid) == 140U);
    assert(OFF(subdevice_id) == 144U);
    assert(OFF(h_object_ecc_error) == 148U);
    assert(OFF(instance_mem) == 152U);
    assert(OFF(userd_mem) == 176U);
    assert(OFF(ramfc_mem) == 200U);
    assert(OFF(mthdbuf_mem) == 224U);
    assert(OFF(h_phys_channel_group) == 248U);
    assert(OFF(internal_flags) == 252U);
    assert(OFF(error_notifier_mem) == 256U);
    assert(OFF(ecc_error_notifier_mem) == 280U);
    assert(OFF(process_id) == 304U);
    assert(OFF(sub_process_id) == 308U);
    assert(OFF(encrypt_iv) == 312U);
    assert(OFF(decrypt_iv) == 324U);
    assert(OFF(hmac_nonce) == 336U);
    assert(OFF(tpc_config_id) == 368U);
    /* NV_MEMORY_DESC_PARAMS: base(8) size(8) addressSpace(4) cacheAttrib(4). */
    assert(sizeof(nvx_gsp_memory_desc_params_t) == 24U);
}

static void test_ce_method_encoding(void)
{
    volatile uint32_t pb[NVX_GSP_CE_COPY_COMMAND_DWORDS] = {0};
    size_t bytes = 0U;

    assert(nvx_gsp_ce_build_copy(pb, sizeof(pb), NVX_GSP_CE_GA10X_COPY_CLASS,
                                 0U, UINT64_C(0x10000), UINT64_C(0x20000),
                                 UINT64_C(0x30000), 0x100U, 0xAB, &bytes) ==
           NVX_GSP_CE_OK);
    /* clc56f.h NVC56F_DMA_*: SEC_OP(31:29)=INC_METHOD(1), COUNT(28:16),
     * SUBCHANNEL(15:13), ADDRESS(11:0)=method>>2. */
    assert(pb[0] == UINT32_C(0x20010000));   /* SET_OBJECT (0x000), 1 word */
    assert(pb[1] == UINT32_C(0x0000C7B5));   /* NVCLASS 15:0 = class, ENGINE = 0 */
    assert(pb[2] == UINT32_C(0x20030090));   /* SET_SEMAPHORE_A (0x240), 3 words */
    assert(pb[6] == UINT32_C(0x20040100));   /* OFFSET_IN_UPPER (0x400), 4 words */
    assert(pb[11] == UINT32_C(0x20020106));  /* LINE_LENGTH_IN (0x418), 2 words */
    assert(pb[14] == UINT32_C(0x200100C0));  /* LAUNCH_DMA (0x300), 1 word */
    assert(pb[15] == UINT32_C(0x0000018E));
}

static void test_gpfifo_entry_encoding(void)
{
    /* clc56f.h: GP_ENTRY0.GET (31:2) holds VA[31:2] in place; GP_ENTRY1.GET_HI
     * (7:0) = VA[39:32]; LEVEL (9) = MAIN; LENGTH (30:10) = dwords; SYNC (31)=0.
     * Vectors computed with NVIDIA's DRF-style field macros. */
    struct { uint64_t va; uint32_t bytes; uint32_t e0; uint32_t e1; } v[] = {
        { UINT64_C(0x0000000004),   4U, UINT32_C(0x00000004), UINT32_C(0x00000400) },
        { UINT64_C(0x1234567800),  64U, UINT32_C(0x34567800), UINT32_C(0x00004012) },
        { UINT64_C(0xCD56789ABC), 256U, UINT32_C(0x56789ABC), UINT32_C(0x000100CD) },
        { UINT64_C(0xFFFFFFFFF8),  8U,  UINT32_C(0xFFFFFFF8), UINT32_C(0x000008FF) },
    };

    for (size_t i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
        volatile uint32_t entries[8] = {0};
        volatile uint32_t get = 0U, put = 0U, bell = 0U;
        nvx_gsp_gpfifo_channel_t ch = {0};
        uint32_t idx = 0U;

        assert(nvx_gsp_gpfifo_channel_init(&ch, entries, sizeof(entries), &get,
                                           &put, &bell, 1U, 4U) ==
               NVX_GSP_GPFIFO_OK);
        assert(nvx_gsp_gpfifo_submit(&ch, v[i].va, v[i].bytes, &idx) ==
               NVX_GSP_GPFIFO_OK);
        assert(entries[0] == v[i].e0);
        assert(entries[1] == v[i].e1);
    }
}

int main(void)
{
    test_class_and_command_ids();
    test_channel_alloc_layout();
    test_ce_method_encoding();
    test_gpfifo_entry_encoding();
    return 0;
}
