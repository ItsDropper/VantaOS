/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VANTA_INSTALLER_GPT_H
#define VANTA_INSTALLER_GPT_H

#include <stddef.h>
#include <stdint.h>

#include "vanta_installer_layout.h"

#define VANTA_INSTALLER_GPT_ENTRY_COUNT 128U
#define VANTA_INSTALLER_GPT_ENTRY_SIZE 128U
#define VANTA_INSTALLER_GPT_ARRAY_SECTORS 32U
#define VANTA_INSTALLER_GPT_WORKSPACE_SIZE 16384U

typedef int (*vanta_installer_sector_read_fn)(
    void *context, uint64_t lba, uint32_t count, void *buffer);
typedef int (*vanta_installer_sector_write_fn)(
    void *context, uint64_t lba, uint32_t count, const void *buffer);
typedef int (*vanta_installer_sector_flush_fn)(void *context);

typedef struct {
    vanta_installer_sector_read_fn read;
    vanta_installer_sector_write_fn write;
    vanta_installer_sector_flush_fn flush;
    void *context;
} vanta_installer_disk_io_t;

typedef enum {
    VANTA_INSTALLER_GPT_OK = 0,
    VANTA_INSTALLER_GPT_INVALID_ARGUMENT,
    VANTA_INSTALLER_GPT_INVALID_LAYOUT,
    VANTA_INSTALLER_GPT_INVALID_GUID,
    VANTA_INSTALLER_GPT_IO_ERROR,
    VANTA_INSTALLER_GPT_VERIFY_ERROR
} vanta_installer_gpt_status_t;

/*
 * Writes a protective MBR and primary/backup GPT metadata, then reads back
 * and verifies the metadata. The caller must have already obtained explicit
 * user confirmation for a disposable/selected target disk. This function
 * does not format partitions or install files.
 *
 * workspace must point to at least VANTA_INSTALLER_GPT_WORKSPACE_SIZE bytes.
 * GUID inputs are raw 16-byte GPT on-disk encodings and must be nonzero and
 * pairwise distinct. The caller owns GUID generation and disk authorization.
 */
vanta_installer_gpt_status_t vanta_installer_write_gpt(
    const vanta_installer_disk_io_t *io,
    const vanta_installer_layout_t *layout,
    const uint8_t disk_guid[16],
    const uint8_t efi_partition_guid[16],
    const uint8_t system_partition_guid[16],
    void *workspace,
    size_t workspace_size);

#endif
