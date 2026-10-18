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
static uint8_t beacon_delivered;

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
        memcpy(registers + address, data, length);
        if (address == 0x1000U && length == 8U)
            firmware_read_seen = (registers[address] == 0xA0U);
        if (address == 0x0002U && length == 2U &&
            (registers[2] & 0x04U))
            registers[0x80] |= 0x40U;
    }
    return 1;
}
int xhci_bulk_transfer(uint32_t slot, uint8_t ep, void *buffer,
                       uint32_t length, uint32_t *actual)
{ (void)slot; (void)ep; (void)buffer; (void)length; (void)actual; return 0; }
int xhci_start_bulk_in(uint32_t slot, uint8_t ep)
{ (void)slot; (void)ep; return 1; }
int xhci_take_bulk_in(uint32_t slot, uint8_t ep, void *buffer,
                      uint32_t capacity, uint32_t *actual)
{
    (void)slot; (void)ep;
    if (beacon_delivered || !buffer || !actual || capacity < 69U)
        return 0;
    uint8_t *packet = buffer;
    memset(packet, 0, 69U);
    packet[0] = 45U; /* RX descriptor reports a 45-byte 802.11 frame. */
    uint8_t *frame = packet + 24U;
    frame[0] = 0x80U; /* Beacon management frame. */
    for (uint32_t i = 4U; i < 10U; ++i) frame[i] = 0xFFU;
    const uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    memcpy(frame + 10U, bssid, sizeof(bssid));
    memcpy(frame + 16U, bssid, sizeof(bssid));
    frame[32] = 100U; /* Beacon interval, after 24-byte header + timestamp. */
    frame[34] = 1U;   /* Capability: ESS. */
    frame[36] = 0U; frame[37] = 4U;
    memcpy(frame + 38U, "TEST", 4U);
    frame[42] = 3U; frame[43] = 1U; frame[44] = 6U;
    *actual = 69U;
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
    if (rtl8188eu_probe(1, 1, 0x1234, 0x8179, valid, sizeof(valid)) != 0)
        return 2;
    if (rtl8188eu_probe(1, 1, 0x0BDA, 0x8179, valid, sizeof(valid)) != 1)
        return 3;
    if (configured_in != 0x81 || configured_out != 0x02 ||
        packet_in != 64 || packet_out != 64)
        return 4;

    rtl8188eu_poll();
    if (!firmware_read_seen || registers[0x1000] != 0xA0U ||
        registers[0x1007] != 0xA7U)
        return 5;
    rtl8188eu_poll();
    net_wifi_scan_result_t results[2];
    size_t result_count = rtl8188eu_get_passive_results(results, 2U);
    if (result_count != 1U || results[0].ssid_length != 4U ||
        memcmp(results[0].ssid, "TEST", 4U) != 0 ||
        results[0].channel != 6U ||
        memcmp(results[0].bssid, "\\x02\\x11\\x22\\x33\\x44\\x55", 6U) != 0)
        return 6;
    if (!rtl8188eu_port_disconnected(1U))
        return 7;
    return 0;
}
