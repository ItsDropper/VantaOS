/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include <efi.h>
#include <efilib.h>
#include <elf.h>
#include <stdint.h>
#include <stddef.h>

#include "vanta_boot_info.h"

#define UEFI_KERNEL_PATH L"\\EFI\\VANTA\\KERNEL.ELF"
#define UEFI_MAX_KERNEL_SIZE (64U * 1024U * 1024U)
#define UEFI_MAX_MEMORY_MAP_SIZE (256U * 1024U)
#define UEFI_MAX_PHYSICAL_ADDRESS 0x100000000ULL

static void port_out8(UINT16 port, UINT8 value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static UINT8 port_in8(UINT16 port)
{
    UINT8 value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_initialize(void)
{
    port_out8(0x3F9, 0x00);
    port_out8(0x3FB, 0x80);
    port_out8(0x3F8, 0x01);
    port_out8(0x3F9, 0x00);
    port_out8(0x3FB, 0x03);
    port_out8(0x3FA, 0xC7);
    port_out8(0x3FC, 0x0B);
}

static void serial_write(const char* text)
{
    while (*text)
    {
        while ((port_in8(0x3FD) & 0x20U) == 0)
            ;
        port_out8(0x3F8, (UINT8)*text++);
    }
}

static void zero_bytes(void* destination, UINTN length)
{
    UINT8* bytes = (UINT8*)destination;
    for (UINTN i = 0; i < length; i++)
        bytes[i] = 0;
}

static int guid_equal(const EFI_GUID* a, const EFI_GUID* b)
{
    if (!a || !b || a->Data1 != b->Data1 ||
        a->Data2 != b->Data2 || a->Data3 != b->Data3)
        return 0;

    for (UINTN i = 0; i < sizeof(a->Data4); i++)
        if (a->Data4[i] != b->Data4[i])
            return 0;

    return 1;
}

static UINT64 align_down_page(UINT64 address)
{
    return address & ~(EFI_PAGE_SIZE - 1ULL);
}

static int align_up_page(UINT64 address, UINT64* result)
{
    if (!result || address > UINT64_MAX - (EFI_PAGE_SIZE - 1ULL))
        return 0;
    *result = (address + EFI_PAGE_SIZE - 1ULL) & ~(EFI_PAGE_SIZE - 1ULL);
    return 1;
}

static UINT32 normalize_efi_memory_type(UINT32 type)
{
    switch (type)
    {
        case EfiConventionalMemory:
            return VANTA_MEMORY_USABLE;
        case EfiACPIReclaimMemory:
            return VANTA_MEMORY_ACPI_RECLAIMABLE;
        case EfiACPIMemoryNVS:
            return VANTA_MEMORY_ACPI_NVS;
        case EfiUnusableMemory:
            return VANTA_MEMORY_BAD;
        default:
            /* Conservative: firmware, loader, MMIO, and unknown memory stay reserved. */
            return VANTA_MEMORY_RESERVED;
    }
}

static EFI_STATUS open_kernel_file(
    EFI_HANDLE image_handle,
    EFI_FILE_PROTOCOL** out_file,
    UINT8** out_contents,
    UINTN* out_size
)
{
    EFI_STATUS status;
    EFI_LOADED_IMAGE* loaded_image = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* filesystem = NULL;
    EFI_FILE_PROTOCOL* root = NULL;
    EFI_FILE_PROTOCOL* file = NULL;
    EFI_FILE_INFO* file_info = NULL;
    UINT8* contents = NULL;
    UINTN info_size = 0;

    status = uefi_call_wrapper(
        BS->HandleProtocol, 3, image_handle, &LoadedImageProtocol,
        (void**)&loaded_image
    );
    if (EFI_ERROR(status) || !loaded_image)
        return EFI_LOAD_ERROR;

    status = uefi_call_wrapper(
        BS->HandleProtocol, 3, loaded_image->DeviceHandle,
        &FileSystemProtocol, (void**)&filesystem
    );
    if (EFI_ERROR(status) || !filesystem)
        return EFI_LOAD_ERROR;

    status = uefi_call_wrapper(filesystem->OpenVolume, 2, filesystem, &root);
    if (EFI_ERROR(status) || !root)
        return EFI_LOAD_ERROR;

    status = uefi_call_wrapper(
        root->Open, 5, root, &file, UEFI_KERNEL_PATH,
        EFI_FILE_MODE_READ, 0
    );
    uefi_call_wrapper(root->Close, 1, root);
    if (EFI_ERROR(status) || !file)
        return EFI_NOT_FOUND;

    status = uefi_call_wrapper(
        file->GetInfo, 4, file, &gEfiFileInfoGuid, &info_size, NULL
    );
    if (status != EFI_BUFFER_TOO_SMALL || info_size < sizeof(EFI_FILE_INFO))
    {
        uefi_call_wrapper(file->Close, 1, file);
        return EFI_LOAD_ERROR;
    }

    status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData,
                               info_size, (void**)&file_info);
    if (EFI_ERROR(status) || !file_info)
    {
        uefi_call_wrapper(file->Close, 1, file);
        return EFI_OUT_OF_RESOURCES;
    }

    status = uefi_call_wrapper(
        file->GetInfo, 4, file, &gEfiFileInfoGuid, &info_size, file_info
    );
    if (EFI_ERROR(status) || file_info->FileSize == 0 ||
        file_info->FileSize > UEFI_MAX_KERNEL_SIZE ||
        file_info->FileSize > (UINT64)(~(UINTN)0))
    {
        uefi_call_wrapper(BS->FreePool, 1, file_info);
        uefi_call_wrapper(file->Close, 1, file);
        return EFI_LOAD_ERROR;
    }

    UINTN file_size = (UINTN)file_info->FileSize;
    uefi_call_wrapper(BS->FreePool, 1, file_info);

    status = uefi_call_wrapper(
        BS->AllocatePool, 3, EfiLoaderData, file_size, (void**)&contents
    );
    if (EFI_ERROR(status) || !contents)
    {
        uefi_call_wrapper(file->Close, 1, file);
        return EFI_OUT_OF_RESOURCES;
    }

    UINTN bytes_read = file_size;
    status = uefi_call_wrapper(file->Read, 3, file, &bytes_read, contents);
    uefi_call_wrapper(file->Close, 1, file);
    if (EFI_ERROR(status) || bytes_read != file_size)
    {
        uefi_call_wrapper(BS->FreePool, 1, contents);
        return EFI_LOAD_ERROR;
    }

    *out_file = NULL;
    *out_contents = contents;
    *out_size = file_size;
    return EFI_SUCCESS;
}

static EFI_STATUS load_elf64(
    const UINT8* file,
    UINTN file_size,
    UINT64* out_image_start,
    UINT64* out_image_end,
    UINT64* out_entry
)
{
    if (!file || file_size < sizeof(Elf64_Ehdr))
        return EFI_LOAD_ERROR;

    const Elf64_Ehdr* header = (const Elf64_Ehdr*)file;
    if (header->e_ident[EI_MAG0] != ELFMAG0 ||
        header->e_ident[EI_MAG1] != ELFMAG1 ||
        header->e_ident[EI_MAG2] != ELFMAG2 ||
        header->e_ident[EI_MAG3] != ELFMAG3 ||
        header->e_ident[EI_CLASS] != ELFCLASS64 ||
        header->e_ident[EI_DATA] != ELFDATA2LSB ||
        header->e_version != EV_CURRENT ||
        header->e_type != ET_EXEC ||
        header->e_machine != EM_X86_64 ||
        header->e_ehsize != sizeof(Elf64_Ehdr) ||
        header->e_phentsize != sizeof(Elf64_Phdr) ||
        header->e_phnum == 0 || header->e_phnum > 128U)
        return EFI_LOAD_ERROR;

    if (header->e_phoff > file_size ||
        (UINT64)header->e_phnum >
            (UINT64)(file_size - (UINTN)header->e_phoff) /
                sizeof(Elf64_Phdr))
        return EFI_LOAD_ERROR;

    const Elf64_Phdr* phdrs =
        (const Elf64_Phdr*)(file + (UINTN)header->e_phoff);
    UINT64 image_start = UINT64_MAX;
    UINT64 image_end = 0;
    int entry_is_executable = 0;

    for (UINTN i = 0; i < header->e_phnum; i++)
    {
        const Elf64_Phdr* ph = &phdrs[i];
        if (ph->p_type != PT_LOAD)
            continue;

        if (ph->p_memsz == 0 || ph->p_filesz > ph->p_memsz ||
            ph->p_offset > file_size ||
            ph->p_filesz > file_size - (UINTN)ph->p_offset ||
            ph->p_paddr != ph->p_vaddr ||
            ph->p_paddr < 0x100000ULL ||
            ph->p_memsz > UINT64_MAX - ph->p_paddr)
            return EFI_LOAD_ERROR;

        UINT64 segment_end = ph->p_paddr + ph->p_memsz;
        if (segment_end > UEFI_MAX_PHYSICAL_ADDRESS)
            return EFI_UNSUPPORTED;

        /* PT_LOAD destinations must not alias one another. */
        for (UINTN previous = 0; previous < i; previous++)
        {
            const Elf64_Phdr* other = &phdrs[previous];
            if (other->p_type != PT_LOAD || other->p_memsz == 0)
                continue;

            UINT64 other_end = other->p_paddr + other->p_memsz;
            if (ph->p_paddr < other_end && other->p_paddr < segment_end)
                return EFI_LOAD_ERROR;
        }

        if (ph->p_align > 1U &&
            ((ph->p_align & (ph->p_align - 1U)) != 0 ||
             (ph->p_vaddr % ph->p_align) != (ph->p_offset % ph->p_align)))
            return EFI_LOAD_ERROR;

        UINT64 segment_page_start = align_down_page(ph->p_paddr);
        UINT64 segment_page_end;
        if (!align_up_page(segment_end, &segment_page_end))
            return EFI_LOAD_ERROR;

        if (segment_page_start < image_start)
            image_start = segment_page_start;
        if (segment_page_end > image_end)
            image_end = segment_page_end;

        if ((ph->p_flags & PF_X) &&
            header->e_entry >= ph->p_vaddr &&
            header->e_entry < segment_end)
            entry_is_executable = 1;
    }

    if (image_start == UINT64_MAX || image_end <= image_start ||
        image_end > UEFI_MAX_PHYSICAL_ADDRESS || !entry_is_executable)
        return EFI_LOAD_ERROR;

    UINT64 image_bytes = image_end - image_start;
    UINTN pages = (UINTN)(image_bytes / EFI_PAGE_SIZE);
    EFI_PHYSICAL_ADDRESS allocation = image_start;
    EFI_STATUS status = uefi_call_wrapper(
        BS->AllocatePages, 4, AllocateAddress, EfiLoaderData, pages,
        &allocation
    );
    if (EFI_ERROR(status) || allocation != image_start)
        return EFI_OUT_OF_RESOURCES;

    SetMem((void*)(UINTN)image_start, (UINTN)image_bytes, 0);

    for (UINTN i = 0; i < header->e_phnum; i++)
    {
        const Elf64_Phdr* ph = &phdrs[i];
        if (ph->p_type != PT_LOAD || ph->p_filesz == 0)
            continue;

        CopyMem(
            (void*)(UINTN)ph->p_paddr,
            (const void*)(file + (UINTN)ph->p_offset),
            (UINTN)ph->p_filesz
        );
    }

    *out_image_start = image_start;
    *out_image_end = image_end;
    *out_entry = header->e_entry;
    return EFI_SUCCESS;
}

static EFI_STATUS collect_graphics(
    vanta_boot_info_t* boot_info,
    UINT64* framebuffer_size
)
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = NULL;
    EFI_STATUS status = uefi_call_wrapper(
        BS->LocateProtocol, 3, &GraphicsOutputProtocol, NULL, (void**)&gop
    );
    if (EFI_ERROR(status) || !gop || !gop->Mode || !gop->Mode->Info)
        return EFI_UNSUPPORTED;

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info = gop->Mode->Info;
    if (gop->Mode->FrameBufferBase == 0 ||
        info->HorizontalResolution == 0 ||
        info->VerticalResolution == 0 ||
        info->PixelsPerScanLine < info->HorizontalResolution ||
        (info->PixelFormat != PixelRedGreenBlueReserved8BitPerColor &&
         info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor))
        return EFI_UNSUPPORTED;

    UINT64 pitch = (UINT64)info->PixelsPerScanLine * 4ULL;
    UINT64 bytes = pitch * info->VerticalResolution;
    if (pitch > UINT32_MAX || bytes == 0 ||
        gop->Mode->FrameBufferBase >= UEFI_MAX_PHYSICAL_ADDRESS ||
        bytes > UEFI_MAX_PHYSICAL_ADDRESS - gop->Mode->FrameBufferBase ||
        bytes > gop->Mode->FrameBufferSize)
        return EFI_UNSUPPORTED;

    boot_info->framebuffer.address = gop->Mode->FrameBufferBase;
    boot_info->framebuffer.pitch = (UINT32)pitch;
    boot_info->framebuffer.width = info->HorizontalResolution;
    boot_info->framebuffer.height = info->VerticalResolution;
    boot_info->framebuffer.bits_per_pixel = 32U;
    boot_info->framebuffer.type = 1U;

    if (info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor)
    {
        boot_info->framebuffer.red_position = 0U;
        boot_info->framebuffer.green_position = 8U;
        boot_info->framebuffer.blue_position = 16U;
    }
    else
    {
        boot_info->framebuffer.blue_position = 0U;
        boot_info->framebuffer.green_position = 8U;
        boot_info->framebuffer.red_position = 16U;
    }

    boot_info->framebuffer.red_mask_size = 8U;
    boot_info->framebuffer.green_mask_size = 8U;
    boot_info->framebuffer.blue_mask_size = 8U;
    boot_info->flags |= VANTA_BOOT_FLAG_FRAMEBUFFER_VALID;
    *framebuffer_size = bytes;
    return EFI_SUCCESS;
}

static void collect_acpi(vanta_boot_info_t* boot_info)
{
    EFI_GUID acpi20_guid = {
        0x8868E871U, 0xE4F1U, 0x11D3U,
        {0xBCU, 0x22U, 0x00U, 0x80U, 0xC7U, 0x3CU, 0x88U, 0x81U}
    };
    EFI_GUID acpi10_guid = {
        0xEB9D2D30U, 0x2D88U, 0x11D3U,
        {0x9AU, 0x16U, 0x00U, 0x90U, 0x27U, 0x3FU, 0xC1U, 0x4DU}
    };

    for (UINTN i = 0; i < ST->NumberOfTableEntries; i++)
    {
        EFI_CONFIGURATION_TABLE* table = &ST->ConfigurationTable[i];
        if (guid_equal(&table->VendorGuid, &acpi20_guid) ||
            guid_equal(&table->VendorGuid, &acpi10_guid))
        {
            boot_info->acpi_rsdp_address =
                (UINT64)(UINTN)table->VendorTable;
            boot_info->flags |= VANTA_BOOT_FLAG_ACPI_RSDP_VALID;
            if (guid_equal(&table->VendorGuid, &acpi20_guid))
                return;
        }
    }
}

static EFI_STATUS refresh_memory_map(
    vanta_boot_info_t* boot_info,
    void* map_buffer,
    UINTN* map_size,
    UINTN map_capacity,
    UINTN* map_key
)
{
    *map_size = map_capacity;
    UINTN descriptor_size = 0;
    UINT32 descriptor_version = 0;
    EFI_STATUS status = uefi_call_wrapper(
        BS->GetMemoryMap, 5, map_size, map_buffer, map_key,
        &descriptor_size, &descriptor_version
    );
    if (EFI_ERROR(status) || descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR) ||
        descriptor_size == 0 || *map_size > map_capacity)
    {
        serial_write("VANTAOS_UEFI_MEMORY_MAP_GET_FAIL\n");
        return EFI_LOAD_ERROR;
    }

    boot_info->memory_region_count = 0;
    boot_info->flags &= ~(VANTA_BOOT_FLAG_MEMORY_MAP_VALID |
                          VANTA_BOOT_FLAG_MEMORY_MAP_TRUNCATED);

    for (UINTN offset = 0; offset < *map_size; offset += descriptor_size)
    {
        if (offset > *map_size ||
            descriptor_size > *map_size - offset)
        {
            serial_write("VANTAOS_UEFI_MEMORY_MAP_MALFORMED_FAIL\n");
            return EFI_LOAD_ERROR;
        }

        EFI_MEMORY_DESCRIPTOR* descriptor =
            (EFI_MEMORY_DESCRIPTOR*)((UINT8*)map_buffer + offset);
        if (descriptor->NumberOfPages == 0)
            continue;
        if (descriptor->NumberOfPages > UINT64_MAX / EFI_PAGE_SIZE)
        {
            serial_write("VANTAOS_UEFI_MEMORY_MAP_OVERFLOW_FAIL\n");
            return EFI_LOAD_ERROR;
        }

        UINT64 length = descriptor->NumberOfPages * EFI_PAGE_SIZE;
        if (length > UINT64_MAX - descriptor->PhysicalStart)
        {
            serial_write("VANTAOS_UEFI_MEMORY_MAP_RANGE_FAIL\n");
            return EFI_LOAD_ERROR;
        }

        if (boot_info->memory_region_count >=
            VANTA_BOOT_INFO_MAX_MEMORY_REGIONS)
        {
            serial_write("VANTAOS_UEFI_MEMORY_MAP_CAPACITY_FAIL\n");
            return EFI_UNSUPPORTED;
        }

        vanta_memory_region_t* region =
            &boot_info->memory_regions[boot_info->memory_region_count++];
        region->base = descriptor->PhysicalStart;
        region->length = length;
        region->type = normalize_efi_memory_type(descriptor->Type);
        region->reserved = 0;
    }

    if (boot_info->memory_region_count == 0)
    {
        serial_write("VANTAOS_UEFI_MEMORY_MAP_EMPTY_FAIL\n");
        return EFI_LOAD_ERROR;
    }

    boot_info->flags |= VANTA_BOOT_FLAG_MEMORY_MAP_VALID;
    return EFI_SUCCESS;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table)
{
    InitializeLib(image_handle, system_table);
    serial_initialize();
    serial_write("VANTAOS_UEFI_LOADER_START\n");

    EFI_STATUS status;
    EFI_FILE_PROTOCOL* unused_file = NULL;
    UINT8* kernel_file = NULL;
    UINTN kernel_file_size = 0;
    status = open_kernel_file(
        image_handle, &unused_file, &kernel_file, &kernel_file_size
    );
    if (EFI_ERROR(status))
    {
        serial_write("VANTAOS_UEFI_LOADER_FAIL\n");
        return status;
    }

    EFI_PHYSICAL_ADDRESS boot_info_address = 0xFFFFFFFFULL;
    UINTN boot_info_pages =
        (sizeof(vanta_boot_info_t) + (UINTN)EFI_PAGE_SIZE - 1U) /
        (UINTN)EFI_PAGE_SIZE;
    status = uefi_call_wrapper(
        BS->AllocatePages, 4, AllocateMaxAddress, EfiLoaderData,
        boot_info_pages, &boot_info_address
    );
    if (EFI_ERROR(status) || boot_info_address >= UEFI_MAX_PHYSICAL_ADDRESS)
    {
        uefi_call_wrapper(BS->FreePool, 1, kernel_file);
        serial_write("VANTAOS_UEFI_LOADER_FAIL\n");
        return EFI_OUT_OF_RESOURCES;
    }

    vanta_boot_info_t* boot_info =
        (vanta_boot_info_t*)(UINTN)boot_info_address;
    zero_bytes(boot_info, sizeof(*boot_info));
    boot_info->magic = VANTA_BOOT_INFO_MAGIC;
    boot_info->struct_size = sizeof(*boot_info);
    boot_info->version = VANTA_BOOT_INFO_VERSION;
    boot_info->source = VANTA_BOOT_SOURCE_UEFI;

    UINT64 framebuffer_size = 0;
    status = collect_graphics(boot_info, &framebuffer_size);
    if (EFI_ERROR(status))
    {
        uefi_call_wrapper(BS->FreePool, 1, kernel_file);
        serial_write("VANTAOS_UEFI_LOADER_FAIL\n");
        return status;
    }
    collect_acpi(boot_info);

    UINT64 image_start = 0, image_end = 0, entry_address = 0;
    status = load_elf64(
        kernel_file, kernel_file_size,
        &image_start, &image_end, &entry_address
    );
    uefi_call_wrapper(BS->FreePool, 1, kernel_file);
    if (EFI_ERROR(status))
    {
        serial_write("VANTAOS_UEFI_LOADER_FAIL\n");
        return status;
    }

    boot_info->kernel_image_start = image_start;
    boot_info->kernel_image_end = image_end;
    serial_write("VANTAOS_UEFI_ELF_LOADED\n");

    void* map_buffer = NULL;
    UINTN map_capacity = UEFI_MAX_MEMORY_MAP_SIZE;
    status = uefi_call_wrapper(
        BS->AllocatePool, 3, EfiLoaderData, map_capacity, &map_buffer
    );
    if (EFI_ERROR(status) || !map_buffer)
    {
        serial_write("VANTAOS_UEFI_LOADER_FAIL\n");
        return EFI_OUT_OF_RESOURCES;
    }

    /* GetMemoryMap and ExitBootServices are retried without allocating memory. */
    int exited = 0;
    for (UINTN attempt = 0; attempt < 3U; attempt++)
    {
        UINTN map_size = 0;
        UINTN map_key = 0;
        status = refresh_memory_map(
            boot_info, map_buffer, &map_size, map_capacity, &map_key
        );
        if (EFI_ERROR(status))
            break;

        status = uefi_call_wrapper(
            BS->ExitBootServices, 2, image_handle, map_key
        );
        if (!EFI_ERROR(status))
        {
            exited = 1;
            break;
        }

        if (status != EFI_INVALID_PARAMETER)
        {
            serial_write("VANTAOS_UEFI_EXIT_BOOT_SERVICES_FAIL\n");
            break;
        }
    }

    if (!exited)
    {
        /* Boot services may still be active if ExitBootServices never succeeded. */
        serial_write("VANTAOS_UEFI_LOADER_FAIL\n");
        return EFI_LOAD_ERROR;
    }

    __asm__ volatile ("cli");
    serial_write("VANTAOS_UEFI_EXIT_BOOT_SERVICES_OK\n");
    serial_write("VANTAOS_UEFI_KERNEL_HANDOFF\n");

    /* The kernel entry uses SysV x86-64, unlike UEFI service calls. */
    typedef void (*kernel_entry_t)(vanta_boot_info_t*);
    kernel_entry_t kernel_entry = (kernel_entry_t)(UINTN)entry_address;
    kernel_entry(boot_info);

    serial_write("VANTAOS_UEFI_KERNEL_RETURNED_FAIL\n");
    return EFI_ABORTED;
}
