#include "graphics_internal.h"
#include "paging.h"
#include "pci.h"
#include "mouse.h"
#include <stdint.h>

#define BOCHS_VBE_INDEX 0x01CE
#define BOCHS_VBE_DATA  0x01CF

#define BOCHS_VBE_INDEX_ID        0x00
#define BOCHS_VBE_INDEX_XRES      0x01
#define BOCHS_VBE_INDEX_YRES      0x02
#define BOCHS_VBE_INDEX_BPP       0x03
#define BOCHS_VBE_INDEX_ENABLE    0x04
#define BOCHS_VBE_INDEX_VIRT_WIDTH  0x06
#define BOCHS_VBE_INDEX_VIRT_HEIGHT 0x07
#define BOCHS_VBE_INDEX_LFB       0x0D

#define BOCHS_VBE_ID 0xB0C5
#define BOCHS_VBE_ENABLE_LFB 0x0041

void bochs_vbe_write(uint16_t index, uint16_t value)
{
    __asm__ volatile (
        "outw %0, %1"
        :
        : "a"(index), "Nd"((uint16_t)BOCHS_VBE_INDEX)
    );

    __asm__ volatile (
        "outw %0, %1"
        :
        : "a"(value), "Nd"((uint16_t)BOCHS_VBE_DATA)
    );
}



static inline uint16_t bochs_vbe_read(uint16_t index)
{
    uint16_t value;

    __asm__ volatile (
        "outw %0, %1"
        :
        : "a"(index), "Nd"((uint16_t)BOCHS_VBE_INDEX)
    );

    __asm__ volatile (
        "inw %1, %0"
        : "=a"(value)
        : "Nd"((uint16_t)BOCHS_VBE_DATA)
    );

    return value;
}

uint32_t graphics_pack_color(uint32_t color)
{
    uint32_t red=(color>>16)&0xFF, green=(color>>8)&0xFF, blue=color&0xFF;
    if(red_mask_size<8) red>>=8-red_mask_size;
    if(green_mask_size<8) green>>=8-green_mask_size;
    if(blue_mask_size<8) blue>>=8-blue_mask_size;
    return (red<<red_position)|(green<<green_position)|(blue<<blue_position);
}

int graphics_initialize_bochs(void)
{
    if (bochs_vbe_read(BOCHS_VBE_INDEX_ID) != BOCHS_VBE_ID)
        return 0;

    bochs_vbe_write(BOCHS_VBE_INDEX_ENABLE, 0);

    bochs_vbe_write(BOCHS_VBE_INDEX_XRES, 1024);
    bochs_vbe_write(BOCHS_VBE_INDEX_YRES, 768);
    bochs_vbe_write(BOCHS_VBE_INDEX_BPP, 32);
    bochs_vbe_write(BOCHS_VBE_INDEX_VIRT_WIDTH, 1024);
    bochs_vbe_write(BOCHS_VBE_INDEX_VIRT_HEIGHT, 768);

    bochs_vbe_write(BOCHS_VBE_INDEX_ENABLE, BOCHS_VBE_ENABLE_LFB);

    /*
     * Bochs/QEMU VBE exposes the linear framebuffer at the standard
     * PCI/VBE LFB address 0xE0000000.  The VBE DISPI register at 0x0D
     * is X_OFFSET, not the framebuffer address, so reading it here
     * produces a bogus physical address and can corrupt rendering.
     */
    uint32_t physical = 0;

    for (int i = 0; i < pci_get_device_count(); i++)
    {
        const struct pci_device* device = pci_get_device(i);

        if (!device ||
            device->vendor_id != 0x1234 ||
            device->device_id != 0x1111 ||
            device->class_code != 0x03)
            continue;

        if (pci_get_bar0(device, &physical))
            break;
    }

    if (physical == 0)
        return 0;

    uint32_t offset = physical & 0xFFF;
    uint32_t first_page = physical & 0xFFFFF000U;
    uint32_t pitch = 1024 * 4;
    uint64_t bytes = (uint64_t)pitch * 768;

    uint32_t pages =
        (uint32_t)((offset + bytes + 4095) / 4096);

    if (pages == 0 || pages > GRAPHICS_MAX_PAGES)
        return 0;

    for (uint32_t i = 0; i < pages; i++)
    {
        if (!paging_map_page(
                GRAPHICS_VIRTUAL_BASE + i * 4096,
                first_page + i * 4096))
            return 0;
    }

    framebuffer = (uint8_t*)(GRAPHICS_VIRTUAL_BASE + offset);
    framebuffer_pitch = pitch;
    framebuffer_width = 1024;
    framebuffer_height = 768;

    red_position = 16;
    red_mask_size = 8;
    green_position = 8;
    green_mask_size = 8;
    blue_position = 0;
    blue_mask_size = 8;

    cursor_x = (int)framebuffer_width / 2;
    cursor_y = (int)framebuffer_height / 2;
    terminal_x = (int)framebuffer_width / 2 - 440;
    terminal_y = 64;
    terminal_restore_x = terminal_x;
    terminal_restore_y = terminal_y;
    terminal_dragging = 0;

    initialized = 1;
    return 1;
}

int graphics_initialize(multiboot_info_t* mbd)
{
    initialized = 0;

    /*
     * Prefer the QEMU/Bochs PCI framebuffer when the device is present.
     * Multiboot remains the fallback for other framebuffer-capable boots.
     */
    if (graphics_initialize_bochs())
        return 1;

    if (mbd &&
        (mbd->flags & MULTIBOOT_INFO_FRAMEBUFFER) &&
        mbd->framebuffer_addr <= 0xFFFFFFFFULL &&
        mbd->framebuffer_type == 1 &&
        mbd->framebuffer_bpp == 32 &&
        mbd->framebuffer_width != 0 &&
        mbd->framebuffer_height != 0)
    {
        uint32_t physical = (uint32_t)mbd->framebuffer_addr;
        uint32_t offset = physical & 0xFFFU;
        uint32_t first_page = physical & 0xFFFFF000U;

        uint64_t bytes =
            (uint64_t)mbd->framebuffer_pitch *
            mbd->framebuffer_height;

        uint32_t pages =
            (uint32_t)((offset + bytes + 4095U) / 4096U);

        if (pages != 0 && pages <= GRAPHICS_MAX_PAGES)
        {
            for (uint32_t i = 0; i < pages; i++)
            {
                if (!paging_map_page(
                        GRAPHICS_VIRTUAL_BASE + i * 4096U,
                        first_page + i * 4096U))
                    return 0;
            }

            framebuffer = (uint8_t*)(GRAPHICS_VIRTUAL_BASE + offset);
            framebuffer_pitch = mbd->framebuffer_pitch;
            framebuffer_width = mbd->framebuffer_width;
            framebuffer_height = mbd->framebuffer_height;

            red_position = mbd->framebuffer_red_position;
            red_mask_size = mbd->framebuffer_red_mask_size;
            green_position = mbd->framebuffer_green_position;
            green_mask_size = mbd->framebuffer_green_mask_size;
            blue_position = mbd->framebuffer_blue_position;
            blue_mask_size = mbd->framebuffer_blue_mask_size;

            cursor_x = (int)framebuffer_width / 2;
            cursor_y = (int)framebuffer_height / 2;
            terminal_x = (int)framebuffer_width / 2 - 440;
            terminal_y = 64;
            terminal_restore_x = terminal_x;
            terminal_restore_y = terminal_y;
            terminal_dragging = 0;

            initialized = 1;
            return 1;
        }
    }

    return 0;
}
