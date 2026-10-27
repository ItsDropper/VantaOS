/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "usb_bluetooth.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define CONFIG_PREFIX 9U
#define IFACE 9U
#define ENDPOINT 7U

static const uint8_t valid_configuration[] = {
    9, 2, 39, 0, 1, 1, 0, 0x80, 50,
    IFACE, 4, 3, 0, 3, 0xE0, 0x01, 0x01, 0,
    ENDPOINT, 5, 0x81, 3, 16, 0, 1,
    ENDPOINT, 5, 0x82, 2, 64, 0, 0,
    ENDPOINT, 5, 0x02, 2, 64, 0, 0
};

static void test_complete_hci_interface(void)
{
    usb_bluetooth_hci_candidate_t candidate;
    assert(usb_bluetooth_find_hci_interface(valid_configuration,
                                              sizeof(valid_configuration),
                                              &candidate));
    assert(candidate.interface_number == 3);
    assert(candidate.event_endpoint == 0x81);
    assert(candidate.acl_in_endpoint == 0x82);
    assert(candidate.acl_out_endpoint == 0x02);
    assert(candidate.event_interval == 1);
    assert(candidate.event_max_packet == 16);
    assert(candidate.acl_in_max_packet == 64);
    assert(candidate.acl_out_max_packet == 64);
}

static void test_incomplete_interface_is_not_claimed(void)
{
    uint8_t configuration[sizeof(valid_configuration)];
    usb_bluetooth_hci_candidate_t candidate;
    for (unsigned int i = 0; i < sizeof(valid_configuration); ++i)
        configuration[i] = valid_configuration[i];
    /* Remove the ACL OUT endpoint by shortening the reported stream. */
    assert(!usb_bluetooth_find_hci_interface(configuration,
                                               sizeof(configuration) - ENDPOINT,
                                               &candidate));
    assert(candidate.event_endpoint == 0);
    assert(candidate.acl_in_endpoint == 0);
}

static void test_malformed_lengths_are_rejected(void)
{
    usb_bluetooth_hci_candidate_t candidate;
    uint8_t malformed[] = {9, 2, 9, 0, 1, 1, 0, 0x80, 50, 0};
    assert(!usb_bluetooth_find_hci_interface(malformed, sizeof(malformed),
                                               &candidate));
    assert(!usb_bluetooth_find_hci_interface(valid_configuration, 1,
                                               &candidate));
    assert(!usb_bluetooth_find_hci_interface(NULL,
                                               sizeof(valid_configuration),
                                               &candidate));
    assert(!usb_bluetooth_find_hci_interface(valid_configuration,
                                               sizeof(valid_configuration),
                                               NULL));
}

static void test_malformed_tail_after_valid_interface_is_rejected(void)
{
    uint8_t configuration[sizeof(valid_configuration) + 2U];
    usb_bluetooth_hci_candidate_t candidate;
    for (unsigned int i = 0; i < sizeof(valid_configuration); ++i)
        configuration[i] = valid_configuration[i];
    configuration[sizeof(valid_configuration)] = 0U;
    configuration[sizeof(valid_configuration) + 1U] = 0xFFU;
    assert(!usb_bluetooth_find_hci_interface(configuration,
                                               sizeof(configuration),
                                               &candidate));
    assert(candidate.event_endpoint == 0U);
}

static void test_configuration_header_is_validated(void)
{
    uint8_t configuration[sizeof(valid_configuration)];
    usb_bluetooth_hci_candidate_t candidate;
    for (unsigned int i = 0; i < sizeof(valid_configuration); ++i)
        configuration[i] = valid_configuration[i];

    configuration[2] = (uint8_t)(sizeof(configuration) - 1U);
    configuration[3] = 0U;
    assert(!usb_bluetooth_find_hci_interface(configuration,
                                               sizeof(configuration),
                                               &candidate));
    assert(candidate.event_endpoint == 0U);

    for (unsigned int i = 0; i < sizeof(valid_configuration); ++i)
        configuration[i] = valid_configuration[i];
    configuration[1] = 4U; /* Interface descriptor, not configuration. */
    assert(!usb_bluetooth_find_hci_interface(configuration,
                                               sizeof(configuration),
                                               &candidate));
}

static void test_non_bluetooth_interface_is_ignored(void)
{
    uint8_t configuration[sizeof(valid_configuration)];
    usb_bluetooth_hci_candidate_t candidate;
    for (unsigned int i = 0; i < sizeof(valid_configuration); ++i)
        configuration[i] = valid_configuration[i];
    configuration[CONFIG_PREFIX + 5U] = 3U;
    assert(!usb_bluetooth_find_hci_interface(configuration,
                                               sizeof(configuration),
                                               &candidate));
}

int main(void)
{
    test_complete_hci_interface();
    test_incomplete_interface_is_not_claimed();
    test_malformed_lengths_are_rejected();
    test_malformed_tail_after_valid_interface_is_rejected();
    test_configuration_header_is_validated();
    test_non_bluetooth_interface_is_ignored();
    puts("USB Bluetooth descriptor tests passed");
    return 0;
}
