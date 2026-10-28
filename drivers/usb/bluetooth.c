/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "usb_bluetooth.h"

#include <stddef.h>
#include <stdint.h>

#define USB_DESC_INTERFACE 4U
#define USB_DESC_ENDPOINT 5U
#define USB_CLASS_WIRELESS_CONTROLLER 0xE0U
#define USB_SUBCLASS_RF_CONTROLLER 0x01U
#define USB_PROTOCOL_BLUETOOTH 0x01U
#define USB_TRANSFER_BULK 2U
#define USB_TRANSFER_INTERRUPT 3U
#define USB_BLUETOOTH_EVENT_BUFFER_SIZE 64U
#define USB_BLUETOOTH_MAX_BULK_PACKET 1024U

typedef struct {
    uint8_t active;
    uint8_t interface_number;
    uint8_t event_endpoint;
    uint8_t acl_in_endpoint;
    uint8_t acl_out_endpoint;
    uint8_t event_interval;
    uint16_t event_max_packet;
    uint16_t acl_in_max_packet;
    uint16_t acl_out_max_packet;
} hci_interface_t;

static uint16_t endpoint_max_packet(const uint8_t *descriptor)
{
    return (uint16_t)descriptor[4] |
           ((uint16_t)(descriptor[5] & 0x07U) << 8);
}

static int endpoint_address_valid(uint8_t address)
{
    /* Bits 4..6 are reserved; endpoint zero is the control endpoint. */
    return (address & 0x70U) == 0U && (address & 0x0FU) != 0U;
}

int usb_bluetooth_find_hci_interface(const uint8_t *configuration,
                                     uint16_t length,
                                     usb_bluetooth_hci_candidate_t *candidate)
{
    uint32_t offset = 0;
    hci_interface_t current = {0};
    usb_bluetooth_hci_candidate_t found = {0};
    int have_candidate = 0;

    if (candidate == NULL)
        return 0;

    *candidate = (usb_bluetooth_hci_candidate_t){0};
    if (configuration == NULL || length < 9U)
        return 0;

    /*
     * The caller supplies the complete configuration descriptor returned by
     * enumeration, not an arbitrary slice. Validate its header and total
     * length before considering interface descriptors.
     */
    if (configuration[0] < 9U || configuration[1] != 2U)
        return 0;
    if (((uint16_t)configuration[2] |
         ((uint16_t)configuration[3] << 8)) != length)
        return 0;

    while (offset < length) {
        uint8_t descriptor_length;
        uint8_t descriptor_type;

        if ((uint32_t)length - offset < 2U)
            return 0;

        descriptor_length = configuration[offset];
        descriptor_type = configuration[offset + 1U];
        if (descriptor_length < 2U ||
            (uint32_t)descriptor_length > (uint32_t)length - offset)
            return 0;

        if (descriptor_type == USB_DESC_INTERFACE) {
            if (descriptor_length < 9U)
                return 0;

            if (!have_candidate && current.active &&
                current.event_endpoint != 0U &&
                current.acl_in_endpoint != 0U &&
                current.acl_out_endpoint != 0U) {
                found.interface_number = current.interface_number;
                found.event_endpoint = current.event_endpoint;
                found.acl_in_endpoint = current.acl_in_endpoint;
                found.acl_out_endpoint = current.acl_out_endpoint;
                found.event_interval = current.event_interval;
                found.event_max_packet = current.event_max_packet;
                found.acl_in_max_packet = current.acl_in_max_packet;
                found.acl_out_max_packet = current.acl_out_max_packet;
                have_candidate = 1;
            }

            current = (hci_interface_t){0};
            current.interface_number = configuration[offset + 2U];
            current.active =
                configuration[offset + 5U] == USB_CLASS_WIRELESS_CONTROLLER &&
                configuration[offset + 6U] == USB_SUBCLASS_RF_CONTROLLER &&
                configuration[offset + 7U] == USB_PROTOCOL_BLUETOOTH;
        } else if (descriptor_type == USB_DESC_ENDPOINT) {
            uint8_t address;
            uint8_t transfer_type;
            uint16_t max_packet;

            if (descriptor_length < 7U)
                return 0;
            if (!current.active)
                goto next_descriptor;

            address = configuration[offset + 2U];
            transfer_type = configuration[offset + 3U] & 0x03U;
            max_packet = endpoint_max_packet(configuration + offset);
            /*
             * The transport does not implement high-bandwidth endpoint
             * transactions. Reject wMaxPacketSize multiplier/reserved bits
             * rather than silently treating them as a plain packet size.
             */
            if (!endpoint_address_valid(address) || max_packet == 0U ||
                (configuration[offset + 5U] & 0xF8U) != 0U)
                goto next_descriptor;

            if (transfer_type == USB_TRANSFER_INTERRUPT &&
                (address & 0x80U) != 0U && current.event_endpoint == 0U &&
                max_packet <= USB_BLUETOOTH_EVENT_BUFFER_SIZE &&
                configuration[offset + 6U] != 0U) {
                current.event_endpoint = address;
                current.event_interval = configuration[offset + 6U];
                current.event_max_packet = max_packet;
            } else if (transfer_type == USB_TRANSFER_BULK &&
                       (address & 0x80U) != 0U &&
                       current.acl_in_endpoint == 0U &&
                       max_packet <= USB_BLUETOOTH_MAX_BULK_PACKET) {
                current.acl_in_endpoint = address;
                current.acl_in_max_packet = max_packet;
            } else if (transfer_type == USB_TRANSFER_BULK &&
                       (address & 0x80U) == 0U &&
                       current.acl_out_endpoint == 0U &&
                       max_packet <= USB_BLUETOOTH_MAX_BULK_PACKET) {
                current.acl_out_endpoint = address;
                current.acl_out_max_packet = max_packet;
            }
        }

next_descriptor:
        offset += descriptor_length;
    }

    if (!have_candidate && current.active &&
        current.event_endpoint != 0U &&
        current.acl_in_endpoint != 0U &&
        current.acl_out_endpoint != 0U) {
        found.interface_number = current.interface_number;
        found.event_endpoint = current.event_endpoint;
        found.acl_in_endpoint = current.acl_in_endpoint;
        found.acl_out_endpoint = current.acl_out_endpoint;
        found.event_interval = current.event_interval;
        found.event_max_packet = current.event_max_packet;
        found.acl_in_max_packet = current.acl_in_max_packet;
        found.acl_out_max_packet = current.acl_out_max_packet;
        have_candidate = 1;
    }

    /*
     * Validate the entire stream before publishing a match. Returning as soon
     * as a valid interface ended would accept a valid prefix followed by a
     * malformed descriptor tail.
     */
    if (!have_candidate)
        return 0;
    *candidate = found;
    return 1;
}

/* USB interrupt transfers carry one complete HCI event. */
int usb_bluetooth_hci_event_valid(const uint8_t *packet, uint32_t length)
{
    return packet != NULL && length >= 2U &&
           (uint32_t)packet[1] == length - 2U;
}

/* Return Reset status, or -1 if this is not its Command Complete event. */
int usb_bluetooth_hci_reset_status(const uint8_t *packet, uint32_t length)
{
    if (!usb_bluetooth_hci_event_valid(packet, length) ||
        packet[0] != 0x0EU || packet[1] < 4U ||
        packet[3] != 0x03U || packet[4] != 0x0CU)
        return -1;
    return (int)packet[5];
}

/* Command Complete event for HCI Reset (opcode 0x0C03, status zero). */
int usb_bluetooth_hci_reset_complete(const uint8_t *packet, uint32_t length)
{
    return usb_bluetooth_hci_reset_status(packet, length) == 0;
}

/*
 * Apply an HCI Reset Command Complete event to the transport state.
 * Returns 1 for a newly observed success, -1 for a newly observed failure,
 * and 0 for unrelated or duplicate events.
 */
int usb_bluetooth_update_reset_state(const uint8_t *packet, uint32_t length,
                                     uint8_t *reset_complete,
                                     uint8_t *reset_failed)
{
    if (reset_complete == NULL || reset_failed == NULL)
        return 0;
    int status = usb_bluetooth_hci_reset_status(packet, length);
    if (status == 0 && *reset_complete == 0U) {
        *reset_complete = 1U;
        *reset_failed = 0U;
        return 1;
    }
    if (status > 0 && *reset_complete == 0U && *reset_failed == 0U) {
        *reset_failed = 1U;
        return -1;
    }
    return 0;
}

/* ACL is available to consumers only after a successful HCI Reset. */
int usb_bluetooth_transport_ready(int reset_complete, int reset_failed)
{
    return reset_complete != 0 && reset_failed == 0;
}

/*
 * Decode an HCI ACL header. The PB/BC bits are exposed instead of silently
 * interpreting continuation fragments as standalone L2CAP packets.
 */
int usb_bluetooth_acl_parse(const uint8_t *packet, uint32_t length,
                            usb_bluetooth_acl_view_t *view)
{
    if (view == NULL)
        return 0;
    *view = (usb_bluetooth_acl_view_t){0};
    if (!usb_bluetooth_acl_packet_valid(packet, length))
        return 0;

    uint16_t handle_flags = (uint16_t)packet[0] |
                            ((uint16_t)packet[1] << 8);
    view->handle = handle_flags & 0x0FFFU;
    view->packet_boundary = (uint8_t)((handle_flags >> 12) & 0x03U);
    view->broadcast = (uint8_t)((handle_flags >> 14) & 0x03U);
    view->payload = packet + 4U;
    view->payload_length = length - 4U;
    return 1;
}

/*
 * Parse a single complete L2CAP PDU. Fragment reassembly is intentionally
 * not implied: continuation ACL fragments must be assembled by a future
 * connection manager before calling this helper.
 */
int usb_bluetooth_l2cap_parse(const uint8_t *packet, uint32_t length,
                              usb_bluetooth_l2cap_view_t *view)
{
    if (view == NULL)
        return 0;
    *view = (usb_bluetooth_l2cap_view_t){0};

    usb_bluetooth_acl_view_t acl;
    if (!usb_bluetooth_acl_parse(packet, length, &acl) ||
        (acl.packet_boundary != 0U && acl.packet_boundary != 2U &&
         acl.packet_boundary != 3U) ||
        acl.payload_length < 4U)
        return 0;

    uint16_t l2cap_length = (uint16_t)acl.payload[0] |
                           ((uint16_t)acl.payload[1] << 8);
    uint16_t cid = (uint16_t)acl.payload[2] |
                   ((uint16_t)acl.payload[3] << 8);
    if (cid == 0U || (uint32_t)l2cap_length != acl.payload_length - 4U)
        return 0;

    view->channel_id = cid;
    view->payload = acl.payload + 4U;
    view->payload_length = l2cap_length;
    return 1;
}

void usb_bluetooth_acl_reassembly_reset(usb_bluetooth_acl_reassembly_t *state)
{
    if (state == NULL)
        return;
    state->active = 0U;
    state->complete = 0U;
    state->handle = 0U;
    state->expected_length = 0U;
    state->received_length = 0U;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_ACL_MAX_PACKET; ++i)
        state->packet[i] = 0U;
}

/*
 * Reassemble a single connection handle at a time. This is deliberately a
 * per-connection primitive, not a global multi-handle dispatcher; callers
 * must keep one state object per live HCI connection handle.
 */
int usb_bluetooth_acl_reassembly_feed(usb_bluetooth_acl_reassembly_t *state,
                                      const uint8_t *packet, uint32_t length)
{
    if (state == NULL || packet == NULL ||
        !usb_bluetooth_acl_packet_valid(packet, length))
        return -22;
    if (state->complete != 0U)
        return -16;

    usb_bluetooth_acl_view_t acl;
    if (!usb_bluetooth_acl_parse(packet, length, &acl) ||
        acl.handle == 0U || acl.broadcast != 0U)
        return -22;

    if (acl.packet_boundary == 0U || acl.packet_boundary == 2U ||
        acl.packet_boundary == 3U) {
        if (state->active != 0U)
            return -16;
        if (acl.payload_length < 4U)
            return -22;
        uint32_t l2cap_length = (uint32_t)acl.payload[0] |
                                ((uint32_t)acl.payload[1] << 8);
        uint32_t expected = l2cap_length + 4U;
        if (expected > USB_BLUETOOTH_ACL_MAX_PACKET - 4U ||
            acl.payload_length > expected)
            return -22;
        if (acl.packet_boundary == 3U && acl.payload_length != expected)
            return -22;
        for (uint32_t i = 0U; i < 4U; ++i)
            state->packet[i] = packet[i];
        for (uint32_t i = 0U; i < acl.payload_length; ++i)
            state->packet[4U + i] = acl.payload[i];
        state->handle = acl.handle;
        state->expected_length = expected;
        state->received_length = acl.payload_length;
        state->active = 1U;
    } else if (acl.packet_boundary == 1U) {
        if (state->active == 0U || state->handle != acl.handle ||
            acl.payload_length == 0U ||
            acl.payload_length > state->expected_length - state->received_length)
            return -22;
        for (uint32_t i = 0U; i < acl.payload_length; ++i)
            state->packet[4U + state->received_length + i] = acl.payload[i];
        state->received_length += acl.payload_length;
    } else {
        return -22;
    }

    uint16_t total = (uint16_t)state->received_length;
    state->packet[2] = (uint8_t)(total & 0xFFU);
    state->packet[3] = (uint8_t)(total >> 8);
    if (state->received_length == state->expected_length) {
        state->complete = 1U;
        state->active = 0U;
        return 1;
    }
    return 0;
}

int usb_bluetooth_acl_reassembly_take(usb_bluetooth_acl_reassembly_t *state,
                                      uint8_t *packet, uint32_t capacity)
{
    if (state == NULL || packet == NULL || capacity == 0U)
        return -22;
    if (state->complete == 0U)
        return -11;
    uint32_t length = state->expected_length + 4U;
    if (length > USB_BLUETOOTH_ACL_MAX_PACKET ||
        state->received_length != state->expected_length ||
        !usb_bluetooth_acl_packet_valid(state->packet, length))
        return -22;
    if (length > capacity)
        return -90;
    for (uint32_t i = 0U; i < length; ++i)
        packet[i] = state->packet[i];
    usb_bluetooth_acl_reassembly_reset(state);
    return (int)length;
}

/*
 * L2CAP signaling uses a four-byte command header (code, identifier, LE16
 * length). Validate the entire command list before any caller dispatches it.
 * Unknown nonzero codes remain structurally valid for forward compatibility.
 */
int usb_bluetooth_l2cap_signaling_valid(const uint8_t *payload,
                                        uint32_t length)
{
    if (payload == NULL || length == 0U)
        return 0;
    uint32_t offset = 0U;
    while (offset < length) {
        if (length - offset < 4U)
            return 0;
        uint8_t code = payload[offset];
        uint8_t identifier = payload[offset + 1U];
        uint16_t command_length = (uint16_t)payload[offset + 2U] |
                                 ((uint16_t)payload[offset + 3U] << 8);
        if (code == 0U || identifier == 0U ||
            (uint32_t)command_length > length - offset - 4U)
            return 0;
        offset += 4U + command_length;
    }
    return offset == length;
}

/* ACL packet header: LE16 handle/flags followed by an LE16 payload length. */
int usb_bluetooth_acl_packet_valid(const uint8_t *packet, uint32_t length)
{
    if (packet == NULL || length < 4U)
        return 0;
    uint32_t payload_length = (uint32_t)packet[2] |
                              ((uint32_t)packet[3] << 8);
    return payload_length == length - 4U;
}


void usb_bluetooth_hci_event_queue_reset(
    usb_bluetooth_hci_event_queue_t *queue)
{
    if (queue == NULL)
        return;
    queue->head = 0U;
    queue->count = 0U;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_HCI_EVENT_QUEUE_DEPTH; ++i) {
        queue->lengths[i] = 0U;
        for (uint32_t j = 0U; j < USB_BLUETOOTH_HCI_EVENT_MAX; ++j)
            queue->packets[i][j] = 0U;
    }
}

int usb_bluetooth_hci_event_queue_push(
    usb_bluetooth_hci_event_queue_t *queue, const uint8_t *event,
    uint32_t length)
{
    if (queue == NULL || event == NULL ||
        length > USB_BLUETOOTH_HCI_EVENT_MAX ||
        !usb_bluetooth_hci_event_valid(event, length) ||
        queue->head >= USB_BLUETOOTH_HCI_EVENT_QUEUE_DEPTH ||
        queue->count > USB_BLUETOOTH_HCI_EVENT_QUEUE_DEPTH)
        return -22;
    if (queue->count == USB_BLUETOOTH_HCI_EVENT_QUEUE_DEPTH)
        return -11;
    uint32_t tail = (queue->head + queue->count) %
                    USB_BLUETOOTH_HCI_EVENT_QUEUE_DEPTH;
    for (uint32_t i = 0U; i < length; ++i)
        queue->packets[tail][i] = event[i];
    queue->lengths[tail] = length;
    ++queue->count;
    return 0;
}

int usb_bluetooth_hci_event_queue_pop(
    usb_bluetooth_hci_event_queue_t *queue, uint8_t *event,
    uint32_t capacity)
{
    if (queue == NULL || event == NULL || capacity == 0U ||
        queue->head >= USB_BLUETOOTH_HCI_EVENT_QUEUE_DEPTH ||
        queue->count > USB_BLUETOOTH_HCI_EVENT_QUEUE_DEPTH)
        return -22;
    if (queue->count == 0U)
        return -11;
    uint32_t length = queue->lengths[queue->head];
    if (length < 2U || length > USB_BLUETOOTH_HCI_EVENT_MAX ||
        !usb_bluetooth_hci_event_valid(queue->packets[queue->head], length))
        return -22;
    if (length > capacity)
        return -90;
    for (uint32_t i = 0U; i < length; ++i) {
        event[i] = queue->packets[queue->head][i];
        queue->packets[queue->head][i] = 0U;
    }
    queue->lengths[queue->head] = 0U;
    queue->head = (queue->head + 1U) % USB_BLUETOOTH_HCI_EVENT_QUEUE_DEPTH;
    --queue->count;
    return (int)length;
}

void usb_bluetooth_acl_queue_reset(usb_bluetooth_acl_queue_t *queue)
{
    if (queue == NULL)
        return;
    queue->head = 0U;
    queue->count = 0U;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_ACL_QUEUE_DEPTH; ++i) {
        queue->lengths[i] = 0U;
        for (uint32_t j = 0U; j < USB_BLUETOOTH_ACL_MAX_PACKET; ++j)
            queue->packets[i][j] = 0U;
    }
}

int usb_bluetooth_acl_queue_push(usb_bluetooth_acl_queue_t *queue,
                                 const uint8_t *packet, uint32_t length)
{
    if (queue == NULL || packet == NULL ||
        length > USB_BLUETOOTH_ACL_MAX_PACKET ||
        !usb_bluetooth_acl_packet_valid(packet, length) ||
        queue->head >= USB_BLUETOOTH_ACL_QUEUE_DEPTH ||
        queue->count > USB_BLUETOOTH_ACL_QUEUE_DEPTH)
        return -22;
    if (queue->count == USB_BLUETOOTH_ACL_QUEUE_DEPTH)
        return -11;

    uint32_t tail = (queue->head + queue->count) %
                    USB_BLUETOOTH_ACL_QUEUE_DEPTH;
    for (uint32_t i = 0U; i < length; ++i)
        queue->packets[tail][i] = packet[i];
    queue->lengths[tail] = length;
    ++queue->count;
    return 0;
}

int usb_bluetooth_acl_queue_pop(usb_bluetooth_acl_queue_t *queue,
                                uint8_t *packet, uint32_t capacity)
{
    if (queue == NULL || packet == NULL || capacity == 0U ||
        queue->head >= USB_BLUETOOTH_ACL_QUEUE_DEPTH ||
        queue->count > USB_BLUETOOTH_ACL_QUEUE_DEPTH)
        return -22;
    if (queue->count == 0U)
        return -11;

    uint32_t length = queue->lengths[queue->head];
    if (length < 4U || length > USB_BLUETOOTH_ACL_MAX_PACKET ||
        !usb_bluetooth_acl_packet_valid(queue->packets[queue->head], length))
        return -22;
    if (length > capacity)
        return -90;

    for (uint32_t i = 0U; i < length; ++i) {
        packet[i] = queue->packets[queue->head][i];
        queue->packets[queue->head][i] = 0U;
    }
    queue->lengths[queue->head] = 0U;
    queue->head = (queue->head + 1U) % USB_BLUETOOTH_ACL_QUEUE_DEPTH;
    --queue->count;
    return (int)length;
}
