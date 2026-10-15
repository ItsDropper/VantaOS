#ifndef VANTA_BOOT_INFO_H
#define VANTA_BOOT_INFO_H

#include <stdint.h>

/*
 * Stable, loader-independent handoff contract.
 *
 * Bump VANTA_BOOT_INFO_VERSION when an incompatible layout/meaning change is
 * made. Loaders must set struct_size to sizeof(vanta_boot_info_t); consumers
 * must validate magic, version, and size before using the structure.
 */
#define VANTA_BOOT_INFO_MAGIC 0x56424F49U /* "VBOI" */
#define VANTA_BOOT_INFO_VERSION 1U
#define VANTA_BOOT_INFO_MAX_MEMORY_REGIONS 128U

#define VANTA_BOOT_FLAG_MEMORY_MAP_VALID      (1U << 0)
#define VANTA_BOOT_FLAG_MEMORY_MAP_TRUNCATED  (1U << 1)
#define VANTA_BOOT_FLAG_FRAMEBUFFER_VALID     (1U << 2)
#define VANTA_BOOT_FLAG_ACPI_RSDP_VALID       (1U << 3)
#define VANTA_BOOT_FLAG_LEGACY_INFO_PRESENT   (1U << 4)

typedef enum vanta_boot_source
{
    VANTA_BOOT_SOURCE_UNKNOWN = 0,
    VANTA_BOOT_SOURCE_MULTIBOOT1 = 1,
    VANTA_BOOT_SOURCE_UEFI = 2
} vanta_boot_source_t;

typedef enum vanta_memory_region_type
{
    VANTA_MEMORY_USABLE = 1,
    VANTA_MEMORY_RESERVED = 2,
    VANTA_MEMORY_ACPI_RECLAIMABLE = 3,
    VANTA_MEMORY_ACPI_NVS = 4,
    VANTA_MEMORY_BAD = 5,
    VANTA_MEMORY_BOOTLOADER_RECLAIMABLE = 6
} vanta_memory_region_type_t;

typedef struct vanta_memory_region
{
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} vanta_memory_region_t;

typedef struct vanta_framebuffer_info
{
    uint64_t address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bits_per_pixel;
    uint8_t type;
    uint8_t red_position;
    uint8_t red_mask_size;
    uint8_t green_position;
    uint8_t green_mask_size;
    uint8_t blue_position;
    uint8_t blue_mask_size;
} vanta_framebuffer_info_t;

typedef struct vanta_boot_info
{
    uint32_t magic;
    uint32_t struct_size;
    uint32_t version;
    uint32_t source;
    uint32_t flags;
    uint32_t memory_region_count;
    uint32_t legacy_mem_lower_kib;
    uint32_t legacy_mem_upper_kib;

    uint64_t kernel_image_start;
    uint64_t kernel_image_end;
    uint64_t acpi_rsdp_address;
    uint64_t loader_reclaimable_start;
    uint64_t loader_reclaimable_end;
    uint64_t legacy_info_address;

    vanta_framebuffer_info_t framebuffer;
    vanta_memory_region_t memory_regions[VANTA_BOOT_INFO_MAX_MEMORY_REGIONS];

    /* Optional legacy metadata ranges retained during the GRUB transition. */
    uint64_t legacy_info_length;
    uint64_t legacy_mmap_address;
    uint64_t legacy_mmap_length;
} vanta_boot_info_t;

/* Kernel-owned normalized handoff retained for the kernel lifetime. */
const vanta_boot_info_t* kernel_get_boot_info(void);

#endif
