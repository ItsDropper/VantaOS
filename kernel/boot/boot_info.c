#include "boot_info_multiboot.h"

#include <stddef.h>
#include <stdint.h>

extern char _start;
extern char end;

static void clear_bytes(void* pointer, size_t length)
{
    uint8_t* bytes = (uint8_t*)pointer;
    for (size_t i = 0; i < length; i++)
        bytes[i] = 0;
}

static uint32_t normalize_memory_type(uint32_t multiboot_type)
{
    switch (multiboot_type)
    {
        case MULTIBOOT_MEMORY_AVAILABLE:
            return VANTA_MEMORY_USABLE;
        case 3U:
            return VANTA_MEMORY_ACPI_RECLAIMABLE;
        case 4U:
            return VANTA_MEMORY_ACPI_NVS;
        case 5U:
            return VANTA_MEMORY_BAD;
        default:
            return VANTA_MEMORY_RESERVED;
    }
}

int vanta_boot_info_from_multiboot(
    const multiboot_info_t* mbd,
    vanta_boot_info_t* out_info
)
{
    if (!mbd || !out_info)
        return 0;

    clear_bytes(out_info, sizeof(*out_info));
    out_info->magic = VANTA_BOOT_INFO_MAGIC;
    out_info->struct_size = (uint32_t)sizeof(*out_info);
    out_info->version = VANTA_BOOT_INFO_VERSION;
    out_info->source = VANTA_BOOT_SOURCE_MULTIBOOT1;
    out_info->flags = VANTA_BOOT_FLAG_LEGACY_INFO_PRESENT;
    out_info->legacy_mem_lower_kib = mbd->mem_lower;
    out_info->legacy_mem_upper_kib = mbd->mem_upper;
    out_info->legacy_info_address = (uint64_t)(uintptr_t)mbd;
    out_info->legacy_info_length = sizeof(*mbd);
    out_info->legacy_mmap_address = mbd->mmap_addr;
    out_info->legacy_mmap_length = mbd->mmap_length;
    out_info->kernel_image_start = (uint64_t)(uintptr_t)&_start;
    out_info->kernel_image_end = (uint64_t)(uintptr_t)&end;

    if (mbd->flags & MULTIBOOT_INFO_FRAMEBUFFER)
    {
        out_info->framebuffer.address = mbd->framebuffer_addr;
        out_info->framebuffer.pitch = mbd->framebuffer_pitch;
        out_info->framebuffer.width = mbd->framebuffer_width;
        out_info->framebuffer.height = mbd->framebuffer_height;
        out_info->framebuffer.bits_per_pixel = mbd->framebuffer_bpp;
        out_info->framebuffer.type = mbd->framebuffer_type;
        out_info->framebuffer.red_position = mbd->framebuffer_red_position;
        out_info->framebuffer.red_mask_size = mbd->framebuffer_red_mask_size;
        out_info->framebuffer.green_position = mbd->framebuffer_green_position;
        out_info->framebuffer.green_mask_size = mbd->framebuffer_green_mask_size;
        out_info->framebuffer.blue_position = mbd->framebuffer_blue_position;
        out_info->framebuffer.blue_mask_size = mbd->framebuffer_blue_mask_size;

        if (mbd->framebuffer_addr != 0 &&
            mbd->framebuffer_pitch != 0 &&
            mbd->framebuffer_width != 0 &&
            mbd->framebuffer_height != 0)
        {
            out_info->flags |= VANTA_BOOT_FLAG_FRAMEBUFFER_VALID;
        }
    }

    if (!(mbd->flags & MULTIBOOT_INFO_MEM_MAP) ||
        mbd->mmap_addr == 0 ||
        mbd->mmap_length == 0)
    {
        return 1;
    }

    const uint64_t map_start = mbd->mmap_addr;
    const uint64_t map_end = map_start + (uint64_t)mbd->mmap_length;
    if (map_end <= map_start || map_end > 0x100000000ULL)
        return 0;

    uint64_t cursor = map_start;
    while (cursor < map_end)
    {
        const uint64_t remaining = map_end - cursor;
        if (remaining < sizeof(uint32_t))
            return 0;

        const multiboot_memory_map_t* entry =
            (const multiboot_memory_map_t*)(uintptr_t)cursor;
        const uint32_t payload_size = entry->size;

        if (payload_size < 20U ||
            (uint64_t)payload_size > remaining - sizeof(uint32_t))
        {
            return 0;
        }

        if (entry->len > UINT64_MAX - entry->addr)
            return 0;

        if (out_info->memory_region_count <
            VANTA_BOOT_INFO_MAX_MEMORY_REGIONS)
        {
            vanta_memory_region_t* region =
                &out_info->memory_regions[out_info->memory_region_count++];
            region->base = entry->addr;
            region->length = entry->len;
            region->type = normalize_memory_type(entry->type);
            region->reserved = 0;
        }
        else
        {
            out_info->flags |= VANTA_BOOT_FLAG_MEMORY_MAP_TRUNCATED;
        }

        cursor += sizeof(uint32_t) + (uint64_t)payload_size;
    }

    if (cursor != map_end)
        return 0;

    out_info->flags |= VANTA_BOOT_FLAG_MEMORY_MAP_VALID;
    return 1;
}
