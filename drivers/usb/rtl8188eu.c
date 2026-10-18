#include "rtl8188eu.h"

#include "xhci.h"
#include "serial.h"
#include "fat32.h"
#include "net_wifi_80211.h"

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
#define RTL8188EU_REG_SYS_FUNC 0x0002U
#define RTL8188EU_REG_RSV_CTRL 0x001CU
#define RTL8188EU_REG_MCU_FW_DL 0x0080U
#define RTL8188EU_REG_FW_START 0x1000U
#define RTL8188EU_FW_HEADER_SIZE 32U
#define RTL8188EU_FW_MAX_SIZE 32768U /* Eight 4-KiB pages, selected by 3 page bits. */
#define RTL8188EU_FW_PAGE_SIZE 4096U
#define RTL8188EU_FW_WRITE_CHUNK 196U
#define RTL8188EU_FWDL_ENABLE 0x01U
#define RTL8188EU_FWDL_READY 0x02U
#define RTL8188EU_FWDL_CHECKSUM 0x04U
#define RTL8188EU_FWDL_RAM_SELECT 0x80U
#define RTL8188EU_FW_INIT_READY 0x40U

typedef struct {
    uint8_t found;
    uint8_t interface_number;
    uint8_t bulk_in;
    uint8_t bulk_out;
    uint16_t bulk_in_packet;
    uint16_t bulk_out_packet;
} rtl8188eu_endpoints_t;

static uint32_t rtl_slot;
static uint32_t rtl_port;
static uint8_t rtl_bound;
static uint8_t rtl_bulk_in;
static uint8_t rtl_bulk_out;
static uint8_t rtl_register_buffer[RTL8188EU_FW_WRITE_CHUNK] __attribute__((aligned(64)));
static uint8_t rtl_firmware[RTL8188EU_FW_MAX_SIZE + 1U] __attribute__((aligned(64)));
static uint8_t rtl_firmware_attempted;
static uint8_t rtl_firmware_ready;
/*
 * Firmware readiness is not radio readiness. No independently verified
 * MAC/BB/RF/eFuse/calibration sequence exists yet, so keep TX disabled until
 * that sequence is implemented and explicitly completes.
 */
static uint8_t rtl_radio_ready;
static uint8_t rtl_rx_armed;
static uint8_t rtl_rx_buffer[4096] __attribute__((aligned(64)));
#define RTL8188EU_TX_DESC_SIZE 32U
#define RTL8188EU_TX_FRAME_MAX 2304U
static uint8_t rtl_tx_buffer[RTL8188EU_TX_DESC_SIZE + RTL8188EU_TX_FRAME_MAX]
    __attribute__((aligned(64)));
static net_wifi_scan_result_t rtl_passive_results[NET_WIFI_MAX_SCAN_RESULTS];
static size_t rtl_passive_result_count;
static int rtl_reg_read16(uint16_t address, uint16_t *value);
static int rtl_reg_read32(uint16_t address, uint32_t *value);

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
        configuration[0] < 9U || configuration[1] != 2U ||
        configuration[4] == 0U)
        return 0;

    /*
     * Do not parse bytes beyond the configuration descriptor's declared
     * extent, even when the caller supplies a larger DMA buffer. Conversely,
     * reject a truncated descriptor instead of silently accepting a prefix.
     */
    size_t total_length = get_le16(configuration + 2U);
    if (total_length < configuration[0] || total_length > length)
        return 0;

    while (offset + 2U <= total_length) {
        uint8_t descriptor_length = configuration[offset];
        uint8_t descriptor_type = configuration[offset + 1U];
        if (descriptor_length < 2U || descriptor_length > total_length - offset)
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
            /* Only the default alternate setting is configured here. */
            current_is_vendor = configuration[offset + 3U] == 0U &&
                                configuration[offset + 5U] ==
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
            /* Endpoint address bits 4:6 are reserved; endpoint zero is
             * the control pipe and cannot be selected as a bulk endpoint. */
            if ((address & 0x70U) != 0U || (address & 0x0FU) == 0U ||
                (attributes & 0x03U) != USB_TRANSFER_BULK ||
                (raw_packet & 0xF800U) != 0U || packet == 0U) {
                /* Reject malformed endpoint descriptors; never retry this offset. */
                return 0;
            }

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

static int rtl_reg_read8(uint16_t address, uint8_t *value)
{
    return value && rtl_reg_read(address, value, 1U);
}

static int rtl_reg_write8(uint16_t address, uint8_t value)
{
    return rtl_reg_write(address, &value, 1U);
}

static int rtl_reg_write16(uint16_t address, uint16_t value)
{
    uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    return rtl_reg_write(address, bytes, sizeof(bytes));
}

static int rtl_reg_write32(uint16_t address, uint32_t value)
{
    uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8),
                        (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    return rtl_reg_write(address, bytes, sizeof(bytes));
}

static int rtl_name_equal(const char *left, const char *right)
{
    while (*left && *right) {
        char a = *left++, b = *right++;
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
        if (a != b) return 0;
    }
    return *left == 0 && *right == 0;
}

/* Firmware is supplied by the system integrator, not embedded in the kernel. */
static int rtl_load_firmware_file(uint32_t *firmware_size)
{
    fat32_dirent_t root[64];
    fat32_dirent_t system_entries[128];
    uint32_t system_cluster = 0U;
    int root_count, system_count;
    if (!firmware_size || !fat32_is_mounted())
        return 0;

    root_count = fat32_list_directory(fat32_root_cluster(), root, 64U);
    if (root_count < 0 || root_count >= 64)
        return 0;
    for (int i = 0; i < root_count; ++i) {
        if (root[i].directory && rtl_name_equal(root[i].name, "SYSTEM")) {
            system_cluster = root[i].first_cluster;
            break;
        }
    }
    if (system_cluster < 2U)
        return 0;

    system_count = fat32_list_directory(system_cluster, system_entries, 128U);
    if (system_count < 0 || system_count >= 128)
        return 0;
    for (int i = 0; i < system_count; ++i) {
        fat32_dirent_t *entry = &system_entries[i];
        if (entry->directory ||
            !rtl_name_equal(entry->name, "RTL8188EU.BIN"))
            continue;
        if (entry->size <= RTL8188EU_FW_HEADER_SIZE ||
            entry->size > RTL8188EU_FW_MAX_SIZE ||
            fat32_read_file(entry->first_cluster, entry->size,
                            (char *)rtl_firmware, sizeof(rtl_firmware)) != (int)entry->size)
            return 0;
        uint16_t signature = (uint16_t)rtl_firmware[0] |
                             ((uint16_t)rtl_firmware[1] << 8);
        if ((signature & 0xFFF0U) != 0x88E0U)
            return 0;
        *firmware_size = entry->size - RTL8188EU_FW_HEADER_SIZE;
        /* Keep only the validated payload; never upload the 32-byte header. */
        for (uint32_t j = 0; j < *firmware_size; ++j)
            rtl_firmware[j] = rtl_firmware[j + RTL8188EU_FW_HEADER_SIZE];
        return 1;
    }
    return 0;
}

static int rtl_download_firmware(void)
{
    uint32_t size = 0U;
    uint8_t value8;
    uint32_t value32;
    if (!rtl_load_firmware_file(&size) || size == 0U ||
        size > RTL8188EU_FW_MAX_SIZE - RTL8188EU_FW_HEADER_SIZE)
        return 0;

    /* Follow the RTL8xxxU firmware download protocol with bounded polling. */
    if (!rtl_reg_read8(RTL8188EU_REG_MCU_FW_DL, &value8))
        return 0;
    if (value8 & RTL8188EU_FWDL_RAM_SELECT) {
        if (!rtl_reg_write8(RTL8188EU_REG_MCU_FW_DL, 0U) ||
            !rtl_reg_read8(RTL8188EU_REG_SYS_FUNC, &value8) ||
            !rtl_reg_write8(RTL8188EU_REG_SYS_FUNC,
                            (uint8_t)(value8 & ~0x04U)) ||
            !rtl_reg_write8(RTL8188EU_REG_SYS_FUNC,
                            (uint8_t)(value8 | 0x04U)))
            return 0;
    }

    /*
     * Enable MCU download and clear the stale checksum-report state before
     * writing any payload. FWDL_CHKSUM is a device-reported status bit; writing
     * it as if it were a command can make software mistake an unverified image
     * for a completed download.
     */
    if (!rtl_reg_read8(RTL8188EU_REG_MCU_FW_DL, &value8) ||
        !rtl_reg_write8(RTL8188EU_REG_MCU_FW_DL,
                        (uint8_t)(value8 | RTL8188EU_FWDL_ENABLE)) ||
        !rtl_reg_read32(RTL8188EU_REG_MCU_FW_DL, &value32) ||
        !rtl_reg_write32(RTL8188EU_REG_MCU_FW_DL, value32 & ~(1U << 19)))
        return 0;

    uint32_t offset = 0U;
    uint8_t page = 0U;
    /* MCUFWDL page selector has three bits; never wrap into page zero. */
    if (size > 8U * RTL8188EU_FW_PAGE_SIZE)
        goto abort_download;
    while (offset < size) {
        uint32_t page_remaining = RTL8188EU_FW_PAGE_SIZE -
                                  (offset % RTL8188EU_FW_PAGE_SIZE);
        uint32_t amount = size - offset;
        if (amount > page_remaining) amount = page_remaining;
        if (!rtl_reg_read8(RTL8188EU_REG_MCU_FW_DL + 2U, &value8) ||
            !rtl_reg_write8(RTL8188EU_REG_MCU_FW_DL + 2U,
                            (uint8_t)((value8 & 0xF8U) | page)))
            goto abort_download;
        uint32_t chunk_offset = 0U;
        while (chunk_offset < amount) {
            uint32_t chunk = amount - chunk_offset;
            if (chunk > RTL8188EU_FW_WRITE_CHUNK)
                chunk = RTL8188EU_FW_WRITE_CHUNK;
            if (!rtl_reg_write(RTL8188EU_REG_FW_START,
                               rtl_firmware + offset + chunk_offset,
                               (uint16_t)chunk))
                goto abort_download;
            chunk_offset += chunk;
        }
        offset += amount;
        page++;
    }

    uint16_t control16;
    uint16_t system_function;
    uint8_t reserved;

    if (!rtl_reg_read16(RTL8188EU_REG_MCU_FW_DL, &control16) ||
        !rtl_reg_write16(RTL8188EU_REG_MCU_FW_DL,
                         (uint16_t)(control16 & ~RTL8188EU_FWDL_ENABLE)))
        goto abort_download;

    for (uint32_t retry = 0; retry < 10000U; ++retry) {
        if (!rtl_reg_read32(RTL8188EU_REG_MCU_FW_DL, &value32))
            goto abort_download;
        if (value32 & RTL8188EU_FWDL_CHECKSUM)
            break;
        if (retry == 9999U)
            goto abort_download;
    }

    value32 |= RTL8188EU_FWDL_READY;
    value32 &= ~RTL8188EU_FW_INIT_READY;
    if (!rtl_reg_write32(RTL8188EU_REG_MCU_FW_DL, value32) ||
        !rtl_reg_read8(RTL8188EU_REG_RSV_CTRL + 1U, &reserved) ||
        !rtl_reg_write8(RTL8188EU_REG_RSV_CTRL + 1U,
                        (uint8_t)(reserved & ~1U)) ||
        !rtl_reg_read16(RTL8188EU_REG_SYS_FUNC, &system_function) ||
        !rtl_reg_write16(RTL8188EU_REG_SYS_FUNC,
                         (uint16_t)(system_function & ~0x0004U)) ||
        !rtl_reg_read8(RTL8188EU_REG_RSV_CTRL + 1U, &reserved) ||
        !rtl_reg_write8(RTL8188EU_REG_RSV_CTRL + 1U,
                        (uint8_t)(reserved | 1U)) ||
        !rtl_reg_write16(RTL8188EU_REG_SYS_FUNC,
                         (uint16_t)(system_function | 0x0004U)))
        goto abort_download;

    for (uint32_t retry = 0; retry < 10000U; ++retry) {
        if (!rtl_reg_read32(RTL8188EU_REG_MCU_FW_DL, &value32))
            goto abort_download;
        if (value32 & RTL8188EU_FW_INIT_READY) {
            rtl_firmware_ready = 1U;
            return 1;
        }
    }

abort_download:
    if (rtl_reg_read8(RTL8188EU_REG_MCU_FW_DL, &value8))
        (void)rtl_reg_write8(RTL8188EU_REG_MCU_FW_DL,
                             (uint8_t)(value8 & ~RTL8188EU_FWDL_ENABLE));
    return 0;
}

static int rtl_reg_read16(uint16_t address, uint16_t *value)
{
    uint8_t bytes[2];
    if (!value || !rtl_reg_read(address, bytes, sizeof(bytes)))
        return 0;
    *value = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
    return 1;
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

int rtl8188eu_probe(uint32_t slot_id, uint32_t port, uint16_t vendor_id, uint16_t device_id,
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
    rtl_port = port;
    rtl_bulk_in = endpoints.bulk_in;
    rtl_bulk_out = endpoints.bulk_out;
    if (!xhci_configure_bulk_endpoint(slot_id, endpoints.bulk_in,
                                      endpoints.bulk_in_packet) ||
        !xhci_configure_bulk_endpoint(slot_id, endpoints.bulk_out,
                                      endpoints.bulk_out_packet)) {
        (void)xhci_disable_device(slot_id);
        rtl_slot = 0;
        rtl_port = 0;
        return 0;
    }


    /*
     * A successful register read verifies that the USB control path reaches
     * the Realtek device. It is not proof that firmware/radio are operational.
     */
    if (!rtl_reg_read32(RTL8188EU_REG_SYS_CFG, &system_config)) {
        (void)xhci_disable_device(slot_id);
        rtl_slot = 0;
        return 0;
    }

    (void)system_config;
    rtl_bound = 1U;
    serial_write("VANTAOS_RTL8188EU_USB_TRANSPORT_READY\n");
    return 1;
}

static void rtl_cache_beacon(const uint8_t *frame, size_t length)
{
    net_wifi_scan_result_t result;
    if (!net_wifi_80211_parse_scan_frame(frame, length, -127, &result))
        return;

    for (size_t i = 0; i < rtl_passive_result_count; ++i) {
        int same = 1;
        for (size_t j = 0; j < NET_WIFI_BSSID_SIZE; ++j)
            if (rtl_passive_results[i].bssid[j] != result.bssid[j])
                same = 0;
        if (same) {
            rtl_passive_results[i] = result;
            return;
        }
    }
    if (rtl_passive_result_count < NET_WIFI_MAX_SCAN_RESULTS)
        rtl_passive_results[rtl_passive_result_count++] = result;
}

static void rtl_process_rx_packet(const uint8_t *packet, size_t length)
{
    /*
     * USB bulk-IN may return an aggregate of RX descriptors and 802.11
     * frames, not exactly one packet. Each descriptor is 24 bytes; the next
     * packet starts at the next 8-byte boundary after the current frame.
     * Validate every offset against the actual transfer length before
     * inspecting a frame, and stop at the first malformed descriptor.
     */
    size_t cursor = 0U;
    if (!packet)
        return;

    while (length - cursor >= 24U) {
        const uint8_t *rx = packet + cursor;
        uint32_t descriptor = (uint32_t)rx[0] |
                              ((uint32_t)rx[1] << 8) |
                              ((uint32_t)rx[2] << 16) |
                              ((uint32_t)rx[3] << 24);
        uint32_t frame_length = descriptor & 0x3FFFU;
        uint32_t driver_info = ((descriptor >> 16) & 0x0FU) * 8U;
        uint32_t shift = (descriptor >> 24) & 0x03U;
        size_t frame_offset = 24U + driver_info + shift;

        /* All-zero tail padding is not a valid RX descriptor. */
        if (descriptor == 0U)
            break;
        if ((descriptor & 0xC000U) != 0U || frame_length == 0U ||
            frame_offset > length - cursor ||
            frame_length > length - cursor - frame_offset)
            break;

        rtl_cache_beacon(rx + frame_offset, frame_length);

        size_t packet_end = cursor + frame_offset + frame_length;
        if (packet_end >= length)
            break;
        /* RX packet boundaries are padded to an 8-byte alignment. */
        size_t next = (packet_end + 7U) & ~(size_t)7U;
        if (next <= cursor || next > length)
            break;
        cursor = next;
    }
}

/*
 * USB TX descriptors for the RTL8188E family are 32 bytes. The host supplies
 * a complete 802.11 MPDU without FCS; the MAC appends the FCS. Use the
 * management queue for management frames and best-effort for data frames.
 *
 * The caller remains responsible for radio/channel state, sequence numbers,
 * association and encryption. This routine deliberately does not claim those
 * higher-level operations are implemented.
 */
static void rtl_put_le16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void rtl_put_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

int rtl8188eu_get_mac_address(uint8_t mac[6])
{
    uint8_t address[6];
    if (!mac || !rtl_bound || !rtl_firmware_ready ||
        !rtl_reg_read(0x0610U, address, sizeof(address)))
        return 0;
    /* A zero, broadcast, or multicast address is not a usable station MAC. */
    uint8_t any = 0U, all_ff = 1U;
    for (size_t i = 0U; i < sizeof(address); ++i) {
        any |= address[i];
        if (address[i] != 0xFFU)
            all_ff = 0U;
    }
    if (any == 0U || all_ff || (address[0] & 1U) != 0U)
        return 0;
    for (size_t i = 0U; i < sizeof(address); ++i)
        mac[i] = address[i];
    return 1;
}

int rtl8188eu_start_active_scan(void)
{
    uint8_t frame[38] = {0};
    uint8_t mac[6];
    static uint16_t sequence;
    if (!rtl8188eu_get_mac_address(mac))
        return 0;

    /* IEEE 802.11 Probe Request. The chipset's current channel is unchanged. */
    frame[0] = 0x40U;
    frame[1] = 0x00U;
    for (size_t i = 0U; i < 6U; ++i) {
        frame[4U + i] = 0xFFU;
        frame[16U + i] = 0xFFU;
        frame[10U + i] = mac[i];
    }
    sequence = (uint16_t)((sequence + 1U) & 0x0FFFU);
    frame[22] = (uint8_t)(sequence << 4);
    frame[23] = (uint8_t)(sequence >> 4);
    /* Wildcard SSID followed by the mandatory legacy supported-rate IE. */
    frame[24] = 0U;
    frame[25] = 0U;
    frame[26] = 1U;
    frame[27] = 8U;
    frame[28] = 0x82U;
    frame[29] = 0x84U;
    frame[30] = 0x8BU;
    frame[31] = 0x96U;
    frame[32] = 0x0CU;
    frame[33] = 0x12U;
    frame[34] = 0x18U;
    frame[35] = 0x24U;
    /* Leave the remaining bytes zeroed; exact MPDU length is 36 bytes. */
    return rtl8188eu_transmit_frame(frame, 36U);
}

int rtl8188eu_transmit_frame(const uint8_t *frame, size_t length)
{
    uint32_t actual = 0U;
    uint8_t *descriptor = rtl_tx_buffer;
    uint8_t frame_type;

    if (!rtl_bound || !rtl_firmware_ready || !rtl_radio_ready ||
        rtl_bulk_out == 0U || !frame || length < 24U ||
        length > RTL8188EU_TX_FRAME_MAX)
        return 0;

    /* Management/control/data parsing is deliberately conservative. */
    frame_type = (uint8_t)(frame[0] & 0x0CU);
    if (frame_type != 0x00U && frame_type != 0x08U)
        return 0;

    for (size_t i = 0U; i < sizeof(rtl_tx_buffer); ++i)
        rtl_tx_buffer[i] = 0U;

    rtl_put_le16(descriptor, (uint16_t)length);
    descriptor[2] = (uint8_t)RTL8188EU_TX_DESC_SIZE;
    descriptor[3] = 0x0CU; /* First and last segment. */
    /* TXDW1: management queue (0x12) or best-effort data queue (0x03). */
    rtl_put_le32(descriptor + 4U,
                 (uint32_t)((frame_type == 0x00U ? 0x12U : 0x03U) << 8));
    /* TXDW4: driver-selected legacy rate; TXDW5 rate zero is 1 Mbps CCK. */
    rtl_put_le32(descriptor + 16U, 0x00000100U);
    for (size_t i = 0U; i < length; ++i)
        rtl_tx_buffer[RTL8188EU_TX_DESC_SIZE + i] = frame[i];

    if (!xhci_bulk_transfer(rtl_slot, rtl_bulk_out,
                            rtl_tx_buffer,
                            (uint32_t)(RTL8188EU_TX_DESC_SIZE + length),
                            &actual))
        return 0;
    return actual == RTL8188EU_TX_DESC_SIZE + length;
}

rtl8188eu_status_t rtl8188eu_get_status(void)
{
    if (!rtl_bound)
        return RTL8188EU_STATUS_UNBOUND;
    if (rtl_firmware_ready)
        return RTL8188EU_STATUS_FIRMWARE_READY;
    if (rtl_firmware_attempted)
        return RTL8188EU_STATUS_FIRMWARE_FAILED;
    return fat32_is_mounted() ? RTL8188EU_STATUS_FIRMWARE_PENDING
                              : RTL8188EU_STATUS_USB_BOUND;
}

void rtl8188eu_poll(void)
{
    if (!rtl_bound)
        return;
    if (!rtl_firmware_attempted && fat32_is_mounted()) {
        rtl_firmware_attempted = 1U;
        if (rtl_download_firmware() && rtl_firmware_ready)
            serial_write("VANTAOS_RTL8188EU_FIRMWARE_READY\n");
        else
            serial_write("VANTAOS_RTL8188EU_FIRMWARE_LOAD_FAILED\n");
    }
    if (!rtl_firmware_ready)
        return;

    if (!rtl_rx_armed) {
        rtl_rx_armed = (uint8_t)xhci_start_bulk_in(rtl_slot, rtl_bulk_in);
        return;
    }
    uint32_t actual = 0U;
    int received = xhci_take_bulk_in(rtl_slot, rtl_bulk_in, rtl_rx_buffer,
                                     sizeof(rtl_rx_buffer), &actual);
    if (received == 1) {
        rtl_rx_armed = 0U;
        rtl_process_rx_packet(rtl_rx_buffer, actual);
    } else if (received < 0) {
        rtl_rx_armed = 0U;
    }
    if (!rtl_rx_armed)
        rtl_rx_armed = (uint8_t)xhci_start_bulk_in(rtl_slot, rtl_bulk_in);
}

int rtl8188eu_port_is_bound(uint32_t port)
{
    return rtl_bound && port != 0U && rtl_port == port;
}

int rtl8188eu_port_disconnected(uint32_t port)
{
    if (!rtl_bound || port == 0U || port != rtl_port)
        return 0;

    uint32_t old_slot = rtl_slot;
    rtl_bound = 0U;
    rtl_slot = 0U;
    rtl_port = 0U;
    rtl_bulk_in = 0U;
    rtl_bulk_out = 0U;
    rtl_rx_armed = 0U;
    rtl_firmware_attempted = 0U;
    rtl_firmware_ready = 0U;
    rtl_radio_ready = 0U;
    rtl_passive_result_count = 0U;
    if (old_slot != 0U)
        (void)xhci_disable_device(old_slot);
    return 1;
}

size_t rtl8188eu_get_passive_results(net_wifi_scan_result_t *results,
                                     size_t capacity)
{
    if (!results || capacity == 0U)
        return 0U;
    size_t count = rtl_passive_result_count < capacity
        ? rtl_passive_result_count : capacity;
    for (size_t i = 0; i < count; ++i)
        results[i] = rtl_passive_results[i];
    return count;
}
