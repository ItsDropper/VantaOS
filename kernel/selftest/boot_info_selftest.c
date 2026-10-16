#include "selftest.h"

#include <stdint.h>
#include <stddef.h>
#include "boot_info_multiboot.h"
#include "multiboot.h"
#include "pmm.h"
#include "serial.h"
#include "vanta_boot_info.h"

#ifdef VANTAOS_SELF_TEST
#ifdef VANTAOS_SELF_TEST
static multiboot_memory_map_t boot_info_test_map[2];

static void boot_info_adapter_probe(const vanta_boot_info_t* normalized)
{
    static multiboot_info_t synthetic;
    vanta_boot_info_t converted;

    boot_info_test_map[0] = (multiboot_memory_map_t){
        .size = 20U,
        .addr = 0x100000ULL,
        .len = 0x3F00000ULL,
        .type = MULTIBOOT_MEMORY_AVAILABLE
    };
    boot_info_test_map[1] = (multiboot_memory_map_t){
        .size = 20U,
        .addr = 0x4000000ULL,
        .len = 0x100000ULL,
        .type = MULTIBOOT_MEMORY_RESERVED
    };

    synthetic.flags =
        MULTIBOOT_INFO_MEM_MAP | MULTIBOOT_INFO_FRAMEBUFFER;
    synthetic.mmap_addr = (uint32_t)(uintptr_t)&boot_info_test_map[0];
    synthetic.mmap_length = (uint32_t)sizeof(boot_info_test_map);
    synthetic.framebuffer_addr = 0xE0000000ULL;
    synthetic.framebuffer_pitch = 4096U;
    synthetic.framebuffer_width = 1024U;
    synthetic.framebuffer_height = 768U;
    synthetic.framebuffer_bpp = 32U;
    synthetic.framebuffer_type = 1U;

    int ok = vanta_boot_info_from_multiboot(&synthetic, &converted);
    ok = ok &&
        converted.magic == VANTA_BOOT_INFO_MAGIC &&
        converted.struct_size == sizeof(vanta_boot_info_t) &&
        converted.version == VANTA_BOOT_INFO_VERSION &&
        converted.source == VANTA_BOOT_SOURCE_MULTIBOOT1 &&
        (converted.flags & VANTA_BOOT_FLAG_MEMORY_MAP_VALID) != 0 &&
        (converted.flags & VANTA_BOOT_FLAG_FRAMEBUFFER_VALID) != 0 &&
        converted.memory_region_count == 2U &&
        converted.memory_regions[0].base == 0x100000ULL &&
        converted.memory_regions[0].length == 0x3F00000ULL &&
        converted.memory_regions[0].type == VANTA_MEMORY_USABLE &&
        converted.memory_regions[1].base == 0x4000000ULL &&
        converted.memory_regions[1].type == VANTA_MEMORY_RESERVED &&
        converted.framebuffer.address == 0xE0000000ULL &&
        converted.framebuffer.width == 1024U &&
        converted.framebuffer.height == 768U &&
        converted.legacy_info_address == (uint64_t)(uintptr_t)&synthetic &&
        converted.kernel_image_start < converted.kernel_image_end;

    /* Malformed map entries must be rejected, not partially normalized. */
    boot_info_test_map[0].size = 0U;
    ok = !vanta_boot_info_from_multiboot(&synthetic, &converted) && ok;
    boot_info_test_map[0].size = 20U;
    boot_info_test_map[0].addr = UINT64_MAX - 7U;
    boot_info_test_map[0].len = 16U;
    ok = !vanta_boot_info_from_multiboot(&synthetic, &converted) && ok;
    boot_info_test_map[0].addr = 0x100000ULL;
    boot_info_test_map[0].len = 0x3F00000ULL;
    ok = !vanta_boot_info_from_multiboot(NULL, &converted) && ok;
    ok = !vanta_boot_info_from_multiboot(&synthetic, NULL) && ok;

    ok = normalized->magic == VANTA_BOOT_INFO_MAGIC &&
        normalized->struct_size == sizeof(vanta_boot_info_t) &&
        normalized->version == VANTA_BOOT_INFO_VERSION &&
        (normalized->source == VANTA_BOOT_SOURCE_MULTIBOOT1 ||
         normalized_boot_info.source == VANTA_BOOT_SOURCE_UEFI) &&
        (normalized->flags & VANTA_BOOT_FLAG_MEMORY_MAP_VALID) != 0 &&
        normalized->memory_region_count > 0U && ok;

    serial_write(ok ?
        "VANTAOS_BOOT_INFO_ADAPTER_OK\n" :
        "VANTAOS_BOOT_INFO_ADAPTER_FAIL\n");
}
static void pmm_boot_info_probe(const vanta_boot_info_t* normalized)
{
    static vanta_boot_info_t synthetic;
    synthetic.magic = VANTA_BOOT_INFO_MAGIC;
    synthetic.struct_size = sizeof(synthetic);
    synthetic.version = VANTA_BOOT_INFO_VERSION;
    synthetic.source = VANTA_BOOT_SOURCE_MULTIBOOT1;
    synthetic.flags = VANTA_BOOT_FLAG_MEMORY_MAP_VALID |
        VANTA_BOOT_FLAG_LEGACY_INFO_PRESENT;
    synthetic.memory_region_count = 2U;
    synthetic.kernel_image_start = 0x100000ULL;
    synthetic.kernel_image_end = 0x101000ULL;
    synthetic.legacy_info_address = 0x104000ULL;
    synthetic.legacy_info_length = 0x1000ULL;
    synthetic.legacy_mmap_address = 0x105000ULL;
    synthetic.legacy_mmap_length = 0x1000ULL;
    synthetic.memory_regions[0] = (vanta_memory_region_t){
        .base = 0x100000ULL,
        .length = 0x6000ULL,
        .type = VANTA_MEMORY_USABLE
    };
    synthetic.memory_regions[1] = (vanta_memory_region_t){
        .base = 0x102000ULL,
        .length = 0x1000ULL,
        .type = VANTA_MEMORY_RESERVED
    };

    pmm_initialize(&synthetic);
    int ok = pmm_is_initialized() &&
        pmm_get_total_blocks() == 0x106000ULL / PAGE_SIZE &&
        pmm_get_free_blocks() == 2U;

    synthetic.struct_size--;
    pmm_initialize(&synthetic);
    ok = !pmm_is_initialized() && ok;
    synthetic.struct_size = sizeof(synthetic);
    pmm_initialize(&synthetic);
    ok = pmm_is_initialized() && pmm_get_free_blocks() == 2U && ok;

    void* first = pmm_alloc_block();
    void* second = pmm_alloc_block();
    void* third = pmm_alloc_block();

    ok = first == (void*)(uintptr_t)0x101000ULL && ok;
    ok = second == (void*)(uintptr_t)0x103000ULL && ok;
    ok = third == NULL && ok;

    pmm_free_block(first);
    pmm_free_block(second);
    ok = pmm_get_free_blocks() == 2U && ok;

    synthetic.memory_regions[0].base = UINT64_MAX - 7U;
    synthetic.memory_regions[0].length = 16U;
    pmm_initialize(&synthetic);
    ok = !pmm_is_initialized() && ok;

    pmm_initialize(NULL);
    ok = !pmm_is_initialized() && pmm_alloc_block() == NULL && ok;

    /* Restore the real boot map before the normal kernel initialization. */
    pmm_initialize(normalized);
    ok = pmm_is_initialized() && ok;

    serial_write(ok ?
        "VANTAOS_PMM_BOOT_INFO_OK\n" :
        "VANTAOS_PMM_BOOT_INFO_FAIL\n");
}

void kernel_boot_info_selftest(const vanta_boot_info_t* normalized)
{
    boot_info_adapter_probe(normalized);
    pmm_boot_info_probe(normalized);
}
#endif
