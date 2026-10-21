/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "vanta_installer_layout.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_minimum_disk_layout(void)
{
    vanta_installer_layout_t layout;
    assert(vanta_installer_plan_gpt(
        VANTA_INSTALLER_MIN_DISK_SECTORS,
        VANTA_INSTALLER_SECTOR_SIZE, &layout) == VANTA_INSTALLER_LAYOUT_OK);
    assert(layout.disk_sector_count == VANTA_INSTALLER_MIN_DISK_SECTORS);
    assert(layout.first_usable_lba == 34U);
    assert(layout.last_usable_lba ==
           VANTA_INSTALLER_MIN_DISK_SECTORS -
               VANTA_INSTALLER_GPT_RESERVED_SECTORS);
    assert(layout.efi.first_lba == 2048U);
    assert(layout.efi.sector_count == VANTA_INSTALLER_ESP_SECTORS);
    assert(layout.efi.first_lba % VANTA_INSTALLER_ALIGNMENT_SECTORS == 0U);
    assert(layout.system.first_lba % VANTA_INSTALLER_ALIGNMENT_SECTORS == 0U);
    assert(layout.efi.first_lba + layout.efi.sector_count <=
           layout.system.first_lba);
    assert(layout.system.first_lba > layout.efi.first_lba);
    assert(layout.system.sector_count > 0U);
    assert(layout.system.first_lba + layout.system.sector_count - 1U ==
           layout.last_usable_lba);
}

static void test_larger_disk_and_no_overlap(void)
{
    vanta_installer_layout_t layout;
    const uint64_t sectors = 64ULL * 1024ULL * 1024ULL;
    assert(vanta_installer_plan_gpt(
        sectors, VANTA_INSTALLER_SECTOR_SIZE, &layout) == VANTA_INSTALLER_LAYOUT_OK);
    assert(layout.disk_sector_count == sectors);
    assert(layout.efi.first_lba + layout.efi.sector_count <=
           layout.system.first_lba);
    assert(layout.system.first_lba + layout.system.sector_count <= sectors);
    assert(layout.system.sector_count > VANTA_INSTALLER_ESP_SECTORS);
}

static void test_rejects_small_disk(void)
{
    vanta_installer_layout_t layout;
    assert(vanta_installer_plan_gpt(
        VANTA_INSTALLER_MIN_DISK_SECTORS - 1U,
        VANTA_INSTALLER_SECTOR_SIZE, &layout) ==
        VANTA_INSTALLER_LAYOUT_DISK_TOO_SMALL);
}

static void test_rejects_unsupported_sector_sizes(void)
{
    vanta_installer_layout_t layout;
    assert(vanta_installer_plan_gpt(
        VANTA_INSTALLER_MIN_DISK_SECTORS, 4096U, &layout) ==
        VANTA_INSTALLER_LAYOUT_UNSUPPORTED_SECTOR_SIZE);
    assert(vanta_installer_plan_gpt(
        VANTA_INSTALLER_MIN_DISK_SECTORS, 0U, &layout) ==
        VANTA_INSTALLER_LAYOUT_UNSUPPORTED_SECTOR_SIZE);
}

static void test_rejects_invalid_arguments(void)
{
    vanta_installer_layout_t layout;
    assert(vanta_installer_plan_gpt(
        0U, VANTA_INSTALLER_SECTOR_SIZE, &layout) ==
        VANTA_INSTALLER_LAYOUT_INVALID_ARGUMENT);
    assert(vanta_installer_plan_gpt(
        VANTA_INSTALLER_MIN_DISK_SECTORS,
        VANTA_INSTALLER_SECTOR_SIZE, NULL) ==
        VANTA_INSTALLER_LAYOUT_INVALID_ARGUMENT);
}

static void test_large_capacity_arithmetic(void)
{
    vanta_installer_layout_t layout;
    assert(vanta_installer_plan_gpt(
        UINT64_MAX, VANTA_INSTALLER_SECTOR_SIZE, &layout) ==
        VANTA_INSTALLER_LAYOUT_OK);
    assert(layout.system.first_lba + layout.system.sector_count - 1U ==
           layout.last_usable_lba);
}

int main(void)
{
    test_minimum_disk_layout();
    test_larger_disk_and_no_overlap();
    test_rejects_small_disk();
    test_rejects_unsupported_sector_sizes();
    test_rejects_invalid_arguments();
    test_large_capacity_arithmetic();
    puts("VANTAOS_INSTALLER_LAYOUT_OK");
    return 0;
}
