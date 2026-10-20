/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_fwsec_interface.h"
#include "nvx_gsp_fwsec_stage.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DMEM_SIZE 512U
#define IFACE_OFFSET 16U
#define MAPPER_OFFSET 128U
#define INPUT_OFFSET 300U

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8U);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8U);
    p[2] = (uint8_t)(v >> 16U);
    p[3] = (uint8_t)(v >> 24U);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static void make_dmem(uint8_t dmem[DMEM_SIZE])
{
    uint8_t *iface;
    uint8_t *entry;
    uint8_t *mapper;

    memset(dmem, 0, DMEM_SIZE);
    iface = dmem + IFACE_OFFSET;
    iface[0] = 1U;
    iface[1] = 4U;
    iface[2] = 8U;
    iface[3] = 2U;

    entry = iface + 4U;
    put32(entry, 1U); /* An unrelated interface entry. */
    put32(entry + 4U, 200U);
    entry += 8U;
    put32(entry, 4U); /* DMEM mapper entry ID. */
    put32(entry + 4U, MAPPER_OFFSET);

    mapper = dmem + MAPPER_OFFSET;
    put32(mapper, 0x33504D44U); /* Opaque signature: preserved, not trusted. */
    put16(mapper + 4U, 3U);
    put16(mapper + 6U, 64U);
    put32(mapper + 8U, INPUT_OFFSET);
    put32(mapper + 12U, 64U);
}

int main(void)
{
    uint8_t dmem[DMEM_SIZE];
    uint8_t original[DMEM_SIZE];
    nvx_gsp_fwsec_command_window_t window;
    nvx_gsp_fwsec_interface_status_t status;

    make_dmem(dmem);
    memcpy(original, dmem, sizeof(dmem));
    memset(&window, 0xA5, sizeof(window));
    status = nvx_gsp_fwsec_find_command_window(
        dmem, sizeof(dmem), IFACE_OFFSET, 0x15U, 32U, &window);
    assert(status == NVX_GSP_FWSEC_INTERFACE_OK);
    assert(window.mapper_offset == MAPPER_OFFSET);
    assert(window.mapper_size == 64U);
    assert(window.command_input_offset == INPUT_OFFSET);
    assert(window.command_input_capacity == 64U);
    assert(window.command_id == 0x15U);
    assert(memcmp(original, dmem, sizeof(dmem)) == 0); /* Read-only planner. */

    assert(nvx_gsp_fwsec_find_command_window(
        dmem, sizeof(dmem), IFACE_OFFSET, 0x15U, 65U, &window) ==
        NVX_GSP_FWSEC_INTERFACE_COMMAND_TOO_LARGE);
    assert(window.command_input_offset == 0U);

    make_dmem(dmem);
    dmem[IFACE_OFFSET + 2U] = 7U;
    assert(nvx_gsp_fwsec_find_command_window(
        dmem, sizeof(dmem), IFACE_OFFSET, 0x15U, 1U, &window) ==
        NVX_GSP_FWSEC_INTERFACE_BAD_HEADER);

    make_dmem(dmem);
    put32(dmem + IFACE_OFFSET + 4U + 8U + 4U, DMEM_SIZE - 16U);
    assert(nvx_gsp_fwsec_find_command_window(
        dmem, sizeof(dmem), IFACE_OFFSET, 0x15U, 1U, &window) ==
        NVX_GSP_FWSEC_INTERFACE_RANGE_ERROR);

    make_dmem(dmem);
    put32(dmem + IFACE_OFFSET + 4U, 4U);
    put32(dmem + IFACE_OFFSET + 4U + 4U, 200U);
    assert(nvx_gsp_fwsec_find_command_window(
        dmem, sizeof(dmem), IFACE_OFFSET, 0x15U, 1U, &window) ==
        NVX_GSP_FWSEC_INTERFACE_AMBIGUOUS);

    make_dmem(dmem);
    put32(dmem + MAPPER_OFFSET + 8U, UINT32_MAX - 8U);
    assert(nvx_gsp_fwsec_find_command_window(
        dmem, sizeof(dmem), IFACE_OFFSET, 0x15U, 1U, &window) ==
        NVX_GSP_FWSEC_INTERFACE_RANGE_ERROR);

    make_dmem(dmem);
    put32(dmem + MAPPER_OFFSET + 8U, MAPPER_OFFSET + 8U);
    assert(nvx_gsp_fwsec_find_command_window(
        dmem, sizeof(dmem), IFACE_OFFSET, 0x15U, 1U, &window) ==
        NVX_GSP_FWSEC_INTERFACE_BAD_MAPPER);

    assert(nvx_gsp_fwsec_find_command_window(
        NULL, sizeof(dmem), IFACE_OFFSET, 0x15U, 1U, &window) ==
        NVX_GSP_FWSEC_INTERFACE_INVALID_ARGUMENT);
    assert(nvx_gsp_fwsec_find_command_window(
        dmem, sizeof(dmem), IFACE_OFFSET, 0x15U, 1U,
        (nvx_gsp_fwsec_command_window_t *)(void *)(dmem + 8U)) ==
        NVX_GSP_FWSEC_INTERFACE_OVERLAPPING_OUTPUT);

    /* Staging publishes init_cmd only after the complete input is copied. */
    make_dmem(dmem);
    assert(nvx_gsp_fwsec_stage_command(
        dmem, sizeof(dmem), IFACE_OFFSET, NVX_GSP_FWSEC_CMD_FRTS,
        0x30000000ULL, &window) == NVX_GSP_FWSEC_STAGE_OK);
    assert(window.init_cmd_offset == MAPPER_OFFSET + 44U);
    assert(get32(dmem + window.init_cmd_offset) == NVX_GSP_FWSEC_CMD_FRTS);
    assert(get32(dmem + INPUT_OFFSET) == 1U);
    assert(get32(dmem + INPUT_OFFSET + 4U) == 24U);
    assert(get32(dmem + INPUT_OFFSET + 20U) == NVX_GSP_FWSEC_READ_VBIOS_FLAGS);
    assert(get32(dmem + INPUT_OFFSET + 32U) == 0x30000U);
    assert(get32(dmem + INPUT_OFFSET + 36U) == NVX_GSP_FWSEC_FRTS_SIZE_1MB_IN_4K);
    assert(get32(dmem + INPUT_OFFSET + 40U) == NVX_GSP_FWSEC_FRTS_MEDIA_FB);

    make_dmem(dmem);
    assert(nvx_gsp_fwsec_stage_command(
        dmem, sizeof(dmem), IFACE_OFFSET, NVX_GSP_FWSEC_CMD_SB,
        0U, &window) == NVX_GSP_FWSEC_STAGE_OK);
    assert(get32(dmem + window.init_cmd_offset) == NVX_GSP_FWSEC_CMD_SB);
    assert(get32(dmem + INPUT_OFFSET + 4U) == NVX_GSP_FWSEC_READ_VBIOS_DESC_SIZE);

    make_dmem(dmem);
    memcpy(original, dmem, sizeof(dmem));
    assert(nvx_gsp_fwsec_stage_command(
        dmem, sizeof(dmem), IFACE_OFFSET, NVX_GSP_FWSEC_CMD_FRTS,
        0x30000001ULL, &window) == NVX_GSP_FWSEC_STAGE_COMMAND_BUILD_FAILED);
    assert(memcmp(original, dmem, sizeof(dmem)) == 0);

    make_dmem(dmem);
    put32(dmem + MAPPER_OFFSET + 8U, MAPPER_OFFSET + 8U);
    memcpy(original, dmem, sizeof(dmem));
    assert(nvx_gsp_fwsec_stage_command(
        dmem, sizeof(dmem), IFACE_OFFSET, NVX_GSP_FWSEC_CMD_FRTS,
        0x30000000ULL, &window) == NVX_GSP_FWSEC_STAGE_INTERFACE_FAILED);
    assert(memcmp(original, dmem, sizeof(dmem)) == 0);

    make_dmem(dmem);
    memcpy(original, dmem, sizeof(dmem));
    assert(nvx_gsp_fwsec_stage_command(
        dmem, sizeof(dmem), IFACE_OFFSET, NVX_GSP_FWSEC_CMD_FRTS,
        0x30000000ULL,
        (nvx_gsp_fwsec_command_window_t *)(void *)(dmem + 8U)) ==
        NVX_GSP_FWSEC_STAGE_OUTPUT_OVERLAP);
    assert(memcmp(original, dmem, sizeof(dmem)) == 0);

    puts("NVX FWSEC interface parser tests passed");
    return 0;
}
