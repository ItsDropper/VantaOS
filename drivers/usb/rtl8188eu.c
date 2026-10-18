#include "rtl8188eu.h"

#include "xhci.h"
#include "serial.h"
#include "fat32.h"
#include "net_wifi_80211.h"
#include "wifi_wpa2.h"
#include "wifi_ccmp.h"
#include "crypto_rng.h"
#include "net.h"

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
#define RTL8188EU_REG_RF_CTRL 0x001FU
#define RTL8188EU_REG_MAC_CR 0x0100U
#define RTL8188EU_REG_HSSI_PARAM2_A 0x0824U
#define RTL8188EU_REG_LSSI_PARAM_A 0x0840U
#define RTL8188EU_REG_RF_READBACK_A 0x08A0U
#define RTL8188EU_RF_CHNLBW 0x18U
#define RTL8188EU_RF_DATA_MASK 0x000FFFFFU
#define RTL8188EU_RF_CHANNEL_MASK 0x000000FFU
#define RTL8188EU_RF_READ_EDGE (1U << 31)
#define RTL8188EU_RF_ADDRESS_SHIFT 23U
#define RTL8188EU_RF_READBACK_MASK 0x000FFFFFU
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
/* A scan is advanced incrementally from poll; no synchronous wait in USB context. */
static uint8_t rtl_scan_active;
static uint8_t rtl_scan_channel;
static uint16_t rtl_scan_dwell_polls;
static uint8_t rtl_current_channel;

typedef enum {
    RTL_STATION_IDLE = 0,
    RTL_STATION_WAIT_AUTH,
    RTL_STATION_WAIT_ASSOC,
    RTL_STATION_WPA2_HANDSHAKE,
    RTL_STATION_AUTHENTICATED,
    RTL_STATION_FAILED
} rtl_station_state_t;

static rtl_station_state_t rtl_station_state;
static uint8_t rtl_station_ssid[NET_WIFI_SSID_MAX];
static size_t rtl_station_ssid_length;
static uint8_t rtl_station_passphrase[NET_WIFI_PASSPHRASE_MAX];
static size_t rtl_station_passphrase_length;
static uint8_t rtl_station_bssid[6];
static uint16_t rtl_station_sequence;
static uint16_t rtl_station_aid;
static uint16_t rtl_station_timeout;
static uint8_t rtl_station_retries;
static uint8_t rtl_station_rsn_ie[22] = {
    48U, 20U, 1U, 0U,
    0U, 15U, 172U, 4U, /* group CCMP-128 */
    1U, 0U, 0U, 15U, 172U, 4U, /* pairwise CCMP-128 */
    1U, 0U, 0U, 15U, 172U, 2U, /* AKM PSK */
    0U, 0U /* capabilities: no PMF requirement */
};
static wifi_wpa2_supplicant_t rtl_supplicant;
static uint8_t rtl_pairwise_key[16];
static uint8_t rtl_group_key[WIFI_WPA2_GTK_MAX];
static size_t rtl_group_key_length;
static uint8_t rtl_group_key_id;
static uint8_t rtl_keys_installed;
static wifi_ccmp_replay_t rtl_pairwise_pn;
static wifi_ccmp_replay_t rtl_group_pn;
static uint8_t rtl_netdev_registered;
static int rtl_netdev_transmit(net_device_t *device, const uint8_t *frame,
                               size_t length);
static const net_device_ops_t rtl_netdev_ops = {
    .transmit = rtl_netdev_transmit
};
static net_device_t rtl_netdev = {
    .name = "wlan0",
    .mtu = NET_MTU_DEFAULT,
    .ops = &rtl_netdev_ops
};
/* Host-side driver tests omit the network core; kernel builds resolve these. */
extern int net_device_register(net_device_t *) __attribute__((weak));
extern int net_device_unregister(net_device_t *) __attribute__((weak));
extern int net_device_set_link(net_device_t *, int) __attribute__((weak));
extern int net_receive_frame(net_device_t *, const void *, size_t) __attribute__((weak));
static void rtl_station_receive_frame(const uint8_t *frame, size_t length);
static void rtl_station_poll(void);
static void rtl_station_reset(void);
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

/*
 * RTL8188E RF serial interface (path A, the single 1T1R path). These are
 * transport primitives only; they do not substitute for board-specific PHY
 * configuration, transmit-power setup, or LC/IQ calibration.
 */
static int rtl_rf_read(uint8_t address, uint32_t *value)
{
    uint32_t hssi, trigger, readback;
    if (!value || !rtl_bound || !rtl_firmware_ready)
        return 0;
    if (!rtl_reg_read32(RTL8188EU_REG_HSSI_PARAM2_A, &hssi))
        return 0;
    trigger = (hssi & ~(0xFFU << RTL8188EU_RF_ADDRESS_SHIFT)) |
              ((uint32_t)address << RTL8188EU_RF_ADDRESS_SHIFT) |
              RTL8188EU_RF_READ_EDGE;
    if (!rtl_reg_write32(RTL8188EU_REG_HSSI_PARAM2_A,
                         hssi & ~RTL8188EU_RF_READ_EDGE) ||
        !rtl_reg_write32(RTL8188EU_REG_HSSI_PARAM2_A, trigger) ||
        !rtl_reg_read32(RTL8188EU_REG_RF_READBACK_A, &readback))
        return 0;
    *value = readback & RTL8188EU_RF_READBACK_MASK;
    return 1;
}

static int rtl_rf_write(uint8_t address, uint32_t value)
{
    uint32_t command;
    if (!rtl_bound || !rtl_firmware_ready ||
        (value & ~RTL8188EU_RF_DATA_MASK) != 0U)
        return 0;
    command = ((uint32_t)address << 20) | value;
    return rtl_reg_write32(RTL8188EU_REG_LSSI_PARAM_A, command);
}

/*
 * Tune only after the full radio initialization/calibration gate is set.
 * RTL8188E RF_CHNLBW register 0x18 carries the channel in its low byte; retain
 * bandwidth and unrelated RF control bits. Readback is mandatory before the
 * channel is considered selected.
 */
static int rtl_program_channel(uint8_t channel)
{
    uint32_t current, updated, verify;
    if (!rtl_radio_ready || channel < 1U || channel > 14U)
        return 0;
    if (!rtl_rf_read(RTL8188EU_RF_CHNLBW, &current))
        return 0;
    updated = (current & ~RTL8188EU_RF_CHANNEL_MASK) | channel;
    if (!rtl_rf_write(RTL8188EU_RF_CHNLBW, updated) ||
        !rtl_rf_read(RTL8188EU_RF_CHNLBW, &verify) ||
        (verify & RTL8188EU_RF_CHANNEL_MASK) != channel)
        return 0;
    rtl_current_channel = channel;
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

        rtl_station_receive_frame(rx + frame_offset, frame_length);
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

static int rtl_send_probe_request(void)
{
    uint8_t frame[36] = {0};
    uint8_t mac[6];
    static uint16_t sequence;
    if (!rtl_radio_ready || !rtl_current_channel ||
        !rtl8188eu_get_mac_address(mac))
        return 0;

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
    return rtl8188eu_transmit_frame(frame, sizeof(frame));
}

int rtl8188eu_start_active_scan(void)
{
    if (!rtl_radio_ready || rtl_scan_active)
        return 0;
    rtl_passive_result_count = 0U;
    rtl_scan_channel = 1U;
    if (!rtl_program_channel(rtl_scan_channel) || !rtl_send_probe_request()) {
        rtl_scan_active = 0U;
        rtl_scan_channel = 0U;
        rtl_scan_dwell_polls = 0U;
        return 0;
    }
    rtl_scan_active = 1U;
    rtl_scan_dwell_polls = 500U;
    return 1;
}

static void rtl_scan_poll(void)
{
    if (!rtl_scan_active)
        return;
    if (rtl_scan_dwell_polls != 0U) {
        --rtl_scan_dwell_polls;
        return;
    }
    if (rtl_scan_channel >= 14U) {
        rtl_scan_active = 0U;
        rtl_scan_channel = 0U;
        return;
    }
    ++rtl_scan_channel;
    if (!rtl_program_channel(rtl_scan_channel) || !rtl_send_probe_request()) {
        rtl_scan_active = 0U;
        rtl_scan_channel = 0U;
        rtl_scan_dwell_polls = 0U;
        serial_write("VANTAOS_RTL8188EU_SCAN_FAILED\\n");
        return;
    }
    rtl_scan_dwell_polls = 500U;
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

static int rtl_ccmp_build_aad(const uint8_t *frame, size_t length,
                                  uint8_t aad[22])
{
    if (!frame || !aad || length < 24U ||
        (frame[0] & 0x80U) != 0U || /* QoS is not implemented yet. */
        (frame[1] & 0x04U) != 0U || /* fragmented MPDUs are not supported. */
        (frame[22] & 0x0FU) != 0U)
        return 0;
    aad[0] = (uint8_t)(frame[0] & 0x8FU);
    aad[1] = (uint8_t)(frame[1] & 0xC7U);
    for (size_t i = 0U; i < 18U; ++i) aad[2U + i] = frame[4U + i];
    aad[20] = (uint8_t)(frame[22] & 0x0FU);
    aad[21] = 0U;
    return 1;
}

static void rtl_network_link_up(void)
{
    uint8_t mac[6];
    if (rtl_station_state != RTL_STATION_AUTHENTICATED || !rtl_keys_installed ||
        !rtl8188eu_get_mac_address(mac))
        return;
    for (size_t i = 0U; i < sizeof(rtl_netdev.mac); ++i)
        rtl_netdev.mac[i] = mac[i];
    if (!rtl_netdev_registered) {
        if (!net_device_register || !net_device_register(&rtl_netdev))
            return;
        rtl_netdev_registered = 1U;
    }
    rtl_netdev.link_up = 1U;
    if (net_device_set_link)
        (void)net_device_set_link(&rtl_netdev, 1);
}

static void rtl_network_link_down(void)
{
    rtl_netdev.link_up = 0U;
    if (rtl_netdev_registered && net_device_set_link)
        (void)net_device_set_link(&rtl_netdev, 0);
}

static int rtl_netdev_transmit(net_device_t *device, const uint8_t *ethernet,
                               size_t ethernet_length)
{
    uint8_t frame[24U + WIFI_CCMP_HEADER_SIZE + WIFI_CCMP_MAX_PAYLOAD +
                  WIFI_CCMP_TAG_SIZE];
    uint8_t payload[WIFI_CCMP_MAX_PAYLOAD], aad[22], nonce[WIFI_CCMP_NONCE_SIZE];
    uint8_t tag[WIFI_CCMP_TAG_SIZE], local_mac[6];
    const uint8_t *key;
    wifi_ccmp_replay_t *pn_state;
    uint8_t key_id;
    uint64_t pn;
    size_t payload_length = 0U, frame_length;
    int multicast;
    if (device != &rtl_netdev || !ethernet || ethernet_length < 14U ||
        ethernet_length > NET_FRAME_MAX_SIZE ||
        !rtl_netdev_registered || !rtl_netdev.link_up ||
        rtl_station_state != RTL_STATION_AUTHENTICATED || !rtl_keys_installed ||
        !rtl8188eu_get_mac_address(local_mac))
        return 0;

    multicast = (ethernet[0] & 1U) != 0U;
    if (multicast) {
        if (rtl_group_key_length != WIFI_CCMP_KEY_SIZE)
            return 0;
        key = rtl_group_key;
        key_id = rtl_group_key_id;
        pn_state = &rtl_group_pn;
    } else {
        key = rtl_pairwise_key;
        key_id = 0U;
        pn_state = &rtl_pairwise_pn;
    }
    if (!net_wifi_80211_snap_encapsulate(ethernet, ethernet_length, payload,
            sizeof(payload), &payload_length) ||
        payload_length > WIFI_CCMP_MAX_PAYLOAD ||
        !wifi_ccmp_next_tx_pn(pn_state, &pn))
        return 0;

    for (size_t i = 0U; i < 24U; ++i) frame[i] = 0U;
    frame[0] = 0x08U; /* Data */
    frame[1] = 0x41U; /* To-DS + Protected */
    for (size_t i = 0U; i < 6U; ++i) {
        frame[4U + i] = rtl_station_bssid[i];
        frame[10U + i] = local_mac[i];
        frame[16U + i] = ethernet[i]; /* destination */
    }
    rtl_station_sequence = (uint16_t)((rtl_station_sequence + 1U) & 0x0FFFU);
    frame[22] = (uint8_t)(rtl_station_sequence << 4);
    frame[23] = (uint8_t)(rtl_station_sequence >> 4);
    if (!rtl_ccmp_build_aad(frame, 24U, aad) ||
        !wifi_ccmp_header_encode(frame + 24U, pn, key_id))
        return 0;
    wifi_ccmp_build_nonce(nonce, 0U, local_mac, pn);
    if (!wifi_ccmp_encrypt(key, nonce, aad, sizeof(aad), payload,
            payload_length, tag))
        return 0;
    for (size_t i = 0U; i < payload_length; ++i)
        frame[32U + i] = payload[i];
    for (size_t i = 0U; i < sizeof(tag); ++i)
        frame[32U + payload_length + i] = tag[i];
    frame_length = 32U + payload_length + sizeof(tag);
    return rtl8188eu_transmit_frame(frame, frame_length);
}

static void rtl_process_protected_data(uint8_t *frame, size_t length)
{
    uint8_t local_mac[6], aad[22], nonce[WIFI_CCMP_NONCE_SIZE];
    uint8_t ethernet[NET_FRAME_MAX_SIZE], key_id;
    uint64_t pn;
    size_t payload_length, ethernet_length = 0U;
    const uint8_t *key;
    wifi_ccmp_replay_t *replay;
    if (!frame || !rtl_keys_installed || !rtl_netdev_registered ||
        !net_receive_frame || length < 24U + WIFI_CCMP_HEADER_SIZE +
                                      WIFI_CCMP_TAG_SIZE ||
        (frame[0] & 0x0CU) != 0x08U || (frame[0] & 0x80U) != 0U ||
        (frame[1] & 0x03U) != 0x02U || /* From-DS only */
        (frame[1] & 0x40U) == 0U ||   /* Protected */
        (frame[1] & 0x04U) != 0U || (frame[22] & 0x0FU) != 0U ||
        !rtl8188eu_get_mac_address(local_mac))
        return;

    /* AP transmitter must match the associated BSSID. Unicast is for this
       station; multicast is accepted only under the group key. */
    uint8_t bssid_difference = 0U, local_difference = 0U;
    for (size_t i = 0U; i < 6U; ++i) {
        bssid_difference |= (uint8_t)(frame[10U + i] ^ rtl_station_bssid[i]);
        local_difference |= (uint8_t)(frame[4U + i] ^ local_mac[i]);
    }
    if (bssid_difference != 0U ||
        (local_difference != 0U && (frame[4] & 1U) == 0U))
        return;

    if (!wifi_ccmp_header_decode(frame + 24U, &pn, &key_id))
        return;
    if (key_id == 0U && (frame[4] & 1U) == 0U) {
        key = rtl_pairwise_key;
        replay = &rtl_pairwise_pn;
    } else if (rtl_group_key_length == WIFI_CCMP_KEY_SIZE &&
               key_id == rtl_group_key_id && (frame[4] & 1U) != 0U) {
        key = rtl_group_key;
        replay = &rtl_group_pn;
    } else {
        return;
    }
    payload_length = length - 24U - WIFI_CCMP_HEADER_SIZE - WIFI_CCMP_TAG_SIZE;
    if (payload_length == 0U || payload_length > WIFI_CCMP_MAX_PAYLOAD ||
        !rtl_ccmp_build_aad(frame, length, aad))
        return;
    wifi_ccmp_build_nonce(nonce, 0U, frame + 10U, pn);
    if (!wifi_ccmp_decrypt(key, nonce, aad, sizeof(aad), frame + 32U,
            payload_length, frame + 32U + payload_length) ||
        !wifi_ccmp_accept_rx_pn(replay, pn))
        return;

    if (!net_wifi_80211_snap_decapsulate(frame + 4U, frame + 16U,
            frame + 32U, payload_length, ethernet, sizeof(ethernet),
            &ethernet_length))
        return;
    (void)net_receive_frame(&rtl_netdev, ethernet, ethernet_length);
}

static int rtl_station_send_eapol(const uint8_t *bssid,
                                   const uint8_t *payload, size_t length,
                                   void *context)
{
    uint8_t frame[24U + 8U + WIFI_WPA2_EAPOL_MAX];
    uint8_t local_mac[6];
    size_t total;
    (void)context;
    if (!bssid || !payload || length == 0U ||
        length > WIFI_WPA2_EAPOL_MAX || !rtl_radio_ready ||
        !rtl8188eu_get_mac_address(local_mac))
        return 0;
    total = 32U + length;
    for (size_t i = 0U; i < total; ++i) frame[i] = 0U;
    frame[0] = 0x08U; /* Data */
    frame[1] = 0x01U; /* To-DS */
    for (size_t i = 0U; i < 6U; ++i) {
        frame[4U + i] = bssid[i];      /* Address 1: AP receiver */
        frame[10U + i] = local_mac[i]; /* Address 2: station transmitter */
        frame[16U + i] = bssid[i];     /* Address 3: Ethernet destination */
    }
    rtl_station_sequence = (uint16_t)((rtl_station_sequence + 1U) & 0x0FFFU);
    frame[22] = (uint8_t)(rtl_station_sequence << 4);
    frame[23] = (uint8_t)(rtl_station_sequence >> 4);
    frame[24] = 0xAAU; frame[25] = 0xAAU; frame[26] = 0x03U;
    frame[27] = 0U; frame[28] = 0U; frame[29] = 0U;
    frame[30] = 0x88U; frame[31] = 0x8EU; /* EAPOL EtherType */
    for (size_t i = 0U; i < length; ++i) frame[32U + i] = payload[i];
    return rtl8188eu_transmit_frame(frame, total);
}

static int rtl_station_random(uint8_t *output, size_t length, void *context)
{
    (void)context;
    return crypto_rng_fill(output, length);
}

static int rtl_station_install_keys(const uint8_t *bssid, const uint8_t *peer,
                                    const uint8_t temporal_key[16],
                                    const uint8_t *gtk, size_t gtk_length,
                                    uint8_t gtk_id, void *context)
{
    (void)context;
    if (!bssid || !peer || !temporal_key ||
        gtk_length != WIFI_CCMP_KEY_SIZE || !gtk ||
        rtl_station_state != RTL_STATION_WPA2_HANDSHAKE)
        return 0;
    for (size_t i = 0U; i < 6U; ++i)
        if (bssid[i] != rtl_station_bssid[i])
            return 0;
    for (size_t i = 0U; i < 16U; ++i) rtl_pairwise_key[i] = temporal_key[i];
    for (size_t i = 0U; i < sizeof(rtl_group_key); ++i)
        rtl_group_key[i] = (i < gtk_length) ? gtk[i] : 0U;
    rtl_group_key_length = gtk_length;
    rtl_group_key_id = gtk_id;
    wifi_ccmp_replay_reset(&rtl_pairwise_pn);
    wifi_ccmp_replay_reset(&rtl_group_pn);
    rtl_keys_installed = 1U;
    return 1;
}

static int rtl_station_send_auth(void)
{
    uint8_t frame[32];
    uint8_t local_mac[6];
    size_t length = 0U;
    if (!rtl8188eu_get_mac_address(local_mac))
        return 0;
    rtl_station_sequence = (uint16_t)((rtl_station_sequence + 1U) & 0x0FFFU);
    if (!net_wifi_80211_build_open_auth_request(frame, sizeof(frame),
            rtl_station_bssid, local_mac, rtl_station_bssid,
            rtl_station_sequence, &length))
        return 0;
    return rtl8188eu_transmit_frame(frame, length);
}

static int rtl_station_send_assoc(void)
{
    uint8_t frame[128];
    uint8_t local_mac[6];
    size_t length = 0U;
    if (!rtl8188eu_get_mac_address(local_mac))
        return 0;
    rtl_station_sequence = (uint16_t)((rtl_station_sequence + 1U) & 0x0FFFU);
    if (!net_wifi_80211_build_assoc_request(frame, sizeof(frame),
            rtl_station_bssid, local_mac, rtl_station_bssid,
            rtl_station_sequence, rtl_station_ssid, rtl_station_ssid_length,
            rtl_station_rsn_ie, sizeof(rtl_station_rsn_ie), &length))
        return 0;
    return rtl8188eu_transmit_frame(frame, length);
}

int rtl8188eu_connect(const uint8_t *ssid, size_t ssid_length,
                      const uint8_t *passphrase, size_t passphrase_length)
{
    net_wifi_scan_result_t results[NET_WIFI_MAX_SCAN_RESULTS];
    size_t count;
    int selected = -1;
    uint8_t local_mac[6];
    if (!rtl_radio_ready || rtl_scan_active || !ssid || ssid_length == 0U ||
        ssid_length > NET_WIFI_SSID_MAX || !passphrase ||
        passphrase_length < NET_WIFI_PASSPHRASE_MIN ||
        passphrase_length > NET_WIFI_PASSPHRASE_MAX ||
        !crypto_rng_available() || !rtl8188eu_get_mac_address(local_mac) ||
        rtl_station_state == RTL_STATION_WAIT_AUTH ||
        rtl_station_state == RTL_STATION_WAIT_ASSOC ||
        rtl_station_state == RTL_STATION_WPA2_HANDSHAKE)
        return 0;
    for (size_t i = 0U; i < passphrase_length; ++i)
        if (passphrase[i] < 0x20U || passphrase[i] > 0x7EU)
            return 0;
    count = rtl8188eu_get_passive_results(results, NET_WIFI_MAX_SCAN_RESULTS);
    for (size_t i = 0U; i < count; ++i) {
        if (results[i].ssid_length != ssid_length ||
            results[i].security != NET_WIFI_SECURITY_WPA2_PSK)
            continue;
        size_t j = 0U;
        while (j < ssid_length && results[i].ssid[j] == ssid[j]) ++j;
        if (j == ssid_length) { selected = (int)i; break; }
    }
    if (selected < 0 || !rtl_program_channel(results[selected].channel))
        return 0;

    rtl_station_reset();
    for (size_t i = 0U; i < sizeof(rtl_station_ssid); ++i)
        rtl_station_ssid[i] = (i < ssid_length) ? ssid[i] : 0U;
    rtl_station_ssid_length = ssid_length;
    for (size_t i = 0U; i < sizeof(rtl_station_passphrase); ++i)
        rtl_station_passphrase[i] = (i < passphrase_length) ? passphrase[i] : 0U;
    rtl_station_passphrase_length = passphrase_length;
    for (size_t i = 0U; i < sizeof(rtl_station_bssid); ++i)
        rtl_station_bssid[i] = results[selected].bssid[i];
    rtl_station_retries = 0U;
    rtl_station_timeout = 1000U;
    rtl_station_aid = 0U;
    rtl_keys_installed = 0U;
    rtl_station_state = RTL_STATION_WAIT_AUTH;
    if (!rtl_station_send_auth()) {
        rtl_station_state = RTL_STATION_FAILED;
        return 0;
    }
    return 1;
}

rtl8188eu_connection_state_t rtl8188eu_get_connection_state(void)
{
    switch (rtl_station_state) {
    case RTL_STATION_WAIT_AUTH: return RTL8188EU_CONNECTION_AUTHENTICATING;
    case RTL_STATION_WAIT_ASSOC: return RTL8188EU_CONNECTION_ASSOCIATING;
    case RTL_STATION_WPA2_HANDSHAKE: return RTL8188EU_CONNECTION_WPA2_HANDSHAKE;
    case RTL_STATION_AUTHENTICATED: return RTL8188EU_CONNECTION_AUTHENTICATED;
    case RTL_STATION_FAILED: return RTL8188EU_CONNECTION_FAILED;
    default: return RTL8188EU_CONNECTION_IDLE;
    }
}

static void rtl_station_receive_frame(const uint8_t *frame, size_t length)
{
    uint16_t status, aid;
    if (!frame || length < 24U || rtl_station_state == RTL_STATION_IDLE ||
        rtl_station_state == RTL_STATION_FAILED)
        return;
    if (rtl_station_state == RTL_STATION_AUTHENTICATED) {
        rtl_process_protected_data((uint8_t *)frame, length);
        return;
    }
    if ((frame[0] & 0x0CU) == 0x00U) {
        if (rtl_station_state == RTL_STATION_WAIT_AUTH &&
            net_wifi_80211_parse_auth_response(frame, length,
                rtl_station_bssid, &status)) {
            if (status != 0U || !rtl_station_send_assoc()) {
                rtl_station_state = RTL_STATION_FAILED;
                return;
            }
            rtl_station_state = RTL_STATION_WAIT_ASSOC;
            rtl_station_timeout = 1000U;
            rtl_station_retries = 0U;
            return;
        }
        if (rtl_station_state == RTL_STATION_WAIT_ASSOC &&
            net_wifi_80211_parse_assoc_response(frame, length,
                rtl_station_bssid, &status, &aid)) {
            if (status != 0U || aid == 0U) {
                rtl_station_state = RTL_STATION_FAILED;
                return;
            }
            rtl_station_aid = aid;
            wifi_wpa2_ops_t ops = {
                .random_bytes = rtl_station_random,
                .send_eapol = rtl_station_send_eapol,
                .install_ccmp_keys = rtl_station_install_keys,
                .context = 0
            };
            uint8_t local_mac[6];
            if (!rtl8188eu_get_mac_address(local_mac) ||
                !wifi_wpa2_begin(&rtl_supplicant, &ops,
                    rtl_station_ssid, rtl_station_ssid_length,
                    rtl_station_passphrase, rtl_station_passphrase_length,
                    local_mac, rtl_station_bssid,
                    rtl_station_rsn_ie, sizeof(rtl_station_rsn_ie))) {
                rtl_station_state = RTL_STATION_FAILED;
                return;
            }
            rtl_station_state = RTL_STATION_WPA2_HANDSHAKE;
            rtl_station_timeout = 4000U;
            rtl_station_retries = 0U;
            return;
        }
    }

    /* WPA2 EAPOL is carried in an unprotected From-DS data MPDU before port
       authorization. Do not feed frames from another BSSID to the supplicant. */
    if (rtl_station_state == RTL_STATION_WPA2_HANDSHAKE &&
        (frame[0] & 0x0CU) == 0x08U && (frame[1] & 0x03U) == 0x02U &&
        (frame[1] & 0x40U) == 0U && length >= 32U) {
        uint8_t different = 0U;
        for (size_t i = 0U; i < 6U; ++i) different |= frame[10U + i] ^ rtl_station_bssid[i];
        if (different != 0U || frame[24] != 0xAAU || frame[25] != 0xAAU ||
            frame[26] != 0x03U || frame[27] != 0U || frame[28] != 0U ||
            frame[29] != 0U || frame[30] != 0x88U || frame[31] != 0x8EU)
            return;
        (void)wifi_wpa2_receive_eapol(&rtl_supplicant, frame + 10U,
                                      frame + 32U, length - 32U);
        if (wifi_wpa2_get_state(&rtl_supplicant) == WIFI_WPA2_AUTHENTICATED &&
            rtl_keys_installed) {
            rtl_station_state = RTL_STATION_AUTHENTICATED;
            rtl_network_link_up();
        }
        else if (wifi_wpa2_get_state(&rtl_supplicant) == WIFI_WPA2_FAILED)
            rtl_station_state = RTL_STATION_FAILED;
    }
}

static void rtl_station_poll(void)
{
    if (rtl_station_state == RTL_STATION_AUTHENTICATED) {
        rtl_network_link_up();
        return;
    }
    if (rtl_station_state != RTL_STATION_WAIT_AUTH &&
        rtl_station_state != RTL_STATION_WAIT_ASSOC &&
        rtl_station_state != RTL_STATION_WPA2_HANDSHAKE)
        return;
    if (rtl_station_timeout != 0U) {
        --rtl_station_timeout;
        return;
    }
    if (rtl_station_retries >= 3U) {
        rtl_station_state = RTL_STATION_FAILED;
        wifi_wpa2_reset(&rtl_supplicant);
        return;
    }
    ++rtl_station_retries;
    rtl_station_timeout = rtl_station_state == RTL_STATION_WPA2_HANDSHAKE
        ? 4000U : 1000U;
    if ((rtl_station_state == RTL_STATION_WAIT_AUTH && !rtl_station_send_auth()) ||
        (rtl_station_state == RTL_STATION_WAIT_ASSOC && !rtl_station_send_assoc())) {
        rtl_station_state = RTL_STATION_FAILED;
        wifi_wpa2_reset(&rtl_supplicant);
    }
    /* WPA2 message retransmission is handled by the supplicant, not by
       replaying a stale EAPOL frame with an old replay counter. */
}

static void rtl_station_reset(void)
{
    rtl_network_link_down();
    wifi_wpa2_reset(&rtl_supplicant);
    for (size_t i = 0U; i < sizeof(rtl_station_passphrase); ++i)
        rtl_station_passphrase[i] = 0U;
    for (size_t i = 0U; i < sizeof(rtl_pairwise_key); ++i)
        rtl_pairwise_key[i] = 0U;
    for (size_t i = 0U; i < sizeof(rtl_group_key); ++i)
        rtl_group_key[i] = 0U;
    rtl_station_ssid_length = 0U;
    rtl_station_passphrase_length = 0U;
    rtl_station_timeout = 0U;
    rtl_station_retries = 0U;
    rtl_station_aid = 0U;
    rtl_group_key_length = 0U;
    rtl_group_key_id = 0U;
    rtl_keys_installed = 0U;
    rtl_station_state = RTL_STATION_IDLE;
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
    rtl_scan_poll();
    rtl_station_poll();
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
    rtl_scan_active = 0U;
    rtl_scan_channel = 0U;
    rtl_scan_dwell_polls = 0U;
    rtl_current_channel = 0U;
    rtl_station_reset();
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
