#include "rtl8188eu.h"
#include "../drivers/usb/rtl8188eu_phy_tables.h"
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
static uint8_t firmware_read_fail;
static uint8_t scan_request_valid;

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
                          (bytes[32] == 0x80U || bytes[32] == 0x40U);
    if (bytes[32] == 0x40U) {
        scan_request_valid = bytes[36] == 0xFFU && bytes[41] == 0xFFU &&
            bytes[42] == 0x02U && bytes[43] == 0x11U &&
            bytes[44] == 0x22U && bytes[45] == 0x33U &&
            bytes[46] == 0x44U && bytes[47] == 0x55U &&
            bytes[48] == 0xFFU && bytes[53] == 0xFFU &&
            bytes[56] == 0U && bytes[57] == 0U &&
            bytes[58] == 1U && bytes[59] == 8U;
    }
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
    if (firmware_read_fail || cluster != 3U || size != 40U || capacity < size || !buffer)
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

static int test_efuse_logical_map_decoder(void)
{
    uint8_t physical[32], logical[512];
    memset(physical, 0xFF, sizeof(physical));
    /* Section 26, word 3: three-byte extended header, logical 0xD6-0xD7. */
    physical[0] = 0x4FU;
    physical[1] = 0x0FU;
    physical[2] = 0x37U;
    physical[3] = 0xA6U;
    physical[4] = 0xA7U;
    /* Section 27, words 0-2: logical bytes 0xD8-0xDD. */
    physical[5] = 0x6FU;
    physical[6] = 0x0FU;
    physical[7] = 0x38U;
    physical[8] = 0xA8U;
    physical[9] = 0xA9U;
    physical[10] = 0xAAU;
    physical[11] = 0xABU;
    physical[12] = 0xACU;
    physical[13] = 0xADU;
    if (!rtl8188eu_decode_efuse_map(physical, sizeof(physical),
                                    logical, sizeof(logical)))
        return 0;
    if (logical[0xD6U] != 0xA6U || logical[0xD7U] != 0xA7U ||
        logical[0xD8U] != 0xA8U || logical[0xDCU] != 0xACU ||
        logical[0xDDU] != 0xADU || logical[0xD5U] != 0xFFU)
        return 0;

    /* Two-byte extended header: section 2, word 3. */
    memset(physical, 0xFF, sizeof(physical));
    physical[0] = 0x4FU;
    physical[1] = 0x07U;
    physical[2] = 0xB6U;
    physical[3] = 0xB7U;
    if (!rtl8188eu_decode_efuse_map(physical, sizeof(physical),
                                    logical, sizeof(logical)) ||
        logical[0x16U] != 0xB6U || logical[0x17U] != 0xB7U)
        return 0;
    /*
     * 0x5F is a regular section-5 header with all words disabled,
     * not an extended header. The following 0x00 header must still map
     * its eight bytes into logical section 0.
     */
    memset(physical, 0xFF, sizeof(physical));
    physical[0] = 0x5FU;
    physical[1] = 0x00U;
    for (size_t i = 0U; i < 8U; ++i)
        physical[2U + i] = (uint8_t)(0x11U + i);
    if (!rtl8188eu_decode_efuse_map(physical, sizeof(physical),
                                    logical, sizeof(logical)))
        return 0;
    for (size_t i = 0U; i < 8U; ++i)
        if (logical[i] != (uint8_t)(0x11U + i))
            return 0;
    if (logical[16U] != 0xFFU)
        return 0;

    memset(physical, 0xFF, sizeof(physical));
    physical[0] = 0x4FU;
    if (rtl8188eu_decode_efuse_map(physical, 1U, logical, sizeof(logical)))
        return 0;
    physical[1] = 0x0FU;
    if (rtl8188eu_decode_efuse_map(physical, 2U, logical, sizeof(logical)))
        return 0;
    if (rtl8188eu_decode_efuse_map(physical, sizeof(physical), logical, 8U))
        return 0;

    /* Section 64 is outside the RTL8188E 512-byte logical map. */
    memset(physical, 0xFF, sizeof(physical));
    physical[0] = 0x0FU;
    physical[1] = 0x0FU;
    physical[2] = 0x80U;
    if (rtl8188eu_decode_efuse_map(physical, sizeof(physical),
                                   logical, sizeof(logical)))
        return 0;
    return 1;
}


static uint32_t test_phy_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return ~crc;
}

static void test_phy_put_le16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void test_phy_put_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void test_phy_record(uint8_t *record, uint8_t op, uint8_t phase,
                            uint16_t address, uint32_t mask, uint32_t value)
{
    memset(record, 0, 16U);
    record[0] = op;
    record[1] = phase;
    test_phy_put_le16(record + 4U, address);
    test_phy_put_le32(record + 8U, mask);
    test_phy_put_le32(record + 12U, value);
}

static int test_efuse_tx_power_calibration(void)
{
    uint8_t logical[512];
    rtl8188eu_tx_power_calibration_t calibration;
    memset(logical, 0xFF, sizeof(logical));
    for (size_t i = 0U; i < 6U; ++i)
        logical[0x10U + i] = (uint8_t)(0x20U + i);
    for (size_t i = 0U; i < 5U; ++i)
        logical[0x16U + i] = (uint8_t)(0x30U + i);
    /* Low nibble OFDM = -2; high nibble HT20 = +3. */
    logical[0x1BU] = 0x3EU;

    if (!rtl8188eu_decode_tx_power_calibration(logical, sizeof(logical),
                                               &calibration))
        return 0;
    for (size_t i = 0U; i < 6U; ++i)
        if (calibration.cck_base[i] != (uint8_t)(0x20U + i))
            return 0;
    for (size_t i = 0U; i < 5U; ++i)
        if (calibration.ht40_base[i] != (uint8_t)(0x30U + i))
            return 0;
    if (calibration.ofdm_diff != -2 || calibration.ht20_diff != 3)
        return 0;

    /* Channel 14 is disabled by policy, so its CCK group may be blank. */
    logical[0x15U] = 0xFFU;
    if (!rtl8188eu_decode_tx_power_calibration(logical, sizeof(logical),
                                                &calibration))
        return 0;
    logical[0x15U] = 0x25U;

    logical[0x11U] = 0xFFU;
    if (rtl8188eu_decode_tx_power_calibration(logical, sizeof(logical),
                                               &calibration))
        return 0;
    logical[0x11U] = 0x21U;
    logical[0x12U] = 0x40U;
    if (rtl8188eu_decode_tx_power_calibration(logical, sizeof(logical),
                                               &calibration))
        return 0;
    if (rtl8188eu_decode_tx_power_calibration(logical, 511U, &calibration))
        return 0;
    return 1;
}

static int test_tx_power_index_calculation(void)
{
    rtl8188eu_tx_power_calibration_t calibration = {
        .cck_base = {10U, 20U, 30U, 40U, 50U, 60U},
        .ht40_base = {12U, 22U, 32U, 42U, 52U},
        .ofdm_diff = -3,
        .ht20_diff = 4
    };
    rtl8188eu_tx_power_indices_t indices;
    if (!rtl8188eu_calculate_tx_power_indices(&calibration, 1U, &indices) ||
        indices.cck != 10U || indices.ht40 != 12U ||
        indices.ofdm != 9U || indices.ht20 != 16U)
        return 0;
    if (!rtl8188eu_calculate_tx_power_indices(&calibration, 4U, &indices) ||
        indices.cck != 20U || indices.ht40 != 22U ||
        indices.ofdm != 19U || indices.ht20 != 26U)
        return 0;
    if (!rtl8188eu_calculate_tx_power_indices(&calibration, 7U, &indices) ||
        indices.cck != 30U || indices.ht40 != 32U)
        return 0;
    if (!rtl8188eu_calculate_tx_power_indices(&calibration, 10U, &indices) ||
        indices.cck != 40U || indices.ht40 != 42U)
        return 0;
    if (!rtl8188eu_calculate_tx_power_indices(&calibration, 13U, &indices) ||
        indices.cck != 50U || indices.ht40 != 52U ||
        indices.ofdm != 49U || indices.ht20 != 56U)
        return 0;
    calibration.ht40_base[4] = 2U;
    calibration.ofdm_diff = -8;
    calibration.ht20_diff = 7;
    if (!rtl8188eu_calculate_tx_power_indices(&calibration, 13U, &indices) ||
        indices.cck != 50U || indices.ht40 != 2U ||
        indices.ofdm != 0U || indices.ht20 != 9U)
        return 0;
    if (rtl8188eu_calculate_tx_power_indices(&calibration, 0U, &indices) ||
        rtl8188eu_calculate_tx_power_indices(&calibration, 14U, &indices))
        return 0;
    calibration.ht40_base[4] = 63U;
    calibration.ofdm_diff = 7;
    calibration.ht20_diff = -8;
    if (!rtl8188eu_calculate_tx_power_indices(&calibration, 13U, &indices) ||
        indices.ofdm != 63U || indices.ht20 != 55U)
        return 0;
    return 1;
}

static int test_phy_profile_validator(void)
{
    uint8_t profile[24U + 6U * 16U];
    memset(profile, 0, sizeof(profile));
    memcpy(profile, "V8EUPHY1", 8U);
    test_phy_put_le16(profile + 8U, 1U);
    test_phy_put_le16(profile + 10U, 24U);
    test_phy_put_le32(profile + 12U, (uint32_t)sizeof(profile));
    profile[20U] = 2U;
    test_phy_put_le16(profile + 22U, 6U);

    uint8_t *records = profile + 24U;
    test_phy_record(records + 0U * 16U, 1U, 1U, 0x0020U, 0U, 0U);
    test_phy_record(records + 1U * 16U, 4U, 2U, 0x0800U, 0U, 0U);
    test_phy_record(records + 2U * 16U, 4U, 3U, 0x0804U, 0U, 0U);
    test_phy_record(records + 3U * 16U, 5U, 4U, 0x0018U,
                     0x00000C00U, 0x00000C00U);
    test_phy_record(records + 4U * 16U, 4U, 5U, 0x0810U, 0U, 0U);
    test_phy_record(records + 5U * 16U, 9U, 5U, 0x0810U, 1U, 0U);
    test_phy_put_le32(profile + 16U, test_phy_crc32(records, sizeof(profile) - 24U));

    if (!rtl8188eu_validate_phy_profile(profile, sizeof(profile), 2U))
        return 0;

    /* TX-power programming is only valid when the profile explicitly selects 20 MHz. */
    test_phy_put_le32(records + 3U * 16U + 12U, 0U);
    test_phy_put_le32(profile + 16U, test_phy_crc32(records, sizeof(profile) - 24U));
    if (rtl8188eu_validate_phy_profile(profile, sizeof(profile), 2U))
        return 0;
    test_phy_put_le32(records + 3U * 16U + 12U, 0x00000C00U);
    test_phy_put_le32(profile + 16U, test_phy_crc32(records, sizeof(profile) - 24U));
    if (!rtl8188eu_validate_phy_profile(profile, sizeof(profile), 2U))
        return 0;

    /* CRC must cover every operation byte. */
    profile[24U + 12U] ^= 1U;
    if (rtl8188eu_validate_phy_profile(profile, sizeof(profile), 2U))
        return 0;
    profile[24U + 12U] ^= 1U;

    /* A board-specific profile must not silently match another chip cut. */
    profile[20U] = 3U;
    if (rtl8188eu_validate_phy_profile(profile, sizeof(profile), 2U))
        return 0;
    /* Wildcard chip cuts are unsafe for calibration-bearing RF profiles. */
    profile[20U] = 0xFFU;
    if (rtl8188eu_validate_phy_profile(profile, sizeof(profile), 2U))
        return 0;
    profile[20U] = 2U;

    /* BB operations cannot target the MAC register window. */
    test_phy_put_le16(records + 1U * 16U + 4U, 0x07FFU);
    test_phy_put_le32(profile + 16U, test_phy_crc32(records, sizeof(profile) - 24U));
    if (rtl8188eu_validate_phy_profile(profile, sizeof(profile), 2U))
        return 0;

    /* Restore the address but deliberately reorder phases. */
    test_phy_put_le16(records + 1U * 16U + 4U, 0x0800U);
    records[2U * 16U + 1U] = 1U;
    test_phy_put_le32(profile + 16U, test_phy_crc32(records, sizeof(profile) - 24U));
    if (rtl8188eu_validate_phy_profile(profile, sizeof(profile), 2U))
        return 0;
    return 1;
}


static int test_reference_phy_tables(void)
{
    if(sizeof(rtl8188eu_mac_init)/sizeof(rtl8188eu_mac_init[0])!=91U ||
       sizeof(rtl8188eu_bb_init)/sizeof(rtl8188eu_bb_init[0])!=192U ||
       sizeof(rtl8188eu_agc_init)/sizeof(rtl8188eu_agc_init[0])!=128U ||
       sizeof(rtl8188eu_rf_init)/sizeof(rtl8188eu_rf_init[0])!=95U)
        return 0;
    if(rtl8188eu_mac_init[0].address!=0x026U ||
       rtl8188eu_mac_init[0].value!=0x41U ||
       rtl8188eu_bb_init[0].address!=0x800U ||
       rtl8188eu_bb_init[0].value!=0x80040000U ||
       rtl8188eu_agc_init[0]!=0xFB000001U ||
       rtl8188eu_rf_init[0].address!=0x00U ||
       rtl8188eu_rf_init[0].value!=0x30000U)
        return 0;
    unsigned delays=0U;
    for(size_t i=0U;i<sizeof(rtl8188eu_rf_init)/sizeof(rtl8188eu_rf_init[0]);++i)
        if(rtl8188eu_rf_init[i].address==0xFEU) {
            if(rtl8188eu_rf_init[i].value<1U) return 0;
            ++delays;
        }
    return delays==4U;
}

int main(void)
{
    if (!test_reference_phy_tables()) return 25;
    if (!test_efuse_logical_map_decoder())
        return 19;
    if (!test_efuse_tx_power_calibration())
        return 23;
    if (!test_tx_power_index_calculation())
        return 24;
    if (!test_phy_profile_validator())
        return 20;
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
    if (rtl8188eu_get_status() != RTL8188EU_STATUS_FIRMWARE_PENDING ||
        configured_in != 0x81 || configured_out != 0x02 ||
        packet_in != 64 || packet_out != 64 ||
        !rtl8188eu_port_is_bound(1U) || rtl8188eu_port_is_bound(2U))
        return 4;

    /* The current driver has one global device context: reject a second
     * adapter without disturbing the first adapter's binding. */
    if (rtl8188eu_probe(2U, 2U, 0x0BDAU, 0x8179U, valid,
                        sizeof(valid)) != 0 ||
        !rtl8188eu_port_is_bound(1U) || rtl8188eu_port_is_bound(2U) ||
        rtl8188eu_get_status() != RTL8188EU_STATUS_FIRMWARE_PENDING)
        return 21;

    registers[0x610] = 0x02U; registers[0x611] = 0x11U;
    registers[0x612] = 0x22U; registers[0x613] = 0x33U;
    registers[0x614] = 0x44U; registers[0x615] = 0x55U;
    rtl8188eu_poll();
    if (rtl8188eu_get_status() != RTL8188EU_STATUS_FIRMWARE_READY ||
        !firmware_read_seen || registers[0x1000] != 0xA0U ||
        registers[0x1007] != 0xA7U || !checksum_reported ||
        checksum_reported_too_early)
        return 5;
    /*
     * Firmware-ready is not radio-ready. Since this driver does not yet
     * perform verified eFuse/PHY/RF initialization and calibration, it must
     * fail closed rather than transmit a probe on an unknown radio state.
     */
    if (rtl8188eu_start_active_scan() || scan_request_valid || tx_seen)
        return 18;
    /* No successful association may be reported while radio initialization
     * is blocked. This is a negative test, not a simulated Wi-Fi success. */
    const uint8_t test_ssid[] = {'T', 'E', 'S', 'T'};
    const uint8_t test_passphrase[] = {'t','e','s','t','p','a','s','s'};
    if (rtl8188eu_connect(test_ssid, sizeof(test_ssid),
                          test_passphrase, sizeof(test_passphrase)) != 0 ||
        rtl8188eu_get_connection_state() != RTL8188EU_CONNECTION_IDLE)
        return 22;
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
    /* Raw TX must also remain disabled until radio initialization succeeds. */
    if (rtl8188eu_transmit_frame(management_frame, sizeof(management_frame)) ||
        tx_seen || tx_descriptor_valid)
        return 13;
    if (rtl8188eu_transmit_frame(management_frame, 23U))
        return 14;
    if (!rtl8188eu_port_disconnected(1U) || rtl8188eu_port_is_bound(1U) ||
        rtl8188eu_get_status() != RTL8188EU_STATUS_UNBOUND)
        return 10;

    /* A firmware read failure must not leave the driver reporting READY. */
    firmware_read_fail = 1U;
    if (rtl8188eu_probe(1, 1, 0x0BDA, 0x8179, valid, sizeof(valid)) != 1)
        return 15;
    rtl8188eu_poll();
    if (rtl8188eu_get_status() != RTL8188EU_STATUS_FIRMWARE_FAILED)
        return 16;
    if (!rtl8188eu_port_disconnected(1U))
        return 17;
    return 0;
}
