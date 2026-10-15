#include "ata.h"
#include "gdt.h"
#include "heap.h"
#include "interrupts.h"
#include "multiboot.h"
#include "os.h"
#include "paging.h"
#include "pci.h"
#include "pmm.h"
#include "serial.h"
#include "vanta_boot_info.h"

static inline unsigned long long read_tsc(void)
{
    unsigned int low;
    unsigned int high;

    __asm__ volatile (
        "rdtsc"
        : "=a"(low), "=d"(high)
    );

    return ((unsigned long long)high << 32) | low;
}

static unsigned long long boot_start;
static unsigned long long boot_gdt;
static unsigned long long boot_interrupts;
static vanta_boot_info_t normalized_boot_info;

#ifdef VANTAOS_SELF_TEST
static multiboot_memory_map_t boot_info_test_map[2];

static void boot_info_adapter_probe(void)
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

    ok = normalized_boot_info.magic == VANTA_BOOT_INFO_MAGIC &&
        normalized_boot_info.struct_size == sizeof(vanta_boot_info_t) &&
        normalized_boot_info.version == VANTA_BOOT_INFO_VERSION &&
        normalized_boot_info.source == VANTA_BOOT_SOURCE_MULTIBOOT1 &&
        (normalized_boot_info.flags & VANTA_BOOT_FLAG_MEMORY_MAP_VALID) != 0 &&
        normalized_boot_info.memory_region_count > 0U && ok;

    serial_write(ok ?
        "VANTAOS_BOOT_INFO_ADAPTER_OK\n" :
        "VANTAOS_BOOT_INFO_ADAPTER_FAIL\n");
}
#endif

static void kernel_initialize(multiboot_info_t* mbd)
{
    gdt_initialize();
    boot_gdt = read_tsc();

    pmm_initialize(mbd);
    interrupts_initialize();
    boot_interrupts = read_tsc();

    paging_initialize();
    pci_initialize();
    ata_initialize();
    heap_initialize();
}

static void kernel_start_os(multiboot_info_t* mbd)
{
    os_initialize(mbd);
    os_run();
}

void kernel_main(multiboot_info_t* mbd)
{
    serial_initialize();
    serial_write("VANTAOS: kernel entry\n");

    boot_start = read_tsc();

    if (!vanta_boot_info_from_multiboot(mbd, &normalized_boot_info))
    {
        serial_write("VANTAOS_BOOT_INFO_NORMALIZATION_FAIL\n");
    }
    else
    {
        serial_write("VANTAOS_BOOT_INFO_READY\n");
    }

#ifdef VANTAOS_SELF_TEST
    boot_info_adapter_probe();
#endif

    kernel_initialize(mbd);
    serial_write("VANTAOS: kernel initialized\n");
    kernel_start_os(mbd);
}

const vanta_boot_info_t* kernel_get_boot_info(void)
{
    return &normalized_boot_info;
}

unsigned long long kernel_boot_start(void)
{
    return boot_start;
}

unsigned long long kernel_boot_gdt(void)
{
    return boot_gdt;
}

unsigned long long kernel_boot_interrupts(void)
{
    return boot_interrupts;
}
