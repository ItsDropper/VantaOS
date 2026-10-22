/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "vanta_installer_layout.h"

static int align_up(uint64_t value, uint64_t alignment, uint64_t *out)
{
    uint64_t remainder;
    uint64_t increment;

    if (!out || alignment == 0U)
        return 0;
    remainder = value % alignment;
    increment = remainder == 0U ? 0U : alignment - remainder;
    if (value > UINT64_MAX - increment)
        return 0;
    *out = value + increment;
    return 1;
}

vanta_installer_layout_status_t vanta_installer_plan_gpt_with_efi_size(
    uint64_t disk_sector_count,
    uint32_t logical_sector_size,
    uint32_t efi_size_mib,
    vanta_installer_layout_t *out_layout)
{
    vanta_installer_layout_t layout;
    uint64_t efi_end_exclusive;
    uint64_t efi_sector_count;

    if (!out_layout || disk_sector_count == 0U ||
        (efi_size_mib != 256U && efi_size_mib != 512U))
        return VANTA_INSTALLER_LAYOUT_INVALID_ARGUMENT;

    if (logical_sector_size != VANTA_INSTALLER_SECTOR_SIZE)
        return VANTA_INSTALLER_LAYOUT_UNSUPPORTED_SECTOR_SIZE;

    if (disk_sector_count < VANTA_INSTALLER_MIN_DISK_SECTORS)
        return VANTA_INSTALLER_LAYOUT_DISK_TOO_SMALL;

    if (disk_sector_count <= VANTA_INSTALLER_GPT_RESERVED_SECTORS)
        return VANTA_INSTALLER_LAYOUT_OVERFLOW;

    efi_sector_count = (uint64_t)efi_size_mib * 2048ULL;
    layout.disk_sector_count = disk_sector_count;
    layout.first_usable_lba = 2U + 32U;
    layout.last_usable_lba =
        disk_sector_count - VANTA_INSTALLER_GPT_RESERVED_SECTORS;

    if (!align_up(layout.first_usable_lba,
                  VANTA_INSTALLER_ALIGNMENT_SECTORS,
                  &layout.efi.first_lba))
        return VANTA_INSTALLER_LAYOUT_OVERFLOW;

    if (layout.efi.first_lba > UINT64_MAX - efi_sector_count)
        return VANTA_INSTALLER_LAYOUT_OVERFLOW;
    efi_end_exclusive =
        layout.efi.first_lba + efi_sector_count;
    layout.efi.sector_count = efi_sector_count;

    if (!align_up(efi_end_exclusive,
                  VANTA_INSTALLER_ALIGNMENT_SECTORS,
                  &layout.system.first_lba))
        return VANTA_INSTALLER_LAYOUT_OVERFLOW;

    if (layout.system.first_lba > layout.last_usable_lba)
        return VANTA_INSTALLER_LAYOUT_OUT_OF_BOUNDS;

    /* last_usable_lba is inclusive; reserve no sectors outside the GPT bounds. */
    layout.system.sector_count =
        layout.last_usable_lba - layout.system.first_lba + 1U;

    if (layout.efi.first_lba < layout.first_usable_lba ||
        layout.efi.first_lba > layout.last_usable_lba ||
        layout.efi.sector_count == 0U ||
        layout.efi.sector_count - 1U >
            layout.last_usable_lba - layout.efi.first_lba ||
        layout.efi.first_lba + layout.efi.sector_count > layout.system.first_lba ||
        layout.system.sector_count == 0U ||
        layout.system.sector_count - 1U >
            layout.last_usable_lba - layout.system.first_lba)
        return VANTA_INSTALLER_LAYOUT_OUT_OF_BOUNDS;

    *out_layout = layout;
    return VANTA_INSTALLER_LAYOUT_OK;
}


vanta_installer_layout_status_t vanta_installer_plan_gpt(
    uint64_t disk_sector_count,
    uint32_t logical_sector_size,
    vanta_installer_layout_t *out_layout)
{
    return vanta_installer_plan_gpt_with_efi_size(
        disk_sector_count, logical_sector_size, 512U, out_layout);
}
