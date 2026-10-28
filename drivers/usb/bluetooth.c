/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "usb_bluetooth.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

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

int usb_bluetooth_hci_command_encode(uint16_t opcode,
                                     const uint8_t *parameters,
                                     uint8_t parameter_length,
                                     uint8_t *output, uint32_t capacity)
{
    uint32_t required = (uint32_t)parameter_length + 3U;
    if (opcode == 0U || output == NULL ||
        (parameter_length != 0U && parameters == NULL))
        return -22;
    if (required > capacity)
        return -90;

    output[0] = (uint8_t)(opcode & 0xFFU);
    output[1] = (uint8_t)(opcode >> 8);
    output[2] = parameter_length;
    for (uint32_t i = 0U; i < parameter_length; ++i)
        output[3U + i] = parameters[i];
    return (int)required;
}

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
int usb_bluetooth_l2cap_build_acl_packet(uint16_t handle, uint16_t cid,
                                         const uint8_t *payload,
                                         uint32_t payload_length,
                                         uint8_t *output, uint32_t capacity)
{
    if (handle == 0U || handle > 0x0EFFU || cid == 0U ||
        payload_length > 0xFFFFU - 4U || output == NULL ||
        (payload_length != 0U && payload == NULL))
        return -22;
    /* L2CAP's length field counts payload bytes, excluding its 4-byte header. */
    uint32_t l2cap_length = payload_length;
    uint32_t acl_length = l2cap_length + 4U;
    uint32_t total = acl_length + 4U;
    if (total > capacity)
        return -90;
    uint16_t handle_flags = (uint16_t)(handle | 0x2000U);
    output[0] = (uint8_t)handle_flags;
    output[1] = (uint8_t)(handle_flags >> 8);
    output[2] = (uint8_t)acl_length;
    output[3] = (uint8_t)(acl_length >> 8);
    output[4] = (uint8_t)l2cap_length;
    output[5] = (uint8_t)(l2cap_length >> 8);
    output[6] = (uint8_t)cid;
    output[7] = (uint8_t)(cid >> 8);
    for (uint32_t i = 0U; i < payload_length; ++i)
        output[8U + i] = payload[i];
    return (int)total;
}

int usb_bluetooth_l2cap_build_signaling_acl_packet(
    uint16_t handle, const uint8_t *commands, uint32_t command_length,
    uint8_t *output, uint32_t capacity)
{
    if (commands == NULL || command_length == 0U ||
        !usb_bluetooth_l2cap_signaling_valid(commands, command_length))
        return -22;
    return usb_bluetooth_l2cap_build_acl_packet(
        handle, 0x0001U, commands, command_length, output, capacity);
}

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
    /*
     * Handle zero is reserved. Broadcast flag 3 is reserved by BR/EDR HCI;
     * do not pass either encoding into connection-specific reassembly.
     */
    if (view->handle == 0U || view->broadcast == 3U) {
        *view = (usb_bluetooth_acl_view_t){0};
        return 0;
    }
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
    if (state->active > 1U || state->complete > 1U ||
        state->expected_length > USB_BLUETOOTH_ACL_MAX_PACKET - 4U ||
        state->received_length > state->expected_length ||
        (state->active == 0U &&
         (state->expected_length != 0U || state->received_length != 0U)) ||
        (state->active != 0U &&
         (state->handle == 0U || state->expected_length < 4U)))
        return -22;

    usb_bluetooth_acl_view_t acl;
    if (!usb_bluetooth_acl_parse(packet, length, &acl) ||
        acl.handle == 0U || acl.broadcast != 0U)
        return -22;

    if (acl.packet_boundary == 0U || acl.packet_boundary == 2U ||
        acl.packet_boundary == 3U) {
        if (state->active != 0U) {
            /*
             * A new start packet on this handle means the prior PDU was
             * interrupted/lost. Drop that partial state instead of leaving
             * the connection permanently unable to accept another PDU.
             * This incoming packet is rejected; the next start can proceed.
             */
            usb_bluetooth_acl_reassembly_reset(state);
            return -16;
        }
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

static int l2cap_signal_shape_valid(uint8_t code, uint16_t length)
{
    /*
     * Enforce the fixed fields mandated by Core Specification L2CAP signaling.
     * Variable option/data tails remain validated by the enclosing command
     * length and are interpreted by the eventual channel state machine.
     */
    switch (code) {
    case 0x01U: return length >= 2U; /* Command Reject */
    case 0x02U: return length == 4U; /* Connection Request */
    case 0x03U: return length == 8U; /* Connection Response */
    case 0x04U: return length >= 4U; /* Configure Request + options */
    case 0x05U: return length >= 6U; /* Configure Response + options */
    case 0x06U: return length == 4U; /* Disconnection Request */
    case 0x07U: return length == 4U; /* Disconnection Response */
    case 0x08U: return 1;            /* Echo Request */
    case 0x09U: return 1;            /* Echo Response */
    case 0x0AU: return length == 2U; /* Information Request */
    case 0x0BU: return length >= 4U; /* Information Response + data */
    default: return 1;               /* Unknown codes can be rejected by peer. */
    }
}

int usb_bluetooth_l2cap_signaling_next(
    const uint8_t *payload, uint32_t length, uint32_t *offset,
    usb_bluetooth_l2cap_signal_command_t *command)
{
    if (payload == NULL || offset == NULL || command == NULL ||
        *offset > length)
        return -22;
    *command = (usb_bluetooth_l2cap_signal_command_t){0};
    if (*offset == length)
        return 0;
    if (length - *offset < 4U)
        return -22;
    uint32_t at = *offset;
    uint16_t command_length = (uint16_t)payload[at + 2U] |
                              ((uint16_t)payload[at + 3U] << 8);
    if (payload[at] == 0U || payload[at + 1U] == 0U ||
        (uint32_t)command_length > length - at - 4U ||
        !l2cap_signal_shape_valid(payload[at], command_length))
        return -22;
    command->code = payload[at];
    command->identifier = payload[at + 1U];
    command->payload = payload + at + 4U;
    command->payload_length = command_length;
    *offset = at + 4U + command_length;
    return 1;
}

int usb_bluetooth_l2cap_signaling_build(uint8_t code, uint8_t identifier,
                                        const uint8_t *parameters,
                                        uint16_t parameter_length,
                                        uint8_t *output, uint32_t capacity)
{
    uint32_t required = (uint32_t)parameter_length + 4U;
    if (code == 0U || identifier == 0U || output == NULL ||
        (parameter_length != 0U && parameters == NULL))
        return -22;
    if (required > capacity)
        return -90;
    output[0] = code;
    output[1] = identifier;
    output[2] = (uint8_t)(parameter_length & 0xFFU);
    output[3] = (uint8_t)(parameter_length >> 8);
    for (uint32_t i = 0U; i < parameter_length; ++i)
        output[4U + i] = parameters[i];
    return (int)required;
}

/*
 * L2CAP signaling uses a four-byte command header (code, identifier, LE16
 * length). Validate the entire command list before any caller dispatches it.
 * Unknown nonzero codes remain structurally valid for forward compatibility.
 */
int usb_bluetooth_l2cap_build_connection_request(uint8_t identifier,
                                                  uint16_t psm,
                                                  uint16_t local_cid,
                                                  uint8_t *output,
                                                  uint32_t capacity)
{
    if ((psm & 0x0101U) != 0x0001U ||
        local_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN)
        return -22;
    const uint8_t parameters[4] = {
        (uint8_t)psm, (uint8_t)(psm >> 8),
        (uint8_t)local_cid, (uint8_t)(local_cid >> 8)
    };
    return usb_bluetooth_l2cap_signaling_build(0x02U, identifier, parameters,
                                                sizeof(parameters), output,
                                                capacity);
}

int usb_bluetooth_l2cap_build_configuration_request(uint8_t identifier,
                                                     uint16_t remote_cid,
                                                     uint8_t *output,
                                                     uint32_t capacity)
{
    if (remote_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN)
        return -22;
    const uint8_t parameters[4] = {
        (uint8_t)remote_cid, (uint8_t)(remote_cid >> 8), 0U, 0U
    };
    return usb_bluetooth_l2cap_signaling_build(0x04U, identifier, parameters,
                                                sizeof(parameters), output,
                                                capacity);
}

int usb_bluetooth_l2cap_build_connection_response(
    uint8_t identifier, uint16_t destination_cid, uint16_t source_cid,
    uint16_t result, uint16_t status, uint8_t *output, uint32_t capacity)
{
    if ((destination_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN &&
         (destination_cid != 0U || result == 0U)) ||
        source_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN ||
        result > 4U || status > 2U ||
        (result != 0U && status != 0U))
        return -22;
    const uint8_t parameters[8] = {
        (uint8_t)destination_cid, (uint8_t)(destination_cid >> 8),
        (uint8_t)source_cid, (uint8_t)(source_cid >> 8),
        (uint8_t)result, (uint8_t)(result >> 8),
        (uint8_t)status, (uint8_t)(status >> 8)
    };
    return usb_bluetooth_l2cap_signaling_build(
        0x03U, identifier, parameters, sizeof(parameters), output, capacity);
}

/* Reject inbound connection requests until an application registers a service.
 * BR/EDR result 0x0002 means PSM not supported; destination CID is zero on
 * rejection. This prevents silently ignoring requests or advertising a service
 * whose security and profile semantics are not implemented.
 */
int usb_bluetooth_l2cap_reject_connection_request(
    const uint8_t *command, uint32_t length,
    uint8_t *response, uint32_t capacity)
{
    if (command == NULL || response == NULL || length != 8U ||
        command[0] != 0x02U || command[1] == 0U ||
        command[2] != 4U || command[3] != 0U)
        return -22;
    uint16_t psm = (uint16_t)command[4] |
                   ((uint16_t)command[5] << 8);
    uint16_t source_cid = (uint16_t)command[6] |
                          ((uint16_t)command[7] << 8);
    if ((psm & 0x0101U) != 0x0001U ||
        source_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN)
        return -22;
    return usb_bluetooth_l2cap_build_connection_response(
        command[1], 0U, source_cid, 2U, 0U, response, capacity);
}

int usb_bluetooth_l2cap_build_configuration_response(
    uint8_t identifier, uint16_t source_cid, uint16_t flags,
    uint16_t result, uint16_t mtu, uint8_t *output, uint32_t capacity)
{
    if (source_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN ||
        (flags & 0xFFFEU) != 0U || result > 4U ||
        (result == 0U && mtu < 48U))
        return -22;
    uint8_t parameters[10] = {
        (uint8_t)source_cid, (uint8_t)(source_cid >> 8),
        (uint8_t)flags, (uint8_t)(flags >> 8),
        (uint8_t)result, (uint8_t)(result >> 8),
        0x01U, 2U, (uint8_t)mtu, (uint8_t)(mtu >> 8)
    };
    uint16_t parameter_length = result == 0U ? 10U : 6U;
    return usb_bluetooth_l2cap_signaling_build(
        0x05U, identifier, parameters, parameter_length, output, capacity);
}

int usb_bluetooth_l2cap_build_disconnection_request(uint8_t identifier,
                                                     uint16_t remote_cid,
                                                     uint16_t local_cid,
                                                     uint8_t *output,
                                                     uint32_t capacity)
{
    if (remote_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN ||
        local_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN)
        return -22;
    const uint8_t parameters[4] = {
        (uint8_t)remote_cid, (uint8_t)(remote_cid >> 8),
        (uint8_t)local_cid, (uint8_t)(local_cid >> 8)
    };
    return usb_bluetooth_l2cap_signaling_build(0x06U, identifier, parameters,
                                                sizeof(parameters), output,
                                                capacity);
}

int usb_bluetooth_l2cap_dispatch_acl_responses(
    usb_bluetooth_l2cap_table_t *table, const uint8_t *packet,
    uint32_t length)
{
    if (table == NULL || packet == NULL)
        return -22;
    usb_bluetooth_l2cap_view_t l2cap;
    if (!usb_bluetooth_l2cap_parse(packet, length, &l2cap))
        return -22;
    if (l2cap.channel_id != 0x0001U)
        return 0;
    return usb_bluetooth_l2cap_dispatch_responses(
        table, l2cap.payload, l2cap.payload_length);
}

int usb_bluetooth_l2cap_dispatch_acl_responses_for_handle(
    usb_bluetooth_l2cap_table_t *table, uint16_t expected_handle,
    const uint8_t *packet, uint32_t length)
{
    if (table == NULL || packet == NULL || expected_handle == 0U)
        return -22;
    usb_bluetooth_acl_view_t acl;
    if (!usb_bluetooth_acl_parse(packet, length, &acl))
        return -22;
    if (acl.handle != expected_handle)
        return -13;
    return usb_bluetooth_l2cap_dispatch_acl_responses(table, packet, length);
}

int usb_bluetooth_l2cap_dispatch_responses(
    usb_bluetooth_l2cap_table_t *table, const uint8_t *payload,
    uint32_t length)
{
    if (table == NULL || payload == NULL ||
        !usb_bluetooth_l2cap_signaling_valid(payload, length))
        return -22;
    uint32_t offset = 0U;
    int handled = 0;
    usb_bluetooth_l2cap_signal_command_t command;
    for (;;) {
        uint32_t command_offset = offset;
        int next = usb_bluetooth_l2cap_signaling_next(
            payload, length, &offset, &command);
        if (next == 0)
            return handled;
        if (next < 0)
            return next;
        uint32_t command_size = (uint32_t)command.payload_length + 4U;
        int result = 0;
        if (command.code == 0x03U) {
            result = usb_bluetooth_l2cap_connection_response(
                table, payload + command_offset, command_size, 672U);
        } else if (command.code == 0x05U) {
            result = usb_bluetooth_l2cap_configuration_response(
                table, payload + command_offset, command_size);
        } else if (command.code == 0x07U) {
            result = usb_bluetooth_l2cap_disconnection_response(
                table, payload + command_offset, command_size);
        } else {
            continue;
        }
        /*
         * A response for an expired/unknown transaction is harmless and must
         * not suppress later commands in the same validated signaling packet.
         * Peer rejection (-5) is also a completed transaction, not a framing
         * failure. Propagate malformed state/fields and other internal errors.
         */
        if (result == -9 ||
            (command.code == 0x07U && result == -16))
            continue;
        if (result < 0 && result != -5)
            return result;
        ++handled;
    }
}

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
            (uint32_t)command_length > length - offset - 4U ||
            !l2cap_signal_shape_valid(code, command_length))
            return 0;
        offset += 4U + command_length;
    }
    return offset == length;
}

void usb_bluetooth_hci_command_state_reset(
    usb_bluetooth_hci_command_state_t *state)
{
    if (state == NULL)
        return;
    *state = (usb_bluetooth_hci_command_state_t){0};
}

int usb_bluetooth_hci_command_begin(usb_bluetooth_hci_command_state_t *state,
                                    uint16_t opcode)
{
    if (state == NULL || opcode == 0U)
        return -22;
    if (state->faulted != 0U)
        return -5;
    if (state->outstanding != 0U)
        return -16;
    if (state->credits == 0U)
        return -11;
    --state->credits;
    state->opcode = opcode;
    state->outstanding = 1U;
    state->completed = 0U;
    state->status = 0xFFU;
    state->event_kind = 0U;
    return 0;
}

int usb_bluetooth_hci_command_event(usb_bluetooth_hci_command_state_t *state,
                                    const uint8_t *event, uint32_t length)
{
    if (state == NULL || !usb_bluetooth_hci_event_valid(event, length))
        return -22;
    uint16_t opcode;
    uint8_t credits, status, kind;
    if (event[0] == 0x0EU) {
        if (length < 6U)
            return -22;
        credits = event[2];
        opcode = (uint16_t)event[3] | ((uint16_t)event[4] << 8);
        status = event[5];
        kind = 1U;
    } else if (event[0] == 0x0FU) {
        if (length != 6U)
            return -22;
        status = event[2];
        credits = event[3];
        opcode = (uint16_t)event[4] | ((uint16_t)event[5] << 8);
        kind = 2U;
    } else {
        return 0;
    }
    state->credits = credits;
    if (state->outstanding == 0U || state->opcode != opcode)
        return 0;
    state->outstanding = 0U;
    state->completed = 1U;
    state->status = status;
    state->event_kind = kind;
    return status == 0U ? 1 : -5;
}

static int hci_build_simple(uint16_t opcode, uint8_t parameter_length,
                            const uint8_t *parameters, uint8_t *out,
                            uint32_t capacity)
{
    return usb_bluetooth_hci_command_encode(opcode, parameters,
                                             parameter_length, out, capacity);
}

int usb_bluetooth_hci_build_read_local_version(uint8_t *out, uint32_t capacity)
{
    return hci_build_simple(0x1001U, 0U, NULL, out, capacity);
}

int usb_bluetooth_hci_build_read_bd_addr(uint8_t *out, uint32_t capacity)
{
    return hci_build_simple(0x1009U, 0U, NULL, out, capacity);
}

int usb_bluetooth_hci_build_read_buffer_size(uint8_t *out, uint32_t capacity)
{
    return hci_build_simple(0x1005U, 0U, NULL, out, capacity);
}

int usb_bluetooth_hci_read_buffer_size_result(
    const uint8_t *event, uint32_t length,
    usb_bluetooth_hci_buffer_size_t *result)
{
    if (event == NULL || result == NULL ||
        !usb_bluetooth_hci_event_valid(event, length))
        return -22;
    if (event[0] != 0x0EU || event[1] != 0x0BU ||
        event[3] != 0x05U || event[4] != 0x10U)
        return 0;
    if (length != 13U || event[5] != 0U)
        return -22;
    uint16_t acl_length = (uint16_t)event[6] |
                          ((uint16_t)event[7] << 8);
    uint16_t acl_packets = (uint16_t)event[9] |
                           ((uint16_t)event[10] << 8);
    uint16_t sco_packets = (uint16_t)event[11] |
                           ((uint16_t)event[12] << 8);
    if (acl_length == 0U || acl_packets == 0U)
        return -22;
    result->acl_packet_length = acl_length;
    result->sco_packet_length = event[8];
    result->total_acl_packets = acl_packets;
    result->total_sco_packets = sco_packets;
    return 1;
}

int usb_bluetooth_hci_build_inquiry(uint8_t *out, uint32_t capacity,
                                    uint8_t inquiry_length, uint8_t responses)
{
    if (inquiry_length == 0U || inquiry_length > 0x30U)
        return -22;
    const uint8_t parameters[5] = {0x33U, 0x8BU, 0x9EU,
                                   inquiry_length, responses};
    return hci_build_simple(0x0401U, sizeof(parameters), parameters,
                            out, capacity);
}

int usb_bluetooth_hci_inquiry_result(const uint8_t *event, uint32_t length,
                                     uint8_t index,
                                     usb_bluetooth_inquiry_result_t *result)
{
    if (event == NULL || result == NULL || length < 3U ||
        !usb_bluetooth_hci_event_valid(event, length) ||
        (event[0] != 0x02U && event[0] != 0x22U))
        return -22;

    uint32_t stride = event[0] == 0x02U ? 13U : 14U;
    uint32_t response_count = event[2];
    if (event[1] != 1U + response_count * stride ||
        length != (uint32_t)event[1] + 2U)
        return -22;
    if ((uint32_t)index >= response_count)
        return 0;

    uint32_t offset = 3U + (uint32_t)index * stride;
    usb_bluetooth_inquiry_result_t parsed = {0};
    uint8_t address_nonzero = 0U;
    for (uint32_t i = 0U; i < 6U; ++i) {
        parsed.address[i] = event[offset + i];
        address_nonzero |= parsed.address[i];
    }
    parsed.page_scan_repetition_mode = event[offset + 6U];
    if (address_nonzero == 0U || parsed.page_scan_repetition_mode > 2U ||
        event[offset + 7U] != 0U)
        return -22;

    parsed.class_of_device = (uint32_t)event[offset + 8U] |
                             ((uint32_t)event[offset + 9U] << 8) |
                             ((uint32_t)event[offset + 10U] << 16);
    parsed.clock_offset = (uint16_t)event[offset + 11U] |
                          ((uint16_t)event[offset + 12U] << 8);
    if (stride == 14U) {
        parsed.rssi = (int8_t)event[offset + 13U];
        parsed.has_rssi = 1U;
    }
    *result = parsed;
    return 1;
}

void usb_bluetooth_discovery_reset(usb_bluetooth_discovery_table_t *table)
{
    if (table == NULL)
        return;
    *table = (usb_bluetooth_discovery_table_t){0};
}

int usb_bluetooth_discovery_record(
    usb_bluetooth_discovery_table_t *table,
    const usb_bluetooth_inquiry_result_t *result)
{
    if (table == NULL || result == NULL || table->count >
            USB_BLUETOOTH_DISCOVERY_MAX)
        return -22;
    uint8_t nonzero = 0U;
    for (uint32_t i = 0U; i < 6U; ++i)
        nonzero |= result->address[i];
    if (nonzero == 0U || result->page_scan_repetition_mode > 2U ||
        result->class_of_device > 0xFFFFFFU || result->has_rssi > 1U)
        return -22;
    for (uint32_t i = 0U; i < table->count; ++i) {
        if (memcmp(table->entries[i].address, result->address, 6U) == 0) {
            table->entries[i] = *result;
            return 1;
        }
    }
    if (table->count == USB_BLUETOOTH_DISCOVERY_MAX)
        return -28;
    table->entries[table->count++] = *result;
    return 0;
}

int usb_bluetooth_discovery_get(
    const usb_bluetooth_discovery_table_t *table, uint32_t index,
    usb_bluetooth_inquiry_result_t *result)
{
    if (table == NULL || result == NULL ||
        table->count > USB_BLUETOOTH_DISCOVERY_MAX)
        return -22;
    if (index >= table->count)
        return 0;
    *result = table->entries[index];
    return 1;
}

int usb_bluetooth_hci_build_create_connection(
    const uint8_t address[6], uint16_t packet_type,
    uint8_t page_scan_repetition_mode, uint16_t clock_offset,
    uint8_t allow_role_switch, uint8_t *out, uint32_t capacity)
{
    if (address == NULL || out == NULL || packet_type == 0U ||
        page_scan_repetition_mode > 2U ||
        allow_role_switch > 1U)
        return -22;
    uint8_t address_nonzero = 0U;
    for (uint32_t i = 0U; i < 6U; ++i)
        address_nonzero |= address[i];
    if (address_nonzero == 0U)
        return -22;

    const uint8_t parameters[13] = {
        address[0], address[1], address[2],
        address[3], address[4], address[5],
        (uint8_t)packet_type, (uint8_t)(packet_type >> 8),
        page_scan_repetition_mode, 0U,
        (uint8_t)clock_offset, (uint8_t)(clock_offset >> 8),
        allow_role_switch
    };
    return hci_build_simple(0x0405U, sizeof(parameters), parameters,
                            out, capacity);
}

int usb_bluetooth_hci_build_disconnect(uint16_t handle, uint8_t reason,
                                       uint8_t *out, uint32_t capacity)
{
    if (handle == 0U || handle > 0x0EFFU || reason == 0U || out == NULL)
        return -22;
    const uint8_t parameters[3] = {
        (uint8_t)handle, (uint8_t)(handle >> 8), reason
    };
    return hci_build_simple(0x0406U, sizeof(parameters), parameters,
                            out, capacity);
}

int usb_bluetooth_hci_build_create_connection_cancel(
    const uint8_t address[6], uint8_t *out, uint32_t capacity)
{
    if (address == NULL || out == NULL)
        return -22;
    uint8_t address_nonzero = 0U;
    for (uint32_t i = 0U; i < 6U; ++i)
        address_nonzero |= address[i];
    if (address_nonzero == 0U)
        return -22;
    return hci_build_simple(0x0408U, 6U, address, out, capacity);
}

void usb_bluetooth_l2cap_table_reset(usb_bluetooth_l2cap_table_t *table)
{
    if (table != NULL)
        *table = (usb_bluetooth_l2cap_table_t){.next_local_cid = 0x0040U,
                                                .next_identifier = 1U};
}

static int l2cap_identifier_in_use(
    const usb_bluetooth_l2cap_table_t *table, uint8_t identifier)
{
    if (table == NULL || identifier == 0U)
        return 0;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i) {
        const usb_bluetooth_l2cap_channel_t *c = &table->channels[i];
        if (c->active && (c->identifier == identifier ||
                          c->config_identifier == identifier ||
                          c->disconnect_identifier == identifier))
            return 1;
    }
    return 0;
}

int usb_bluetooth_l2cap_channel_begin(usb_bluetooth_l2cap_table_t *table,
                                      uint16_t psm, uint16_t local_mtu,
                                      uint32_t deadline,
                                      uint16_t *local_cid,
                                      uint8_t *identifier)
{
    if (table == NULL || local_cid == NULL || identifier == NULL ||
        (psm & 0x0101U) != 0x0001U || local_mtu < 48U ||
        local_mtu > USB_BLUETOOTH_L2CAP_DATA_MAX)
        return -22;
    uint32_t index = USB_BLUETOOTH_L2CAP_CHANNEL_MAX;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i)
        if (!table->channels[i].active) { index = i; break; }
    if (index == USB_BLUETOOTH_L2CAP_CHANNEL_MAX)
        return -28;
    uint16_t cid = table->next_local_cid;
    if (cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN)
        cid = USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN;
    for (uint32_t attempts = 0U; attempts < 0xFFC0U; ++attempts) {
        int used = 0;
        for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i)
            if (table->channels[i].active &&
                table->channels[i].local_cid == cid) used = 1;
        if (!used) break;
        ++cid;
        if (cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN) cid = 0x0040U;
    }
    uint8_t id = table->next_identifier;
    int identifier_found = 0;
    for (uint32_t attempts = 0U; attempts < 255U; ++attempts) {
        if (id == 0U) id = 1U;
        if (!l2cap_identifier_in_use(table, id)) {
            identifier_found = 1;
            break;
        }
        id = (uint8_t)(id + 1U);
    }
    if (!identifier_found)
        return -28;
    table->next_identifier = (uint8_t)(id + 1U);
    if (table->next_identifier == 0U) table->next_identifier = 1U;
    table->next_local_cid = (uint16_t)(cid + 1U);
    if (table->next_local_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN)
        table->next_local_cid = USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN;
    table->channels[index] = (usb_bluetooth_l2cap_channel_t){
        .active = 1U, .state = 1U, .identifier = id,
        .local_cid = cid, .psm = psm, .local_mtu = local_mtu,
        .remote_mtu = 672U, .deadline = deadline
    };
    *local_cid = cid;
    *identifier = id;
    return 0;
}

int usb_bluetooth_l2cap_channel_abort(usb_bluetooth_l2cap_table_t *table,
                                      uint16_t local_cid)
{
    if (table == NULL || local_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN)
        return -22;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i) {
        usb_bluetooth_l2cap_channel_t *channel = &table->channels[i];
        if (channel->active && channel->local_cid == local_cid) {
            if (channel->state == 4U || channel->state == 5U)
                return -16;
            *channel = (usb_bluetooth_l2cap_channel_t){0};
            return 1;
        }
    }
    return -9;
}

int usb_bluetooth_l2cap_channel_response(usb_bluetooth_l2cap_table_t *table,
                                         uint8_t identifier, uint16_t remote_cid,
                                         uint16_t result, uint16_t remote_mtu)
{
    if (table == NULL || identifier == 0U)
        return -22;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i) {
        usb_bluetooth_l2cap_channel_t *c = &table->channels[i];
        if (!c->active || c->identifier != identifier || c->state != 1U)
            continue;
        if (result != 0U) { *c = (usb_bluetooth_l2cap_channel_t){0}; return -5; }
        if (remote_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN ||
            remote_mtu < 48U)
            return -22;
        for (uint32_t j = 0U; j < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++j)
            if (j != i && table->channels[j].active &&
                table->channels[j].remote_cid == remote_cid)
                return -17;
        c->remote_cid = remote_cid;
        c->remote_mtu = remote_mtu;
        c->state = 3U;
        return 1;
    }
    return -9;
}

int usb_bluetooth_l2cap_connection_response(
    usb_bluetooth_l2cap_table_t *table,
    const uint8_t *command, uint32_t length, uint16_t default_remote_mtu)
{
    if (table == NULL || command == NULL || length != 12U ||
        command[0] != 0x03U || command[1] == 0U ||
        command[2] != 8U || command[3] != 0U)
        return -22;
    uint16_t remote_cid = (uint16_t)command[4] |
                          ((uint16_t)command[5] << 8);
    uint16_t echoed_local_cid = (uint16_t)command[6] |
                                ((uint16_t)command[7] << 8);
    uint16_t result = (uint16_t)command[8] |
                      ((uint16_t)command[9] << 8);
    uint16_t status = (uint16_t)command[10] |
                      ((uint16_t)command[11] << 8);
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i) {
        usb_bluetooth_l2cap_channel_t *channel = &table->channels[i];
        if (!channel->active || channel->identifier != command[1] ||
            channel->state != 1U)
            continue;
        if (channel->local_cid != echoed_local_cid)
            return -22;
        /* 0x0001 is Pending; retain the transaction for its later response. */
        if (result == 1U)
            return 0;
        if (result != 0U) {
            *channel = (usb_bluetooth_l2cap_channel_t){0};
            return -5;
        }
        if (status != 0U || remote_cid <
                USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN ||
            default_remote_mtu < 48U)
            return -22;
        return usb_bluetooth_l2cap_channel_response(
            table, command[1], remote_cid, 0U, default_remote_mtu);
    }
    return -9;
}

int usb_bluetooth_l2cap_channel_configure_begin(
    usb_bluetooth_l2cap_table_t *table, uint16_t local_cid,
    uint8_t identifier)
{
    if (table == NULL || identifier == 0U ||
        local_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN)
        return -22;
    if (l2cap_identifier_in_use(table, identifier))
        return -16;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i) {
        usb_bluetooth_l2cap_channel_t *c = &table->channels[i];
        if (c->active && c->local_cid == local_cid) {
            if (c->state != 3U) return -16;
            c->config_identifier = identifier;
            return 1;
        }
    }
    return -9;
}

int usb_bluetooth_l2cap_configuration_request_begin(
    usb_bluetooth_l2cap_table_t *table, uint16_t local_cid,
    uint8_t *output, uint32_t capacity)
{
    if (table == NULL || output == NULL ||
        local_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN)
        return -22;
    usb_bluetooth_l2cap_channel_t *channel = NULL;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i) {
        usb_bluetooth_l2cap_channel_t *candidate = &table->channels[i];
        if (candidate->active && candidate->local_cid == local_cid) {
            channel = candidate;
            break;
        }
    }
    if (channel == NULL)
        return -9;
    if (channel->state != 3U || channel->remote_cid <
            USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN ||
        channel->config_identifier != 0U)
        return -16;

    uint8_t identifier = table->next_identifier;
    int found = 0;
    for (uint32_t attempts = 0U; attempts < 255U; ++attempts) {
        if (identifier == 0U)
            identifier = 1U;
        if (!l2cap_identifier_in_use(table, identifier)) {
            found = 1;
            break;
        }
        identifier = (uint8_t)(identifier + 1U);
    }
    if (!found)
        return -28;
    int begun = usb_bluetooth_l2cap_channel_configure_begin(
        table, local_cid, identifier);
    if (begun != 1)
        return begun;
    int built = usb_bluetooth_l2cap_build_configuration_request(
        identifier, channel->remote_cid, output, capacity);
    if (built < 0) {
        channel->config_identifier = 0U;
        return built;
    }
    table->next_identifier = (uint8_t)(identifier + 1U);
    if (table->next_identifier == 0U)
        table->next_identifier = 1U;
    return built;
}

int usb_bluetooth_l2cap_configuration_request_failed(
    usb_bluetooth_l2cap_table_t *table, uint16_t local_cid,
    uint8_t identifier)
{
    if (table == NULL || local_cid <
            USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN || identifier == 0U)
        return -22;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i) {
        usb_bluetooth_l2cap_channel_t *channel = &table->channels[i];
        if (channel->active && channel->local_cid == local_cid) {
            if (channel->state != 3U ||
                channel->config_identifier != identifier)
                return -16;
            channel->config_identifier = 0U;
            return 1;
        }
    }
    return -9;
}

int usb_bluetooth_l2cap_configuration_request(
    usb_bluetooth_l2cap_table_t *table, const uint8_t *command,
    uint32_t length, uint8_t *response, uint32_t capacity,
    uint16_t *local_cid)
{
    if (table == NULL || command == NULL || response == NULL ||
        local_cid == NULL || length < 8U || command[0] != 0x04U ||
        command[1] == 0U ||
        (uint32_t)command[2] + ((uint32_t)command[3] << 8) != length - 4U ||
        !usb_bluetooth_l2cap_signaling_valid(command, length))
        return -22;
    uint16_t cid = (uint16_t)command[4] |
                   ((uint16_t)command[5] << 8);
    uint16_t flags = (uint16_t)command[6] |
                     ((uint16_t)command[7] << 8);
    *local_cid = 0U;

    usb_bluetooth_l2cap_channel_t *channel = NULL;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i) {
        usb_bluetooth_l2cap_channel_t *candidate = &table->channels[i];
        if (candidate->active && candidate->local_cid == cid) {
            channel = candidate;
            break;
        }
    }
    if (channel == NULL || (channel->state != 3U && channel->state != 4U))
        return -9;
    if (flags != 0U)
        return usb_bluetooth_l2cap_build_configuration_response(
            command[1], cid, 0U, 2U, 48U, response, capacity);

    uint32_t offset = 8U;
    uint8_t mtu_seen = 0U;
    uint16_t peer_mtu = 672U;
    while (offset < length) {
        if (length - offset < 2U)
            return -22;
        uint8_t type = command[offset];
        uint8_t option_length = command[offset + 1U];
        offset += 2U;
        if ((uint32_t)option_length > length - offset)
            return -22;
        uint8_t option_type = type & 0x7FU;
        if (option_type == 0x01U) {
            if (mtu_seen != 0U || option_length != 2U)
                return -22;
            peer_mtu = (uint16_t)command[offset] |
                       ((uint16_t)command[offset + 1U] << 8);
            if (peer_mtu < 48U)
                return -22;
            mtu_seen = 1U;
        } else if ((type & 0x80U) == 0U) {
            /* Mandatory options we cannot honor are rejected explicitly. */
            return usb_bluetooth_l2cap_build_configuration_response(
                command[1], cid, 0U, 2U, 48U, response, capacity);
        }
        offset += option_length;
    }

    int built = usb_bluetooth_l2cap_build_configuration_response(
        command[1], cid, 0U, 0U, channel->local_mtu,
        response, capacity);
    if (built < 0)
        return built;
    channel->remote_mtu = peer_mtu;
    *local_cid = cid;
    return built;
}

int usb_bluetooth_l2cap_channel_remote_configured(
    usb_bluetooth_l2cap_table_t *table, uint16_t local_cid)
{
    if (table == NULL || local_cid <
            USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN)
        return -22;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i) {
        usb_bluetooth_l2cap_channel_t *channel = &table->channels[i];
        if (channel->active && channel->local_cid == local_cid) {
            if (channel->state != 3U && channel->state != 4U)
                return -16;
            channel->remote_configured = 1U;
            if (channel->local_configured != 0U) {
                channel->state = 4U;
                channel->deadline = 0U;
            }
            return channel->state == 4U ? 1 : 0;
        }
    }
    return -9;
}

int usb_bluetooth_l2cap_channel_configure_response(
    usb_bluetooth_l2cap_table_t *table, uint16_t local_cid,
    uint8_t identifier, uint16_t flags, uint16_t result,
    uint16_t remote_mtu)
{
    if (table == NULL || identifier == 0U ||
        local_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN)
        return -22;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i) {
        usb_bluetooth_l2cap_channel_t *c = &table->channels[i];
        if (!c->active || c->local_cid != local_cid)
            continue;
        if (c->state != 3U || c->config_identifier != identifier)
            return -16;
        if (flags != 0U)
            return 0;
        if (result > 4U)
            return -22;
        if (result == 4U)
            return 0; /* Configuration is pending; retain the transaction. */
        if (result != 0U) {
            *c = (usb_bluetooth_l2cap_channel_t){0};
            return -5;
        }
        if (remote_mtu < 48U)
            return -22;
        c->remote_mtu = remote_mtu;
        c->local_configured = 1U;
        c->config_identifier = 0U;
        if (c->remote_configured != 0U) {
            c->state = 4U;
            c->deadline = 0U;
        }
        return c->state == 4U ? 1 : 0;
    }
    return -9;
}

int usb_bluetooth_l2cap_channel_configured(usb_bluetooth_l2cap_table_t *table,
                                           uint16_t local_cid)
{
    if (table == NULL || local_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN)
        return -22;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i) {
        usb_bluetooth_l2cap_channel_t *c = &table->channels[i];
        if (c->active && c->local_cid == local_cid) {
            if (c->state != 3U) return -16;
            c->local_configured = 1U;
            c->remote_configured = 1U;
            c->state = 4U;
            c->deadline = 0U;
            return 1;
        }
    }
    return -9;
}

int usb_bluetooth_l2cap_configuration_response(
    usb_bluetooth_l2cap_table_t *table, const uint8_t *command,
    uint32_t length)
{
    if (table == NULL || command == NULL || length < 10U ||
        command[0] != 0x05U || command[1] == 0U ||
        (uint32_t)command[2] + ((uint32_t)command[3] << 8) != length - 4U ||
        !usb_bluetooth_l2cap_signaling_valid(command, length))
        return -22;
    uint16_t local_cid = (uint16_t)command[4] |
                         ((uint16_t)command[5] << 8);
    uint16_t flags = (uint16_t)command[6] |
                     ((uint16_t)command[7] << 8);
    uint16_t result = (uint16_t)command[8] |
                      ((uint16_t)command[9] << 8);
    uint16_t remote_mtu = 672U;
    if (flags == 0U && result == 0U) {
        uint32_t offset = 10U;
        uint8_t mtu_seen = 0U;
        while (offset < length) {
            if (length - offset < 2U)
                return -22;
            uint8_t type = command[offset];
            uint8_t option_length = command[offset + 1U];
            offset += 2U;
            if ((uint32_t)option_length > length - offset)
                return -22;
            uint8_t option_type = type & 0x7FU;
            if (option_type == 0x01U) {
                if (mtu_seen != 0U || option_length != 2U)
                    return -22;
                remote_mtu = (uint16_t)command[offset] |
                             ((uint16_t)command[offset + 1U] << 8);
                if (remote_mtu < 48U)
                    return -22;
                mtu_seen = 1U;
            } else if ((type & 0x80U) == 0U) {
                /* Unknown mandatory option: do not silently accept it. */
                return -95;
            }
            offset += option_length;
        }
    }
    return usb_bluetooth_l2cap_channel_configure_response(
        table, local_cid, command[1], flags, result, remote_mtu);
}

int usb_bluetooth_l2cap_channel_disconnect_begin(
    usb_bluetooth_l2cap_table_t *table, uint16_t local_cid,
    uint8_t identifier)
{
    if (table == NULL || local_cid <
            USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN || identifier == 0U)
        return -22;
    if (l2cap_identifier_in_use(table, identifier))
        return -16;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i) {
        usb_bluetooth_l2cap_channel_t *channel = &table->channels[i];
        if (!channel->active || channel->local_cid != local_cid)
            continue;
        if (channel->state != 4U ||
            channel->remote_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN)
            return -16;
        channel->state = 5U;
        channel->disconnect_identifier = identifier;
        return 1;
    }
    return -9;
}

int usb_bluetooth_l2cap_channel_disconnect_failed(
    usb_bluetooth_l2cap_table_t *table, uint16_t local_cid,
    uint8_t identifier)
{
    if (table == NULL || local_cid <
            USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN || identifier == 0U)
        return -22;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i) {
        usb_bluetooth_l2cap_channel_t *channel = &table->channels[i];
        if (channel->active && channel->local_cid == local_cid) {
            if (channel->state != 5U ||
                channel->disconnect_identifier != identifier)
                return -16;
            channel->state = 4U;
            channel->disconnect_identifier = 0U;
            return 1;
        }
    }
    return -9;
}

int usb_bluetooth_l2cap_disconnection_response(
    usb_bluetooth_l2cap_table_t *table, const uint8_t *command,
    uint32_t length)
{
    if (table == NULL || command == NULL || length != 8U ||
        command[0] != 0x07U || command[1] == 0U ||
        command[2] != 4U || command[3] != 0U)
        return -22;
    uint16_t remote_cid = (uint16_t)command[4] |
                          ((uint16_t)command[5] << 8);
    uint16_t local_cid = (uint16_t)command[6] |
                         ((uint16_t)command[7] << 8);
    if (remote_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN ||
        local_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN)
        return -22;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i) {
        usb_bluetooth_l2cap_channel_t *channel = &table->channels[i];
        if (channel->active && channel->local_cid == local_cid) {
            if (channel->state != 5U ||
                channel->disconnect_identifier != command[1] ||
                channel->remote_cid != remote_cid)
                return -16;
            *channel = (usb_bluetooth_l2cap_channel_t){0};
            return 1;
        }
    }
    return -9;
}

int usb_bluetooth_l2cap_disconnection_request(
    usb_bluetooth_l2cap_table_t *table, const uint8_t *command,
    uint32_t length, uint8_t *response, uint32_t capacity,
    uint16_t *local_cid)
{
    if (table == NULL || command == NULL || response == NULL ||
        local_cid == NULL || length != 8U || command[0] != 0x06U ||
        command[1] == 0U || command[2] != 4U || command[3] != 0U)
        return -22;
    uint16_t destination_cid = (uint16_t)command[4] |
                              ((uint16_t)command[5] << 8);
    uint16_t source_cid = (uint16_t)command[6] |
                          ((uint16_t)command[7] << 8);
    if (destination_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN ||
        source_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN)
        return -22;
    int built = usb_bluetooth_l2cap_signaling_build(
        0x07U, command[1], &command[4], 4U, response, capacity);
    if (built < 0)
        return built;

    *local_cid = 0U;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i) {
        const usb_bluetooth_l2cap_channel_t *channel = &table->channels[i];
        if (channel->active && channel->local_cid == destination_cid &&
            channel->remote_cid == source_cid) {
            *local_cid = destination_cid;
            break;
        }
    }
    return built;
}

int usb_bluetooth_l2cap_channel_close(usb_bluetooth_l2cap_table_t *table,
                                      uint16_t local_cid)
{
    if (table == NULL || local_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN)
        return -22;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i) {
        usb_bluetooth_l2cap_channel_t *c = &table->channels[i];
        if (c->active && c->local_cid == local_cid) {
            *c = (usb_bluetooth_l2cap_channel_t){0};
            return 1;
        }
    }
    return -9;
}

uint32_t usb_bluetooth_l2cap_expire(usb_bluetooth_l2cap_table_t *table,
                                    uint32_t now)
{
    if (table == NULL) return 0U;
    uint32_t expired = 0U;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i) {
        usb_bluetooth_l2cap_channel_t *c = &table->channels[i];
        if (c->active && c->deadline != 0U &&
            (c->state == 1U || c->state == 3U || c->state == 5U) &&
            (int32_t)(now - c->deadline) >= 0) {
            *c = (usb_bluetooth_l2cap_channel_t){0};
            ++expired;
        }
    }
    return expired;
}

void usb_bluetooth_l2cap_data_queue_reset(
    usb_bluetooth_l2cap_data_queue_t *queue)
{
    if (queue == NULL)
        return;
    *queue = (usb_bluetooth_l2cap_data_queue_t){0};
}

int usb_bluetooth_l2cap_data_queue_push(
    usb_bluetooth_l2cap_data_queue_t *queue, uint16_t handle,
    uint16_t local_cid, const uint8_t *payload, uint32_t length)
{
    if (queue == NULL || handle == 0U || handle > 0x0EFFU ||
        local_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN ||
        payload == NULL || length == 0U ||
        length > USB_BLUETOOTH_L2CAP_DATA_MAX)
        return -22;
    if (queue->head >= USB_BLUETOOTH_L2CAP_DATA_QUEUE_DEPTH ||
        queue->count > USB_BLUETOOTH_L2CAP_DATA_QUEUE_DEPTH)
        return -22;
    if (queue->count == USB_BLUETOOTH_L2CAP_DATA_QUEUE_DEPTH)
        return -11;
    uint32_t index = (queue->head + queue->count) %
                     USB_BLUETOOTH_L2CAP_DATA_QUEUE_DEPTH;
    usb_bluetooth_l2cap_data_packet_t *packet = &queue->packets[index];
    packet->handle = handle;
    packet->local_cid = local_cid;
    packet->length = (uint16_t)length;
    for (uint32_t i = 0U; i < length; ++i)
        packet->payload[i] = payload[i];
    for (uint32_t i = length; i < USB_BLUETOOTH_L2CAP_DATA_MAX; ++i)
        packet->payload[i] = 0U;
    ++queue->count;
    return 0;
}

int usb_bluetooth_l2cap_data_queue_pop(
    usb_bluetooth_l2cap_data_queue_t *queue, uint16_t handle,
    uint16_t local_cid, uint8_t *payload, uint32_t capacity)
{
    if (queue == NULL || handle == 0U ||
        local_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN ||
        payload == NULL || capacity == 0U)
        return -22;
    if (queue->head >= USB_BLUETOOTH_L2CAP_DATA_QUEUE_DEPTH ||
        queue->count > USB_BLUETOOTH_L2CAP_DATA_QUEUE_DEPTH)
        return -22;
    uint32_t found = queue->count;
    for (uint32_t i = 0U; i < queue->count; ++i) {
        uint32_t index = (queue->head + i) %
                         USB_BLUETOOTH_L2CAP_DATA_QUEUE_DEPTH;
        if (queue->packets[index].handle == handle &&
            queue->packets[index].local_cid == local_cid) {
            found = i;
            break;
        }
    }
    if (found == queue->count)
        return -11;
    uint32_t index = (queue->head + found) %
                     USB_BLUETOOTH_L2CAP_DATA_QUEUE_DEPTH;
    usb_bluetooth_l2cap_data_packet_t *packet = &queue->packets[index];
    if (packet->length == 0U ||
        packet->length > USB_BLUETOOTH_L2CAP_DATA_MAX)
        return -22;
    if ((uint32_t)packet->length > capacity)
        return -90;
    uint32_t length = packet->length;
    for (uint32_t i = 0U; i < length; ++i)
        payload[i] = packet->payload[i];
    for (uint32_t i = found; i + 1U < queue->count; ++i) {
        uint32_t dst = (queue->head + i) %
                       USB_BLUETOOTH_L2CAP_DATA_QUEUE_DEPTH;
        uint32_t src = (queue->head + i + 1U) %
                       USB_BLUETOOTH_L2CAP_DATA_QUEUE_DEPTH;
        queue->packets[dst] = queue->packets[src];
    }
    uint32_t last = (queue->head + queue->count - 1U) %
                    USB_BLUETOOTH_L2CAP_DATA_QUEUE_DEPTH;
    queue->packets[last] = (usb_bluetooth_l2cap_data_packet_t){0};
    --queue->count;
    return (int)length;
}

static void l2cap_data_queue_forget(
    usb_bluetooth_l2cap_data_queue_t *queue, uint16_t handle,
    uint16_t local_cid, int match_handle_only)
{
    if (queue == NULL ||
        queue->head >= USB_BLUETOOTH_L2CAP_DATA_QUEUE_DEPTH ||
        queue->count > USB_BLUETOOTH_L2CAP_DATA_QUEUE_DEPTH)
        return;
    uint32_t original_count = queue->count;
    uint32_t kept = 0U;
    for (uint32_t i = 0U; i < original_count; ++i) {
        uint32_t source = (queue->head + i) %
                          USB_BLUETOOTH_L2CAP_DATA_QUEUE_DEPTH;
        usb_bluetooth_l2cap_data_packet_t *packet = &queue->packets[source];
        int matches = packet->handle == handle &&
                      (match_handle_only || packet->local_cid == local_cid);
        if (matches)
            continue;
        uint32_t destination = (queue->head + kept) %
                               USB_BLUETOOTH_L2CAP_DATA_QUEUE_DEPTH;
        if (destination != source)
            queue->packets[destination] = *packet;
        ++kept;
    }
    for (uint32_t i = kept; i < original_count; ++i) {
        uint32_t index = (queue->head + i) %
                         USB_BLUETOOTH_L2CAP_DATA_QUEUE_DEPTH;
        queue->packets[index] = (usb_bluetooth_l2cap_data_packet_t){0};
    }
    queue->count = kept;
}

void usb_bluetooth_l2cap_data_queue_forget_channel(
    usb_bluetooth_l2cap_data_queue_t *queue, uint16_t handle,
    uint16_t local_cid)
{
    if (handle == 0U ||
        local_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN)
        return;
    l2cap_data_queue_forget(queue, handle, local_cid, 0);
}

void usb_bluetooth_l2cap_data_queue_forget_handle(
    usb_bluetooth_l2cap_data_queue_t *queue, uint16_t handle)
{
    if (handle == 0U)
        return;
    l2cap_data_queue_forget(queue, handle, 0U, 1);
}

void usb_bluetooth_connection_attempt_reset(
    usb_bluetooth_connection_attempt_t *attempt)
{
    if (attempt == NULL)
        return;
    attempt->active = 0U;
    for (uint32_t i = 0U; i < 6U; ++i)
        attempt->address[i] = 0U;
}

int usb_bluetooth_connection_attempt_begin(
    usb_bluetooth_connection_attempt_t *attempt, const uint8_t address[6])
{
    if (attempt == NULL || address == NULL)
        return -22;
    if (attempt->active)
        return -17;
    uint8_t nonzero = 0U;
    for (uint32_t i = 0U; i < 6U; ++i)
        nonzero |= address[i];
    if (nonzero == 0U)
        return -22;
    attempt->active = 1U;
    for (uint32_t i = 0U; i < 6U; ++i)
        attempt->address[i] = address[i];
    return 0;
}

int usb_bluetooth_connection_attempt_matches(
    const usb_bluetooth_connection_attempt_t *attempt,
    const uint8_t address[6])
{
    if (attempt == NULL || address == NULL || !attempt->active)
        return 0;
    uint8_t difference = 0U;
    for (uint32_t i = 0U; i < 6U; ++i)
        difference |= (uint8_t)(attempt->address[i] ^ address[i]);
    return difference == 0U;
}

int usb_bluetooth_connection_attempt_finish(
    usb_bluetooth_connection_attempt_t *attempt, const uint8_t address[6])
{
    if (!usb_bluetooth_connection_attempt_matches(attempt, address))
        return 0;
    usb_bluetooth_connection_attempt_reset(attempt);
    return 1;
}

void usb_bluetooth_inquiry_state_reset(usb_bluetooth_inquiry_state_t *state)
{
    if (state == NULL)
        return;
    *state = (usb_bluetooth_inquiry_state_t){0};
}

int usb_bluetooth_inquiry_state_begin(usb_bluetooth_inquiry_state_t *state)
{
    if (state == NULL)
        return -22;
    if (state->active != 0U)
        return -16;
    state->active = 1U;
    state->cancel_state = 0U;
    return 0;
}

int usb_bluetooth_inquiry_state_cancel_begin(usb_bluetooth_inquiry_state_t *state)
{
    if (state == NULL)
        return -22;
    if (state->active == 0U)
        return -2;
    if (state->cancel_state != 0U)
        return -16;
    state->cancel_state = 1U;
    return 0;
}

int usb_bluetooth_inquiry_state_cancel_result(
    usb_bluetooth_inquiry_state_t *state, int accepted)
{
    if (state == NULL)
        return -22;
    if (state->active == 0U || state->cancel_state != 1U)
        return -2;
    state->cancel_state = accepted ? 2U : 0U;
    return accepted ? 1 : 0;
}

void usb_bluetooth_inquiry_state_command_rejected(
    usb_bluetooth_inquiry_state_t *state)
{
    if (state == NULL)
        return;
    usb_bluetooth_inquiry_state_reset(state);
}

void usb_bluetooth_inquiry_state_complete(usb_bluetooth_inquiry_state_t *state)
{
    if (state == NULL)
        return;
    usb_bluetooth_inquiry_state_reset(state);
}

void usb_bluetooth_link_table_reset(usb_bluetooth_link_table_t *table)
{
    if (table == NULL)
        return;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_LINK_MAX; ++i) {
        table->links[i].active = 0U;
        table->links[i].handle = 0U;
        usb_bluetooth_acl_reassembly_reset(&table->links[i].reassembly);
    }
}

int usb_bluetooth_link_table_event(usb_bluetooth_link_table_t *table,
                                   const uint8_t *event, uint32_t length)
{
    if (table == NULL || !usb_bluetooth_hci_event_valid(event, length))
        return -22;
    if (event[0] == 0x03U) {
        if (length != 13U || event[1] != 11U)
            return -22;
        if (event[2] != 0U)
            return -5;
        uint16_t handle = (uint16_t)event[3] |
                          ((uint16_t)(event[4] & 0x0FU) << 8);
        if (handle == 0U || (event[4] & 0xF0U) != 0U)
            return -22;
        uint32_t free_index = USB_BLUETOOTH_LINK_MAX;
        for (uint32_t i = 0U; i < USB_BLUETOOTH_LINK_MAX; ++i) {
            if (table->links[i].active && table->links[i].handle == handle)
                return -17;
            if (!table->links[i].active && free_index == USB_BLUETOOTH_LINK_MAX)
                free_index = i;
        }
        if (free_index == USB_BLUETOOTH_LINK_MAX)
            return -28;
        table->links[free_index].active = 1U;
        table->links[free_index].handle = handle;
        usb_bluetooth_acl_reassembly_reset(
            &table->links[free_index].reassembly);
        return 1;
    }
    if (event[0] == 0x05U) {
        if (length != 6U || event[1] != 4U)
            return -22;
        if (event[2] != 0U)
            return -5;
        uint16_t handle = (uint16_t)event[3] |
                          ((uint16_t)(event[4] & 0x0FU) << 8);
        if (handle == 0U || (event[4] & 0xF0U) != 0U)
            return -22;
        for (uint32_t i = 0U; i < USB_BLUETOOTH_LINK_MAX; ++i) {
            if (table->links[i].active && table->links[i].handle == handle) {
                table->links[i].active = 0U;
                table->links[i].handle = 0U;
                usb_bluetooth_acl_reassembly_reset(
                    &table->links[i].reassembly);
                return 1;
            }
        }
        return -9;
    }
    return 0;
}

uint32_t usb_bluetooth_link_table_active_count(
    const usb_bluetooth_link_table_t *table)
{
    if (table == NULL)
        return 0U;
    uint32_t count = 0U;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_LINK_MAX; ++i)
        if (table->links[i].active != 0U)
            ++count;
    return count;
}

int usb_bluetooth_link_table_has_handle(
    const usb_bluetooth_link_table_t *table, uint16_t handle)
{
    if (table == NULL || handle == 0U || handle > 0x0EFFU)
        return 0;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_LINK_MAX; ++i)
        if (table->links[i].active && table->links[i].handle == handle)
            return 1;
    return 0;
}

int usb_bluetooth_link_table_is_single_handle(
    const usb_bluetooth_link_table_t *table, uint16_t handle)
{
    if (table == NULL || handle == 0U)
        return 0;
    int found = 0;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_LINK_MAX; ++i) {
        if (!table->links[i].active)
            continue;
        if (table->links[i].handle != handle)
            return 0;
        found = 1;
    }
    return found;
}

int usb_bluetooth_link_table_acl_feed(usb_bluetooth_link_table_t *table,
                                      const uint8_t *packet, uint32_t length)
{
    if (table == NULL)
        return -22;
    usb_bluetooth_acl_view_t acl;
    if (!usb_bluetooth_acl_parse(packet, length, &acl))
        return -22;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_LINK_MAX; ++i) {
        if (table->links[i].active && table->links[i].handle == acl.handle)
            return usb_bluetooth_acl_reassembly_feed(
                &table->links[i].reassembly, packet, length);
    }
    return -9;
}

int usb_bluetooth_link_table_acl_take(usb_bluetooth_link_table_t *table,
                                      uint16_t handle, uint8_t *packet,
                                      uint32_t capacity)
{
    if (table == NULL || handle == 0U || packet == NULL || capacity == 0U)
        return -22;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_LINK_MAX; ++i) {
        if (table->links[i].active && table->links[i].handle == handle)
            return usb_bluetooth_acl_reassembly_take(
                &table->links[i].reassembly, packet, capacity);
    }
    return -9;
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

int usb_bluetooth_acl_queue_pop_handle(usb_bluetooth_acl_queue_t *queue,
                                       uint16_t handle, uint8_t *packet,
                                       uint32_t capacity)
{
    if (queue == NULL || handle == 0U || packet == NULL || capacity == 0U ||
        queue->head >= USB_BLUETOOTH_ACL_QUEUE_DEPTH ||
        queue->count > USB_BLUETOOTH_ACL_QUEUE_DEPTH)
        return -22;
    if (queue->count == 0U)
        return -11;

    uint32_t match = queue->count;
    for (uint32_t i = 0U; i < queue->count; ++i) {
        uint32_t index = (queue->head + i) % USB_BLUETOOTH_ACL_QUEUE_DEPTH;
        uint32_t length = queue->lengths[index];
        if (length < 4U || length > USB_BLUETOOTH_ACL_MAX_PACKET ||
            !usb_bluetooth_acl_packet_valid(queue->packets[index], length))
            return -22;
        uint16_t flags = (uint16_t)queue->packets[index][0] |
                         ((uint16_t)queue->packets[index][1] << 8);
        if ((flags & 0x0FFFU) == handle) {
            match = i;
            break;
        }
    }
    if (match == queue->count)
        return -11;

    uint32_t index = (queue->head + match) % USB_BLUETOOTH_ACL_QUEUE_DEPTH;
    uint32_t length = queue->lengths[index];
    if (length > capacity)
        return -90;
    for (uint32_t i = 0U; i < length; ++i)
        packet[i] = queue->packets[index][i];

    for (uint32_t i = match; i + 1U < queue->count; ++i) {
        uint32_t destination =
            (queue->head + i) % USB_BLUETOOTH_ACL_QUEUE_DEPTH;
        uint32_t source =
            (queue->head + i + 1U) % USB_BLUETOOTH_ACL_QUEUE_DEPTH;
        uint32_t source_length = queue->lengths[source];
        for (uint32_t j = 0U; j < source_length; ++j)
            queue->packets[destination][j] = queue->packets[source][j];
        queue->lengths[destination] = source_length;
        for (uint32_t j = 0U; j < USB_BLUETOOTH_ACL_MAX_PACKET; ++j)
            queue->packets[source][j] = 0U;
        queue->lengths[source] = 0U;
    }
    uint32_t tail = (queue->head + queue->count - 1U) %
                    USB_BLUETOOTH_ACL_QUEUE_DEPTH;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_ACL_MAX_PACKET; ++i)
        queue->packets[tail][i] = 0U;
    queue->lengths[tail] = 0U;
    --queue->count;
    return (int)length;
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
