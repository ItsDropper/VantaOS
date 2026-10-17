#include "ata.h"
#include "apic.h"
#include "smp.h"
#include "panic.h"
#include "gdt.h"
#include "heap.h"
#include "interrupts.h"
#include "boot_info_multiboot.h"
#include "os.h"
#include "paging.h"
#include "pci.h"
#include "nvme.h"
#include "net.h"
#include "e1000.h"
#include "net_arp.h"
#include "net_ipv4.h"
#include "net_tcp.h"
#include "xhci.h"
#include "pmm.h"
#include "serial.h"
#include "vanta_boot_info.h"
#ifdef VANTAOS_SELF_TEST
#include "selftest/selftest.h"
#endif

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

static void kernel_initialize(const vanta_boot_info_t* boot_info)
{
    gdt_initialize();
    boot_gdt = read_tsc();

    pmm_initialize(boot_info);
    interrupts_initialize();
    boot_interrupts = read_tsc();

    paging_initialize();
    apic_initialize();
    smp_init_result_t smp_result = smp_initialize();
    if (smp_result == SMP_INIT_FAILED)
    {
        serial_write("VANTAOS_SMP_INIT_FATAL_FAIL\n");
        kernel_panic_halt_all();
    }
#ifdef VANTAOS_SELF_TEST
    kernel_platform_selftest();
#endif
    pci_initialize();
    heap_initialize();
    net_initialize();
    if (!net_arp_initialize() || !net_ipv4_initialize() || !net_udp_initialize() ||
        !net_tcp_initialize())
        serial_write("VANTAOS_NETWORK_PROTOCOL_REGISTER_FAIL\n");
    if (!e1000_initialize())
        serial_write("VANTAOS_E1000_DRIVER_REGISTER_FAIL\n");
    (void)net_probe_pci_devices();
#ifdef VANTAOS_SELF_TEST
    network_framework_selftest();
#endif
    xhci_initialize();
    ata_initialize();
    nvme_initialize();
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
    kernel_boot_info_selftest(&normalized_boot_info);
#endif

    kernel_initialize(&normalized_boot_info);
    serial_write("VANTAOS: kernel initialized\n");
    kernel_start_os(mbd);
}

/*
 * UEFI enters in 64-bit mode and supplies the normalized contract directly.
 * Keep the legacy Multiboot entry intact; until all OS services are migrated,
 * synthesize only the framebuffer fields consumed by the existing OS startup.
 */
void kernel_main_uefi(vanta_boot_info_t* boot_info)
{
    serial_initialize();
    serial_write("VANTAOS_UEFI_KERNEL_ENTRY\n");

#define UEFI_BOOT_INFO_REJECT(marker) do { \
        serial_write(marker "\n"); \
        serial_write("VANTAOS_UEFI_BOOT_INFO_FAIL\n"); \
        for (;;) __asm__ volatile ("cli; hlt"); \
    } while (0)

    if (!boot_info)
        UEFI_BOOT_INFO_REJECT("VANTAOS_UEFI_BOOT_INFO_NULL_FAIL");

    if (boot_info->magic != VANTA_BOOT_INFO_MAGIC ||
        boot_info->struct_size < sizeof(vanta_boot_info_t) ||
        boot_info->version != VANTA_BOOT_INFO_VERSION)
        UEFI_BOOT_INFO_REJECT("VANTAOS_UEFI_BOOT_INFO_HEADER_FAIL");

    if (boot_info->source != VANTA_BOOT_SOURCE_UEFI)
        UEFI_BOOT_INFO_REJECT("VANTAOS_UEFI_BOOT_INFO_SOURCE_FAIL");

    if ((boot_info->flags & VANTA_BOOT_FLAG_MEMORY_MAP_VALID) == 0 ||
        (boot_info->flags & VANTA_BOOT_FLAG_MEMORY_MAP_TRUNCATED) != 0)
        UEFI_BOOT_INFO_REJECT("VANTAOS_UEFI_BOOT_INFO_MAP_FLAGS_FAIL");

    if (boot_info->memory_region_count == 0 ||
        boot_info->memory_region_count > VANTA_BOOT_INFO_MAX_MEMORY_REGIONS)
        UEFI_BOOT_INFO_REJECT("VANTAOS_UEFI_BOOT_INFO_REGION_COUNT_FAIL");

    if (boot_info->kernel_image_end <= boot_info->kernel_image_start)
        UEFI_BOOT_INFO_REJECT("VANTAOS_UEFI_BOOT_INFO_IMAGE_RANGE_FAIL");

#undef UEFI_BOOT_INFO_REJECT

    const uint8_t* source = (const uint8_t*)boot_info;
    uint8_t* destination = (uint8_t*)&normalized_boot_info;
    for (size_t i = 0; i < sizeof(normalized_boot_info); i++)
        destination[i] = source[i];

    static multiboot_info_t uefi_legacy_info;
    uint8_t* legacy_bytes = (uint8_t*)&uefi_legacy_info;
    for (size_t i = 0; i < sizeof(uefi_legacy_info); i++)
        legacy_bytes[i] = 0;

    if (normalized_boot_info.flags & VANTA_BOOT_FLAG_FRAMEBUFFER_VALID)
    {
        uefi_legacy_info.flags |= MULTIBOOT_INFO_FRAMEBUFFER;
        uefi_legacy_info.framebuffer_addr =
            normalized_boot_info.framebuffer.address;
        uefi_legacy_info.framebuffer_pitch =
            normalized_boot_info.framebuffer.pitch;
        uefi_legacy_info.framebuffer_width =
            normalized_boot_info.framebuffer.width;
        uefi_legacy_info.framebuffer_height =
            normalized_boot_info.framebuffer.height;
        uefi_legacy_info.framebuffer_bpp =
            normalized_boot_info.framebuffer.bits_per_pixel;
        uefi_legacy_info.framebuffer_type =
            normalized_boot_info.framebuffer.type;
        uefi_legacy_info.framebuffer_red_position =
            normalized_boot_info.framebuffer.red_position;
        uefi_legacy_info.framebuffer_red_mask_size =
            normalized_boot_info.framebuffer.red_mask_size;
        uefi_legacy_info.framebuffer_green_position =
            normalized_boot_info.framebuffer.green_position;
        uefi_legacy_info.framebuffer_green_mask_size =
            normalized_boot_info.framebuffer.green_mask_size;
        uefi_legacy_info.framebuffer_blue_position =
            normalized_boot_info.framebuffer.blue_position;
        uefi_legacy_info.framebuffer_blue_mask_size =
            normalized_boot_info.framebuffer.blue_mask_size;
    }

    boot_start = read_tsc();
    serial_write("VANTAOS_UEFI_BOOT_INFO_READY\n");

#ifdef VANTAOS_SELF_TEST
    kernel_boot_info_selftest(&normalized_boot_info);
#endif

    kernel_initialize(&normalized_boot_info);
    serial_write("VANTAOS: UEFI kernel initialized\n");
    kernel_start_os(&uefi_legacy_info);
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
