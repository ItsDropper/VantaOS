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
#define EVENT_DESCRIPTOR_OFFSET (CONFIG_PREFIX + IFACE)
#define ACL_IN_DESCRIPTOR_OFFSET (EVENT_DESCRIPTOR_OFFSET + ENDPOINT)

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

static void test_hci_event_and_acl_framing(void)
{
    const uint8_t reset_complete[] = {0x0EU, 0x04U, 0x01U,
                                      0x03U, 0x0CU, 0x00U};
    const uint8_t wrong_opcode[] = {0x0EU, 0x04U, 0x01U,
                                    0x01U, 0x10U, 0x00U};
    const uint8_t failed_reset[] = {0x0EU, 0x04U, 0x01U,
                                    0x03U, 0x0CU, 0x01U};
    const uint8_t short_event[] = {0x0EU, 0x04U, 0x01U};
    const uint8_t acl[] = {0x01U, 0x20U, 0x02U, 0x00U, 0xAAU, 0xBBU};
    const uint8_t acl_truncated[] = {0x01U, 0x20U, 0x03U, 0x00U,
                                     0xAAU, 0xBBU};

    assert(usb_bluetooth_hci_event_valid(reset_complete,
                                          sizeof(reset_complete)));
    assert(!usb_bluetooth_hci_event_valid(reset_complete,
                                           sizeof(reset_complete) - 1U));
    assert(!usb_bluetooth_hci_event_valid(short_event, sizeof(short_event)));
    assert(usb_bluetooth_hci_reset_status(reset_complete,
                                          sizeof(reset_complete)) == 0);
    assert(usb_bluetooth_hci_reset_complete(reset_complete,
                                             sizeof(reset_complete)));
    assert(!usb_bluetooth_hci_reset_complete(wrong_opcode,
                                              sizeof(wrong_opcode)));
    assert(usb_bluetooth_hci_reset_status(failed_reset,
                                          sizeof(failed_reset)) == 1);
    assert(!usb_bluetooth_hci_reset_complete(failed_reset,
                                              sizeof(failed_reset)));
    assert(usb_bluetooth_hci_reset_status(wrong_opcode,
                                          sizeof(wrong_opcode)) == -1);
    assert(usb_bluetooth_acl_packet_valid(acl, sizeof(acl)));
    assert(!usb_bluetooth_acl_packet_valid(acl_truncated,
                                            sizeof(acl_truncated)));
    assert(!usb_bluetooth_acl_packet_valid(acl, sizeof(acl) - 1U));
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

static void test_acl_queue_preserves_messages_on_small_buffer(void)
{
    usb_bluetooth_acl_queue_t queue = {0};
    const uint8_t first[] = {0x01U, 0x20U, 0x02U, 0x00U, 0xAAU, 0xBBU};
    const uint8_t second[] = {0x02U, 0x20U, 0x01U, 0x00U, 0xCCU};
    uint8_t output[16] = {0};

    usb_bluetooth_acl_queue_reset(&queue);
    assert(usb_bluetooth_acl_queue_pop(&queue, output, sizeof(output)) == -11);
    assert(usb_bluetooth_acl_queue_push(&queue, first, sizeof(first)) == 0);
    assert(usb_bluetooth_acl_queue_pop(&queue, output, 2U) == -90);
    assert(queue.count == 1U);
    assert(usb_bluetooth_acl_queue_push(&queue, second, sizeof(second)) == 0);
    assert(usb_bluetooth_acl_queue_push(&queue, first, sizeof(first)) == -11);
    assert(usb_bluetooth_acl_queue_pop(&queue, output, sizeof(output)) ==
           (int)sizeof(first));
    assert(output[4] == 0xAAU && output[5] == 0xBBU);
    assert(usb_bluetooth_acl_queue_pop(&queue, output, sizeof(output)) ==
           (int)sizeof(second));
    assert(output[4] == 0xCCU);
    assert(usb_bluetooth_acl_queue_pop(&queue, output, sizeof(output)) == -11);
    assert(usb_bluetooth_acl_queue_push(&queue, first, sizeof(first) - 1U) == -22);
    usb_bluetooth_acl_queue_reset(&queue);
    assert(queue.count == 0U && queue.head == 0U);
}

static void test_acl_queue_wraparound_and_invariants(void)
{
    usb_bluetooth_acl_queue_t queue = {0};
    const uint8_t packet_a[] = {0x01U, 0x20U, 0x01U, 0x00U, 0xA1U};
    const uint8_t packet_b[] = {0x02U, 0x20U, 0x01U, 0x00U, 0xB2U};
    const uint8_t packet_c[] = {0x03U, 0x20U, 0x01U, 0x00U, 0xC3U};
    uint8_t output[16] = {0};

    usb_bluetooth_acl_queue_reset(&queue);
    assert(usb_bluetooth_acl_queue_push(&queue, packet_a, sizeof(packet_a)) == 0);
    assert(usb_bluetooth_acl_queue_push(&queue, packet_b, sizeof(packet_b)) == 0);
    assert(usb_bluetooth_acl_queue_pop(&queue, output, sizeof(output)) == 5);
    assert(output[4] == 0xA1U);
    assert(usb_bluetooth_acl_queue_push(&queue, packet_c, sizeof(packet_c)) == 0);
    assert(usb_bluetooth_acl_queue_pop(&queue, output, sizeof(output)) == 5);
    assert(output[4] == 0xB2U);
    assert(usb_bluetooth_acl_queue_pop(&queue, output, sizeof(output)) == 5);
    assert(output[4] == 0xC3U);
    assert(queue.count == 0U && queue.head == 1U);

    queue.head = USB_BLUETOOTH_ACL_QUEUE_DEPTH;
    assert(usb_bluetooth_acl_queue_push(&queue, packet_a, sizeof(packet_a)) == -22);
    assert(usb_bluetooth_acl_queue_pop(&queue, output, sizeof(output)) == -22);
    usb_bluetooth_acl_queue_reset(&queue);
    queue.count = USB_BLUETOOTH_ACL_QUEUE_DEPTH + 1U;
    assert(usb_bluetooth_acl_queue_push(&queue, packet_a, sizeof(packet_a)) == -22);
    assert(usb_bluetooth_acl_queue_pop(&queue, output, sizeof(output)) == -22);

    usb_bluetooth_acl_queue_reset(&queue);
    assert(queue.packets[0][0] == 0U && queue.packets[1][4] == 0U);
}

static void test_transport_readiness_gate(void)
{
    assert(!usb_bluetooth_transport_ready(0, 0));
    assert(!usb_bluetooth_transport_ready(0, 1));
    assert(!usb_bluetooth_transport_ready(1, 1));
    assert(usb_bluetooth_transport_ready(1, 0));
}

static void test_acl_queue_rejects_corrupt_stored_length(void)
{
    usb_bluetooth_acl_queue_t queue = {0};
    const uint8_t packet[] = {0x01U, 0x20U, 0x01U, 0x00U, 0xAAU};
    uint8_t output[USB_BLUETOOTH_ACL_MAX_PACKET + 8U] = {0};

    usb_bluetooth_acl_queue_reset(&queue);
    assert(usb_bluetooth_acl_queue_push(&queue, packet, sizeof(packet)) == 0);
    queue.lengths[queue.head] = USB_BLUETOOTH_ACL_MAX_PACKET + 1U;
    assert(usb_bluetooth_acl_queue_pop(&queue, output, sizeof(output)) == -22);
    assert(queue.count == 1U);

    usb_bluetooth_acl_queue_reset(&queue);
    assert(usb_bluetooth_acl_queue_push(&queue, packet, sizeof(packet)) == 0);
    queue.lengths[queue.head] = 4U;
    assert(usb_bluetooth_acl_queue_pop(&queue, output, sizeof(output)) == -22);
    assert(queue.count == 1U);
}

static void test_reset_state_transitions_are_idempotent(void)
{
    const uint8_t success[] = {0x0EU, 0x04U, 0x01U,
                               0x03U, 0x0CU, 0x00U};
    const uint8_t failure[] = {0x0EU, 0x04U, 0x01U,
                               0x03U, 0x0CU, 0x01U};
    const uint8_t unrelated[] = {0x0EU, 0x04U, 0x01U,
                                 0x01U, 0x10U, 0x00U};
    uint8_t complete = 0U;
    uint8_t failed = 0U;

    assert(usb_bluetooth_update_reset_state(unrelated, sizeof(unrelated),
                                             &complete, &failed) == 0);
    assert(complete == 0U && failed == 0U);
    assert(usb_bluetooth_update_reset_state(failure, sizeof(failure),
                                             &complete, &failed) == -1);
    assert(complete == 0U && failed == 1U);
    assert(usb_bluetooth_update_reset_state(failure, sizeof(failure),
                                             &complete, &failed) == 0);
    assert(complete == 0U && failed == 1U);
    assert(!usb_bluetooth_transport_ready(complete, failed));
    assert(usb_bluetooth_update_reset_state(success, sizeof(success),
                                             &complete, &failed) == 1);
    assert(complete == 1U && failed == 0U);
    assert(usb_bluetooth_update_reset_state(success, sizeof(success),
                                             &complete, &failed) == 0);
    assert(usb_bluetooth_transport_ready(complete, failed));
    assert(usb_bluetooth_update_reset_state(success, sizeof(success),
                                             NULL, &failed) == 0);
}

static void test_endpoint_packet_bounds_and_interval(void)
{
    uint8_t configuration[sizeof(valid_configuration)];
    usb_bluetooth_hci_candidate_t candidate;

    for (unsigned int i = 0U; i < sizeof(valid_configuration); ++i)
        configuration[i] = valid_configuration[i];
    configuration[EVENT_DESCRIPTOR_OFFSET + 4U] = 65U;
    configuration[EVENT_DESCRIPTOR_OFFSET + 5U] = 0U;
    assert(!usb_bluetooth_find_hci_interface(configuration,
                                              sizeof(configuration),
                                              &candidate));

    for (unsigned int i = 0U; i < sizeof(valid_configuration); ++i)
        configuration[i] = valid_configuration[i];
    configuration[EVENT_DESCRIPTOR_OFFSET + 6U] = 0U;
    assert(!usb_bluetooth_find_hci_interface(configuration,
                                              sizeof(configuration),
                                              &candidate));

    for (unsigned int i = 0U; i < sizeof(valid_configuration); ++i)
        configuration[i] = valid_configuration[i];
    /* First bulk-IN endpoint's wMaxPacketSize becomes 1025. */
    configuration[ACL_IN_DESCRIPTOR_OFFSET + 4U] = 1U;
    configuration[ACL_IN_DESCRIPTOR_OFFSET + 5U] = 4U;
    assert(!usb_bluetooth_find_hci_interface(configuration,
                                              sizeof(configuration),
                                              &candidate));

    for (unsigned int i = 0U; i < sizeof(valid_configuration); ++i)
        configuration[i] = valid_configuration[i];
    /* Reject unsupported high-bandwidth/reserved wMaxPacketSize bits. */
    configuration[ACL_IN_DESCRIPTOR_OFFSET + 5U] = 0x08U;
    assert(!usb_bluetooth_find_hci_interface(configuration,
                                              sizeof(configuration),
                                              &candidate));
}


static void test_endpoint_address_and_transfer_roles(void)
{
    uint8_t configuration[sizeof(valid_configuration)];
    usb_bluetooth_hci_candidate_t candidate;

    /* Endpoint zero is reserved and cannot satisfy an HCI endpoint role. */
    for (unsigned int i = 0U; i < sizeof(valid_configuration); ++i)
        configuration[i] = valid_configuration[i];
    configuration[EVENT_DESCRIPTOR_OFFSET + 2U] = 0x80U;
    assert(!usb_bluetooth_find_hci_interface(configuration,
                                              sizeof(configuration),
                                              &candidate));

    /* Reserved endpoint-address bits must not be accepted. */
    for (unsigned int i = 0U; i < sizeof(valid_configuration); ++i)
        configuration[i] = valid_configuration[i];
    configuration[EVENT_DESCRIPTOR_OFFSET + 2U] = 0x91U;
    assert(!usb_bluetooth_find_hci_interface(configuration,
                                              sizeof(configuration),
                                              &candidate));

    /* An interrupt-OUT endpoint cannot substitute for the event endpoint. */
    for (unsigned int i = 0U; i < sizeof(valid_configuration); ++i)
        configuration[i] = valid_configuration[i];
    configuration[EVENT_DESCRIPTOR_OFFSET + 2U] = 0x01U;
    assert(!usb_bluetooth_find_hci_interface(configuration,
                                              sizeof(configuration),
                                              &candidate));
}

static void test_failed_parse_clears_candidate(void)
{
    uint8_t configuration[sizeof(valid_configuration)];
    usb_bluetooth_hci_candidate_t candidate;

    for (unsigned int i = 0U; i < sizeof(valid_configuration); ++i)
        configuration[i] = valid_configuration[i];
    configuration[0] = 1U;

    candidate = (usb_bluetooth_hci_candidate_t){
        .interface_number = 0xA5U,
        .event_endpoint = 0xA5U,
        .acl_in_endpoint = 0xA5U,
        .acl_out_endpoint = 0xA5U,
        .event_interval = 0xA5U,
        .event_max_packet = 0xA5A5U,
        .acl_in_max_packet = 0xA5A5U,
        .acl_out_max_packet = 0xA5A5U
    };
    assert(!usb_bluetooth_find_hci_interface(configuration,
                                              sizeof(configuration),
                                              &candidate));
    assert(candidate.interface_number == 0U);
    assert(candidate.event_endpoint == 0U);
    assert(candidate.acl_in_endpoint == 0U);
    assert(candidate.acl_out_endpoint == 0U);
    assert(candidate.event_interval == 0U);
    assert(candidate.event_max_packet == 0U);
    assert(candidate.acl_in_max_packet == 0U);
    assert(candidate.acl_out_max_packet == 0U);
}

static void test_descriptor_byte_mutations_are_bounded(void)
{
    uint8_t configuration[sizeof(valid_configuration)];
    usb_bluetooth_hci_candidate_t candidate;

    /*
     * Mutate each byte through every possible value. The parser must never
     * read outside this fixed input or publish a partial candidate.
     */
    for (unsigned int offset = 0U; offset < sizeof(valid_configuration);
         ++offset) {
        for (unsigned int value = 0U; value <= 255U; ++value) {
            for (unsigned int i = 0U; i < sizeof(valid_configuration); ++i)
                configuration[i] = valid_configuration[i];
            configuration[offset] = (uint8_t)value;
            candidate = (usb_bluetooth_hci_candidate_t){0};
            int matched = usb_bluetooth_find_hci_interface(
                configuration, sizeof(configuration), &candidate);
            if (!matched) {
                assert(candidate.interface_number == 0U);
                assert(candidate.event_endpoint == 0U);
                assert(candidate.acl_in_endpoint == 0U);
                assert(candidate.acl_out_endpoint == 0U);
                assert(candidate.event_max_packet == 0U);
                assert(candidate.acl_in_max_packet == 0U);
                assert(candidate.acl_out_max_packet == 0U);
            }
        }
    }
}

int main(void)
{
    test_complete_hci_interface();
    test_incomplete_interface_is_not_claimed();
    test_malformed_lengths_are_rejected();
    test_malformed_tail_after_valid_interface_is_rejected();
    test_configuration_header_is_validated();
    test_non_bluetooth_interface_is_ignored();
    test_hci_event_and_acl_framing();
    test_acl_queue_preserves_messages_on_small_buffer();
    test_acl_queue_wraparound_and_invariants();
    test_transport_readiness_gate();
    test_reset_state_transitions_are_idempotent();
    test_endpoint_packet_bounds_and_interval();
    test_endpoint_address_and_transfer_roles();
    test_failed_parse_clears_candidate();
    test_descriptor_byte_mutations_are_bounded();
    test_acl_queue_rejects_corrupt_stored_length();
    puts("USB Bluetooth descriptor tests passed");
    return 0;
}
