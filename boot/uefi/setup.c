/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include <efi.h>
#include <efilib.h>
#include <stddef.h>
#include <stdint.h>

#include "vanta_installer_fat32.h"
#include "vanta_installer_gpt.h"
#include "vanta_setup_config.h"

#define SETUP_MAX_DISKS 32U
#define SETUP_MAX_FILE_BYTES (64U * 1024U * 1024U)
#define SETUP_MIN_DISK_SECTORS VANTA_INSTALLER_MIN_DISK_SECTORS

typedef struct {
    EFI_HANDLE handle;
    EFI_BLOCK_IO_PROTOCOL *block;
    uint64_t sector_count;
} setup_disk_t;

#define block_io_guid BlockIoProtocol
#define filesystem_guid FileSystemProtocol

static void port_out8(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint8_t port_in8(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_initialize(void)
{
    port_out8(0x3F9U, 0x00U);
    port_out8(0x3FBU, 0x80U);
    port_out8(0x3F8U, 0x01U);
    port_out8(0x3F9U, 0x00U);
    port_out8(0x3FBU, 0x03U);
    port_out8(0x3FAU, 0xC7U);
    port_out8(0x3FCU, 0x0BU);
}

static void serial_write(const char *text)
{
    while (*text) {
        while ((port_in8(0x3FDU) & 0x20U) == 0U)
            ;
        port_out8(0x3F8U, (uint8_t)*text++);
    }
}

static int bytes_equal(const uint8_t *a, const uint8_t *b, size_t length)
{
    for (size_t i = 0U; i < length; ++i)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static int device_paths_share_disk(
    EFI_DEVICE_PATH_PROTOCOL *disk_path,
    EFI_DEVICE_PATH_PROTOCOL *partition_path)
{
    if (!disk_path || !partition_path)
        return 0;
    while (!IsDevicePathEnd(partition_path)) {
        if (DevicePathType(partition_path) == MEDIA_DEVICE_PATH &&
            DevicePathSubType(partition_path) == MEDIA_HARDDRIVE_DP)
            return 1;
        if (IsDevicePathEnd(disk_path))
            return 0;
        UINTN disk_length = DevicePathNodeLength(disk_path);
        UINTN partition_length = DevicePathNodeLength(partition_path);
        if (disk_length != partition_length ||
            !bytes_equal((const uint8_t *)disk_path,
                         (const uint8_t *)partition_path,
                         (size_t)disk_length))
            return 0;
        disk_path = NextDevicePathNode(disk_path);
        partition_path = NextDevicePathNode(partition_path);
    }
    return IsDevicePathEnd(disk_path);
}

static int same_disk_as_source(EFI_HANDLE candidate, EFI_HANDLE source_device)
{
    EFI_DEVICE_PATH_PROTOCOL *candidate_path = DevicePathFromHandle(candidate);
    EFI_DEVICE_PATH_PROTOCOL *source_path = DevicePathFromHandle(source_device);
    return device_paths_share_disk(candidate_path, source_path);
}

static int read_line(char *output, size_t capacity)
{
    size_t length = 0U;
    if (!output || capacity < 2U)
        return 0;
    for (;;) {
        EFI_INPUT_KEY key;
        EFI_STATUS status = uefi_call_wrapper(
            ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &key);
        if (EFI_ERROR(status))
            continue;
        if (key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
            output[length] = '\0';
            Print(L"\r\n");
            return 1;
        }
        if (key.UnicodeChar == CHAR_BACKSPACE) {
            if (length > 0U) {
                --length;
                output[length] = '\0';
                Print(L"\b \b");
            }
            continue;
        }
        if (key.UnicodeChar >= 32U && key.UnicodeChar <= 126U &&
            length + 1U < capacity) {
            output[length++] = (char)key.UnicodeChar;
            output[length] = '\0';
            Print(L"%c", key.UnicodeChar);
        }
    }
}

static int parse_number(const char *text, uint32_t *value)
{
    uint32_t result = 0U;
    size_t i = 0U;
    if (!text || !value || text[0] == '\0')
        return 0;
    while (text[i]) {
        if (text[i] < '0' || text[i] > '9')
            return 0;
        uint32_t digit = (uint32_t)(text[i] - '0');
        if (result > (UINT32_MAX - digit) / 10U)
            return 0;
        result = result * 10U + digit;
        ++i;
    }
    *value = result;
    return 1;
}

static void print_localized(const vanta_setup_config_t *config,
                            const CHAR16 *english,
                            const CHAR16 *finnish)
{
    const CHAR16 *message =
        config && config->language[0] == 'f' ? finnish : english;
    Print(L"%s", message);
}

static int disk_read(void *context, uint64_t lba, uint32_t count, void *buffer)
{
    EFI_BLOCK_IO_PROTOCOL *block = (EFI_BLOCK_IO_PROTOCOL *)context;
    if (!block || !block->Media || !buffer || count == 0U ||
        block->Media->BlockSize != VANTA_INSTALLER_SECTOR_SIZE ||
        lba > block->Media->LastBlock ||
        (uint64_t)count - 1U > block->Media->LastBlock - lba)
        return 0;
    return !EFI_ERROR(uefi_call_wrapper(
        block->ReadBlocks, 5, block, block->Media->MediaId, lba,
        (UINTN)count * VANTA_INSTALLER_SECTOR_SIZE, buffer));
}

static int disk_write(void *context, uint64_t lba, uint32_t count,
                      const void *buffer)
{
    EFI_BLOCK_IO_PROTOCOL *block = (EFI_BLOCK_IO_PROTOCOL *)context;
    if (!block || !block->Media || !buffer || count == 0U ||
        block->Media->ReadOnly ||
        block->Media->BlockSize != VANTA_INSTALLER_SECTOR_SIZE ||
        lba > block->Media->LastBlock ||
        (uint64_t)count - 1U > block->Media->LastBlock - lba)
        return 0;
    return !EFI_ERROR(uefi_call_wrapper(
        block->WriteBlocks, 5, block, block->Media->MediaId, lba,
        (UINTN)count * VANTA_INSTALLER_SECTOR_SIZE, (void *)buffer));
}

static int disk_flush(void *context)
{
    EFI_BLOCK_IO_PROTOCOL *block = (EFI_BLOCK_IO_PROTOCOL *)context;
    if (!block)
        return 0;
    if (!block->FlushBlocks)
        return 1;
    return !EFI_ERROR(uefi_call_wrapper(block->FlushBlocks, 1, block));
}

static int enumerate_disks(
    EFI_HANDLE source_device,
    setup_disk_t disks[SETUP_MAX_DISKS],
    UINTN *out_count,
    int *source_seen)
{
    EFI_HANDLE *handles = NULL;
    UINTN handle_count = 0U;
    UINTN found = 0U;
    EFI_STATUS status = uefi_call_wrapper(
        BS->LocateHandleBuffer, 5, ByProtocol, &block_io_guid,
        NULL, &handle_count, &handles);
    if (EFI_ERROR(status) || !handles)
        return 0;

    *source_seen = 0;
    for (UINTN i = 0U; i < handle_count && found < SETUP_MAX_DISKS; ++i) {
        EFI_BLOCK_IO_PROTOCOL *block = NULL;
        status = uefi_call_wrapper(BS->HandleProtocol, 3, handles[i],
                                   &block_io_guid, (void **)&block);
        if (EFI_ERROR(status) || !block || !block->Media)
            continue;
        if (same_disk_as_source(handles[i], source_device)) {
            *source_seen = 1;
            continue;
        }
        if (block->Media->LogicalPartition || !block->Media->MediaPresent ||
            block->Media->ReadOnly ||
            block->Media->BlockSize != VANTA_INSTALLER_SECTOR_SIZE ||
            block->Media->LastBlock == UINT64_MAX)
            continue;
        uint64_t sectors = block->Media->LastBlock + 1U;
        if (sectors < SETUP_MIN_DISK_SECTORS || sectors > UINT32_MAX)
            continue;
        disks[found].handle = handles[i];
        disks[found].block = block;
        disks[found].sector_count = sectors;
        ++found;
    }
    uefi_call_wrapper(BS->FreePool, 1, handles);
    *out_count = found;
    return 1;
}

static void make_guid(uint8_t guid[16], uint64_t sectors,
                      const EFI_TIME *time, uint8_t tag)
{
    const uint8_t time_bytes[8] = {
        (uint8_t)time->Year, (uint8_t)(time->Year >> 8U),
        (uint8_t)time->Month, (uint8_t)time->Day,
        (uint8_t)time->Hour, (uint8_t)time->Minute,
        (uint8_t)time->Second, tag
    };
    for (size_t i = 0U; i < 16U; ++i) {
        uint8_t disk_byte = (uint8_t)(sectors >> ((i % 8U) * 8U));
        guid[i] = (uint8_t)(disk_byte ^ time_bytes[i % 8U] ^
                            (uint8_t)(i * 29U) ^ (uint8_t)(tag * 53U));
    }
    guid[0] |= 1U;
    guid[15] ^= tag;
}

static EFI_STATUS open_root(EFI_HANDLE device, EFI_FILE_PROTOCOL **out_root)
{
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *filesystem = NULL;
    EFI_STATUS status = uefi_call_wrapper(
        BS->HandleProtocol, 3, device, &filesystem_guid,
        (void **)&filesystem);
    if (EFI_ERROR(status) || !filesystem)
        return EFI_UNSUPPORTED;
    return uefi_call_wrapper(filesystem->OpenVolume, 2, filesystem, out_root);
}

static EFI_STATUS ensure_directory(
    EFI_FILE_PROTOCOL *root,
    const CHAR16 *path,
    EFI_FILE_PROTOCOL **out_directory)
{
    EFI_STATUS status = uefi_call_wrapper(
        root->Open, 5, root, out_directory, path,
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
        EFI_FILE_DIRECTORY);
    return status;
}

static void serial_write_char(CHAR16 character)
{
    char bytes[2];
    if (character > 0x7FU)
        return;
    bytes[0] = (char)character;
    bytes[1] = '\0';
    serial_write(bytes);
}

static void serial_write_path(const CHAR16 *path)
{
    while (*path) {
        serial_write_char(*path++);
    }
    serial_write("\n");
}

static EFI_STATUS copy_file(
    EFI_FILE_PROTOCOL *source_root,
    const CHAR16 *source_path,
    EFI_FILE_PROTOCOL *destination_root,
    const CHAR16 *destination_path,
    UINTN maximum_size)
{
    serial_write("VANTAOS_SETUP_COPY_BEGIN:");
    serial_write_path(source_path);
    EFI_FILE_PROTOCOL *source = NULL;
    EFI_FILE_PROTOCOL *destination = NULL;
    EFI_FILE_INFO *info = NULL;
    UINT8 *contents = NULL;
    UINTN info_size = 0U;
    EFI_STATUS status = uefi_call_wrapper(
        source_root->Open, 5, source_root, &source, source_path,
        EFI_FILE_MODE_READ, 0U);
    if (EFI_ERROR(status) || !source) {
        serial_write("VANTAOS_SETUP_COPY_FAIL\n");
        return EFI_NOT_FOUND;
    }

    status = uefi_call_wrapper(
        source->GetInfo, 4, source, &gEfiFileInfoGuid, &info_size, NULL);
    if (status != EFI_BUFFER_TOO_SMALL || info_size < sizeof(EFI_FILE_INFO)) {
        uefi_call_wrapper(source->Close, 1, source);
        serial_write("VANTAOS_SETUP_COPY_FAIL\n");
        return EFI_LOAD_ERROR;
    }
    status = uefi_call_wrapper(
        BS->AllocatePool, 3, EfiLoaderData, info_size, (void **)&info);
    if (EFI_ERROR(status) || !info) {
        uefi_call_wrapper(source->Close, 1, source);
        serial_write("VANTAOS_SETUP_COPY_FAIL\n");
        return EFI_OUT_OF_RESOURCES;
    }
    status = uefi_call_wrapper(
        source->GetInfo, 4, source, &gEfiFileInfoGuid, &info_size, info);
    if (EFI_ERROR(status) || info->FileSize == 0U ||
        info->FileSize > maximum_size || info->FileSize > (UINT64)(~(UINTN)0)) {
        uefi_call_wrapper(BS->FreePool, 1, info);
        uefi_call_wrapper(source->Close, 1, source);
        serial_write("VANTAOS_SETUP_COPY_FAIL\n");
        return EFI_LOAD_ERROR;
    }
    UINTN file_size = (UINTN)info->FileSize;
    uefi_call_wrapper(BS->FreePool, 1, info);
    status = uefi_call_wrapper(
        BS->AllocatePool, 3, EfiLoaderData, file_size, (void **)&contents);
    if (EFI_ERROR(status) || !contents) {
        uefi_call_wrapper(source->Close, 1, source);
        serial_write("VANTAOS_SETUP_COPY_FAIL\n");
        return EFI_OUT_OF_RESOURCES;
    }
    UINTN read_size = file_size;
    status = uefi_call_wrapper(source->Read, 3, source, &read_size, contents);
    uefi_call_wrapper(source->Close, 1, source);
    if (EFI_ERROR(status) || read_size != file_size) {
        uefi_call_wrapper(BS->FreePool, 1, contents);
        serial_write("VANTAOS_SETUP_COPY_FAIL\n");
        return EFI_LOAD_ERROR;
    }

    status = uefi_call_wrapper(
        destination_root->Open, 5, destination_root, &destination,
        destination_path,
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0U);
    if (!EFI_ERROR(status) && destination) {
        UINTN write_size = file_size;
        status = uefi_call_wrapper(
            destination->Write, 3, destination, &write_size, contents);
        if (!EFI_ERROR(status) && write_size != file_size)
            status = EFI_DEVICE_ERROR;
        uefi_call_wrapper(destination->Close, 1, destination);
    }
    uefi_call_wrapper(BS->FreePool, 1, contents);
    if (!EFI_ERROR(status))
        serial_write("VANTAOS_SETUP_COPY_OK\n");
    else
        serial_write("VANTAOS_SETUP_COPY_FAIL\n");
    return status;
}

static EFI_STATUS write_text_file(
    EFI_FILE_PROTOCOL *root,
    const CHAR16 *path,
    const char *contents,
    UINTN length)
{
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_STATUS status = uefi_call_wrapper(
        root->Open, 5, root, &file, path,
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0U);
    if (EFI_ERROR(status) || !file)
        return status;
    UINTN write_size = length;
    status = uefi_call_wrapper(file->Write, 3, file, &write_size, (void *)contents);
    if (!EFI_ERROR(status) && write_size != length)
        status = EFI_DEVICE_ERROR;
    uefi_call_wrapper(file->Close, 1, file);
    return status;
}

static UINT32 partition_number_from_path(EFI_DEVICE_PATH_PROTOCOL *path)
{
    while (path && !IsDevicePathEnd(path)) {
        if (DevicePathType(path) == MEDIA_DEVICE_PATH &&
            DevicePathSubType(path) == MEDIA_HARDDRIVE_DP) {
            const uint8_t *bytes = (const uint8_t *)path;
            return (uint32_t)bytes[4] | ((uint32_t)bytes[5] << 8U) |
                   ((uint32_t)bytes[6] << 16U) | ((uint32_t)bytes[7] << 24U);
        }
        path = NextDevicePathNode(path);
    }
    return 0U;
}

static EFI_HANDLE find_partition_handle(
    EFI_HANDLE disk_handle,
    UINT32 partition_number)
{
    EFI_HANDLE *handles = NULL;
    UINTN count = 0U;
    EFI_HANDLE found = NULL;
    EFI_STATUS status = uefi_call_wrapper(
        BS->LocateHandleBuffer, 5, ByProtocol, &block_io_guid,
        NULL, &count, &handles);
    if (EFI_ERROR(status) || !handles)
        return NULL;
    EFI_DEVICE_PATH_PROTOCOL *disk_path = DevicePathFromHandle(disk_handle);
    for (UINTN i = 0U; i < count; ++i) {
        EFI_BLOCK_IO_PROTOCOL *block = NULL;
        status = uefi_call_wrapper(BS->HandleProtocol, 3, handles[i],
                                   &block_io_guid, (void **)&block);
        if (EFI_ERROR(status) || !block || !block->Media ||
            !block->Media->LogicalPartition ||
            partition_number_from_path(DevicePathFromHandle(handles[i])) !=
                partition_number)
            continue;
        if (device_paths_share_disk(disk_path, DevicePathFromHandle(handles[i]))) {
            found = handles[i];
            break;
        }
    }
    uefi_call_wrapper(BS->FreePool, 1, handles);
    return found;
}

static int create_directories(EFI_FILE_PROTOCOL *root, int is_efi)
{
    static const CHAR16 *const efi_dirs[] = {
        L"EFI", L"EFI\\BOOT", L"EFI\\VANTA"
    };
    static const CHAR16 *const system_dirs[] = {
        L"SYSTEM", L"SYSTEM\\DRIVERS", L"SYSTEM\\DEVICES",
        L"ETC", L"HOME", L"HOME\\USER"
    };
    const CHAR16 *const *dirs = is_efi ? efi_dirs : system_dirs;
    UINTN count = is_efi
        ? sizeof(efi_dirs) / sizeof(efi_dirs[0])
        : sizeof(system_dirs) / sizeof(system_dirs[0]);
    for (UINTN i = 0U; i < count; ++i) {
        EFI_FILE_PROTOCOL *directory = NULL;
        EFI_STATUS status = ensure_directory(root, dirs[i], &directory);
        if (EFI_ERROR(status) || !directory)
            return 0;
        uefi_call_wrapper(directory->Close, 1, directory);
    }
    return 1;
}

static int source_file_valid(EFI_FILE_PROTOCOL *root,
                             const CHAR16 *path,
                             UINTN maximum_size)
{
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_FILE_INFO *info = NULL;
    UINTN info_size = 0U;
    EFI_STATUS status = uefi_call_wrapper(
        root->Open, 5, root, &file, path, EFI_FILE_MODE_READ, 0U);
    if (EFI_ERROR(status) || !file)
        return 0;

    status = uefi_call_wrapper(
        file->GetInfo, 4, file, &gEfiFileInfoGuid, &info_size, NULL);
    if (status != EFI_BUFFER_TOO_SMALL || info_size < sizeof(EFI_FILE_INFO)) {
        uefi_call_wrapper(file->Close, 1, file);
        return 0;
    }
    status = uefi_call_wrapper(
        BS->AllocatePool, 3, EfiLoaderData, info_size, (void **)&info);
    if (EFI_ERROR(status) || !info) {
        uefi_call_wrapper(file->Close, 1, file);
        return 0;
    }
    status = uefi_call_wrapper(
        file->GetInfo, 4, file, &gEfiFileInfoGuid, &info_size, info);
    int valid = !EFI_ERROR(status) && info->FileSize > 0U &&
                info->FileSize <= maximum_size;
    uefi_call_wrapper(BS->FreePool, 1, info);
    uefi_call_wrapper(file->Close, 1, file);
    return valid;
}

static int preflight_installer_payload(EFI_FILE_PROTOCOL *root,
                                       const CHAR16 **missing_path)
{
    static const struct {
        const CHAR16 *path;
        UINTN maximum_size;
    } required[] = {
        { L"\\EFI\\VANTA\\VANTAOS.EFI", 4U * 1024U * 1024U },
        { L"\\EFI\\VANTA\\KERNEL.ELF", SETUP_MAX_FILE_BYTES },
        { L"\\SYSTEM\\TERMINAL.ELF", SETUP_MAX_FILE_BYTES },
        { L"\\SYSTEM\\SETTINGS.ELF", SETUP_MAX_FILE_BYTES },
        { L"\\SYSTEM\\EXPLORER.ELF", SETUP_MAX_FILE_BYTES },
        { L"\\SYSTEM\\TERMINAL.VX", 4096U },
        { L"\\SYSTEM\\DRIVERS\\ATA.SYS", 4096U },
        { L"\\SYSTEM\\DRIVERS\\KEYBOARD.SYS", 4096U },
        { L"\\SYSTEM\\DRIVERS\\MOUSE.SYS", 4096U },
        { L"\\SYSTEM\\DRIVERS\\PCI.SYS", 4096U },
        { L"\\SYSTEM\\DRIVERS\\FAT32.SYS", 4096U },
        { L"\\SYSTEM\\DEVICES\\DISK0.DEV", 4096U },
        { L"\\SYSTEM\\DEVICES\\KEYBOARD0.DEV", 4096U },
        { L"\\SYSTEM\\DEVICES\\MOUSE0.DEV", 4096U },
        { L"\\ETC\\SYSTEM.CONF", 4096U }
    };
    for (UINTN i = 0U; i < sizeof(required) / sizeof(required[0]); ++i) {
        if (!source_file_valid(root, required[i].path,
                               required[i].maximum_size)) {
            if (missing_path)
                *missing_path = required[i].path;
            return 0;
        }
    }
    return 1;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table)
{
    InitializeLib(image_handle, system_table);
    serial_initialize();
    serial_write("VANTAOS_SETUP_START\n");
    Print(L"\r\nVANTAOS SETUP / VANTAOS-ASENNUS\r\n");
    Print(L"UEFI-only installer. Requires a writable 512-byte-sector disk of at least 4 GiB.\r\n");
    Print(L"Installs GPT with FAT32 EFI and system partitions. Legacy BIOS is not supported.\r\n");
    Print(L"The installer media is excluded from targets. No target writes occur before typing INSTALL.\r\n");
    Print(L"UEFI-asennus. Kohdelevyn on oltava kirjoitettava, vahintaan 4 GiB ja 512 tavun sektoreilla.\r\n");
    Print(L"Asentaa GPT-osionnin ja FAT32 EFI- ja jarjestelmaosiot. Vanha BIOS ei ole tuettu.\r\n");
    Print(L"Asennusmediaa ei voi valita kohteeksi. Levylle ei kirjoiteta ennen INSTALL-vahvistusta.\r\n\r\n");

    EFI_LOADED_IMAGE *loaded_image = NULL;
    EFI_STATUS status = uefi_call_wrapper(
        BS->HandleProtocol, 3, image_handle, &LoadedImageProtocol,
        (void **)&loaded_image);
    if (EFI_ERROR(status) || !loaded_image || !loaded_image->DeviceHandle) {
        Print(L"ERROR: cannot identify the installer boot device. No disks changed.\r\n");
        return EFI_LOAD_ERROR;
    }

    EFI_FILE_PROTOCOL *source_root = NULL;
    status = open_root(loaded_image->DeviceHandle, &source_root);
    if (EFI_ERROR(status) || !source_root) {
        Print(L"ERROR: cannot open the installer media. No disks changed.\r\n");
        return EFI_LOAD_ERROR;
    }

    vanta_setup_config_t config = {{0}, {0}, {0}};
    config.language[0] = 'e'; config.language[1] = 'n'; config.language[2] = '\0';
    config.keyboard[0] = 'u'; config.keyboard[1] = 's'; config.keyboard[2] = '\0';
    config.timezone[0] = '\0';

    char line[32];
    const CHAR16 *missing_payload = NULL;
    Print(L"Checking installer files before any disk can be modified...\r\n");
    if (!preflight_installer_payload(source_root, &missing_payload)) {
        serial_write("VANTAOS_SETUP_PREFLIGHT_FAIL\n");
        Print(L"ERROR: required installer payload is missing, empty, or too large: %s\r\n",
              missing_payload ? missing_payload : L"(unknown)");
        Print(L"No disks changed. Recreate or re-download the installer media.\r\n");
        uefi_call_wrapper(source_root->Close, 1, source_root);
        return EFI_NOT_FOUND;
    }
    serial_write("VANTAOS_SETUP_PREFLIGHT_OK\n");
    Print(L"Installer payload check passed (15 required files).\r\n\r\n");

    serial_write("VANTAOS_SETUP_LANGUAGE_PROMPT\n");
    for (;;) {
        Print(L"Language / Kieli: 1) English  2) Suomi [1]: ");
        if (!read_line(line, sizeof(line))) return EFI_ABORTED;
        if (line[0] == '\0' || (line[0] == '1' && line[1] == '\0'))
            break;
        if (line[0] == '2' && line[1] == '\0') {
            config.language[0] = 'f'; config.language[1] = 'i';
            break;
        }
        Print(L"Choose 1 or 2. Valitse 1 tai 2.\r\n");
    }

    serial_write("VANTAOS_SETUP_KEYBOARD_PROMPT\n");
    for (;;) {
        print_localized(&config, L"Keyboard: 1) US  2) Finnish [1]: ",
                        L"Nappaimisto: 1) US  2) Finnish [1]: ");
        if (!read_line(line, sizeof(line))) return EFI_ABORTED;
        if (line[0] == '\0' || (line[0] == '1' && line[1] == '\0'))
            break;
        if (line[0] == '2' && line[1] == '\0') {
            config.keyboard[0] = 'f'; config.keyboard[1] = 'i';
            break;
        }
        print_localized(&config, L"Choose 1 or 2.\r\n", L"Valitse 1 tai 2.\r\n");
    }

    serial_write("VANTAOS_SETUP_TIMEZONE_PROMPT\n");
    static const char *const zones[] = {
        "Europe/Helsinki", "Etc/UTC", "Europe/Stockholm", "Europe/Oslo",
        "Europe/Copenhagen", "Europe/Berlin", "Europe/Paris", "Europe/Madrid",
        "Europe/Rome", "Europe/Amsterdam", "Europe/Brussels", "Europe/Zurich",
        "Europe/Vienna", "Europe/Warsaw", "Europe/Prague", "Europe/Budapest",
        "Europe/Athens", "Europe/Istanbul", "Europe/Lisbon", "Europe/Dublin",
        "Europe/Kyiv", "America/New_York", "America/Chicago", "America/Denver",
        "America/Los_Angeles", "America/Toronto", "America/Vancouver",
        "America/Sao_Paulo", "Asia/Tokyo", "Asia/Seoul", "Asia/Shanghai",
        "Asia/Kolkata", "Asia/Singapore", "Australia/Sydney", "Pacific/Auckland",
        "Europe/London"
    };
    print_localized(&config, L"Time zone (enter number; default 1):\r\n", L"Aikavyohyke (valitse numero; oletus 1):\r\n");
    Print(L" 1 Helsinki       2 UTC             3 Stockholm       4 Oslo\r\n");
    Print(L" 5 Copenhagen     6 Berlin          7 Paris           8 Madrid\r\n");
    Print(L" 9 Rome          10 Amsterdam      11 Brussels       12 Zurich\r\n");
    Print(L"13 Vienna        14 Warsaw         15 Prague         16 Budapest\r\n");
    Print(L"17 Athens        18 Istanbul       19 Lisbon         20 Dublin\r\n");
    Print(L"21 Kyiv          22 New York       23 Chicago        24 Denver\r\n");
    Print(L"25 Los Angeles   26 Toronto        27 Vancouver      28 Sao Paulo\r\n");
    Print(L"29 Tokyo         30 Seoul          31 Shanghai       32 Kolkata\r\n");
    Print(L"33 Singapore     34 Sydney         35 Auckland       36 London [1]: ");
    uint32_t zone_choice = 1U;
    for (;;) {
        if (!read_line(line, sizeof(line))) {
            uefi_call_wrapper(source_root->Close, 1, source_root);
            return EFI_ABORTED;
        }
        if (line[0] == '\0')
            break;
        if (parse_number(line, &zone_choice) && zone_choice > 0U &&
            zone_choice <= sizeof(zones) / sizeof(zones[0]))
            break;
        print_localized(&config, L"Invalid time-zone choice; enter 1-36: ",
                        L"Virheellinen aikavyohyke; anna numero 1-36: ");
        zone_choice = 1U;
    }
    const char *zone = zones[zone_choice - 1U];
    size_t zone_length = 0U;
    while (zone[zone_length] && zone_length + 1U < sizeof(config.timezone)) {
        config.timezone[zone_length] = zone[zone_length];
        ++zone_length;
    }
    config.timezone[zone_length] = '\0';

    if (!vanta_setup_config_valid(&config)) {
        Print(L"ERROR: setup preferences failed validation.\r\n");
        uefi_call_wrapper(source_root->Close, 1, source_root);
        return EFI_INVALID_PARAMETER;
    }

    serial_write("VANTAOS_SETUP_REVIEW\n");
    print_localized(&config, L"\r\nSETUP REVIEW\r\n", L"\r\nASENNUKSEN YHTEENVETO\r\n");
    if (config.language[0] == 'f') {
        Print(L"  Kayttokieli: Suomi\r\n");
        Print(L"  Nappaimistoasetus: %s\r\n",
              config.keyboard[0] == 'f' ? L"Finnish" : L"US");
        Print(L"  Aikavyohyke: %a\r\n", config.timezone);
    } else {
        Print(L"  Preferred language: English\r\n");
        Print(L"  Keyboard preference: %s\r\n",
              config.keyboard[0] == 'f' ? L"Finnish" : L"US");
        Print(L"  Time zone: %a\r\n", config.timezone);
    }
    print_localized(&config, L"  Partition scheme: GPT (UEFI)\r\n", L"  Osiointitapa: GPT (UEFI)\r\n");
    print_localized(&config, L"  Filesystems: FAT32 EFI System Partition + FAT32 system partition\r\n", L"  Tiedostojarjestelmat: FAT32 EFI-osio ja FAT32 jarjestelmaosio\r\n");
    print_localized(&config, L"  Setup preferences will be saved to \\SYSTEM\\SETUP.CFG.\r\n", L"  Asetukset tallennetaan tiedostoon \\SYSTEM\\SETUP.CFG.\r\n");
    print_localized(&config, L"  Language and keyboard preferences are saved metadata; runtime localization/keymap integration is not yet implemented.\r\n\r\n", L"  Kieli- ja nappaimistoasetukset tallennetaan, mutta kayttojarjestelman kieli- ja asettelutuki ei ole viela toteutettu.\r\n\r\n");

    setup_disk_t disks[SETUP_MAX_DISKS];
    UINTN disk_count = 0U;
    int source_seen = 0;
    if (!enumerate_disks(loaded_image->DeviceHandle, disks,
                         &disk_count, &source_seen) || !source_seen) {
        Print(L"ERROR: cannot safely distinguish the installer media from target disks. No disks changed.\r\n");
        uefi_call_wrapper(source_root->Close, 1, source_root);
        return EFI_UNSUPPORTED;
    }
    if (disk_count == 0U) {
        Print(L"No eligible target disks found (requires writable 512-byte sectors and at least 4 GiB).\r\n");
        uefi_call_wrapper(source_root->Close, 1, source_root);
        return EFI_NOT_FOUND;
    }

    serial_write("VANTAOS_SETUP_DISKS_READY\n");
    print_localized(&config, L"\r\nTarget disks (installer media excluded):\r\n", L"\r\nKohdelevyt (asennusmediaa ei nayteta):\r\n");
    for (UINTN i = 0U; i < disk_count; ++i) {
        UINT64 gib = disks[i].sector_count / 2097152U;
        CHAR16 *path_text = DevicePathToStr(
            DevicePathFromHandle(disks[i].handle));
        Print(L"  %u) %u GiB, %u sectors, %s media\r\n",
              (UINT32)(i + 1U), (UINT32)gib,
              (UINT32)disks[i].sector_count,
              disks[i].block->Media->RemovableMedia ? L"removable" : L"fixed");
        Print(L"     Device path: %s\r\n",
              path_text ? path_text : L"(unavailable)");
        if (path_text)
            uefi_call_wrapper(BS->FreePool, 1, path_text);
    }
    serial_write("VANTAOS_SETUP_DISK_PROMPT\n");
    print_localized(&config, L"Select target disk number, or Q to cancel: ", L"Valitse kohdelevyn numero tai Q peruuttaaksesi: ");
    uint32_t selected = 0U;
    for (;;) {
        if (!read_line(line, sizeof(line))) {
            uefi_call_wrapper(source_root->Close, 1, source_root);
            return EFI_ABORTED;
        }
        if ((line[0] == 'q' || line[0] == 'Q') && line[1] == '\0') {
            print_localized(&config, L"Setup cancelled. No disks changed.\r\n",
                            L"Asennus peruutettu. Levyihin ei tehty muutoksia.\r\n");
            uefi_call_wrapper(source_root->Close, 1, source_root);
            return EFI_ABORTED;
        }
        if (parse_number(line, &selected) && selected > 0U &&
            selected <= disk_count)
            break;
        print_localized(&config, L"Invalid disk selection; try again or Q to cancel: ",
                        L"Virheellinen levyn numero; yrita uudelleen tai Q peruuttaaksesi: ");
    }
    setup_disk_t *target = &disks[selected - 1U];
    CHAR16 *selected_path = DevicePathToStr(DevicePathFromHandle(target->handle));
    if (config.language[0] == 'f') {
        Print(L"\r\nValittu kohde: %u GiB, %u sektoria, %s media\r\n",
              (UINT32)(target->sector_count / 2097152U),
              (UINT32)target->sector_count,
              target->block->Media->RemovableMedia ? L"irrotettava" : L"kiintea");
        Print(L"Laitteen polku: %s\r\n",
              selected_path ? selected_path : L"(ei saatavilla)");
    } else {
        Print(L"\r\nSelected target: %u GiB, %u sectors, %s media\r\n",
              (UINT32)(target->sector_count / 2097152U),
              (UINT32)target->sector_count,
              target->block->Media->RemovableMedia ? L"removable" : L"fixed");
        Print(L"Device path: %s\r\n",
              selected_path ? selected_path : L"(unavailable)");
    }
    vanta_installer_layout_t layout;
    if (vanta_installer_plan_gpt(target->sector_count,
            VANTA_INSTALLER_SECTOR_SIZE, &layout) != VANTA_INSTALLER_LAYOUT_OK) {
        Print(L"ERROR: disk layout is unsupported. No disks changed.\r\n");
        if (selected_path)
            uefi_call_wrapper(BS->FreePool, 1, selected_path);
        uefi_call_wrapper(source_root->Close, 1, source_root);
        return EFI_UNSUPPORTED;
    }
    print_localized(&config,
        L"\r\nPlanned partition layout (preview; no writes yet):\r\n",
        L"\r\nOsiointisuunnitelma (esikatselu; ei kirjoituksia viela):\r\n");
    print_localized(&config, L"  GPT, 1 MiB alignment\r\n",
                    L"  GPT, 1 MiB kohdistus\r\n");
    print_localized(&config, L"  EFI System Partition: ",
                    L"  EFI-jarjestelmaosio: ");
    Print(L"%u MiB, FAT32\r\n", (UINT32)(layout.efi.sector_count / 2048U));
    print_localized(&config, L"  VantaOS system partition: ",
                    L"  VantaOS-jarjestelmaosio: ");
    Print(L"%u MiB, FAT32\r\n", (UINT32)(layout.system.sector_count / 2048U));
    print_localized(&config, L"WARNING: continuing will erase the entire selected disk.\r\n", L"VAROITUS: jatkaminen tyhjentaa koko valitun levyn.\r\n");
    Print(L"Target device: %s\r\n", selected_path ? selected_path : L"(unavailable)");
    Print(L"Target capacity: %u GiB\r\n", (UINT32)(target->sector_count / 2097152U));
    serial_write("VANTAOS_SETUP_CONFIRM_PROMPT\n");
    print_localized(&config, L"Type INSTALL exactly to confirm, or anything else to cancel: ", L"Vahvista kirjoittamalla INSTALL, tai peruuta muulla tekstilla: ");
    if (!read_line(line, sizeof(line)) ||
        line[0] != 'I' || line[1] != 'N' || line[2] != 'S' ||
        line[3] != 'T' || line[4] != 'A' || line[5] != 'L' ||
        line[6] != 'L' || line[7] != '\0') {
        Print(L"Cancelled. No disks changed.\r\n");
        if (selected_path)
            uefi_call_wrapper(BS->FreePool, 1, selected_path);
        uefi_call_wrapper(source_root->Close, 1, source_root);
        return EFI_ABORTED;
    }

    serial_write("VANTAOS_SETUP_CONFIRM_ACCEPTED\n");
    if (selected_path)
        uefi_call_wrapper(BS->FreePool, 1, selected_path);

    EFI_TIME now;
    status = uefi_call_wrapper(RT->GetTime, 2, &now, NULL);
    if (EFI_ERROR(status)) {
        Print(L"ERROR: firmware clock unavailable; refusing to create partition IDs.\r\n");
        uefi_call_wrapper(source_root->Close, 1, source_root);
        return status;
    }
    uint8_t disk_guid[16], efi_guid[16], system_guid[16];
    make_guid(disk_guid, target->sector_count, &now, 1U);
    make_guid(efi_guid, target->sector_count, &now, 2U);
    make_guid(system_guid, target->sector_count, &now, 3U);

    vanta_installer_disk_io_t io = {
        disk_read, disk_write, disk_flush, target->block
    };
    UINT8 *workspace = NULL;
    status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData,
        VANTA_INSTALLER_GPT_WORKSPACE_SIZE, (void **)&workspace);
    if (EFI_ERROR(status) || !workspace) {
        Print(L"ERROR: not enough memory for partition setup.\r\n");
        uefi_call_wrapper(source_root->Close, 1, source_root);
        return EFI_OUT_OF_RESOURCES;
    }
    Print(L"Writing and verifying GPT partition tables...\r\n");
    vanta_installer_gpt_status_t gpt_status = vanta_installer_write_gpt(
        &io, &layout, disk_guid, efi_guid, system_guid, workspace,
        VANTA_INSTALLER_GPT_WORKSPACE_SIZE);
    uefi_call_wrapper(BS->FreePool, 1, workspace);
    if (gpt_status != VANTA_INSTALLER_GPT_OK) {
        serial_write("VANTAOS_SETUP_GPT_FAIL\n");
        Print(L"ERROR: GPT write/verification failed. Do not reboot; inspect the target disk.\r\n");
        uefi_call_wrapper(source_root->Close, 1, source_root);
        return EFI_DEVICE_ERROR;
    }

    serial_write("VANTAOS_SETUP_GPT_OK\n");
    UINT8 *fat_workspace = NULL;
    status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData,
        VANTA_INSTALLER_FAT32_WORKSPACE_SIZE, (void **)&fat_workspace);
    if (EFI_ERROR(status) || !fat_workspace) {
        serial_write("VANTAOS_SETUP_FORMAT_FAIL\n");
        Print(L"ERROR: partition table is written but FAT32 workspace allocation failed.\r\n");
        uefi_call_wrapper(source_root->Close, 1, source_root);
        return EFI_OUT_OF_RESOURCES;
    }
    vanta_installer_fat32_geometry_t efi_geometry, system_geometry;
    Print(L"Formatting EFI and system partitions as FAT32...\r\n");
    vanta_installer_fat32_status_t efi_status = vanta_installer_format_fat32(
        &io, target->sector_count, layout.efi.first_lba, layout.efi.sector_count,
        0x56414E54U, fat_workspace, VANTA_INSTALLER_FAT32_WORKSPACE_SIZE,
        &efi_geometry);
    vanta_installer_fat32_status_t system_status = efi_status == VANTA_INSTALLER_FAT32_OK
        ? vanta_installer_format_fat32(&io, target->sector_count,
            layout.system.first_lba,
            layout.system.sector_count > UINT32_MAX
                ? UINT32_MAX : layout.system.sector_count,
            0x56414E53U, fat_workspace, VANTA_INSTALLER_FAT32_WORKSPACE_SIZE,
            &system_geometry)
        : VANTA_INSTALLER_FAT32_IO_ERROR;
    uefi_call_wrapper(BS->FreePool, 1, fat_workspace);
    if (efi_status != VANTA_INSTALLER_FAT32_OK ||
        system_status != VANTA_INSTALLER_FAT32_OK) {
        serial_write("VANTAOS_SETUP_FORMAT_FAIL\n");
        Print(L"ERROR: FAT32 formatting or read-back verification failed.\r\n");
        uefi_call_wrapper(source_root->Close, 1, source_root);
        return EFI_DEVICE_ERROR;
    }

    serial_write("VANTAOS_SETUP_FORMAT_OK\n");
    Print(L"Connecting firmware partition drivers...\r\n");
    status = uefi_call_wrapper(BS->ConnectController, 4,
        target->handle, NULL, NULL, TRUE);
    if (EFI_ERROR(status))
        Print(L"Firmware did not reconnect the target automatically; retrying all controllers.\r\n");
    (void)uefi_call_wrapper(BS->ConnectController, 4, NULL, NULL, NULL, TRUE);

    EFI_HANDLE efi_partition = find_partition_handle(target->handle, 1U);
    EFI_HANDLE system_partition = find_partition_handle(target->handle, 2U);
    if (!efi_partition || !system_partition) {
        serial_write("VANTAOS_SETUP_PARTITION_DISCOVERY_FAIL\n");
        Print(L"ERROR: firmware did not expose the new partitions as filesystems. GPT and FAT32 are written, but file deployment did not complete.\r\n");
        uefi_call_wrapper(source_root->Close, 1, source_root);
        return EFI_NOT_FOUND;
    }

    EFI_FILE_PROTOCOL *efi_root = NULL;
    EFI_FILE_PROTOCOL *system_root = NULL;
    status = open_root(efi_partition, &efi_root);
    if (EFI_ERROR(status) || !efi_root) {
        serial_write("VANTAOS_SETUP_FILES_FAIL\n");
        Print(L"ERROR: cannot open the new EFI partition.\r\n");
        uefi_call_wrapper(source_root->Close, 1, source_root);
        return EFI_DEVICE_ERROR;
    }
    status = open_root(system_partition, &system_root);
    if (EFI_ERROR(status) || !system_root) {
        serial_write("VANTAOS_SETUP_FILES_FAIL\n");
        Print(L"ERROR: cannot open the new system partition.\r\n");
        uefi_call_wrapper(efi_root->Close, 1, efi_root);
        uefi_call_wrapper(source_root->Close, 1, source_root);
        return EFI_DEVICE_ERROR;
    }
    if (!create_directories(efi_root, 1) || !create_directories(system_root, 0)) {
        serial_write("VANTAOS_SETUP_FILES_FAIL\n");
        Print(L"ERROR: cannot create the required installation directories.\r\n");
        uefi_call_wrapper(system_root->Close, 1, system_root);
        uefi_call_wrapper(efi_root->Close, 1, efi_root);
        uefi_call_wrapper(source_root->Close, 1, source_root);
        return EFI_DEVICE_ERROR;
    }

    Print(L"Copying UEFI loader and kernel...\r\n");
    status = copy_file(source_root, L"\\EFI\\VANTA\\VANTAOS.EFI",
                       efi_root, L"\\EFI\\BOOT\\BOOTX64.EFI", 4U * 1024U * 1024U);
    if (!EFI_ERROR(status))
        status = copy_file(source_root, L"\\EFI\\VANTA\\KERNEL.ELF",
                           efi_root, L"\\EFI\\VANTA\\KERNEL.ELF",
                           SETUP_MAX_FILE_BYTES);
    if (!EFI_ERROR(status))
        status = copy_file(source_root, L"\\SYSTEM\\TERMINAL.ELF",
                           system_root, L"\\TERMINAL.ELF",
                           SETUP_MAX_FILE_BYTES);
    if (!EFI_ERROR(status))
        status = copy_file(source_root, L"\\SYSTEM\\SETTINGS.ELF",
                           system_root, L"\\SYSTEM\\SETTINGS.ELF",
                           SETUP_MAX_FILE_BYTES);
    if (!EFI_ERROR(status))
        status = copy_file(source_root, L"\\SYSTEM\\EXPLORER.ELF",
                           system_root, L"\\EXPLORER.ELF",
                           SETUP_MAX_FILE_BYTES);

    if (!EFI_ERROR(status))
        status = copy_file(source_root, L"\\SYSTEM\\TERMINAL.VX",
                           system_root, L"\\SYSTEM\\TERMINAL.VX", 4096U);
    if (!EFI_ERROR(status))
        status = copy_file(source_root, L"\\SYSTEM\\DRIVERS\\ATA.SYS",
                           system_root, L"\\SYSTEM\\DRIVERS\\ATA.SYS", 4096U);
    if (!EFI_ERROR(status))
        status = copy_file(source_root, L"\\SYSTEM\\DRIVERS\\KEYBOARD.SYS",
                           system_root, L"\\SYSTEM\\DRIVERS\\KEYBOARD.SYS", 4096U);
    if (!EFI_ERROR(status))
        status = copy_file(source_root, L"\\SYSTEM\\DRIVERS\\MOUSE.SYS",
                           system_root, L"\\SYSTEM\\DRIVERS\\MOUSE.SYS", 4096U);
    if (!EFI_ERROR(status))
        status = copy_file(source_root, L"\\SYSTEM\\DRIVERS\\PCI.SYS",
                           system_root, L"\\SYSTEM\\DRIVERS\\PCI.SYS", 4096U);
    if (!EFI_ERROR(status))
        status = copy_file(source_root, L"\\SYSTEM\\DRIVERS\\FAT32.SYS",
                           system_root, L"\\SYSTEM\\DRIVERS\\FAT32.SYS", 4096U);
    if (!EFI_ERROR(status))
        status = copy_file(source_root, L"\\SYSTEM\\DEVICES\\DISK0.DEV",
                           system_root, L"\\SYSTEM\\DEVICES\\DISK0.DEV", 4096U);
    if (!EFI_ERROR(status))
        status = copy_file(source_root, L"\\SYSTEM\\DEVICES\\KEYBOARD0.DEV",
                           system_root, L"\\SYSTEM\\DEVICES\\KEYBOARD0.DEV", 4096U);
    if (!EFI_ERROR(status))
        status = copy_file(source_root, L"\\SYSTEM\\DEVICES\\MOUSE0.DEV",
                           system_root, L"\\SYSTEM\\DEVICES\\MOUSE0.DEV", 4096U);
    if (!EFI_ERROR(status))
        status = copy_file(source_root, L"\\ETC\\SYSTEM.CONF",
                           system_root, L"\\ETC\\SYSTEM.CONF", 4096U);

    char encoded[VANTA_SETUP_CONFIG_MAX_BYTES];
    size_t encoded_length = 0U;
    if (!EFI_ERROR(status) &&
        !vanta_setup_config_encode(&config, encoded, sizeof(encoded), &encoded_length))
        status = EFI_INVALID_PARAMETER;
    if (!EFI_ERROR(status))
        status = write_text_file(system_root, L"\\SYSTEM\\SETUP.CFG",
                                 encoded, (UINTN)encoded_length);

    uefi_call_wrapper(system_root->Close, 1, system_root);
    uefi_call_wrapper(efi_root->Close, 1, efi_root);
    uefi_call_wrapper(source_root->Close, 1, source_root);
    if (EFI_ERROR(status)) {
        serial_write("VANTAOS_SETUP_FILES_FAIL\n");
        Print(L"ERROR: file deployment failed after partitioning. Status: %r\r\n", status);
        return status;
    }

    serial_write("VANTAOS_SETUP_FILES_OK\n");
    serial_write("VANTAOS_SETUP_COMPLETE\n");
    print_localized(&config, L"\r\nVantaOS installation completed.\r\n",
                    L"\r\nVantaOS-asennus valmistui.\r\n");
    print_localized(&config, L"Remove the installer media, then reboot and choose the installed disk.\r\n",
                    L"Irrota asennusmedia, kaynnista uudelleen ja valitse asennettu levy.\r\n");
    print_localized(&config, L"Press any key to restart...\r\n",
                    L"Kaynnista uudelleen painamalla mita tahansa nappainta...\r\n");
    EFI_INPUT_KEY key;
    (void)uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &key);
    (void)uefi_call_wrapper(RT->ResetSystem, 4, EfiResetCold, EFI_SUCCESS, 0U, NULL);
    return EFI_SUCCESS;
}
