#include "rtl8188eu.h"

#include "xhci.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define RTL8188EU_VENDOR_ID 0x0BDAU
#define RTL8188EU_PRODUCT_8179 0x8179U
#define RTL8188EU_PRODUCT_0179 0x0179U
#define RTL8188EU_USB_VENDOR_REQUEST 0x05U
#define RTL8188EU_USB_CLASS_VENDOR 0xFFU
#define USB_DESC_INTERFACE 4U
#define USB_DESC_ENDPOINT 5U
#define USB_TRANSFER_BULK 2U
#define RTL8188EU_REG_SYS_CFG 0x00F0U

typedef struct {
    uint8_t found;
    uint8_t interface_number;
    uint8_t bulk_in;
    uint8_t bulk_out;
    uint16_t bulk_in_packet;
    uint16_t bulk_out_packet;
} rtl8188eu_endpoints_t;

static uint32_t rtl_slot;
static uint8_t rtl_bound;
static uint8_t rtl_interface;
static uint8_t rtl_bulk_in;
static uint8_t rtl_bulk_out;
static uint16_t rtl_bulk_in_packet;
static uint16_t rtl_bulk_out_packet;
static uint8_t rtl_register_buffer[4] __attribute__((aligned(64)));

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static int rtl_find_endpoints(const uint8_t *configuration, size_t length,
                              rtl8188eu_endpoints_t *found)
{
    size_t offset = 0;
    uint8_t current_interface = 0;
    uint8_t current_is_vendor = 0;
    rtl8188eu_endpoints_t candidate = {0};

    if (!configuration || !found || length < 9U ||
        configuration[0] < 9U || configuration[1] != 2U)
        return 0;

    while (offset + 2U <= length) {
        uint8_t descriptor_length = configuration[offset];
        uint8_t descriptor_type = configuration[offset + 1U];
        if (descriptor_length < 2U || descriptor_length > length - offset)
            return 0;

        if (descriptor_type == USB_DESC_INTERFACE) {
            if (descriptor_length < 9U)
                return 0;
            /* Commit a previous candidate only when both directions exist. */
            if (candidate.bulk_in && candidate.bulk_out) {
                *found = candidate;
                return 1;
            }
            current_interface = configuration[offset + 2U];
            current_is_vendor = configuration[offset + 5U] ==
                                RTL8188EU_USB_CLASS_VENDOR;
            candidate = (rtl8188eu_endpoints_t){0};
            candidate.interface_number = current_interface;
        } else if (descriptor_type == USB_DESC_ENDPOINT &&
                   descriptor_length >= 7U && current_is_vendor) {
            uint8_t address = configuration[offset + 2U];
            uint8_t attributes = configuration[offset + 3U];
            uint16_t raw_packet = get_le16(configuration + offset + 4U);
            uint16_t packet = raw_packet & 0x07FFU;

            /* Bulk endpoint wMaxPacketSize has no high-bandwidth multiplier. */
            if ((attributes & 0x03U) != USB_TRANSFER_BULK ||
                (raw_packet & 0xF800U) != 0U || packet == 0U)
                continue;

            if ((address & 0x80U) != 0U && !candidate.bulk_in) {
                candidate.bulk_in = address;
                candidate.bulk_in_packet = packet;
            } else if ((address & 0x80U) == 0U && !candidate.bulk_out) {
                candidate.bulk_out = address;
                candidate.bulk_out_packet = packet;
            }
        }
        offset += descriptor_length;
    }

    if (candidate.bulk_in && candidate.bulk_out) {
        *found = candidate;
        return 1;
    }
    return 0;
}

/*
 * RTL vendor control transfers use request 0x05 with the register address in
 * wValue. Keep all control buffers static and aligned for the xHCI DMA path.
 */
static int rtl_reg_read(uint16_t address, uint8_t *output, uint16_t length)
{
    uint8_t setup[8] = {
        0xC0U, RTL8188EU_USB_VENDOR_REQUEST,
        (uint8_t)address, (uint8_t)(address >> 8),
        0U, 0U, (uint8_t)length, (uint8_t)(length >> 8)
    };
    if (!output || length == 0U || length > sizeof(rtl_register_buffer))
        return 0;
    if (!xhci_control_transfer(rtl_slot, setup, rtl_register_buffer, length))
        return 0;
    for (uint16_t i = 0; i < length; ++i)
        output[i] = rtl_register_buffer[i];
    return 1;
}

static int rtl_reg_write(uint16_t address, const uint8_t *input,
                         uint16_t length)
{
    uint8_t setup[8] = {
        0x40U, RTL8188EU_USB_VENDOR_REQUEST,
        (uint8_t)address, (uint8_t)(address >> 8),
        0U, 0U, (uint8_t)length, (uint8_t)(length >> 8)
    };
    if (!input || length == 0U || length > sizeof(rtl_register_buffer))
        return 0;
    for (uint16_t i = 0; i < length; ++i)
        rtl_register_buffer[i] = input[i];
    return xhci_control_transfer(rtl_slot, setup, rtl_register_buffer, length);
}

static int rtl_reg_read32(uint16_t address, uint32_t *value)
{
    uint8_t bytes[4];
    if (!value || !rtl_reg_read(address, bytes, sizeof(bytes)))
        return 0;
    *value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
             ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    return 1;
}

int rtl8188eu_probe(uint32_t slot_id, uint16_t vendor_id, uint16_t device_id,
                    const uint8_t *configuration, size_t length)
{
    rtl8188eu_endpoints_t endpoints;
    uint32_t system_config;

    if (rtl_bound || vendor_id != RTL8188EU_VENDOR_ID ||
        (device_id != RTL8188EU_PRODUCT_8179 &&
         device_id != RTL8188EU_PRODUCT_0179) ||
        slot_id == 0U || !xhci_controller_online())
        return 0;
    if (!rtl_find_endpoints(configuration, length, &endpoints))
        return 0;

    rtl_slot = slot_id;
    if (!xhci_configure_bulk_endpoint(slot_id, endpoints.bulk_in,
                                      endpoints.bulk_in_packet) ||
        !xhci_configure_bulk_endpoint(slot_id, endpoints.bulk_out,
                                      endpoints.bulk_out_packet)) {
        (void)xhci_disable_device(slot_id);
        rtl_slot = 0;
        return 0;
    }

    rtl_interface = endpoints.interface_number;
    rtl_bulk_in = endpoints.bulk_in;
    rtl_bulk_out = endpoints.bulk_out;
    rtl_bulk_in_packet = endpoints.bulk_in_packet;
    rtl_bulk_out_packet = endpoints.bulk_out_packet;

    /*
     * A successful register read verifies that the USB control path reaches
     * the Realtek device. It is not proof that firmware/radio are operational.
     */
    if (!rtl_reg_read32(RTL8188EU_REG_SYS_CFG, &system_config)) {
        (void)xhci_disable_device(slot_id);
        rtl_slot = 0;
        return 0;
    }

    rtl_bound = 1U;
    serial_write("VANTAOS_RTL8188EU_USB_TRANSPORT_READY\n");
    return 1;
}
