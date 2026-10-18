#include "rtl8188eu.h"
#include "xhci.h"
#include "fat32.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint8_t configured_in, configured_out;
static uint16_t packet_in, packet_out;
static uint8_t registers[65536];
static uint8_t firmware_read_seen;
static uint8_t checksum_reported;
static uint8_t checksum_reported_too_early;
static uint8_t beacon_delivered;
static uint8_t tx_seen;
static uint8_t tx_descriptor_valid;

int xhci_controller_online(void) { return 1; }
int xhci_configure_bulk_endpoint(uint32_t slot, uint8_t ep, uint16_t packet)
{
    (void)slot;
    if (ep & 0x80U) { configured_in = ep; packet_in = packet; }
    else { configured_out = ep; packet_out = packet; }
    return 1;
}
int xhci_disable_device(uint32_t slot) { (void)slot; return 1; }
int xhci_control_transfer(uint32_t slot, const uint8_t setup[8],
                          void *data, uint16_t length)
{
    (void)slot;
    uint16_t address = (uint16_t)setup[2] | ((uint16_t)setup[3] << 8);
    if (setup[0] & 0x80U) {
        if (data)
            memcpy(data, registers + address, length);
    } else if (data) {
        if (address == 0x80U && length >= 1U &&
            (registers[0x80] & 0x01U) != 0U &&
            (((const uint8_t *)data)[0] & 0x04U) != 0U)
            checksum_reported_too_early = 1U;
        memcpy(registers + address, data, length);
        if (address == 0x1000U && length == 8U)
            firmware_read_seen = (registers[address] == 0xA0U);
        /* Hardware sets checksum-report only after download is disabled. */
        if (address == 0x80U && length == 2U &&
            (registers[0x80] & 0x01U) == 0U && firmware_read_seen) {
            registers[0x80] |= 0x04U;
            checksum_reported = 1U;
        }
        if (address == 0x0002U && length == 2U &&
            (registers[2] & 0x04U))
            registers[0x80] |= 0x40U;
    }
    return 1;
}
int xhci_bulk_transfer(uint32_t slot, uint8_t ep, void *buffer,
                       uint32_t length, uint32_t *actual)
{
    (void)slot;
    if (!buffer || !actual || ep != configured_out || length < 56U)
        return 0;
    const uint8_t *bytes = buffer;
    uint16_t packet_length = (uint16_t)bytes[0] |
                             ((uint16_t)bytes[1] << 8);
    uint32_t queue = (uint32_t)bytes[4] |
                     ((uint32_t)bytes[5] << 8) |
                     ((uint32_t)bytes[6] << 16) |
                     ((uint32_t)bytes[7] << 24);
    tx_descriptor_valid = packet_length == length - 32U &&
                          bytes[2] == 32U && bytes[3] == 0x0CU &&
                          (queue & 0x1F00U) == 0x1200U &&
                          bytes[32] == 0x80U;
    tx_seen = 1U;
    *actual = length;
    return 1;
}
int xhci_start_bulk_in(uint32_t slot, uint8_t ep)
{ (void)slot; (void)ep; return 1; }
int xhci_take_bulk_in(uint32_t slot, uint8_t ep, void *buffer,
                      uint32_t capacity, uint32_t *actual)
{
    (void)slot; (void)ep;
    /* Two 45-byte frames, each preceded by a 24-byte descriptor and
     * aligned to the next 8-byte aggregate boundary. */
    if (beacon_delivered || !buffer || !actual || capacity < 141U)
        return 0;
    uint8_t *packet = buffer;
    memset(packet, 0, 141U);
    packet[0] = 45U;
    uint8_t *frame = packet + 24U;
    frame[0] = 0x80U;
    for (uint32_t i = 4U; i < 10U; ++i) frame[i] = 0xFFU;
    const uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    memcpy(frame + 10U, bssid, sizeof(bssid));
    memcpy(frame + 16U, bssid, sizeof(bssid));
    frame[32] = 100U;
    frame[34] = 1U;
    frame[36] = 0U; frame[37] = 4U;
    memcpy(frame + 38U, "TEST", 4U);
    frame[42] = 3U; frame[43] = 1U; frame[44] = 6U;

    uint8_t *second_descriptor = packet + 72U;
    second_descriptor[0] = 45U;
    frame = second_descriptor + 24U;
    frame[0] = 0x80U;
    for (uint32_t i = 4U; i < 10U; ++i) frame[i] = 0xFFU;
    const uint8_t second_bssid[6] = {0x02, 0x21, 0x32, 0x43, 0x54, 0x65};
    memcpy(frame + 10U, second_bssid, sizeof(second_bssid));
    memcpy(frame + 16U, second_bssid, sizeof(second_bssid));
    frame[32] = 100U;
    frame[34] = 1U;
    frame[36] = 0U; frame[37] = 4U;
    memcpy(frame + 38U, "TEST", 4U);
    frame[42] = 3U; frame[43] = 1U; frame[44] = 11U;

    *actual = 141U;
    beacon_delivered = 1U;
    return 1;
}

int fat32_is_mounted(void) { return 1; }
uint32_t fat32_root_cluster(void) { return 1U; }
int fat32_list_directory(uint32_t cluster, fat32_dirent_t *entries,
                         unsigned int capacity)
{
    if (!entries || capacity == 0U) return -1;
    memset(entries, 0, capacity * sizeof(*entries));
    if (cluster == 1U) {
        strcpy(entries[0].name, "SYSTEM");
        entries[0].directory = 1U;
        entries[0].first_cluster = 2U;
        return 1;
    }
    if (cluster == 2U) {
        strcpy(entries[0].name, "RTL8188EU.BIN");
        entries[0].first_cluster = 3U;
        entries[0].size = 40U;
        return 1;
    }
    return 0;
}
int fat32_read_file(uint32_t cluster, uint32_t size, char *buffer,
                    unsigned int capacity)
{
    if (cluster != 3U || size != 40U || capacity < size || !buffer)
        return -1;
    memset(buffer, 0, size);
    buffer[0] = (char)0xE0U;
    buffer[1] = (char)0x88U;
    for (uint32_t i = 32U; i < size; ++i)
        buffer[i] = (char)(0xA0U + i - 32U);
    return (int)size;
}
void serial_write(const char *s) { (void)s; }
void serial_write_bytes(const char *s, size_t n) { (void)s; (void)n; }

int main(void)
{
    const uint8_t valid[] = {
        9, 2, 32, 0, 1, 1, 0, 0x80, 50,
        9, 4, 0, 0, 2, 0xFF, 0, 0, 0,
        7, 5, 0x81, 2, 64, 0, 0,
        7, 5, 0x02, 2, 64, 0, 0
    };
    const uint8_t malformed[] = {
        9, 2, 16, 0, 1, 1, 0, 0x80, 50,
        9, 4, 0, 0, 1, 0xFF, 0, 0, 0,
        7, 5, 0x81, 2, 64, 0, 0
    };
    if (rtl8188eu_probe(1, 1, 0x0BDA, 0x8179, malformed,
                        sizeof(malformed)) != 0)
        return 1;
    /* The declared configuration length must fit the received buffer. */
    if (rtl8188eu_probe(1, 1, 0x0BDA, 0x8179, valid,
                        sizeof(valid) - 1U) != 0)
        return 11;
    /* Reserved endpoint-address bits must not be accepted as real pipes. */
    uint8_t invalid_address[sizeof(valid)];
    memcpy(invalid_address, valid, sizeof(valid));
    invalid_address[20] = 0x91U;
    if (rtl8188eu_probe(1, 1, 0x0BDA, 0x8179, invalid_address,
                        sizeof(invalid_address)) != 0)
        return 12;
    if (rtl8188eu_probe(1, 1, 0x1234, 0x8179, valid, sizeof(valid)) != 0)
        return 2;
    if (rtl8188eu_probe(1, 1, 0x0BDA, 0x8179, valid, sizeof(valid)) != 1)
        return 3;
    if (configured_in != 0x81 || configured_out != 0x02 ||
        packet_in != 64 || packet_out != 64 ||
        !rtl8188eu_port_is_bound(1U) || rtl8188eu_port_is_bound(2U))
        return 4;

    rtl8188eu_poll();
    if (!firmware_read_seen || registers[0x1000] != 0xA0U ||
        registers[0x1007] != 0xA7U || !checksum_reported ||
        checksum_reported_too_early)
        return 5;
    rtl8188eu_poll();
    net_wifi_scan_result_t results[2];
    size_t result_count = rtl8188eu_get_passive_results(results, 2U);
    if (result_count != 2U) return 6;
    if (results[0].ssid_length != 4U ||
        memcmp(results[0].ssid, "TEST", 4U) != 0) return 7;
    if (results[0].channel != 6U || results[1].channel != 11U) return 8;
    const uint8_t expected_bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    const uint8_t expected_second_bssid[6] = {0x02, 0x21, 0x32, 0x43, 0x54, 0x65};
    if (memcmp(results[0].bssid, expected_bssid, 6U) != 0 ||
        memcmp(results[1].bssid, expected_second_bssid, 6U) != 0) return 9;
    uint8_t management_frame[24] = {0x80U};
    if (!rtl8188eu_transmit_frame(management_frame, sizeof(management_frame)) ||
        !tx_seen || !tx_descriptor_valid)
        return 13;
    if (rtl8188eu_transmit_frame(management_frame, 23U))
        return 14;
    if (!rtl8188eu_port_disconnected(1U) || rtl8188eu_port_is_bound(1U))
        return 10;
    return 0;
}
