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
#include <string.h>

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

static void test_hci_command_encoding(void)
{
    uint8_t output[258];
    const uint8_t parameters[] = {0x12U, 0x34U, 0x56U};

    assert(usb_bluetooth_hci_command_encode(0x0C03U, NULL, 0U,
                                             output, sizeof(output)) == 3);
    assert(output[0] == 0x03U && output[1] == 0x0CU && output[2] == 0U);
    assert(usb_bluetooth_hci_command_encode(0x1234U, NULL, 0U,
                                             output, sizeof(output)) == 3);
    assert(output[0] == 0x34U && output[1] == 0x12U && output[2] == 0U);
    assert(usb_bluetooth_hci_command_encode(0x1234U, parameters,
                                             sizeof(parameters), output,
                                             sizeof(output)) == 6);
    assert(output[0] == 0x34U && output[1] == 0x12U &&
           output[2] == 3U && output[3] == 0x12U &&
           output[4] == 0x34U && output[5] == 0x56U);
    assert(usb_bluetooth_hci_command_encode(0U, NULL, 0U, output,
                                             sizeof(output)) == -22);
    assert(usb_bluetooth_hci_command_encode(0x1234U, parameters, 1U,
                                             NULL, sizeof(output)) == -22);
    assert(usb_bluetooth_hci_command_encode(0x1234U, NULL, 1U, output,
                                             sizeof(output)) == -22);
    assert(usb_bluetooth_hci_command_encode(0x1234U, parameters,
                                             sizeof(parameters), output,
                                             5U) == -90);

    uint8_t maximum[255];
    for (uint32_t i = 0U; i < sizeof(maximum); ++i)
        maximum[i] = (uint8_t)i;
    assert(usb_bluetooth_hci_command_encode(0xFC01U, maximum, 255U,
                                             output, sizeof(output)) == 258);
    assert(output[0] == 0x01U && output[1] == 0xFCU &&
           output[2] == 255U && output[3] == 0U &&
           output[257] == 254U);
    assert(usb_bluetooth_hci_command_encode(0xFC01U, maximum, 255U,
                                             output, 257U) == -90);
}

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

    /* Structurally sized packets still cannot use reserved HCI header values. */
    const uint8_t zero_handle[] = {0x00U, 0x00U, 0x00U, 0x00U};
    const uint8_t reserved_broadcast[] = {0x01U, 0xC0U, 0x00U, 0x00U};
    usb_bluetooth_acl_view_t view = {0};
    assert(usb_bluetooth_acl_packet_valid(zero_handle, sizeof(zero_handle)));
    assert(!usb_bluetooth_acl_parse(zero_handle, sizeof(zero_handle), &view));
    assert(view.payload == NULL && view.handle == 0U);
    assert(!usb_bluetooth_acl_parse(reserved_broadcast,
                                    sizeof(reserved_broadcast), &view));
    assert(view.payload == NULL && view.broadcast == 0U);
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

static void test_acl_queue_pop_by_handle(void)
{
    usb_bluetooth_acl_queue_t queue = {0};
    const uint8_t payload[] = {0xAAU, 0x55U};
    uint8_t first[16] = {0}, second[16] = {0}, output[16] = {0};
    usb_bluetooth_acl_queue_reset(&queue);
    assert(usb_bluetooth_l2cap_build_acl_packet(0x000BU, 0x0040U,
                                                payload, sizeof(payload),
                                                first, sizeof(first)) == 10);
    assert(usb_bluetooth_l2cap_build_acl_packet(0x000CU, 0x0040U,
                                                payload, sizeof(payload),
                                                second, sizeof(second)) == 10);
    assert(usb_bluetooth_acl_queue_push(&queue, first, 10U) == 0);
    assert(usb_bluetooth_acl_queue_push(&queue, second, 10U) == 0);
    assert(usb_bluetooth_acl_queue_pop_handle(&queue, 0x000CU, output,
                                              4U) == -90);
    assert(queue.count == 2U);
    assert(usb_bluetooth_acl_queue_pop_handle(&queue, 0x000CU, output,
                                              sizeof(output)) == 10);
    assert(memcmp(output, second, 10U) == 0);
    assert(queue.count == 1U);
    assert(usb_bluetooth_acl_queue_pop(&queue, output, sizeof(output)) == 10);
    assert(memcmp(output, first, 10U) == 0);
    assert(usb_bluetooth_acl_queue_pop_handle(&queue, 0x000BU, output,
                                              sizeof(output)) == -11);

    /* Removing the head entry must preserve the later packet and queue order. */
    usb_bluetooth_acl_queue_reset(&queue);
    assert(usb_bluetooth_acl_queue_push(&queue, first, 10U) == 0);
    assert(usb_bluetooth_acl_queue_push(&queue, second, 10U) == 0);
    assert(usb_bluetooth_acl_queue_pop_handle(&queue, 0x000BU, output,
                                              sizeof(output)) == 10);
    assert(memcmp(output, first, 10U) == 0 && queue.count == 1U);
    assert(usb_bluetooth_acl_queue_pop(&queue, output, sizeof(output)) == 10);
    assert(memcmp(output, second, 10U) == 0 && queue.count == 0U);
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
                assert(candidate.event_interval == 0U);
                assert(candidate.event_max_packet == 0U);
                assert(candidate.acl_in_max_packet == 0U);
                assert(candidate.acl_out_max_packet == 0U);
            }
        }
    }
}


static void test_hci_packet_framing_mutations_are_bounded(void)
{
    uint8_t packet[64];
    for (uint32_t length = 0U; length <= sizeof(packet); ++length) {
        for (uint32_t offset = 0U; offset < length; ++offset) {
            for (uint32_t value = 0U; value <= 255U; ++value) {
                for (uint32_t i = 0U; i < sizeof(packet); ++i)
                    packet[i] = (uint8_t)(i * 13U + length);
                packet[offset] = (uint8_t)value;

                if (usb_bluetooth_hci_event_valid(packet, length))
                    assert(length >= 2U &&
                           (uint32_t)packet[1] + 2U == length);

                if (usb_bluetooth_acl_packet_valid(packet, length)) {
                    uint32_t payload_length = (uint32_t)packet[2] |
                                              ((uint32_t)packet[3] << 8);
                    assert(length >= 4U && payload_length + 4U == length);
                }
            }
        }
    }
}

static void test_hci_event_queue_integrity(void)
{
    usb_bluetooth_hci_event_queue_t queue = {0};
    const uint8_t first[] = {0x0EU, 0x04U, 0x01U, 0x03U, 0x0CU, 0x00U};
    const uint8_t second[] = {0x0FU, 0x04U, 0x01U, 0x00U, 0x00U, 0x00U};
    uint8_t output[USB_BLUETOOTH_HCI_EVENT_MAX] = {0};
    usb_bluetooth_hci_event_queue_reset(&queue);
    assert(usb_bluetooth_hci_event_queue_pop(&queue, output, sizeof(output)) == -11);
    for (uint32_t i = 0U; i < USB_BLUETOOTH_HCI_EVENT_QUEUE_DEPTH; ++i)
        assert(usb_bluetooth_hci_event_queue_push(&queue,
               i % 2U == 0U ? first : second,
               i % 2U == 0U ? sizeof(first) : sizeof(second)) == 0);
    assert(usb_bluetooth_hci_event_queue_push(&queue, first, sizeof(first)) == -11);
    assert(usb_bluetooth_hci_event_queue_pop(&queue, output, 2U) == -90);
    assert(queue.count == USB_BLUETOOTH_HCI_EVENT_QUEUE_DEPTH);
    assert(usb_bluetooth_hci_event_queue_pop(&queue, output, sizeof(output)) ==
           (int)sizeof(first));
    assert(output[0] == 0x0EU && output[5] == 0U);
    assert(usb_bluetooth_hci_event_queue_pop(&queue, output, sizeof(output)) ==
           (int)sizeof(second));
    assert(output[0] == 0x0FU);
    assert(usb_bluetooth_hci_event_queue_pop(&queue, output, sizeof(output)) ==
           (int)sizeof(first));
    assert(usb_bluetooth_hci_event_queue_pop(&queue, output, sizeof(output)) ==
           (int)sizeof(second));
    assert(usb_bluetooth_hci_event_queue_pop(&queue, output, sizeof(output)) == -11);
    assert(usb_bluetooth_hci_event_queue_push(&queue, first,
                                              sizeof(first) - 1U) == -22);
    usb_bluetooth_hci_event_queue_reset(&queue);
    assert(queue.head == 0U && queue.count == 0U);
    for (uint32_t i = 0U; i < USB_BLUETOOTH_HCI_EVENT_QUEUE_DEPTH; ++i) {
        assert(queue.lengths[i] == 0U);
        for (uint32_t j = 0U; j < USB_BLUETOOTH_HCI_EVENT_MAX; ++j)
            assert(queue.packets[i][j] == 0U);
    }
}

static void test_acl_and_l2cap_views(void)
{
    const uint8_t signaling_acl[] = {
        0x01U, 0x20U, 0x0AU, 0x00U,
        0x06U, 0x00U, 0x01U, 0x00U,
        0x02U, 0x01U, 0x02U, 0x00U, 0x34U, 0x12U
    };
    const uint8_t malformed_acl[] = {
        0x01U, 0x20U, 0x09U, 0x00U,
        0x06U, 0x00U, 0x01U, 0x00U,
        0x02U, 0x01U, 0x02U, 0x00U, 0x34U
    };
    usb_bluetooth_acl_view_t acl = {0};
    usb_bluetooth_l2cap_view_t l2cap = {0};

    assert(usb_bluetooth_acl_parse(signaling_acl, sizeof(signaling_acl), &acl));
    assert(acl.handle == 1U && acl.packet_boundary == 2U &&
           acl.broadcast == 0U && acl.payload_length == 10U);
    assert(usb_bluetooth_l2cap_parse(signaling_acl, sizeof(signaling_acl),
                                     &l2cap));
    assert(l2cap.channel_id == 1U && l2cap.payload_length == 6U);
    assert(usb_bluetooth_l2cap_signaling_valid(l2cap.payload,
                                               l2cap.payload_length));
    assert(!usb_bluetooth_l2cap_parse(malformed_acl, sizeof(malformed_acl),
                                      &l2cap));
    assert(l2cap.channel_id == 0U && l2cap.payload == NULL);

    uint8_t continuation[sizeof(signaling_acl)];
    for (size_t i = 0U; i < sizeof(signaling_acl); ++i)
        continuation[i] = signaling_acl[i];
    continuation[1] = 0x10U;
    assert(usb_bluetooth_acl_parse(continuation, sizeof(continuation), &acl));
    assert(acl.packet_boundary == 1U);
    assert(!usb_bluetooth_l2cap_parse(continuation, sizeof(continuation),
                                      &l2cap));
    assert(!usb_bluetooth_acl_parse(signaling_acl,
                                    sizeof(signaling_acl) - 1U, &acl));
    assert(acl.payload == NULL && acl.payload_length == 0U);
}

static void test_hci_link_lifecycle_and_acl_routing(void)
{
    const uint8_t connected[] = {
        0x03U, 0x0BU, 0x00U, 0x01U, 0x00U,
        0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x01U, 0x00U
    };
    const uint8_t disconnected[] = {0x05U, 0x04U, 0x00U, 0x01U, 0x00U, 0x13U};
    const uint8_t first[] = {0x01U, 0x20U, 0x07U, 0x00U,
                             0x06U, 0x00U, 0x01U, 0x00U,
                             0x02U, 0x01U, 0x02U};
    const uint8_t continuation[] = {0x01U, 0x10U, 0x03U, 0x00U,
                                    0x34U, 0x12U, 0x56U};
    usb_bluetooth_link_table_t table = {0};
    uint8_t output[USB_BLUETOOTH_ACL_MAX_PACKET] = {0};
    usb_bluetooth_link_table_reset(&table);
    assert(usb_bluetooth_link_table_event(&table, connected,
                                           sizeof(connected)) == 1);
    assert(usb_bluetooth_link_table_event(&table, connected,
                                           sizeof(connected)) == -17);
    assert(usb_bluetooth_link_table_acl_feed(&table, first, sizeof(first)) == 0);
    assert(usb_bluetooth_link_table_acl_feed(&table, continuation,
                                              sizeof(continuation)) == 1);
    assert(usb_bluetooth_link_table_acl_take(&table, 1U, output, 8U) == -90);
    assert(usb_bluetooth_link_table_acl_take(&table, 1U, output,
                                              sizeof(output)) == 14);
    assert(usb_bluetooth_link_table_event(&table, disconnected,
                                           sizeof(disconnected)) == 1);
    assert(usb_bluetooth_link_table_acl_feed(&table, first, sizeof(first)) == -9);
    assert(usb_bluetooth_link_table_acl_take(&table, 1U, output,
                                              sizeof(output)) == -9);
    assert(usb_bluetooth_link_table_event(&table, disconnected,
                                           sizeof(disconnected)) == -9);
    usb_bluetooth_link_table_reset(&table);
    for (uint32_t i = 0U; i < USB_BLUETOOTH_LINK_MAX; ++i)
        assert(table.links[i].active == 0U && table.links[i].handle == 0U);
}

static void test_l2cap_signaling_iteration_and_build(void)
{
    const uint8_t commands[] = {
        0x02U, 0x01U, 0x02U, 0x00U, 0x34U, 0x12U,
        0x09U, 0x02U, 0x00U, 0x00U
    };
    const uint8_t request_parameters[] = {0x40U, 0x00U, 0x01U, 0x00U};
    uint8_t output[16] = {0};
    uint32_t offset = 0U;
    usb_bluetooth_l2cap_signal_command_t command = {0};
    assert(usb_bluetooth_l2cap_signaling_next(commands, sizeof(commands),
                                               &offset, &command) == 1);
    assert(command.code == 2U && command.identifier == 1U &&
           command.payload_length == 2U && command.payload[0] == 0x34U);
    assert(offset == 6U);
    assert(usb_bluetooth_l2cap_signaling_next(commands, sizeof(commands),
                                               &offset, &command) == 1);
    assert(command.code == 9U && command.identifier == 2U &&
           command.payload_length == 0U);
    assert(usb_bluetooth_l2cap_signaling_next(commands, sizeof(commands),
                                               &offset, &command) == 0);
    assert(usb_bluetooth_l2cap_signaling_next(commands, sizeof(commands),
                                               &offset, &command) == 0);

    assert(usb_bluetooth_l2cap_signaling_build(0x02U, 0x07U,
               request_parameters, sizeof(request_parameters), output, 3U) == -90);
    assert(usb_bluetooth_l2cap_signaling_build(0x02U, 0x07U,
               request_parameters, sizeof(request_parameters), output,
               sizeof(output)) == 8);
    assert(output[0] == 0x02U && output[1] == 0x07U &&
           output[2] == 4U && output[3] == 0U && output[7] == 0U);
    assert(usb_bluetooth_l2cap_signaling_build(0U, 1U, NULL, 0U,
                                               output, sizeof(output)) == -22);
    offset = 9U;
    assert(usb_bluetooth_l2cap_signaling_next(commands, sizeof(commands),
                                               &offset, &command) == -22);
}

static void test_acl_fragment_reassembly(void)
{
    /* L2CAP PDU: 6-byte body, CID 1; first ACL fragment carries 3 bytes. */
    const uint8_t first[] = {0x01U, 0x20U, 0x07U, 0x00U,
                             0x06U, 0x00U, 0x01U, 0x00U,
                             0x02U, 0x01U, 0x02U};
    const uint8_t continuation[] = {0x01U, 0x10U, 0x03U, 0x00U,
                                    0x34U, 0x12U, 0x56U};
    const uint8_t wrong_handle[] = {0x02U, 0x10U, 0x01U, 0x00U, 0xFFU};
    usb_bluetooth_acl_reassembly_t state = {0};
    uint8_t output[USB_BLUETOOTH_ACL_MAX_PACKET] = {0};

    usb_bluetooth_acl_reassembly_reset(&state);
    assert(usb_bluetooth_acl_reassembly_take(&state, output, sizeof(output)) == -11);
    assert(usb_bluetooth_acl_reassembly_feed(&state, first, sizeof(first)) == 0);
    assert(usb_bluetooth_acl_reassembly_feed(&state, wrong_handle,
                                             sizeof(wrong_handle)) == -22);
    assert(usb_bluetooth_acl_reassembly_feed(&state, continuation,
                                             sizeof(continuation)) == 1);
    assert(state.complete == 1U && state.received_length == 10U);
    assert(usb_bluetooth_acl_reassembly_feed(&state, continuation,
                                             sizeof(continuation)) == -16);
    assert(usb_bluetooth_acl_reassembly_take(&state, output, 8U) == -90);
    assert(state.complete == 1U);
    int result = usb_bluetooth_acl_reassembly_take(&state, output, sizeof(output));
    assert(result == 14);
    assert(output[0] == 0x01U && output[1] == 0x20U);
    assert(output[2] == 10U && output[3] == 0U);
    usb_bluetooth_l2cap_view_t view = {0};
    assert(usb_bluetooth_l2cap_parse(output, (uint32_t)result, &view));
    assert(view.channel_id == 1U && view.payload_length == 6U);
    assert(view.payload[0] == 0x02U && view.payload[5] == 0x56U);
    assert(state.complete == 0U && state.received_length == 0U);

    usb_bluetooth_acl_reassembly_reset(&state);
    assert(usb_bluetooth_acl_reassembly_feed(&state, continuation,
                                             sizeof(continuation)) == -22);
    assert(usb_bluetooth_acl_reassembly_feed(&state, first,
                                             sizeof(first) - 1U) == -22);
    usb_bluetooth_acl_reassembly_reset(&state);
    assert(usb_bluetooth_acl_reassembly_feed(&state, first, sizeof(first)) == 0);
    assert(usb_bluetooth_acl_reassembly_feed(&state, first, sizeof(first)) == -16);
    assert(state.active == 0U && state.received_length == 0U);
    /* The stale partial packet was cleared, so a fresh start can recover. */
    assert(usb_bluetooth_acl_reassembly_feed(&state, first, sizeof(first)) == 0);
    const uint8_t overrun[] = {0x01U, 0x10U, 0x04U, 0x00U,
                               0x34U, 0x12U, 0x56U, 0x78U};
    assert(usb_bluetooth_acl_reassembly_feed(&state, overrun, sizeof(overrun)) == -22);
    assert(state.active == 1U && state.received_length == 7U);
    state.received_length = state.expected_length + 1U;
    assert(usb_bluetooth_acl_reassembly_feed(&state, continuation,
                                             sizeof(continuation)) == -22);
    usb_bluetooth_acl_reassembly_reset(&state);
    assert(state.active == 0U && state.complete == 0U);
    for (uint32_t i = 0U; i < USB_BLUETOOTH_ACL_MAX_PACKET; ++i)
        assert(state.packet[i] == 0U);
}

static void test_l2cap_signaling_command_boundaries(void)
{
    const uint8_t two_commands[] = {
        0x02U, 0x01U, 0x02U, 0x00U, 0x34U, 0x12U,
        0x09U, 0x02U, 0x00U, 0x00U
    };
    const uint8_t truncated_header[] = {0x02U, 0x01U, 0x02U};
    const uint8_t truncated_payload[] = {0x02U, 0x01U, 0x02U, 0x00U, 0x34U};
    const uint8_t zero_code[] = {0x00U, 0x01U, 0x00U, 0x00U};
    const uint8_t zero_identifier[] = {0x02U, 0x00U, 0x00U, 0x00U};
    const uint8_t empty[] = {0U};

    assert(usb_bluetooth_l2cap_signaling_valid(two_commands,
                                               sizeof(two_commands)));
    assert(!usb_bluetooth_l2cap_signaling_valid(truncated_header,
                                                sizeof(truncated_header)));
    assert(!usb_bluetooth_l2cap_signaling_valid(truncated_payload,
                                                sizeof(truncated_payload)));
    assert(!usb_bluetooth_l2cap_signaling_valid(zero_code, sizeof(zero_code)));
    assert(!usb_bluetooth_l2cap_signaling_valid(zero_identifier,
                                                sizeof(zero_identifier)));
    const uint8_t short_connection_request[] = {0x02U, 0x01U, 0x03U, 0x00U,
                                                0x40U, 0x00U, 0x01U};
    const uint8_t short_connection_response[] = {0x03U, 0x01U, 0x04U, 0x00U,
                                                 0U, 0U, 0U, 0U};
    assert(!usb_bluetooth_l2cap_signaling_valid(short_connection_request,
                                                sizeof(short_connection_request)));
    assert(!usb_bluetooth_l2cap_signaling_valid(short_connection_response,
                                                sizeof(short_connection_response)));
    assert(!usb_bluetooth_l2cap_signaling_valid(NULL, 4U));
    assert(!usb_bluetooth_l2cap_signaling_valid(empty, 0U));
}

static void test_hci_command_fault_requires_reset(void)
{
    usb_bluetooth_hci_command_state_t state = {.credits = 1U};
    assert(usb_bluetooth_hci_command_begin(&state, 0x1001U) == 0);
    state.faulted = 1U;
    assert(usb_bluetooth_hci_command_begin(&state, 0x1009U) == -5);
    usb_bluetooth_hci_command_state_reset(&state);
    state.credits = 1U;
    assert(usb_bluetooth_hci_command_begin(&state, 0x0C03U) == 0);
    const uint8_t malformed_complete[] = {0x0EU, 0x03U, 0x01U, 0x03U, 0x0CU};
    assert(usb_bluetooth_hci_command_event(&state, malformed_complete,
                                           sizeof(malformed_complete)) == -22);
    assert(state.outstanding == 1U);
    const uint8_t reset_complete[] = {0x0EU, 0x04U, 0x01U, 0x03U, 0x0CU, 0U};
    assert(usb_bluetooth_hci_command_event(&state, reset_complete,
                                           sizeof(reset_complete)) == 1);
    assert(state.outstanding == 0U && state.faulted == 0U);
    assert(usb_bluetooth_hci_command_begin(&state, 0x1009U) == -11);
}

static void test_hci_command_credits_and_discovery_builders(void)
{
    usb_bluetooth_hci_command_state_t state = {.credits = 1U};
    uint8_t out[16] = {0};
    assert(usb_bluetooth_hci_command_begin(&state, 0x1001U) == 0);
    assert(state.credits == 0U && state.outstanding == 1U);
    assert(usb_bluetooth_hci_command_begin(&state, 0x1009U) == -16);
    const uint8_t unrelated[] = {0x0EU, 0x04U, 0x01U, 0x09U, 0x10U, 0U};
    assert(usb_bluetooth_hci_command_event(&state, unrelated,
                                           sizeof(unrelated)) == 0);
    assert(state.outstanding == 1U);
    const uint8_t complete[] = {0x0EU, 0x04U, 0x02U, 0x01U, 0x10U, 0U};
    assert(usb_bluetooth_hci_command_event(&state, complete,
                                           sizeof(complete)) == 1);
    assert(state.outstanding == 0U && state.completed == 1U &&
           state.credits == 2U && state.status == 0U);
    assert(usb_bluetooth_hci_command_begin(&state, 0x1009U) == 0);
    const uint8_t failed[] = {0x0FU, 0x04U, 0x0CU, 0x01U, 0x09U, 0x10U};
    assert(usb_bluetooth_hci_command_event(&state, failed,
                                           sizeof(failed)) == -5);
    assert(state.outstanding == 0U && state.status == 0x0CU &&
           state.event_kind == 2U);
    assert(usb_bluetooth_hci_build_read_local_version(out, sizeof(out)) == 3);
    assert(out[0] == 0x01U && out[1] == 0x10U && out[2] == 0U);
    assert(usb_bluetooth_hci_build_read_bd_addr(out, sizeof(out)) == 3);
    assert(out[0] == 0x09U && out[1] == 0x10U);
    assert(usb_bluetooth_hci_build_read_buffer_size(out, sizeof(out)) == 3);
    assert(out[0] == 0x05U && out[1] == 0x10U);
    assert(usb_bluetooth_hci_build_inquiry(out, sizeof(out), 8U, 0U) == 8);
    assert(out[0] == 0x01U && out[1] == 0x04U && out[2] == 5U &&
           out[3] == 0x33U && out[6] == 8U);
    assert(usb_bluetooth_hci_build_inquiry(out, sizeof(out), 0U, 0U) == -22);
    assert(usb_bluetooth_hci_build_read_bd_addr(out, 2U) == -90);
}

static void test_active_link_count(void)
{
    usb_bluetooth_link_table_t links = {0};
    const uint8_t connected_a[] = {
        0x03U, 11U, 0U, 0x0BU, 0U, 1U, 0U,
        0U, 0U, 0U, 0U, 0U, 0U
    };
    uint8_t connected_b[sizeof(connected_a)];
    const uint8_t disconnected_a[] = {0x05U, 4U, 0U, 0x0BU, 0U, 0U};
    usb_bluetooth_link_table_reset(&links);
    assert(usb_bluetooth_link_table_active_count(&links) == 0U);
    assert(usb_bluetooth_link_table_event(&links, connected_a,
                                          sizeof(connected_a)) == 1);
    assert(usb_bluetooth_link_table_active_count(&links) == 1U);
    memcpy(connected_b, connected_a, sizeof(connected_b));
    connected_b[3] = 0x0CU;
    assert(usb_bluetooth_link_table_event(&links, connected_b,
                                          sizeof(connected_b)) == 1);
    assert(usb_bluetooth_link_table_active_count(&links) == 2U);
    assert(usb_bluetooth_link_table_event(&links, disconnected_a,
                                          sizeof(disconnected_a)) == 1);
    assert(usb_bluetooth_link_table_active_count(&links) == 1U);
    assert(usb_bluetooth_link_table_is_single_handle(&links, 0x000CU) == 1);
}

static void test_single_link_l2cap_dispatch_guard(void)
{
    usb_bluetooth_link_table_t links = {0};
    const uint8_t first_link[] = {
        0x03U, 11U, 0U, 0x0BU, 0U, 1U, 0U,
        0U, 0U, 0U, 0U, 0U, 0U
    };
    uint8_t second_link[sizeof(first_link)];
    const uint8_t disconnect_second[] = {0x05U, 4U, 0U, 0x0CU, 0U, 0U};

    usb_bluetooth_link_table_reset(&links);
    assert(usb_bluetooth_link_table_is_single_handle(&links, 0x000BU) == 0);
    assert(usb_bluetooth_link_table_event(&links, first_link,
                                          sizeof(first_link)) == 1);
    assert(usb_bluetooth_link_table_is_single_handle(&links, 0x000BU) == 1);
    memcpy(second_link, first_link, sizeof(second_link));
    second_link[3] = 0x0CU;
    assert(usb_bluetooth_link_table_event(&links, second_link,
                                          sizeof(second_link)) == 1);
    assert(usb_bluetooth_link_table_is_single_handle(&links, 0x000BU) == 0);
    assert(usb_bluetooth_link_table_is_single_handle(&links, 0x000CU) == 0);
    assert(usb_bluetooth_link_table_event(&links, disconnect_second,
                                          sizeof(disconnect_second)) == 1);
    assert(usb_bluetooth_link_table_is_single_handle(&links, 0x000BU) == 1);
}

static void test_fragmented_acl_dispatches_l2cap_response(void)
{
    usb_bluetooth_link_table_t links = {0};
    usb_bluetooth_l2cap_table_t channels = {0};
    const uint8_t connected[] = {
        0x03U, 11U, 0U, 0x0BU, 0U, 1U, 0U,
        0U, 0U, 0U, 0U, 0U, 0U
    };
    uint16_t local_cid = 0U;
    uint8_t identifier = 0U;
    uint8_t signaling[12] = {
        0x03U, 0U, 8U, 0U, 0x41U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
    };
    uint8_t complete[32] = {0};
    uint8_t first[11] = {0};
    uint8_t continuation[13] = {0};
    uint8_t assembled[32] = {0};

    usb_bluetooth_link_table_reset(&links);
    usb_bluetooth_l2cap_table_reset(&channels);
    assert(usb_bluetooth_link_table_event(&links, connected,
                                          sizeof(connected)) == 1);
    assert(usb_bluetooth_l2cap_channel_begin(&channels, 1U, 672U, 0U,
                                             &local_cid, &identifier) == 0);
    signaling[1] = identifier;
    signaling[6] = (uint8_t)local_cid;
    signaling[7] = (uint8_t)(local_cid >> 8);
    assert(usb_bluetooth_l2cap_build_acl_packet(0x000BU, 0x0001U,
                                                signaling, sizeof(signaling),
                                                complete, sizeof(complete)) == 20);

    first[0] = complete[0];
    first[1] = complete[1];
    first[2] = 7U;
    for (uint32_t i = 0U; i < 7U; ++i)
        first[4U + i] = complete[4U + i];
    continuation[0] = 0x0BU;
    continuation[1] = 0x10U;
    continuation[2] = 9U;
    for (uint32_t i = 0U; i < 9U; ++i)
        continuation[4U + i] = complete[11U + i];

    assert(usb_bluetooth_link_table_acl_feed(&links, first,
                                             sizeof(first)) == 0);
    assert(usb_bluetooth_link_table_acl_feed(&links, continuation,
                                             sizeof(continuation)) == 1);
    assert(usb_bluetooth_link_table_acl_take(&links, 0x000BU, assembled,
                                             sizeof(assembled)) == 20);
    assert(usb_bluetooth_l2cap_dispatch_acl_responses(&channels, assembled,
                                                       20U) == 1);
    assert(channels.channels[0].state == 3U &&
           channels.channels[0].remote_cid == 0x0041U);
}

static void test_l2cap_signaling_acl_builder(void)
{
    const uint8_t request[] = {
        0x02U, 1U, 4U, 0U, 1U, 0U, 0x40U, 0U
    };
    const uint8_t malformed[] = {0U, 1U, 0U, 0U};
    uint8_t packet[32] = {0};
    usb_bluetooth_l2cap_view_t view;

    assert(usb_bluetooth_l2cap_build_signaling_acl_packet(
               0x000BU, request, sizeof(request), packet,
               sizeof(packet)) == 16);
    assert(usb_bluetooth_l2cap_parse(packet, 16U, &view));
    assert(view.channel_id == 0x0001U &&
           view.payload_length == sizeof(request));
    assert(memcmp(view.payload, request, sizeof(request)) == 0);
    assert(usb_bluetooth_l2cap_build_signaling_acl_packet(
               0x000BU, malformed, sizeof(malformed), packet,
               sizeof(packet)) == -22);
    assert(usb_bluetooth_l2cap_build_signaling_acl_packet(
               0x000BU, request, sizeof(request), packet, 8U) == -90);
}

static void test_l2cap_acl_response_dispatch(void)
{
    usb_bluetooth_l2cap_table_t table = {0};
    uint16_t local_cid = 0U;
    uint8_t identifier = 0U;
    uint8_t signaling[16] = {0};
    uint8_t packet[32] = {0};
    const uint8_t data[] = {0xAAU, 0x55U};
    usb_bluetooth_l2cap_table_reset(&table);

    assert(usb_bluetooth_l2cap_channel_begin(&table, 1U, 672U, 0U,
                                             &local_cid, &identifier) == 0);
    signaling[0] = 0x03U;
    signaling[1] = identifier;
    signaling[2] = 8U;
    signaling[4] = 0x41U;
    signaling[6] = (uint8_t)local_cid;
    signaling[7] = (uint8_t)(local_cid >> 8);
    assert(usb_bluetooth_l2cap_build_acl_packet(0x000BU, 0x0001U,
                                                signaling, 12U, packet,
                                                sizeof(packet)) == 20);
    assert(usb_bluetooth_l2cap_dispatch_acl_responses(&table, packet, 20U) == 1);
    assert(table.channels[0].state == 3U &&
           table.channels[0].remote_cid == 0x0041U);

    assert(usb_bluetooth_l2cap_build_acl_packet(0x000BU, 0x0040U, data,
                                                sizeof(data), packet,
                                                sizeof(packet)) == 10);
    assert(usb_bluetooth_l2cap_dispatch_acl_responses(&table, packet, 10U) == 0);
    assert(usb_bluetooth_l2cap_dispatch_acl_responses(&table, packet, 3U) == -22);
}

static void test_l2cap_dispatch_skips_stale_transaction(void)
{
    usb_bluetooth_l2cap_table_t table = {0};
    uint16_t local_cid = 0U;
    uint8_t identifier = 0U;
    uint8_t commands[24] = {0};
    usb_bluetooth_l2cap_table_reset(&table);
    assert(usb_bluetooth_l2cap_channel_begin(&table, 1U, 672U, 0U,
                                             &local_cid, &identifier) == 0);

    for (uint32_t offset = 0U; offset < sizeof(commands); offset += 12U) {
        commands[offset] = 0x03U;
        commands[offset + 1U] = offset == 0U ? 99U : identifier;
        commands[offset + 2U] = 8U;
        commands[offset + 4U] = 0x41U;
        commands[offset + 6U] = (uint8_t)local_cid;
        commands[offset + 7U] = (uint8_t)(local_cid >> 8);
    }
    assert(usb_bluetooth_l2cap_signaling_valid(commands, sizeof(commands)));
    assert(usb_bluetooth_l2cap_dispatch_responses(&table, commands,
                                                   sizeof(commands)) == 1);
    assert(table.channels[0].state == 3U &&
           table.channels[0].remote_cid == 0x0041U);
}

static void test_l2cap_response_dispatcher(void)
{
    usb_bluetooth_l2cap_table_t table = {0};
    uint16_t cid = 0U;
    uint8_t id = 0U;
    uint8_t command[16] = {0};
    usb_bluetooth_l2cap_table_reset(&table);
    assert(usb_bluetooth_l2cap_channel_begin(&table, 1U, 672U, 0U,
                                             &cid, &id) == 0);
    assert(usb_bluetooth_l2cap_build_connection_request(id, 1U, cid,
                                                        command, sizeof(command)) == 8);
    command[0] = 0x03U;
    command[2] = 8U; command[3] = 0U;
    command[4] = 0x41U; command[5] = 0U;
    command[6] = (uint8_t)cid; command[7] = (uint8_t)(cid >> 8);
    command[8] = 0U; command[9] = 0U;
    command[10] = 0U; command[11] = 0U;
    assert(usb_bluetooth_l2cap_dispatch_responses(&table, command, 12U) == 1);
    assert(table.channels[0].state == 3U);
    assert(usb_bluetooth_l2cap_channel_configure_begin(&table, cid, 88U) == 1);
    command[0] = 0x05U; command[1] = 88U;
    command[2] = 6U; command[3] = 0U;
    command[4] = (uint8_t)cid; command[5] = (uint8_t)(cid >> 8);
    command[6] = 0U; command[7] = 0U;
    command[8] = 0U; command[9] = 0U;
    assert(usb_bluetooth_l2cap_dispatch_responses(&table, command, 10U) == 1);
    assert(table.channels[0].state == 4U);
    assert(usb_bluetooth_l2cap_dispatch_responses(&table, command, 9U) == -22);
}

static void test_l2cap_configuration_response_parser(void)
{
    usb_bluetooth_l2cap_table_t table = {0};
    uint16_t cid = 0U;
    uint8_t conn_id = 0U;
    usb_bluetooth_l2cap_table_reset(&table);
    assert(usb_bluetooth_l2cap_channel_begin(&table, 1U, 672U, 0U,
                                             &cid, &conn_id) == 0);
    assert(usb_bluetooth_l2cap_channel_response(&table, conn_id, 0x0041U,
                                                0U, 672U) == 1);
    assert(usb_bluetooth_l2cap_channel_configure_begin(&table, cid, 44U) == 1);
    uint8_t response[] = {
        0x05U, 44U, 8U, 0U,
        (uint8_t)cid, (uint8_t)(cid >> 8),
        0U, 0U, 0U, 0U,
        0x01U, 2U, 0xE8U, 0x03U
    };
    response[2] = 10U;
    assert(usb_bluetooth_l2cap_configuration_response(&table, response,
                                                       sizeof(response)) == 1);
    assert(table.channels[0].state == 4U &&
           table.channels[0].remote_mtu == 1000U);

    assert(usb_bluetooth_l2cap_channel_begin(&table, 1U, 672U, 0U,
                                             &cid, &conn_id) == 0);
    assert(usb_bluetooth_l2cap_channel_response(&table, conn_id, 0x0042U,
                                                0U, 672U) == 1);
    assert(usb_bluetooth_l2cap_channel_configure_begin(&table, cid, 45U) == 1);
    response[1] = 45U;
    response[4] = (uint8_t)cid;
    response[5] = (uint8_t)(cid >> 8);
    response[10] = 0x01U; response[11] = 1U;
    response[2] = 9U;
    assert(usb_bluetooth_l2cap_configuration_response(&table, response,
                                                       13U) == -22);
    response[10] = 0x02U; response[11] = 0U;
    response[2] = 8U;
    assert(usb_bluetooth_l2cap_configuration_response(&table, response,
                                                       12U) == -95);
    response[10] = 0x01U; response[11] = 2U;
    response[12] = 47U; response[13] = 0U;
    response[2] = 10U;
    assert(usb_bluetooth_l2cap_configuration_response(&table, response,
                                                       14U) == -22);
    assert(table.channels[1].state == 3U);
}

static void test_l2cap_identifier_collision_guards(void)
{
    usb_bluetooth_l2cap_table_t table = {0};
    uint16_t cid_a = 0U, cid_b = 0U;
    uint8_t id_a = 0U, id_b = 0U;
    usb_bluetooth_l2cap_table_reset(&table);
    assert(usb_bluetooth_l2cap_channel_begin(&table, 1U, 672U, 0U,
                                             &cid_a, &id_a) == 0);
    assert(usb_bluetooth_l2cap_channel_begin(&table, 1U, 672U, 0U,
                                             &cid_b, &id_b) == 0);
    assert(id_a != id_b);
    assert(usb_bluetooth_l2cap_channel_response(&table, id_a, 0x0041U,
                                                0U, 672U) == 1);
    assert(usb_bluetooth_l2cap_channel_response(&table, id_b, 0x0042U,
                                                0U, 672U) == 1);
    assert(usb_bluetooth_l2cap_channel_configure_begin(&table, cid_a,
                                                       id_b) == -16);
    assert(usb_bluetooth_l2cap_channel_configure_begin(&table, cid_a, 77U) == 1);
    assert(usb_bluetooth_l2cap_channel_configure_begin(&table, cid_b, 77U) == -16);
}

static void test_l2cap_configuration_transaction(void)
{
    usb_bluetooth_l2cap_table_t table = {0};
    uint16_t cid = 0U;
    uint8_t id = 0U;
    usb_bluetooth_l2cap_table_reset(&table);
    assert(usb_bluetooth_l2cap_channel_begin(&table, 1U, 672U, 0U,
                                             &cid, &id) == 0);
    assert(usb_bluetooth_l2cap_channel_response(&table, id, 0x0041U,
                                                0U, 672U) == 1);
    assert(usb_bluetooth_l2cap_channel_configure_begin(&table, cid, 9U) == 1);
    assert(usb_bluetooth_l2cap_channel_configure_response(&table, cid, 8U,
                                                          0U, 0U, 672U) == -16);
    assert(table.channels[0].state == 3U);
    assert(usb_bluetooth_l2cap_channel_configure_response(&table, cid, 9U,
                                                          1U, 0U, 672U) == 0);
    assert(table.channels[0].state == 3U);
    assert(usb_bluetooth_l2cap_channel_configure_response(&table, cid, 9U,
                                                          0U, 0U, 672U) == 1);
    assert(table.channels[0].state == 4U && table.channels[0].remote_mtu == 672U);
    assert(usb_bluetooth_l2cap_channel_configure_begin(&table, cid, 10U) == -16);

    assert(usb_bluetooth_l2cap_channel_begin(&table, 1U, 672U, 0U,
                                             &cid, &id) == 0);
    assert(usb_bluetooth_l2cap_channel_response(&table, id, 0x0042U,
                                                0U, 672U) == 1);
    assert(usb_bluetooth_l2cap_channel_configure_begin(&table, cid, 11U) == 1);
    assert(usb_bluetooth_l2cap_channel_configure_response(&table, cid, 11U,
                                                          0U, 1U, 672U) == -5);
    assert(table.channels[1].state == 3U);
    assert(usb_bluetooth_l2cap_channel_configure_response(&table, cid, 11U,
                                                          0U, 0U, 47U) == -22);
    assert(table.channels[1].state == 3U);
}

static void test_l2cap_acl_packet_builder(void)
{
    const uint8_t signaling[] = {0x02U, 0x01U, 0x04U, 0x00U,
                                 0x01U, 0x00U, 0x40U, 0x00U};
    uint8_t packet[32] = {0};
    usb_bluetooth_l2cap_view_t view;
    assert(usb_bluetooth_l2cap_build_acl_packet(0x000BU, 0x0001U,
                                                signaling, sizeof(signaling),
                                                packet, sizeof(packet)) == 16);
    assert(packet[0] == 0x0BU && packet[1] == 0x20U &&
           packet[2] == 12U && packet[4] == 8U &&
           packet[6] == 1U && packet[7] == 0U);
    assert(usb_bluetooth_l2cap_parse(packet, 16U, &view));
    assert(view.handle == 0x000BU && view.channel_id == 0x0001U &&
           view.payload_length == sizeof(signaling));
    assert(memcmp(view.payload, signaling, sizeof(signaling)) == 0);
    assert(usb_bluetooth_l2cap_build_acl_packet(0U, 1U, signaling,
                                                sizeof(signaling), packet,
                                                sizeof(packet)) == -22);
    assert(usb_bluetooth_l2cap_build_acl_packet(0x0F00U, 1U, signaling,
                                                sizeof(signaling), packet,
                                                sizeof(packet)) == -22);
    assert(usb_bluetooth_l2cap_build_acl_packet(0x000BU, 1U, signaling,
                                                sizeof(signaling), packet,
                                                8U) == -90);
    assert(usb_bluetooth_l2cap_build_acl_packet(0x000BU, 1U, NULL, 1U,
                                                packet, sizeof(packet)) == -22);
}

static void test_l2cap_connection_response_dispatch(void)
{
    usb_bluetooth_l2cap_table_t table = {0};
    uint16_t local_cid = 0U;
    uint8_t identifier = 0U;
    uint8_t response[12] = {0x03U, 0U, 8U, 0U,
                            0x41U, 0x00U, 0U, 0U,
                            0U, 0U, 0U, 0U};
    usb_bluetooth_l2cap_table_reset(&table);
    assert(usb_bluetooth_l2cap_channel_begin(&table, 1U, 672U, 0U,
                                             &local_cid, &identifier) == 0);
    response[1] = identifier;
    response[6] = (uint8_t)local_cid;
    response[7] = (uint8_t)(local_cid >> 8);
    assert(usb_bluetooth_l2cap_connection_response(&table, response,
                                                   sizeof(response), 672U) == 1);
    assert(table.channels[0].remote_cid == 0x0041U &&
           table.channels[0].state == 3U);

    usb_bluetooth_l2cap_table_reset(&table);
    assert(usb_bluetooth_l2cap_channel_begin(&table, 1U, 672U, 0U,
                                             &local_cid, &identifier) == 0);
    response[1] = identifier;
    response[6] = (uint8_t)(local_cid + 1U);
    response[7] = (uint8_t)((local_cid + 1U) >> 8);
    assert(usb_bluetooth_l2cap_connection_response(&table, response,
                                                   sizeof(response), 672U) == -22);
    assert(table.channels[0].state == 1U);
    response[6] = (uint8_t)local_cid;
    response[7] = (uint8_t)(local_cid >> 8);
    response[8] = 1U; response[9] = 0U;
    assert(usb_bluetooth_l2cap_connection_response(&table, response,
                                                   sizeof(response), 672U) == 0);
    assert(table.channels[0].active && table.channels[0].state == 1U);
    response[8] = 2U; response[9] = 0U;
    assert(usb_bluetooth_l2cap_connection_response(&table, response,
                                                   sizeof(response), 672U) == -5);
    assert(!table.channels[0].active);
    assert(usb_bluetooth_l2cap_connection_response(&table, response, 11U,
                                                   672U) == -22);
}

static void test_l2cap_response_builders(void)
{
    uint8_t output[32] = {0};
    const uint8_t expected_connection[] = {
        0x03U, 7U, 8U, 0U, 0x41U, 0U, 0x42U, 0U,
        0U, 0U, 0U, 0U
    };
    const uint8_t expected_configuration[] = {
        0x05U, 8U, 10U, 0U, 0x42U, 0U, 0U, 0U,
        0U, 0U, 1U, 2U, 0xA0U, 0x02U
    };
    assert(usb_bluetooth_l2cap_build_connection_response(
               7U, 0x0041U, 0x0042U, 0U, 0U, output,
               sizeof(output)) == (int)sizeof(expected_connection));
    assert(memcmp(output, expected_connection,
                  sizeof(expected_connection)) == 0);
    assert(usb_bluetooth_l2cap_build_connection_response(
               7U, 0x0041U, 0x0042U, 0U, 0U, output, 8U) == -90);
    assert(usb_bluetooth_l2cap_build_connection_response(
               7U, 0x003FU, 0x0042U, 0U, 0U, output,
               sizeof(output)) == -22);

    assert(usb_bluetooth_l2cap_build_configuration_response(
               8U, 0x0042U, 0U, 0U, 672U, output,
               sizeof(output)) == (int)sizeof(expected_configuration));
    assert(memcmp(output, expected_configuration,
                  sizeof(expected_configuration)) == 0);
    assert(usb_bluetooth_l2cap_build_configuration_response(
               8U, 0x0042U, 0U, 0U, 47U, output,
               sizeof(output)) == -22);
    assert(usb_bluetooth_l2cap_build_configuration_response(
               8U, 0x0042U, 0U, 2U, 672U, output,
               sizeof(output)) == 10);
}

static void test_l2cap_standard_signaling_builders(void)
{
    uint8_t out[16] = {0};
    assert(usb_bluetooth_l2cap_build_connection_request(1U, 1U, 0x0040U,
                                                        out, sizeof(out)) == 8);
    assert(out[0] == 0x02U && out[1] == 1U && out[2] == 4U &&
           out[4] == 1U && out[6] == 0x40U);
    assert(usb_bluetooth_l2cap_signaling_valid(out, 8U));
    assert(usb_bluetooth_l2cap_build_configuration_request(2U, 0x0041U,
                                                           out, sizeof(out)) == 8);
    assert(out[0] == 0x04U && out[4] == 0x41U && out[6] == 0U);
    assert(usb_bluetooth_l2cap_signaling_valid(out, 8U));
    assert(usb_bluetooth_l2cap_build_disconnection_request(3U, 0x0041U,
                                                           0x0040U, out,
                                                           sizeof(out)) == 8);
    assert(out[0] == 0x06U && out[4] == 0x41U && out[6] == 0x40U);
    assert(usb_bluetooth_l2cap_signaling_valid(out, 8U));
    assert(usb_bluetooth_l2cap_build_connection_request(1U, 2U, 0x0040U,
                                                        out, sizeof(out)) == -22);
    assert(usb_bluetooth_l2cap_build_configuration_request(2U, 0x0001U,
                                                           out, sizeof(out)) == -22);
    assert(usb_bluetooth_l2cap_build_disconnection_request(3U, 0x0041U,
                                                           0x0040U, out, 4U) == -90);
}

static void test_l2cap_channel_lifecycle(void)
{
    usb_bluetooth_l2cap_table_t table = {0};
    usb_bluetooth_l2cap_table_reset(&table);
    uint16_t cid = 0U;
    uint8_t id = 0U;
    assert(usb_bluetooth_l2cap_channel_begin(&table, 1U, 672U, 100U,
                                             &cid, &id) == 0);
    assert(cid >= 0x0040U && id != 0U);
    assert(usb_bluetooth_l2cap_channel_response(&table, id, 0x0041U,
                                                0U, 672U) == 1);
    assert(usb_bluetooth_l2cap_channel_configured(&table, cid) == 1);
    assert(usb_bluetooth_l2cap_channel_close(&table, cid) == 1);
    assert(usb_bluetooth_l2cap_channel_close(&table, cid) == -9);

    assert(usb_bluetooth_l2cap_channel_begin(&table, 1U, 672U, 200U,
                                             &cid, &id) == 0);
    assert(usb_bluetooth_l2cap_expire(&table, 199U) == 0U);
    assert(usb_bluetooth_l2cap_expire(&table, 200U) == 1U);
    assert(usb_bluetooth_l2cap_channel_response(&table, id, 0x0042U,
                                                0U, 672U) == -9);
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i)
        assert(usb_bluetooth_l2cap_channel_begin(&table, 1U, 672U, 0U,
                                                 &cid, &id) == 0);
    assert(usb_bluetooth_l2cap_channel_begin(&table, 1U, 672U, 0U,
                                             &cid, &id) == -28);
    assert(usb_bluetooth_l2cap_channel_begin(&table, 2U, 672U, 0U,
                                             &cid, &id) == -22);
}

static void test_l2cap_dispatch_rejects_wrong_handle(void)
{
    usb_bluetooth_l2cap_table_t table = {0};
    uint8_t packet[16] = {0};
    const uint8_t payload[] = {0xA5U};
    usb_bluetooth_l2cap_table_reset(&table);
    assert(usb_bluetooth_l2cap_build_acl_packet(
               0x000BU, 0x0040U, payload, sizeof(payload),
               packet, sizeof(packet)) == 9);
    assert(usb_bluetooth_l2cap_dispatch_acl_responses_for_handle(
               &table, 0x000CU, packet, 9U) == -13);
    assert(usb_bluetooth_l2cap_dispatch_acl_responses_for_handle(
               &table, 0x000BU, packet, 9U) == 0);
    assert(table.channels[0].active == 0U);
}

static void test_hci_connection_command_builders(void)
{
    const uint8_t address[6] = {0x66U, 0x55U, 0x44U, 0x33U, 0x22U, 0x11U};
    uint8_t command[20] = {0};
    assert(usb_bluetooth_hci_build_create_connection(
               address, 0xCC18U, 1U, 0x1234U, 1U,
               command, sizeof(command)) == 16);
    assert(command[0] == 0x05U && command[1] == 0x04U &&
           command[2] == 13U);
    assert(memcmp(&command[3], address, sizeof(address)) == 0);
    assert(command[9] == 0x18U && command[10] == 0xCCU);
    assert(command[11] == 1U && command[12] == 0U);
    assert(command[13] == 0x34U && command[14] == 0x12U &&
           command[15] == 1U);
    assert(usb_bluetooth_hci_build_create_connection(
               address, 0U, 1U, 0U, 0U, command, sizeof(command)) == -22);
    assert(usb_bluetooth_hci_build_create_connection(
               address, 1U, 3U, 0U, 0U, command, sizeof(command)) == -22);
    assert(usb_bluetooth_hci_build_create_connection(
               address, 1U, 1U, 0x8000U, 0U, command, sizeof(command)) == -22);
    assert(usb_bluetooth_hci_build_create_connection(
               address, 1U, 1U, 0U, 2U, command, sizeof(command)) == -22);
    assert(usb_bluetooth_hci_build_create_connection(
               address, 1U, 1U, 0U, 0U, command, 3U) == -90);

    assert(usb_bluetooth_hci_build_disconnect(
               0x0123U, 0x13U, command, sizeof(command)) == 6);
    assert(command[0] == 0x06U && command[1] == 0x04U &&
           command[2] == 3U && command[3] == 0x23U &&
           command[4] == 0x01U && command[5] == 0x13U);
    assert(usb_bluetooth_hci_build_disconnect(
               0U, 0x13U, command, sizeof(command)) == -22);
    assert(usb_bluetooth_hci_build_disconnect(
               0x0F00U, 0x13U, command, sizeof(command)) == -22);
    assert(usb_bluetooth_hci_build_disconnect(
               0x0123U, 0U, command, sizeof(command)) == -22);
    assert(usb_bluetooth_hci_build_disconnect(
               0x0123U, 0x13U, command, 5U) == -90);

    assert(usb_bluetooth_hci_build_create_connection_cancel(
               address, command, sizeof(command)) == 9);
    assert(command[0] == 0x08U && command[1] == 0x04U &&
           command[2] == 6U);
    assert(memcmp(&command[3], address, sizeof(address)) == 0);
    assert(usb_bluetooth_hci_build_create_connection_cancel(
               NULL, command, sizeof(command)) == -22);
    const uint8_t zero_address[6] = {0U};
    assert(usb_bluetooth_hci_build_create_connection_cancel(
               zero_address, command, sizeof(command)) == -22);
    assert(usb_bluetooth_hci_build_create_connection_cancel(
               address, command, 8U) == -90);
}

int main(void)
{
    test_hci_command_encoding();
    test_hci_command_credits_and_discovery_builders();
    test_hci_connection_command_builders();
    test_hci_command_fault_requires_reset();
    test_l2cap_channel_lifecycle();
    test_l2cap_response_builders();
    test_l2cap_standard_signaling_builders();
    test_l2cap_connection_response_dispatch();
    test_l2cap_acl_packet_builder();
    test_l2cap_signaling_acl_builder();
    test_l2cap_configuration_transaction();
    test_l2cap_identifier_collision_guards();
    test_l2cap_configuration_response_parser();
    test_l2cap_response_dispatcher();
    test_l2cap_dispatch_skips_stale_transaction();
    test_l2cap_acl_response_dispatch();
    test_l2cap_dispatch_rejects_wrong_handle();
    test_fragmented_acl_dispatches_l2cap_response();
    test_single_link_l2cap_dispatch_guard();
    test_active_link_count();
    test_complete_hci_interface();
    test_incomplete_interface_is_not_claimed();
    test_malformed_lengths_are_rejected();
    test_malformed_tail_after_valid_interface_is_rejected();
    test_configuration_header_is_validated();
    test_non_bluetooth_interface_is_ignored();
    test_hci_event_and_acl_framing();
    test_acl_queue_pop_by_handle();
    test_acl_queue_preserves_messages_on_small_buffer();
    test_acl_queue_wraparound_and_invariants();
    test_transport_readiness_gate();
    test_reset_state_transitions_are_idempotent();
    test_endpoint_packet_bounds_and_interval();
    test_endpoint_address_and_transfer_roles();
    test_failed_parse_clears_candidate();
    test_descriptor_byte_mutations_are_bounded();
    test_hci_packet_framing_mutations_are_bounded();
    test_hci_event_queue_integrity();
    test_acl_and_l2cap_views();
    test_l2cap_signaling_command_boundaries();
    test_hci_link_lifecycle_and_acl_routing();
    test_l2cap_signaling_iteration_and_build();
    test_acl_fragment_reassembly();
    test_acl_queue_rejects_corrupt_stored_length();
    puts("USB Bluetooth descriptor tests passed");
    return 0;
}
