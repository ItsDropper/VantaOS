/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "usb.h"

#include "../input/usb/hid.h"
#include "xhci.h"
#include "usb_device.h"
#include "rtl8188eu.h"
#include "usb_bluetooth.h"
#include "serial.h"
#include "timer.h"

#include <stdbool.h>
#include <stdint.h>

#define USB_MAX_XHCI_SLOTS 8U
#define USB_DESC_DEVICE 1U
#define USB_DESC_CONFIGURATION 2U
#define USB_DESC_INTERFACE 4U
#define USB_DESC_ENDPOINT 5U
#define USB_CLASS_HID 3U
#define USB_SUBCLASS_BOOT 1U
#define USB_PROTOCOL_KEYBOARD 1U
#define USB_PROTOCOL_MOUSE 2U
#define USB_TRANSFER_INTERRUPT 3U
#define USB_REQUEST_GET_DESCRIPTOR 6U
#define USB_REQUEST_SET_CONFIGURATION 9U
#define USB_HID_SET_PROTOCOL 11U
#define USB_MAX_CONFIGURATION_SIZE 512U
#define USB_BLUETOOTH_CONNECTION_TIMEOUT_TICKS 2000U
#define USB_BLUETOOTH_HCI_COMMAND_TIMEOUT_TICKS 200U
#define USB_BLUETOOTH_CANCEL_RETRY_TICKS 100U

typedef struct {
    uint8_t active;
    uint8_t port;
    uint8_t kind; /* 1 keyboard, 2 mouse */
    uint32_t hid_handle;
} usb_input_slot_t;

typedef struct {
    uint8_t found;
    uint8_t interface_number;
    uint8_t protocol;
    uint8_t endpoint_address;
    uint8_t interval;
    uint16_t max_packet;
} usb_hid_candidate_t;

static usb_input_slot_t input_slots[USB_MAX_XHCI_SLOTS + 1U];
static uint8_t descriptor_buffer[USB_MAX_CONFIGURATION_SIZE]
    __attribute__((aligned(64)));
static uint8_t configuration_buffer[USB_MAX_CONFIGURATION_SIZE]
    __attribute__((aligned(64)));
static uint8_t report_buffer[64] __attribute__((aligned(64)));
static int usb_initialized;

typedef struct {
    uint8_t active;
    uint8_t port;
    uint8_t interface_number;
    uint8_t event_endpoint;
    uint8_t acl_in_endpoint;
    uint8_t acl_out_endpoint;
    uint32_t event_count;
    uint32_t event_dropped;
    uint32_t acl_packet_count;
    uint8_t reset_complete_observed;
    uint8_t reset_failed_observed;
    uint16_t acl_packet_length;
    uint16_t acl_total_packets;
    uint16_t acl_packets_available;
    usb_bluetooth_connection_attempt_t connection_attempt;
    uint32_t connection_deadline;
    usb_bluetooth_inquiry_state_t inquiry_state;
    usb_bluetooth_hci_command_state_t command_state;
} usb_bluetooth_slot_t;

typedef struct {
    uint8_t active;
    uint8_t address[6];
    uint16_t handle;
} usb_bluetooth_peer_t;

static usb_bluetooth_slot_t bluetooth_slots[USB_MAX_XHCI_SLOTS + 1U];
static usb_bluetooth_peer_t bluetooth_peers[USB_MAX_XHCI_SLOTS + 1U][USB_BLUETOOTH_LINK_MAX];
static usb_bluetooth_discovery_table_t
    bluetooth_discovery_tables[USB_MAX_XHCI_SLOTS + 1U];
static uint8_t bluetooth_event_buffer[USB_BLUETOOTH_HCI_EVENT_MAX]
    __attribute__((aligned(64)));
static usb_bluetooth_hci_event_queue_t bluetooth_hci_event_queues[USB_MAX_XHCI_SLOTS + 1U];
static usb_bluetooth_link_table_t bluetooth_link_tables[USB_MAX_XHCI_SLOTS + 1U];
/* Keep L2CAP transactions isolated by the HCI connection handle. */
static uint16_t bluetooth_l2cap_owner_handles[USB_MAX_XHCI_SLOTS + 1U]
    [USB_BLUETOOTH_LINK_MAX];
static usb_bluetooth_l2cap_table_t
    bluetooth_l2cap_tables[USB_MAX_XHCI_SLOTS + 1U]
                          [USB_BLUETOOTH_LINK_MAX];
static usb_bluetooth_l2cap_data_queue_t
    bluetooth_l2cap_data_queues[USB_MAX_XHCI_SLOTS + 1U];
static uint32_t bluetooth_l2cap_data_dropped[USB_MAX_XHCI_SLOTS + 1U];

static usb_bluetooth_l2cap_table_t *bluetooth_l2cap_table_for_handle(
    uint32_t slot, uint16_t handle, int create)
{
    if (slot == 0U || slot > USB_MAX_XHCI_SLOTS ||
        handle == 0U || handle > 0x0EFFU)
        return NULL;
    uint32_t free_index = USB_BLUETOOTH_LINK_MAX;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_LINK_MAX; ++i) {
        if (bluetooth_l2cap_owner_handles[slot][i] == handle)
            return &bluetooth_l2cap_tables[slot][i];
        if (bluetooth_l2cap_owner_handles[slot][i] == 0U &&
            free_index == USB_BLUETOOTH_LINK_MAX)
            free_index = i;
    }
    if (!create || free_index == USB_BLUETOOTH_LINK_MAX)
        return NULL;
    bluetooth_l2cap_owner_handles[slot][free_index] = handle;
    usb_bluetooth_l2cap_table_reset(
        &bluetooth_l2cap_tables[slot][free_index]);
    return &bluetooth_l2cap_tables[slot][free_index];
}

static void bluetooth_l2cap_forget_handle(uint32_t slot, uint16_t handle)
{
    if (slot == 0U || slot > USB_MAX_XHCI_SLOTS || handle == 0U)
        return;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_LINK_MAX; ++i) {
        if (bluetooth_l2cap_owner_handles[slot][i] == handle) {
            usb_bluetooth_l2cap_table_reset(
                &bluetooth_l2cap_tables[slot][i]);
            bluetooth_l2cap_owner_handles[slot][i] = 0U;
            return;
        }
    }
}

static void bluetooth_l2cap_reset_all(uint32_t slot)
{
    if (slot == 0U || slot > USB_MAX_XHCI_SLOTS)
        return;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_LINK_MAX; ++i) {
        usb_bluetooth_l2cap_table_reset(
            &bluetooth_l2cap_tables[slot][i]);
        bluetooth_l2cap_owner_handles[slot][i] = 0U;
    }
}
static uint8_t bluetooth_acl_buffer[USB_BLUETOOTH_ACL_MAX_PACKET]
    __attribute__((aligned(64)));
static uint8_t bluetooth_acl_reassembled_buffer[USB_BLUETOOTH_ACL_MAX_PACKET]
    __attribute__((aligned(64)));
static usb_bluetooth_acl_queue_t bluetooth_acl_queues[USB_MAX_XHCI_SLOTS + 1U];
static usb_bluetooth_acl_queue_t bluetooth_reassembled_acl_queues[USB_MAX_XHCI_SLOTS + 1U];
static uint32_t bluetooth_acl_dropped[USB_MAX_XHCI_SLOTS + 1U];
static uint8_t bluetooth_acl_tx_buffers[USB_MAX_XHCI_SLOTS + 1U][4096]
    __attribute__((aligned(64)));
static uint8_t bluetooth_acl_tx_busy[USB_MAX_XHCI_SLOTS + 1U];
typedef struct __attribute__((aligned(64))) {
    uint8_t bytes[258];
} usb_bluetooth_dma_command_buffer_t;
static usb_bluetooth_dma_command_buffer_t
    bluetooth_hci_command_buffers[USB_MAX_XHCI_SLOTS + 1U];
static uint8_t usb_pending_slot_disable[USB_MAX_XHCI_SLOTS + 1U];
static uint8_t keyboard_input_observed;
static uint8_t mouse_input_observed;

static int usb_control(uint32_t slot, uint8_t request_type, uint8_t request,
                       uint16_t value, uint16_t index, void* data,
                       uint16_t length)
{
    uint8_t setup[8];
    setup[0] = request_type;
    setup[1] = request;
    setup[2] = (uint8_t)value;
    setup[3] = (uint8_t)(value >> 8);
    setup[4] = (uint8_t)index;
    setup[5] = (uint8_t)(index >> 8);
    setup[6] = (uint8_t)length;
    setup[7] = (uint8_t)(length >> 8);
    return xhci_control_transfer(slot, setup, data, length);
}

/*
 * Submit one complete HCI ACL packet through the configured bulk-OUT
 * endpoint. This synchronous path is bounded by the xHCI bulk buffer size.
 * Call from the USB polling/owner CPU, not an interrupt handler or another
 * CPU: device disconnect and xHCI slot teardown are serialized there.
 */
int usb_bluetooth_hci_command(uint32_t port, uint16_t opcode,
                              const uint8_t *parameters, uint8_t length)
{
    if (port == 0U || opcode == 0U || (length != 0U && parameters == 0))
        return 0;
    uint32_t slot = 0U;
    for (uint32_t i = 1U; i <= USB_MAX_XHCI_SLOTS; ++i) {
        if (bluetooth_slots[i].active && bluetooth_slots[i].port == port) {
            slot = i;
            break;
        }
    }
    if (slot == 0U)
        return 0;
    if (!usb_bluetooth_transport_ready(
            bluetooth_slots[slot].reset_complete_observed,
            bluetooth_slots[slot].reset_failed_observed) &&
        opcode != 0x0C03U)
        return 0;

    if (opcode == 0x0C03U) {
        bluetooth_slots[slot].reset_complete_observed = 0U;
        bluetooth_slots[slot].reset_failed_observed = 0U;
        bluetooth_slots[slot].acl_packet_length = 0U;
        bluetooth_slots[slot].acl_total_packets = 0U;
        bluetooth_slots[slot].acl_packets_available = 0U;
        usb_bluetooth_connection_attempt_reset(
            &bluetooth_slots[slot].connection_attempt);
        usb_bluetooth_inquiry_state_reset(
            &bluetooth_slots[slot].inquiry_state);
        usb_bluetooth_hci_command_state_reset(
            &bluetooth_slots[slot].command_state);
        bluetooth_slots[slot].command_state.credits = 1U;
        usb_bluetooth_acl_queue_reset(&bluetooth_acl_queues[slot]);
        usb_bluetooth_acl_queue_reset(&bluetooth_reassembled_acl_queues[slot]);
        usb_bluetooth_hci_event_queue_reset(&bluetooth_hci_event_queues[slot]);
        usb_bluetooth_link_table_reset(&bluetooth_link_tables[slot]);
        bluetooth_l2cap_reset_all(slot);
    usb_bluetooth_l2cap_data_queue_reset(&bluetooth_l2cap_data_queues[slot]);
    bluetooth_l2cap_data_dropped[slot] = 0U;
    }
    uint8_t *command_buffer = bluetooth_hci_command_buffers[slot].bytes;
    int encoded = usb_bluetooth_hci_command_encode(
        opcode, parameters, length, command_buffer, 258U);
    if (encoded < 0)
        return 0;
    if (usb_bluetooth_hci_command_begin(
            &bluetooth_slots[slot].command_state, opcode) != 0) {
        for (uint32_t i = 0U; i < (uint32_t)encoded; ++i)
            command_buffer[i] = 0U;
        return 0;
    }

    /*
     * USB Bluetooth HCI commands belong in the control-transfer data stage.
     * wValue is zero and wIndex is the Bluetooth interface number. The
     * command buffer remains valid until the synchronous xHCI transfer ends.
     */
    int sent = usb_control(slot, 0x20U, 0U, 0U,
                           bluetooth_slots[slot].interface_number,
                           command_buffer, (uint16_t)encoded);
    for (uint32_t i = 0U; i < (uint32_t)encoded; ++i)
        command_buffer[i] = 0U;
    if (sent && usb_bluetooth_hci_command_watchdog_arm(
                    &bluetooth_slots[slot].command_state,
                    timer_get_ticks(), USB_BLUETOOTH_HCI_COMMAND_TIMEOUT_TICKS) != 0) {
        usb_bluetooth_hci_command_state_reset(
            &bluetooth_slots[slot].command_state);
        bluetooth_slots[slot].command_state.faulted = 1U;
        sent = 0;
    }
    if (!sent) {
        /*
         * A failed control transfer may have reached the controller even
         * though the host did not observe successful completion. Do not
         * guess the controller's credits or allow another ordinary command;
         * only a fresh HCI Reset can re-synchronize this transport.
         */
        usb_bluetooth_hci_command_state_reset(
            &bluetooth_slots[slot].command_state);
        bluetooth_slots[slot].command_state.faulted = 1U;
    }
    return sent;
}

uint32_t usb_bluetooth_adapter_count(void)
{
    uint32_t count = 0U;
    for (uint32_t slot = 1U; slot <= USB_MAX_XHCI_SLOTS; ++slot)
        if (bluetooth_slots[slot].active)
            count++;
    return count;
}

int usb_bluetooth_adapter_port(uint32_t index, uint32_t *port)
{
    if (port == NULL)
        return -22;
    for (uint32_t slot = 1U; slot <= USB_MAX_XHCI_SLOTS; ++slot) {
        if (!bluetooth_slots[slot].active)
            continue;
        if (index == 0U) {
            *port = bluetooth_slots[slot].port;
            return 1;
        }
        index--;
    }
    *port = 0U;
    return 0;
}

static void bluetooth_peer_record(uint32_t slot, const uint8_t address[6],
                                  uint16_t handle)
{
    if (slot == 0U || slot > USB_MAX_XHCI_SLOTS || address == NULL ||
        handle == 0U)
        return;
    uint32_t free_index = USB_BLUETOOTH_LINK_MAX;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_LINK_MAX; ++i) {
        usb_bluetooth_peer_t *peer = &bluetooth_peers[slot][i];
        if (peer->active && peer->handle == handle) {
            free_index = i;
            break;
        }
        if (!peer->active && free_index == USB_BLUETOOTH_LINK_MAX)
            free_index = i;
    }
    if (free_index == USB_BLUETOOTH_LINK_MAX)
        return;
    usb_bluetooth_peer_t *peer = &bluetooth_peers[slot][free_index];
    peer->active = 1U;
    peer->handle = handle;
    for (uint32_t i = 0U; i < 6U; ++i)
        peer->address[i] = address[i];
}

static void bluetooth_peer_forget_handle(uint32_t slot, uint16_t handle)
{
    if (slot == 0U || slot > USB_MAX_XHCI_SLOTS || handle == 0U)
        return;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_LINK_MAX; ++i) {
        usb_bluetooth_peer_t *peer = &bluetooth_peers[slot][i];
        if (!peer->active || peer->handle != handle)
            continue;
        peer->active = 0U;
        peer->handle = 0U;
        for (uint32_t j = 0U; j < 6U; ++j)
            peer->address[j] = 0U;
        return;
    }
}

int usb_bluetooth_peer_state(uint32_t port, const uint8_t address[6],
                             uint16_t *handle)
{
    if (port == 0U || address == NULL || handle == NULL)
        return -22;
    *handle = 0U;
    for (uint32_t slot = 1U; slot <= USB_MAX_XHCI_SLOTS; ++slot) {
        if (!bluetooth_slots[slot].active || bluetooth_slots[slot].port != port)
            continue;
        for (uint32_t i = 0U; i < USB_BLUETOOTH_LINK_MAX; ++i) {
            const usb_bluetooth_peer_t *peer = &bluetooth_peers[slot][i];
            if (!peer->active)
                continue;
            uint32_t j = 0U;
            while (j < 6U && peer->address[j] == address[j])
                ++j;
            if (j == 6U) {
                *handle = peer->handle;
                return 1;
            }
        }
        return usb_bluetooth_connection_attempt_matches(
            &bluetooth_slots[slot].connection_attempt, address) ? 2 : 0;
    }
    return -19;
}

uint32_t usb_bluetooth_discovered_count(uint32_t port)
{
    if (port == 0U)
        return 0U;
    for (uint32_t slot = 1U; slot <= USB_MAX_XHCI_SLOTS; ++slot) {
        if (bluetooth_slots[slot].active && bluetooth_slots[slot].port == port)
            return bluetooth_discovery_tables[slot].count;
    }
    return 0U;
}

int usb_bluetooth_discovered_get(uint32_t port, uint32_t index,
                                 usb_bluetooth_inquiry_result_t *result)
{
    if (port == 0U || result == NULL)
        return -22;
    for (uint32_t slot = 1U; slot <= USB_MAX_XHCI_SLOTS; ++slot) {
        if (bluetooth_slots[slot].active && bluetooth_slots[slot].port == port)
            return usb_bluetooth_discovery_get(
                &bluetooth_discovery_tables[slot], index, result);
    }
    return -19;
}

int usb_bluetooth_inquiry(uint32_t port, uint8_t inquiry_length,
                          uint8_t responses)
{
    uint8_t command[8];
    int encoded = usb_bluetooth_hci_build_inquiry(
        command, sizeof(command), inquiry_length, responses);
    if (port == 0U || encoded != 8)
        return 0;
    uint32_t slot = 0U;
    for (uint32_t i = 1U; i <= USB_MAX_XHCI_SLOTS; ++i) {
        if (bluetooth_slots[i].active && bluetooth_slots[i].port == port) {
            slot = i;
            break;
        }
    }
    if (slot == 0U ||
        usb_bluetooth_inquiry_state_begin(
            &bluetooth_slots[slot].inquiry_state) != 0) {
        for (uint32_t i = 0U; i < sizeof(command); ++i)
            command[i] = 0U;
        return 0;
    }
    usb_bluetooth_discovery_reset(&bluetooth_discovery_tables[slot]);
    int sent = usb_bluetooth_hci_command(port, 0x0401U, &command[3], 5U);
    if (!sent)
        usb_bluetooth_inquiry_state_command_rejected(
            &bluetooth_slots[slot].inquiry_state);
    for (uint32_t i = 0U; i < sizeof(command); ++i)
        command[i] = 0U;
    return sent;
}

int usb_bluetooth_inquiry_cancel(uint32_t port)
{
    if (port == 0U)
        return 0;
    uint32_t slot = 0U;
    for (uint32_t i = 1U; i <= USB_MAX_XHCI_SLOTS; ++i) {
        if (bluetooth_slots[i].active && bluetooth_slots[i].port == port) {
            slot = i;
            break;
        }
    }
    if (slot == 0U ||
        usb_bluetooth_inquiry_state_cancel_begin(
            &bluetooth_slots[slot].inquiry_state) != 0)
        return 0;
    int sent = usb_bluetooth_hci_command(port, 0x0402U, NULL, 0U);
    if (!sent)
        (void)usb_bluetooth_inquiry_state_cancel_result(
            &bluetooth_slots[slot].inquiry_state, 0);
    /* A successful USB transfer is not the controller's Command Complete. */
    return sent;
}

int usb_bluetooth_connect(uint32_t port, const uint8_t address[6],
                          uint16_t packet_type,
                          uint8_t page_scan_repetition_mode,
                          uint16_t clock_offset, uint8_t allow_role_switch)
{
    uint8_t command[16];
    int encoded = usb_bluetooth_hci_build_create_connection(
        address, packet_type, page_scan_repetition_mode, clock_offset,
        allow_role_switch, command, sizeof(command));
    if (port == 0U || encoded != 16)
        return 0;
    uint32_t slot = 0U;
    for (uint32_t i = 1U; i <= USB_MAX_XHCI_SLOTS; ++i) {
        if (bluetooth_slots[i].active && bluetooth_slots[i].port == port) {
            slot = i;
            break;
        }
    }
    if (slot == 0U ||
        usb_bluetooth_connection_attempt_begin(
            &bluetooth_slots[slot].connection_attempt, address) != 0) {
        for (uint32_t i = 0U; i < sizeof(command); ++i)
            command[i] = 0U;
        return 0;
    }
    int sent = usb_bluetooth_hci_command(port, 0x0405U, &command[3], 13U);
    if (!sent) {
        usb_bluetooth_connection_attempt_reset(
            &bluetooth_slots[slot].connection_attempt);
        bluetooth_slots[slot].connection_deadline = 0U;
    } else {
        bluetooth_slots[slot].connection_deadline =
            timer_get_ticks() + USB_BLUETOOTH_CONNECTION_TIMEOUT_TICKS;
    }
    for (uint32_t i = 0U; i < sizeof(command); ++i)
        command[i] = 0U;
    return sent;
}

int usb_bluetooth_cancel_connection(uint32_t port,
                                    const uint8_t address[6])
{
    uint8_t command[9];
    int encoded = usb_bluetooth_hci_build_create_connection_cancel(
        address, command, sizeof(command));
    if (port == 0U || encoded != 9)
        return 0;
    uint32_t slot = 0U;
    for (uint32_t i = 1U; i <= USB_MAX_XHCI_SLOTS; ++i) {
        if (bluetooth_slots[i].active && bluetooth_slots[i].port == port) {
            slot = i;
            break;
        }
    }
    if (slot == 0U ||
        !usb_bluetooth_connection_attempt_matches(
            &bluetooth_slots[slot].connection_attempt, address)) {
        for (uint32_t i = 0U; i < sizeof(command); ++i)
            command[i] = 0U;
        return 0;
    }
    int sent = usb_bluetooth_hci_command(port, 0x0408U, &command[3], 6U);
    if (sent)
        bluetooth_slots[slot].connection_deadline = timer_get_ticks() +
            USB_BLUETOOTH_CANCEL_RETRY_TICKS;
    for (uint32_t i = 0U; i < sizeof(command); ++i)
        command[i] = 0U;
    return sent;
}

int usb_bluetooth_connection_pending(uint32_t port,
                                     const uint8_t address[6])
{
    if (port == 0U || address == NULL)
        return -22;
    for (uint32_t slot = 1U; slot <= USB_MAX_XHCI_SLOTS; ++slot) {
        if (bluetooth_slots[slot].active && bluetooth_slots[slot].port == port)
            return usb_bluetooth_connection_attempt_matches(
                &bluetooth_slots[slot].connection_attempt, address);
    }
    return -19;
}

int usb_bluetooth_is_connected(uint32_t port, uint16_t handle)
{
    if (port == 0U || handle == 0U || handle > 0x0EFFU)
        return -22;
    for (uint32_t slot = 1U; slot <= USB_MAX_XHCI_SLOTS; ++slot) {
        if (bluetooth_slots[slot].active && bluetooth_slots[slot].port == port)
            return usb_bluetooth_link_table_has_handle(
                &bluetooth_link_tables[slot], handle);
    }
    return -19;
}

int usb_bluetooth_disconnect(uint32_t port, uint16_t handle, uint8_t reason)
{
    uint8_t command[6];
    int encoded = usb_bluetooth_hci_build_disconnect(
        handle, reason, command, sizeof(command));
    if (port == 0U || encoded != 6)
        return 0;
    uint32_t slot = 0U;
    for (uint32_t i = 1U; i <= USB_MAX_XHCI_SLOTS; ++i) {
        if (bluetooth_slots[i].active && bluetooth_slots[i].port == port) {
            slot = i;
            break;
        }
    }
    if (slot == 0U ||
        !usb_bluetooth_link_table_has_handle(&bluetooth_link_tables[slot],
                                             handle)) {
        for (uint32_t i = 0U; i < sizeof(command); ++i)
            command[i] = 0U;
        return 0;
    }
    int sent = usb_bluetooth_hci_command(port, 0x0406U, &command[3], 3U);
    for (uint32_t i = 0U; i < sizeof(command); ++i)
        command[i] = 0U;
    return sent;
}

int usb_bluetooth_l2cap_connect(uint32_t port, uint16_t handle,
                                uint16_t psm, uint16_t local_mtu,
                                uint32_t deadline, uint16_t *local_cid,
                                uint8_t *identifier)
{
    if (local_cid == NULL || identifier == NULL)
        return 0;
    *local_cid = 0U;
    *identifier = 0U;
    if (port == 0U || handle == 0U || handle > 0x0EFFU)
        return 0;
    if (deadline != 0U &&
        (int32_t)(deadline - timer_get_ticks()) <= 0)
        return 0;

    uint32_t slot = 0U;
    for (uint32_t i = 1U; i <= USB_MAX_XHCI_SLOTS; ++i) {
        if (bluetooth_slots[i].active && bluetooth_slots[i].port == port) {
            slot = i;
            break;
        }
    }
    if (slot == 0U ||
        !usb_bluetooth_transport_ready(
            bluetooth_slots[slot].reset_complete_observed,
            bluetooth_slots[slot].reset_failed_observed) ||
        !usb_bluetooth_link_table_has_handle(&bluetooth_link_tables[slot],
                                             handle))
        return 0;

    usb_bluetooth_l2cap_table_t *table =
        bluetooth_l2cap_table_for_handle(slot, handle, 0);
    if (table == NULL)
        return 0;

    uint16_t pending_cid = 0U;
    uint8_t pending_identifier = 0U;
    if (usb_bluetooth_l2cap_channel_begin(
            table, psm, local_mtu, deadline, &pending_cid,
            &pending_identifier) != 0)
        return 0;

    uint8_t command[8];
    uint8_t packet[32];
    int command_length = usb_bluetooth_l2cap_build_connection_request(
        pending_identifier, psm, pending_cid, command, sizeof(command));
    int packet_length = command_length == (int)sizeof(command)
        ? usb_bluetooth_l2cap_build_signaling_acl_packet(
              handle, command, (uint32_t)command_length,
              packet, sizeof(packet))
        : -22;
    int sent = packet_length > 0 &&
               usb_bluetooth_acl_send(port, packet, (uint32_t)packet_length);
    for (uint32_t i = 0U; i < sizeof(command); ++i)
        command[i] = 0U;
    for (uint32_t i = 0U; i < sizeof(packet); ++i)
        packet[i] = 0U;
    if (!sent) {
        (void)usb_bluetooth_l2cap_channel_abort(table, pending_cid);
        return 0;
    }
    *local_cid = pending_cid;
    *identifier = pending_identifier;
    return 1;
}

int usb_bluetooth_l2cap_channel_status(
    uint32_t port, uint16_t handle, uint16_t local_cid, uint8_t *state,
    uint16_t *remote_cid, uint16_t *local_mtu, uint16_t *remote_mtu)
{
    if (port == 0U || handle == 0U || handle > 0x0EFFU ||
        local_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN || state == NULL)
        return 0;
    uint32_t slot = 0U;
    for (uint32_t i = 1U; i <= USB_MAX_XHCI_SLOTS; ++i) {
        if (bluetooth_slots[i].active && bluetooth_slots[i].port == port) {
            slot = i;
            break;
        }
    }
    if (slot == 0U ||
        !usb_bluetooth_link_table_has_handle(&bluetooth_link_tables[slot],
                                             handle))
        return 0;
    usb_bluetooth_l2cap_table_t *table =
        bluetooth_l2cap_table_for_handle(slot, handle, 0);
    if (table == NULL)
        return 0;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i) {
        const usb_bluetooth_l2cap_channel_t *channel = &table->channels[i];
        if (channel->active && channel->local_cid == local_cid) {
            *state = channel->state;
            if (remote_cid != NULL) *remote_cid = channel->remote_cid;
            if (local_mtu != NULL) *local_mtu = channel->local_mtu;
            if (remote_mtu != NULL) *remote_mtu = channel->remote_mtu;
            return 1;
        }
    }
    return 0;
}

int usb_bluetooth_l2cap_send(uint32_t port, uint16_t handle,
                             uint16_t local_cid, const uint8_t *payload,
                             uint32_t length)
{
    if (port == 0U || handle == 0U || handle > 0x0EFFU ||
        local_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN ||
        payload == NULL || length == 0U ||
        length > USB_BLUETOOTH_L2CAP_DATA_MAX)
        return 0;
    uint32_t slot = 0U;
    for (uint32_t i = 1U; i <= USB_MAX_XHCI_SLOTS; ++i) {
        if (bluetooth_slots[i].active && bluetooth_slots[i].port == port) {
            slot = i;
            break;
        }
    }
    if (slot == 0U ||
        !usb_bluetooth_transport_ready(
            bluetooth_slots[slot].reset_complete_observed,
            bluetooth_slots[slot].reset_failed_observed) ||
        !usb_bluetooth_link_table_has_handle(&bluetooth_link_tables[slot],
                                             handle))
        return 0;
    usb_bluetooth_l2cap_table_t *table =
        bluetooth_l2cap_table_for_handle(slot, handle, 0);
    if (table == NULL)
        return 0;
    const usb_bluetooth_l2cap_channel_t *channel = NULL;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i) {
        if (table->channels[i].active &&
            table->channels[i].local_cid == local_cid) {
            channel = &table->channels[i];
            break;
        }
    }
    if (channel == NULL || channel->state != 4U ||
        channel->remote_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN ||
        length > channel->remote_mtu)
        return 0;

    uint8_t packet[USB_BLUETOOTH_L2CAP_DATA_MAX + 8U];
    int packet_length = usb_bluetooth_l2cap_build_acl_packet(
        handle, channel->remote_cid, payload, length,
        packet, sizeof(packet));
    int sent = packet_length > 0 &&
               usb_bluetooth_acl_send(port, packet, (uint32_t)packet_length);
    for (uint32_t i = 0U; i < sizeof(packet); ++i)
        packet[i] = 0U;
    return sent;
}

int usb_bluetooth_l2cap_disconnect(uint32_t port, uint16_t handle,
                                  uint16_t local_cid)
{
    if (port == 0U || handle == 0U || handle > 0x0EFFU ||
        local_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN)
        return 0;
    uint32_t slot = 0U;
    for (uint32_t i = 1U; i <= USB_MAX_XHCI_SLOTS; ++i) {
        if (bluetooth_slots[i].active && bluetooth_slots[i].port == port) {
            slot = i;
            break;
        }
    }
    if (slot == 0U ||
        !usb_bluetooth_transport_ready(
            bluetooth_slots[slot].reset_complete_observed,
            bluetooth_slots[slot].reset_failed_observed) ||
        !usb_bluetooth_link_table_has_handle(&bluetooth_link_tables[slot],
                                             handle))
        return 0;
    usb_bluetooth_l2cap_table_t *table =
        bluetooth_l2cap_table_for_handle(slot, handle, 0);
    if (table == NULL)
        return 0;
    usb_bluetooth_l2cap_channel_t *channel = NULL;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i) {
        if (table->channels[i].active &&
            table->channels[i].local_cid == local_cid) {
            channel = &table->channels[i];
            break;
        }
    }
    if (channel == NULL || channel->state != 4U ||
        channel->remote_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN)
        return 0;

    uint8_t identifier = table->next_identifier;
    int found = 0;
    for (uint32_t attempts = 0U; attempts < 255U; ++attempts) {
        if (identifier == 0U)
            identifier = 1U;
        int used = 0;
        for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i) {
            const usb_bluetooth_l2cap_channel_t *candidate =
                &table->channels[i];
            if (candidate->active &&
                (candidate->identifier == identifier ||
                 candidate->config_identifier == identifier ||
                 candidate->disconnect_identifier == identifier)) {
                used = 1;
                break;
            }
        }
        if (!used) {
            found = 1;
            break;
        }
        identifier = (uint8_t)(identifier + 1U);
    }
    if (!found || usb_bluetooth_l2cap_channel_disconnect_begin(
                      table, local_cid, identifier) != 1)
        return 0;

    uint16_t remote_cid = channel->remote_cid;
    uint8_t command[8];
    uint8_t packet[32];
    int command_length = usb_bluetooth_l2cap_build_disconnection_request(
        identifier, remote_cid, local_cid, command, sizeof(command));
    int packet_length = command_length == (int)sizeof(command)
        ? usb_bluetooth_l2cap_build_signaling_acl_packet(
              handle, command, (uint32_t)command_length,
              packet, sizeof(packet))
        : -22;
    int sent = packet_length > 0 &&
               usb_bluetooth_acl_send(port, packet, (uint32_t)packet_length);
    for (uint32_t i = 0U; i < sizeof(command); ++i)
        command[i] = 0U;
    for (uint32_t i = 0U; i < sizeof(packet); ++i)
        packet[i] = 0U;
    if (!sent) {
        (void)usb_bluetooth_l2cap_channel_disconnect_failed(
            table, local_cid, identifier);
        return 0;
    }
    table->next_identifier = (uint8_t)(identifier + 1U);
    if (table->next_identifier == 0U)
        table->next_identifier = 1U;
    usb_bluetooth_l2cap_data_queue_forget_channel(
        &bluetooth_l2cap_data_queues[slot], handle, local_cid);
    return 1;
}

int usb_bluetooth_l2cap_receive(uint32_t port, uint16_t handle,
                                uint16_t local_cid, uint8_t *payload,
                                uint32_t capacity)
{
    if (port == 0U || handle == 0U || handle > 0x0EFFU ||
        local_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN ||
        payload == NULL || capacity == 0U)
        return -22;
    for (uint32_t slot = 1U; slot <= USB_MAX_XHCI_SLOTS; ++slot) {
        if (bluetooth_slots[slot].active &&
            bluetooth_slots[slot].port == port) {
            if (!usb_bluetooth_link_table_has_handle(
                    &bluetooth_link_tables[slot], handle))
                return -9;
            return usb_bluetooth_l2cap_data_queue_pop(
                &bluetooth_l2cap_data_queues[slot], handle, local_cid,
                payload, capacity);
        }
    }
    return -9;
}

int usb_bluetooth_hci_event_receive(uint32_t port, uint8_t *event,
                                    uint32_t capacity)
{
    if (port == 0U || event == 0 || capacity == 0U)
        return -22;
    for (uint32_t slot = 1U; slot <= USB_MAX_XHCI_SLOTS; ++slot) {
        if (bluetooth_slots[slot].active && bluetooth_slots[slot].port == port)
            return usb_bluetooth_hci_event_queue_pop(
                &bluetooth_hci_event_queues[slot], event, capacity);
    }
    return -9;
}

int usb_bluetooth_reassembled_acl_receive(uint32_t port, uint16_t handle,
                                          uint8_t *packet, uint32_t capacity)
{
    if (port == 0U || handle == 0U || packet == 0 || capacity == 0U)
        return -22;
    for (uint32_t slot = 1U; slot <= USB_MAX_XHCI_SLOTS; ++slot) {
        if (bluetooth_slots[slot].active && bluetooth_slots[slot].port == port)
            return usb_bluetooth_acl_queue_pop_handle(
                &bluetooth_reassembled_acl_queues[slot], handle, packet,
                capacity);
    }
    return -9;
}

int usb_bluetooth_acl_send(uint32_t port, const uint8_t *packet,
                           uint32_t length)
{
    if (port == 0U || packet == NULL || length < 5U ||
        length > USB_BLUETOOTH_ACL_MAX_PACKET ||
        !usb_bluetooth_acl_packet_valid(packet, length))
        return 0;

    uint32_t slot = 0U;
    for (uint32_t i = 1U; i <= USB_MAX_XHCI_SLOTS; ++i) {
        if (bluetooth_slots[i].active && bluetooth_slots[i].port == port) {
            slot = i;
            break;
        }
    }
    if (slot == 0U || bluetooth_slots[slot].acl_out_endpoint == 0U ||
        bluetooth_slots[slot].acl_packet_length == 0U ||
        !usb_bluetooth_transport_ready(
            bluetooth_slots[slot].reset_complete_observed,
            bluetooth_slots[slot].reset_failed_observed))
        return 0;

    if (__atomic_test_and_set(&bluetooth_acl_tx_busy[slot], __ATOMIC_ACQUIRE))
        return 0;

    int success = 0;
    if (bluetooth_slots[slot].active &&
        bluetooth_slots[slot].port == port) {
        uint16_t flags = (uint16_t)packet[0] |
                         ((uint16_t)packet[1] << 8);
        uint16_t handle = flags & 0x0FFFU;
        uint32_t acl_payload_length = length - 4U;
        uint32_t fragment_limit = bluetooth_slots[slot].acl_packet_length;
        uint32_t buffer_limit = sizeof(bluetooth_acl_tx_buffers[slot]) - 4U;
        if (fragment_limit > buffer_limit)
            fragment_limit = buffer_limit;

        uint32_t required_fragments = fragment_limit == 0U ? 0U :
            (acl_payload_length + fragment_limit - 1U) / fragment_limit;
        success = handle != 0U && fragment_limit != 0U &&
                  required_fragments != 0U &&
                  required_fragments <= bluetooth_slots[slot].acl_packets_available;
        uint32_t offset = 0U;
        uint32_t fragments = 0U;
        while (success && offset < acl_payload_length) {
            int transfer_length = usb_bluetooth_acl_fragment_build(
                packet, length, (uint16_t)fragment_limit, offset,
                bluetooth_acl_tx_buffers[slot],
                sizeof(bluetooth_acl_tx_buffers[slot]));
            if (transfer_length < 0) {
                success = 0;
                break;
            }
            uint32_t actual_length = 0U;
            /* Reserve conservatively before USB submission: a failed transfer
             * can have an ambiguous controller-side outcome. */
            bluetooth_slots[slot].acl_packets_available--;
            success = xhci_bulk_transfer(
                          slot, bluetooth_slots[slot].acl_out_endpoint,
                          bluetooth_acl_tx_buffers[slot],
                          (uint32_t)transfer_length, &actual_length) &&
                      actual_length == (uint32_t)transfer_length;
            if (!success)
                break;
            offset += (uint32_t)transfer_length - 4U;
            fragments++;
        }
        success = success && fragments != 0U &&
                  offset == acl_payload_length;
        for (uint32_t i = 0U;
             i < sizeof(bluetooth_acl_tx_buffers[slot]); ++i)
            bluetooth_acl_tx_buffers[slot][i] = 0U;
    }

    __atomic_clear(&bluetooth_acl_tx_busy[slot], __ATOMIC_RELEASE);
    return success;
}

int usb_bluetooth_acl_receive(uint32_t port, uint8_t *packet,
                              uint32_t capacity)
{
    if (port == 0U || packet == 0 || capacity == 0U)
        return -22;
    for (uint32_t slot = 1U; slot <= USB_MAX_XHCI_SLOTS; ++slot) {
        if (bluetooth_slots[slot].active && bluetooth_slots[slot].port == port) {
            if (!usb_bluetooth_transport_ready(
                    bluetooth_slots[slot].reset_complete_observed,
                    bluetooth_slots[slot].reset_failed_observed))
                return -11;
            return usb_bluetooth_acl_queue_pop(&bluetooth_acl_queues[slot],
                                               packet, capacity);
        }
    }
    return -9;
}

static void bluetooth_l2cap_handle_signaling_requests(
    uint32_t slot, uint16_t handle, const uint8_t *packet, uint32_t length)
{
    if (slot == 0U || slot > USB_MAX_XHCI_SLOTS || packet == NULL)
        return;
    usb_bluetooth_l2cap_table_t *table =
        bluetooth_l2cap_table_for_handle(slot, handle, 0);
    if (table == NULL)
        return;

    usb_bluetooth_l2cap_view_t l2cap;
    if (!usb_bluetooth_l2cap_parse(packet, length, &l2cap) ||
        l2cap.channel_id != 0x0001U)
        return;

    uint32_t offset = 0U;
    while (offset < l2cap.payload_length) {
        uint32_t command_start = offset;
        usb_bluetooth_l2cap_signal_command_t command;
        int next = usb_bluetooth_l2cap_signaling_next(
            l2cap.payload, l2cap.payload_length, &offset, &command);
        if (next <= 0)
            return;
        if (command.code != 0x02U && command.code != 0x04U &&
            command.code != 0x06U)
            continue;

        uint32_t command_length = offset - command_start;
        uint8_t response[16];
        uint8_t response_acl[32];
        uint16_t local_cid = 0U;
        int response_length;
        if (command.code == 0x02U) {
            /* No inbound services are registered yet: reject explicitly. */
            response_length = usb_bluetooth_l2cap_reject_connection_request(
                l2cap.payload + command_start, command_length,
                response, sizeof(response));
        } else if (command.code == 0x04U) {
            response_length = usb_bluetooth_l2cap_configuration_request(
                table, l2cap.payload + command_start, command_length,
                response, sizeof(response), &local_cid);
        } else {
            response_length = usb_bluetooth_l2cap_disconnection_request(
                table, l2cap.payload + command_start, command_length,
                response, sizeof(response), &local_cid);
        }
        if (response_length <= 0)
            continue;
        int acl_length = usb_bluetooth_l2cap_build_signaling_acl_packet(
            handle, response, (uint32_t)response_length,
            response_acl, sizeof(response_acl));
        int sent = acl_length > 0 &&
                   usb_bluetooth_acl_send(
                       bluetooth_slots[slot].port, response_acl,
                       (uint32_t)acl_length);
        if (sent && command.code == 0x04U && response_length >= 10 &&
            response[8] == 0U && response[9] == 0U &&
            local_cid >= USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN)
            (void)usb_bluetooth_l2cap_channel_remote_configured(
                table, local_cid);
        else if (sent && command.code == 0x06U &&
                 local_cid >= USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN) {
            (void)usb_bluetooth_l2cap_channel_close(table, local_cid);
            usb_bluetooth_l2cap_data_queue_forget_channel(
                &bluetooth_l2cap_data_queues[slot], handle, local_cid);
        }
        for (uint32_t i = 0U; i < sizeof(response); ++i)
            response[i] = 0U;
        for (uint32_t i = 0U; i < sizeof(response_acl); ++i)
            response_acl[i] = 0U;
    }
}

static void bluetooth_l2cap_queue_data(
    uint32_t slot, uint16_t handle, const uint8_t *packet, uint32_t length)
{
    if (slot == 0U || slot > USB_MAX_XHCI_SLOTS || packet == NULL)
        return;
    usb_bluetooth_l2cap_view_t l2cap;
    if (!usb_bluetooth_l2cap_parse(packet, length, &l2cap) ||
        l2cap.channel_id < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN ||
        l2cap.payload_length == 0U ||
        l2cap.payload_length > USB_BLUETOOTH_L2CAP_DATA_MAX)
        return;
    usb_bluetooth_l2cap_table_t *table =
        bluetooth_l2cap_table_for_handle(slot, handle, 0);
    if (table == NULL)
        return;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++i) {
        const usb_bluetooth_l2cap_channel_t *channel = &table->channels[i];
        if (!channel->active || channel->state != 4U ||
            channel->local_cid != l2cap.channel_id)
            continue;
        if (l2cap.payload_length > channel->local_mtu)
            return;
        int queued = usb_bluetooth_l2cap_data_queue_push(
            &bluetooth_l2cap_data_queues[slot], handle, channel->local_cid,
            l2cap.payload, l2cap.payload_length);
        if (queued == -11 && ++bluetooth_l2cap_data_dropped[slot] == 1U)
            serial_write("VANTAOS_BLUETOOTH_L2CAP_DATA_QUEUE_FULL\\n");
        return;
    }
}

static void bluetooth_l2cap_progress_channels(uint32_t slot)
{
    if (slot == 0U || slot > USB_MAX_XHCI_SLOTS ||
        !bluetooth_slots[slot].active)
        return;
    uint32_t port = bluetooth_slots[slot].port;
    for (uint32_t i = 0U; i < USB_BLUETOOTH_LINK_MAX; ++i) {
        uint16_t handle = bluetooth_l2cap_owner_handles[slot][i];
        usb_bluetooth_l2cap_table_t *table =
            handle == 0U ? NULL :
            &bluetooth_l2cap_tables[slot][i];
        if (table == NULL)
            continue;
        uint32_t now = timer_get_ticks();
        for (uint32_t j = 0U; j < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++j) {
            const usb_bluetooth_l2cap_channel_t *channel =
                &table->channels[j];
            if (channel->active && channel->deadline != 0U &&
                (channel->state == 1U || channel->state == 3U ||
                 channel->state == 5U) &&
                (int32_t)(now - channel->deadline) >= 0)
                usb_bluetooth_l2cap_data_queue_forget_channel(
                    &bluetooth_l2cap_data_queues[slot], handle,
                    channel->local_cid);
        }
        (void)usb_bluetooth_l2cap_expire(table, now);
        for (uint32_t j = 0U; j < USB_BLUETOOTH_L2CAP_CHANNEL_MAX; ++j) {
            usb_bluetooth_l2cap_channel_t *channel = &table->channels[j];
            if (!channel->active || channel->state != 3U ||
                channel->local_configured != 0U ||
                channel->config_identifier != 0U ||
                channel->remote_cid < USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN)
                continue;

            uint16_t local_cid = channel->local_cid;
            uint8_t command[16];
            uint8_t packet[32];
            int command_length =
                usb_bluetooth_l2cap_configuration_request_begin(
                    table, local_cid, command, sizeof(command));
            if (command_length <= 0)
                continue;
            uint8_t identifier = command[1];
            int packet_length = usb_bluetooth_l2cap_build_signaling_acl_packet(
                handle, command, (uint32_t)command_length,
                packet, sizeof(packet));
            int sent = packet_length > 0 &&
                       usb_bluetooth_acl_send(
                           port, packet, (uint32_t)packet_length);
            if (!sent)
                (void)usb_bluetooth_l2cap_configuration_request_failed(
                    table, local_cid, identifier);
            for (uint32_t k = 0U; k < sizeof(command); ++k)
                command[k] = 0U;
            for (uint32_t k = 0U; k < sizeof(packet); ++k)
                packet[k] = 0U;
        }
    }
}

static uint16_t usb_initial_ep0_packet(uint32_t speed)
{
    if (speed == 3U)
        return 64U;
    if (speed >= 4U)
        return 512U;
    return 8U;
}

static uint16_t usb_descriptor_ep0_packet(uint32_t speed, uint8_t value)
{
    if (speed >= 4U) {
        if (value != 9U)
            return 0;
        return 512U;
    }
    if (value == 8U || value == 16U || value == 32U || value == 64U)
        return value;
    return 0;
}

static int usb_find_boot_interface(const uint8_t* configuration,
                                   uint16_t length,
                                   usb_hid_candidate_t* candidate)
{
    uint32_t offset = 0;
    uint8_t current_interface = 0;
    uint8_t current_protocol = 0;
    bool current_boot_hid = false;

    candidate->found = 0;
    while (offset + 2U <= length) {
        uint8_t descriptor_length = configuration[offset];
        uint8_t descriptor_type = configuration[offset + 1U];
        if (descriptor_length < 2U || offset + descriptor_length > length)
            return 0;

        if (descriptor_type == USB_DESC_INTERFACE && descriptor_length >= 9U) {
            current_interface = configuration[offset + 2U];
            uint8_t interface_class = configuration[offset + 5U];
            uint8_t subclass = configuration[offset + 6U];
            current_protocol = configuration[offset + 7U];
            current_boot_hid = interface_class == USB_CLASS_HID &&
                               subclass == USB_SUBCLASS_BOOT &&
                               (current_protocol == USB_PROTOCOL_KEYBOARD ||
                                current_protocol == USB_PROTOCOL_MOUSE);
        } else if (descriptor_type == USB_DESC_ENDPOINT &&
                   descriptor_length >= 7U && current_boot_hid &&
                   !candidate->found) {
            uint8_t address = configuration[offset + 2U];
            uint8_t attributes = configuration[offset + 3U];
            uint16_t packet = (uint16_t)configuration[offset + 4U] |
                              ((uint16_t)(configuration[offset + 5U] & 0x07U) << 8);
            if ((address & 0x80U) != 0U &&
                (attributes & 0x03U) == USB_TRANSFER_INTERRUPT &&
                packet != 0U && packet <= sizeof(report_buffer)) {
                candidate->found = 1;
                candidate->interface_number = current_interface;
                candidate->protocol = current_protocol;
                candidate->endpoint_address = address;
                candidate->max_packet = packet;
                candidate->interval = configuration[offset + 6U];
            }
        }
        offset += descriptor_length;
    }
    return candidate->found;
}

static void usb_bluetooth_detach_slot(uint32_t slot)
{
    if (slot == 0U || slot > USB_MAX_XHCI_SLOTS)
        return;
    bluetooth_slots[slot].active = 0U;
    bluetooth_slots[slot].port = 0U;
    for (uint32_t peer = 0U; peer < USB_BLUETOOTH_LINK_MAX; ++peer) {
        bluetooth_peers[slot][peer].active = 0U;
        bluetooth_peers[slot][peer].handle = 0U;
        for (uint32_t byte = 0U; byte < 6U; ++byte)
            bluetooth_peers[slot][peer].address[byte] = 0U;
    }
    bluetooth_slots[slot].interface_number = 0U;
    bluetooth_slots[slot].event_endpoint = 0U;
    bluetooth_slots[slot].acl_in_endpoint = 0U;
    bluetooth_slots[slot].acl_out_endpoint = 0U;
    bluetooth_slots[slot].event_count = 0U;
    bluetooth_slots[slot].event_dropped = 0U;
    bluetooth_slots[slot].acl_packet_count = 0U;
    bluetooth_slots[slot].reset_complete_observed = 0U;
    bluetooth_slots[slot].reset_failed_observed = 0U;
    bluetooth_slots[slot].connection_deadline = 0U;
    usb_bluetooth_connection_attempt_reset(
        &bluetooth_slots[slot].connection_attempt);
    usb_bluetooth_inquiry_state_reset(
        &bluetooth_slots[slot].inquiry_state);
    usb_bluetooth_discovery_reset(&bluetooth_discovery_tables[slot]);
    usb_bluetooth_hci_command_state_reset(
        &bluetooth_slots[slot].command_state);
    usb_bluetooth_acl_queue_reset(&bluetooth_acl_queues[slot]);
    usb_bluetooth_acl_queue_reset(&bluetooth_reassembled_acl_queues[slot]);
    usb_bluetooth_hci_event_queue_reset(&bluetooth_hci_event_queues[slot]);
    usb_bluetooth_link_table_reset(&bluetooth_link_tables[slot]);
    bluetooth_l2cap_reset_all(slot);
    usb_bluetooth_l2cap_data_queue_reset(&bluetooth_l2cap_data_queues[slot]);
    bluetooth_l2cap_data_dropped[slot] = 0U;
    bluetooth_acl_dropped[slot] = 0U;
    __atomic_store_n(&bluetooth_acl_tx_busy[slot], 0U, __ATOMIC_RELEASE);
}

/*
 * Failed Disable Slot commands must not permanently leak a slot after
 * disconnect or partial enumeration. Retry from the USB owner poll loop.
 */
static void usb_disable_slot_or_defer(uint32_t slot)
{
    if (slot == 0U || slot > USB_MAX_XHCI_SLOTS)
        return;
    if (xhci_disable_device(slot))
        usb_pending_slot_disable[slot] = 0U;
    else
        usb_pending_slot_disable[slot] = 1U;
}

static void usb_detach_slot(uint32_t slot)
{
    if (slot > USB_MAX_XHCI_SLOTS || !input_slots[slot].active)
        return;
    if (input_slots[slot].kind == 1U)
        usb_hid_keyboard_detach(input_slots[slot].hid_handle);
    else if (input_slots[slot].kind == 2U)
        usb_hid_mouse_detach(input_slots[slot].hid_handle);
    input_slots[slot].active = 0;
    input_slots[slot].port = 0;
    input_slots[slot].kind = 0;
    input_slots[slot].hid_handle = 0;
}

static int usb_enumerate_port(uint32_t port)
{
    uint32_t port_status = xhci_port_status(port);
    if ((port_status & 1U) == 0U)
        return 0;
    if (!xhci_reset_port(port))
        return 0;

    uint32_t speed = xhci_port_speed(port);
    uint16_t initial_packet = usb_initial_ep0_packet(speed);
    if (initial_packet == 0U)
        return 0;

    uint32_t slot = 0;
    if (!xhci_enable_device(port, speed, initial_packet, &slot))
        return 0;

    int success = 0;
    usb_hid_candidate_t candidate = {0};
    uint16_t packet = 0;
    uint16_t configuration_length = 0;
    uint8_t configuration_value = 0;

    /* Read the first eight device-descriptor bytes before adjusting EP0 MPS. */
    if (!usb_control(slot, 0x80U, USB_REQUEST_GET_DESCRIPTOR, 0x0100U,
                     0, descriptor_buffer, 8U))
        goto cleanup;
    packet = usb_descriptor_ep0_packet(speed, descriptor_buffer[7]);
    if (packet == 0U)
        goto cleanup;
    if (packet != initial_packet && !xhci_set_ep0_packet(slot, packet))
        goto cleanup;

    if (!usb_control(slot, 0x80U, USB_REQUEST_GET_DESCRIPTOR, 0x0100U,
                     0, descriptor_buffer, 18U) ||
        descriptor_buffer[0] < 18U ||
        descriptor_buffer[1] != USB_DESC_DEVICE)
        goto cleanup;

    if (!usb_control(slot, 0x80U, USB_REQUEST_GET_DESCRIPTOR, 0x0200U,
                     0, configuration_buffer, 9U) ||
        configuration_buffer[1] != USB_DESC_CONFIGURATION)
        goto cleanup;
    configuration_length = (uint16_t)configuration_buffer[2] |
                           ((uint16_t)configuration_buffer[3] << 8);
    configuration_value = configuration_buffer[5];
    if (configuration_length < 9U ||
        configuration_length > USB_MAX_CONFIGURATION_SIZE ||
        configuration_value == 0U)
        goto cleanup;

    if (!usb_control(slot, 0x80U, USB_REQUEST_GET_DESCRIPTOR, 0x0200U,
                     0, configuration_buffer, configuration_length))
        goto cleanup;
    uint16_t vendor_id = (uint16_t)descriptor_buffer[8] |
                         ((uint16_t)descriptor_buffer[9] << 8);
    uint16_t product_id = (uint16_t)descriptor_buffer[10] |
                          ((uint16_t)descriptor_buffer[11] << 8);

    if (!usb_control(slot, 0x00U, USB_REQUEST_SET_CONFIGURATION,
                     configuration_value, 0, 0, 0))
        goto cleanup;

    usb_device_identity_t identity = {
        .vendor_id = vendor_id,
        .product_id = product_id,
        .class_code = descriptor_buffer[4],
        .subclass = descriptor_buffer[5],
        .protocol = descriptor_buffer[6],
        .revision = descriptor_buffer[12]
    };
    if (!usb_device_register(slot, port, xhci_controller_device(), &identity))
        goto cleanup;
    if (!usb_device_register_interfaces(slot, configuration_buffer,
                                         configuration_length)) {
        (void)usb_device_unregister(slot);
        goto cleanup;
    }
    /* Enumeration is a successful generic USB bind even without a class driver. */
    success = 1;

    /* Bind supported RTL8188EU USB IDs before generic interface matching. */
    if (vendor_id == 0x0BDAU &&
        (product_id == 0x8179U || product_id == 0x0179U)) {
        if (rtl8188eu_probe(slot, port, vendor_id, product_id, configuration_buffer,
                            configuration_length)) {
            serial_write("VANTAOS_USB_RTL8188EU_BOUND\n");
            success = 1;
        }
        goto cleanup;
    }

    /*
     * Bind USB HCI transport after endpoint configuration. This is not a full
     * Bluetooth host stack: pairing, L2CAP, security, profiles, and discovery
     * remain separate layers.
     */
    usb_bluetooth_hci_candidate_t bluetooth_candidate = {0};
    if (usb_bluetooth_find_hci_interface(configuration_buffer,
                                         configuration_length,
                                         &bluetooth_candidate)) {
        if (!xhci_configure_interrupt_in(slot,
                                         bluetooth_candidate.event_endpoint,
                                         bluetooth_candidate.event_max_packet,
                                         bluetooth_candidate.event_interval) ||
            !xhci_configure_bulk_endpoint(slot,
                                         bluetooth_candidate.acl_in_endpoint,
                                         bluetooth_candidate.acl_in_max_packet) ||
            !xhci_configure_bulk_endpoint(slot,
                                         bluetooth_candidate.acl_out_endpoint,
                                         bluetooth_candidate.acl_out_max_packet))
            goto cleanup;

        /* Bind first; HCI Reset follows after event reception is armed. */
        bluetooth_slots[slot].active = 1U;
        bluetooth_slots[slot].port = (uint8_t)port;
        bluetooth_slots[slot].interface_number =
            bluetooth_candidate.interface_number;
        bluetooth_slots[slot].event_endpoint =
            bluetooth_candidate.event_endpoint;
        bluetooth_slots[slot].acl_in_endpoint =
            bluetooth_candidate.acl_in_endpoint;
        bluetooth_slots[slot].acl_out_endpoint =
            bluetooth_candidate.acl_out_endpoint;
        bluetooth_slots[slot].event_count = 0U;
        bluetooth_slots[slot].event_dropped = 0U;
        bluetooth_slots[slot].acl_packet_count = 0U;
        bluetooth_slots[slot].reset_complete_observed = 0U;
        bluetooth_slots[slot].reset_failed_observed = 0U;
        usb_bluetooth_hci_command_state_reset(
            &bluetooth_slots[slot].command_state);
        bluetooth_slots[slot].command_state.credits = 1U;
        bluetooth_l2cap_reset_all(slot);
    usb_bluetooth_l2cap_data_queue_reset(&bluetooth_l2cap_data_queues[slot]);
    bluetooth_l2cap_data_dropped[slot] = 0U;
        usb_bluetooth_acl_queue_reset(&bluetooth_acl_queues[slot]);
        usb_bluetooth_acl_queue_reset(&bluetooth_reassembled_acl_queues[slot]);
        usb_bluetooth_hci_event_queue_reset(&bluetooth_hci_event_queues[slot]);
        usb_bluetooth_link_table_reset(&bluetooth_link_tables[slot]);
        bluetooth_l2cap_reset_all(slot);
    usb_bluetooth_l2cap_data_queue_reset(&bluetooth_l2cap_data_queues[slot]);
    bluetooth_l2cap_data_dropped[slot] = 0U;
        bluetooth_acl_dropped[slot] = 0U;
        __atomic_store_n(&bluetooth_acl_tx_busy[slot], 0U, __ATOMIC_RELEASE);
        if (!xhci_start_interrupt_in(slot) ||
            !xhci_start_bulk_in(slot, bluetooth_candidate.acl_in_endpoint)) {
            usb_bluetooth_detach_slot(slot);
            goto cleanup;
        }
        if (!usb_bluetooth_hci_command((uint32_t)port, 0x0C03U, 0, 0U)) {
            usb_bluetooth_detach_slot(slot);
            goto cleanup;
        }
        serial_write("VANTAOS_USB_BLUETOOTH_HCI_TRANSPORT_BOUND\n");
        success = 1;
        goto cleanup;
    }

    if (!usb_find_boot_interface(configuration_buffer, configuration_length,
                                 &candidate))
        goto cleanup;
    if (!usb_control(slot, 0x21U, USB_HID_SET_PROTOCOL, 0,
                     candidate.interface_number, 0, 0))
        goto cleanup;

    uint32_t hid_handle = 0;
    uint8_t kind = candidate.protocol == USB_PROTOCOL_KEYBOARD ? 1U : 2U;
    int attached = kind == 1U
        ? usb_hid_keyboard_attach(&hid_handle)
        : usb_hid_mouse_attach(&hid_handle);
    if (!attached)
        goto cleanup;

    if (!xhci_configure_interrupt_in(slot, candidate.endpoint_address,
                                     candidate.max_packet, candidate.interval)) {
        if (kind == 1U)
            usb_hid_keyboard_detach(hid_handle);
        else
            usb_hid_mouse_detach(hid_handle);
        goto cleanup;
    }

    input_slots[slot].active = 1;
    input_slots[slot].port = (uint8_t)port;
    input_slots[slot].kind = kind;
    input_slots[slot].hid_handle = hid_handle;
    if (!xhci_start_interrupt_in(slot)) {
        usb_detach_slot(slot);
        goto cleanup;
    }

    serial_write(kind == 1U ? "VANTAOS_USB_HID_KEYBOARD_ONLINE\n"
                            : "VANTAOS_USB_HID_MOUSE_ONLINE\n");
    success = 1;

cleanup:
    if (!success) {
        (void)usb_device_unregister(slot);
        usb_disable_slot_or_defer(slot);
    }
    return success;
}

void usb_initialize(void)
{
    if (usb_initialized)
        return;
    usb_initialized = 1;
    if (!xhci_controller_online()) {
        serial_write("VANTAOS_USB_NO_XHCI_HOST\n");
        return;
    }

    uint32_t ports = xhci_port_count();
    for (uint32_t port = 1; port <= ports; ++port)
        (void)usb_enumerate_port(port);
}

void usb_poll(void)
{
    if (!usb_initialized || !xhci_controller_online())
        return;
    xhci_poll();
    for (uint32_t slot = 1U; slot <= USB_MAX_XHCI_SLOTS; ++slot) {
        if (usb_pending_slot_disable[slot])
            usb_disable_slot_or_defer(slot);
    }
    rtl8188eu_poll();
    uint32_t changed_ports = xhci_take_port_events();
    for (uint32_t port = 1; port <= xhci_port_count(); ++port) {
        if ((changed_ports & (1U << (port - 1U))) == 0U)
            continue;
        uint32_t port_status = xhci_port_status(port);
        xhci_ack_port_changes(port);
        if ((port_status & 1U) == 0U) {
            (void)rtl8188eu_port_disconnected(port);
            for (uint32_t slot = 1; slot <= USB_MAX_XHCI_SLOTS; ++slot) {
                int slot_disconnected = 0;
                if (bluetooth_slots[slot].active &&
                    bluetooth_slots[slot].port == port) {
                    usb_bluetooth_detach_slot(slot);
                    slot_disconnected = 1;
                }
                if (input_slots[slot].active && input_slots[slot].port == port) {
                    usb_detach_slot(slot);
                    slot_disconnected = 1;
                }
                if (usb_device_port(slot) == port) {
                    (void)usb_device_unregister(slot);
                    slot_disconnected = 1;
                }
                if (slot_disconnected)
                    usb_disable_slot_or_defer(slot);
            }
        } else {
            int already_enumerated = rtl8188eu_port_is_bound(port);
            for (uint32_t slot = 1; slot <= USB_MAX_XHCI_SLOTS; ++slot) {
                if ((input_slots[slot].active && input_slots[slot].port == port) ||
                    (bluetooth_slots[slot].active &&
                     bluetooth_slots[slot].port == port) ||
                    usb_device_port(slot) == port)
                    already_enumerated = 1;
            }
            if (!already_enumerated)
                (void)usb_enumerate_port(port);
        }
    }
    for (uint32_t slot = 1; slot <= USB_MAX_XHCI_SLOTS; ++slot) {
        usb_bluetooth_slot_t *bluetooth = &bluetooth_slots[slot];
        if (!bluetooth->active)
            continue;

        uint32_t now = timer_get_ticks();
        if (usb_bluetooth_hci_command_watchdog_expire(
                &bluetooth->command_state, now) > 0)
            serial_write("VANTAOS_USB_BLUETOOTH_HCI_WATCHDOG_TIMEOUT\n");
        if (bluetooth->connection_attempt.active != 0U &&
            bluetooth->connection_deadline != 0U &&
            (int32_t)(now - bluetooth->connection_deadline) >= 0) {
            int cancel_sent = usb_bluetooth_cancel_connection(
                bluetooth->port, bluetooth->connection_attempt.address);
            if (!cancel_sent)
                bluetooth->connection_deadline =
                    now + USB_BLUETOOTH_CANCEL_RETRY_TICKS;
        }

        uint32_t event_length = 0U;
        int event_result = xhci_take_interrupt_report(
            slot, bluetooth_event_buffer, sizeof(bluetooth_event_buffer),
            &event_length);
        if (event_result > 0) {
            if (usb_bluetooth_hci_event_valid(bluetooth_event_buffer,
                                              event_length)) {
                if ((bluetooth_event_buffer[0] == 0x02U ||
                     bluetooth_event_buffer[0] == 0x22U) &&
                    event_length >= 3U) {
                    for (uint32_t index = 0U;
                         index < bluetooth_event_buffer[2]; ++index) {
                        usb_bluetooth_inquiry_result_t discovered;
                        int parsed = usb_bluetooth_hci_inquiry_result(
                            bluetooth_event_buffer, event_length,
                            (uint8_t)index, &discovered);
                        if (parsed < 0)
                            break;
                        if (parsed > 0)
                            (void)usb_bluetooth_discovery_record(
                                &bluetooth_discovery_tables[slot], &discovered);
                    }
                }
                bluetooth->event_count++;
                (void)usb_bluetooth_hci_command_event(
                    &bluetooth->command_state, bluetooth_event_buffer,
                    event_length);
                if (bluetooth_event_buffer[0] == 0x0EU &&
                    event_length >= 6U &&
                    bluetooth_event_buffer[3] == 0x05U &&
                    bluetooth_event_buffer[4] == 0x10U) {
                    usb_bluetooth_hci_buffer_size_t buffer_size = {0U};
                    int parsed_size = usb_bluetooth_hci_read_buffer_size_result(
                        bluetooth_event_buffer, event_length, &buffer_size);
                    bluetooth->acl_packet_length = parsed_size > 0 ?
                        buffer_size.acl_packet_length : 0U;
                    bluetooth->acl_total_packets = parsed_size > 0 ?
                        buffer_size.total_acl_packets : 0U;
                    bluetooth->acl_packets_available = parsed_size > 0 ?
                        buffer_size.total_acl_packets : 0U;
                }
                uint16_t completed_acl_packets = 0U;
                int completed_result = usb_bluetooth_hci_completed_acl_packets(
                    bluetooth_event_buffer, event_length,
                    &completed_acl_packets);
                if (completed_result > 0) {
                    uint32_t available =
                        (uint32_t)bluetooth->acl_packets_available +
                        completed_acl_packets;
                    if (available > bluetooth->acl_total_packets)
                        available = bluetooth->acl_total_packets;
                    bluetooth->acl_packets_available = (uint16_t)available;
                }
                int link_transition = usb_bluetooth_link_table_event(
                    &bluetooth_link_tables[slot], bluetooth_event_buffer,
                    event_length);
                if (bluetooth_event_buffer[0] == 0x01U &&
                    event_length == 3U)
                    usb_bluetooth_inquiry_state_complete(
                        &bluetooth->inquiry_state);
                if (bluetooth_event_buffer[0] == 0x03U &&
                    event_length == 13U) {
                    if (usb_bluetooth_connection_attempt_finish(
                            &bluetooth->connection_attempt,
                            &bluetooth_event_buffer[5]) != 0)
                        bluetooth->connection_deadline = 0U;
                } else if (bluetooth_event_buffer[0] == 0x0FU &&
                           event_length == 6U &&
                           bluetooth_event_buffer[2] != 0U &&
                           bluetooth_event_buffer[4] == 0x05U &&
                           bluetooth_event_buffer[5] == 0x04U) {
                    /* Controller rejected the Create Connection command. */
                    usb_bluetooth_connection_attempt_reset(
                        &bluetooth->connection_attempt);
                    bluetooth->connection_deadline = 0U;
                } else if (bluetooth_event_buffer[0] == 0x0FU &&
                           event_length == 6U &&
                           bluetooth_event_buffer[2] != 0U &&
                           bluetooth_event_buffer[4] == 0x01U &&
                           bluetooth_event_buffer[5] == 0x04U) {
                    /* Controller rejected the Inquiry command. */
                    usb_bluetooth_inquiry_state_command_rejected(
                        &bluetooth->inquiry_state);
                } else if (bluetooth_event_buffer[0] == 0x0FU &&
                           event_length == 6U &&
                           bluetooth_event_buffer[2] != 0U &&
                           bluetooth_event_buffer[4] == 0x02U &&
                           bluetooth_event_buffer[5] == 0x04U) {
                    /* A rejected cancel leaves the inquiry active. */
                    (void)usb_bluetooth_inquiry_state_cancel_result(
                        &bluetooth->inquiry_state, 0);
                } else if (bluetooth_event_buffer[0] == 0x0EU &&
                           event_length == 12U &&
                           bluetooth_event_buffer[3] == 0x08U &&
                           bluetooth_event_buffer[4] == 0x04U &&
                           bluetooth_event_buffer[5] == 0U) {
                    /* Successful Create Connection Cancel returns BD_ADDR. */
                    if (usb_bluetooth_connection_attempt_finish(
                            &bluetooth->connection_attempt,
                            &bluetooth_event_buffer[6]) != 0)
                        bluetooth->connection_deadline = 0U;
                } else if (bluetooth_event_buffer[0] == 0x0EU &&
                           event_length == 12U &&
                           bluetooth_event_buffer[3] == 0x08U &&
                           bluetooth_event_buffer[4] == 0x04U &&
                           bluetooth_event_buffer[5] != 0U) {
                    bluetooth->connection_deadline = timer_get_ticks() +
                        USB_BLUETOOTH_CANCEL_RETRY_TICKS;
                } else if (bluetooth_event_buffer[0] == 0x0FU &&
                           event_length == 6U &&
                           bluetooth_event_buffer[4] == 0x08U &&
                           bluetooth_event_buffer[5] == 0x04U &&
                           bluetooth_event_buffer[2] != 0U) {
                    bluetooth->connection_deadline = timer_get_ticks() +
                        USB_BLUETOOTH_CANCEL_RETRY_TICKS;
                } else if (bluetooth_event_buffer[0] == 0x0EU &&
                           event_length >= 6U &&
                           bluetooth_event_buffer[3] == 0x02U &&
                           bluetooth_event_buffer[4] == 0x04U) {
                    /* Inquiry Cancel completes through Command Complete. */
                    (void)usb_bluetooth_inquiry_state_cancel_result(
                        &bluetooth->inquiry_state,
                        bluetooth_event_buffer[5] == 0U);
                }
                if (bluetooth_event_buffer[0] == 0x03U &&
                    event_length == 13U && bluetooth_event_buffer[2] == 0U &&
                    link_transition > 0) {
                    uint16_t connected_handle =
                        (uint16_t)bluetooth_event_buffer[3] |
                        ((uint16_t)(bluetooth_event_buffer[4] & 0x0FU) << 8);
                    bluetooth_peer_record(slot, &bluetooth_event_buffer[5],
                                          connected_handle);
                    if (bluetooth_l2cap_table_for_handle(
                            slot, connected_handle, 1) == NULL)
                        serial_write(
                            "VANTAOS_BLUETOOTH_L2CAP_OWNER_TABLE_FULL\\n");
                }
                if (bluetooth_event_buffer[0] == 0x05U &&
                    event_length == 6U && bluetooth_event_buffer[2] == 0U &&
                    link_transition > 0) {
                    uint16_t disconnected_handle =
                        (uint16_t)bluetooth_event_buffer[3] |
                        ((uint16_t)(bluetooth_event_buffer[4] & 0x0FU) << 8);
                    bluetooth_l2cap_forget_handle(slot, disconnected_handle);
                    bluetooth_peer_forget_handle(slot, disconnected_handle);
                    usb_bluetooth_l2cap_data_queue_forget_handle(
                        &bluetooth_l2cap_data_queues[slot], disconnected_handle);
                    if (usb_bluetooth_link_table_active_count(
                            &bluetooth_link_tables[slot]) == 0U)
                        usb_bluetooth_acl_queue_reset(
                            &bluetooth_reassembled_acl_queues[slot]);
                }
                int event_queued = usb_bluetooth_hci_event_queue_push(
                    &bluetooth_hci_event_queues[slot], bluetooth_event_buffer,
                    event_length);
                if (event_queued == 0 && bluetooth->event_count == 1U)
                    serial_write("VANTAOS_BLUETOOTH_HCI_EVENT_RECEIVED\n");
                else if (event_queued == -11) {
                    bluetooth->event_dropped++;
                    if (bluetooth->event_dropped == 1U)
                        serial_write("VANTAOS_BLUETOOTH_HCI_EVENT_QUEUE_FULL\n");
                }
                int reset_transition = usb_bluetooth_update_reset_state(
                    bluetooth_event_buffer, event_length,
                    &bluetooth->reset_complete_observed,
                    &bluetooth->reset_failed_observed);
                if (reset_transition > 0) {
                    bluetooth->acl_packet_length = 0U;
                    bluetooth->acl_total_packets = 0U;
                    bluetooth->acl_packets_available = 0U;
                    serial_write("VANTAOS_BLUETOOTH_HCI_RESET_COMPLETE\n");
                    /* Discover the controller's ACL fragment size before TX. */
                    (void)usb_bluetooth_hci_command(
                        bluetooth->port, 0x1005U, NULL, 0U);
                }
                else if (reset_transition < 0)
                    serial_write("VANTAOS_BLUETOOTH_HCI_RESET_FAILED\n");
            }
            /*
             * Consuming a completed transfer does not arm the next event.
             * Re-arm even after malformed event bytes so one bad packet
             * cannot permanently stall the controller's HCI event stream.
             */
            (void)xhci_start_interrupt_in(slot);
        } else if (event_result != -2) {
            /* Retry a missing submission or a failed transfer. */
            (void)xhci_start_interrupt_in(slot);
        }

        uint32_t acl_length = 0U;
        int acl_result = xhci_take_bulk_in(slot, bluetooth->acl_in_endpoint,
                                           bluetooth_acl_buffer,
                                           sizeof(bluetooth_acl_buffer),
                                           &acl_length);
        if (acl_result > 0) {
            if (usb_bluetooth_transport_ready(
                    bluetooth->reset_complete_observed,
                    bluetooth->reset_failed_observed) &&
                usb_bluetooth_acl_packet_valid(bluetooth_acl_buffer,
                                               acl_length)) {
                usb_bluetooth_acl_view_t incoming_acl;
                int feed_result = usb_bluetooth_link_table_acl_feed(
                    &bluetooth_link_tables[slot], bluetooth_acl_buffer,
                    acl_length);
                /*
                 * Only dispatch signaling after per-handle reassembly yields
                 * one complete L2CAP PDU. Preserve the original ACL fragment
                 * for the raw receive queue; assembled bytes use a separate
                 * bounded buffer.
                 */
                if (feed_result == 1 &&
                    usb_bluetooth_acl_parse(bluetooth_acl_buffer, acl_length,
                                            &incoming_acl)) {
                    int assembled_length = usb_bluetooth_link_table_acl_take(
                        &bluetooth_link_tables[slot], incoming_acl.handle,
                        bluetooth_acl_reassembled_buffer,
                        sizeof(bluetooth_acl_reassembled_buffer));
                    /* Each connection handle owns an isolated L2CAP table. */
                    if (assembled_length > 0) {
                        (void)usb_bluetooth_acl_queue_push(
                            &bluetooth_reassembled_acl_queues[slot],
                            bluetooth_acl_reassembled_buffer,
                            (uint32_t)assembled_length);
                        usb_bluetooth_l2cap_table_t *handle_channels =
                            bluetooth_l2cap_table_for_handle(
                                slot, incoming_acl.handle, 0);
                        if (handle_channels != NULL) {
                            (void)usb_bluetooth_l2cap_dispatch_acl_responses_for_handle(
                                handle_channels, incoming_acl.handle,
                                bluetooth_acl_reassembled_buffer,
                                (uint32_t)assembled_length);
                            bluetooth_l2cap_handle_signaling_requests(
                                slot, incoming_acl.handle,
                                bluetooth_acl_reassembled_buffer,
                                (uint32_t)assembled_length);
                            bluetooth_l2cap_queue_data(
                                slot, incoming_acl.handle,
                                bluetooth_acl_reassembled_buffer,
                                (uint32_t)assembled_length);
                        }
                    }
                }
                int queued = usb_bluetooth_acl_queue_push(
                    &bluetooth_acl_queues[slot], bluetooth_acl_buffer,
                    acl_length);
                if (queued == 0) {
                    bluetooth->acl_packet_count++;
                    if (bluetooth->acl_packet_count == 1U)
                        serial_write("VANTAOS_BLUETOOTH_ACL_PACKET_QUEUED\n");
                } else if (queued == -11) {
                    bluetooth_acl_dropped[slot]++;
                    if (bluetooth_acl_dropped[slot] == 1U)
                        serial_write("VANTAOS_BLUETOOTH_ACL_QUEUE_FULL\n");
                }
            }
            (void)xhci_start_bulk_in(slot, bluetooth->acl_in_endpoint);
        } else if (acl_result != -2) {
            /*
             * A failed transfer is re-armed above. Also retry a missing
             * in-flight submission: a transient ring/doorbell failure must
             * not leave this endpoint permanently silent. Starting while a
             * transfer is already active is rejected by xHCI without damage.
             * -2 is different: its completed packet is retained for retry.
             */
            (void)xhci_start_bulk_in(slot, bluetooth->acl_in_endpoint);
        }
        bluetooth_l2cap_progress_channels(slot);
    }

    for (uint32_t slot = 1; slot <= USB_MAX_XHCI_SLOTS; ++slot) {
        if (!input_slots[slot].active)
            continue;
        uint32_t length = 0;
        if (xhci_take_interrupt_report(slot, report_buffer,
                                        sizeof(report_buffer), &length) <= 0)
            continue;
        if (input_slots[slot].kind == 1U) {
            usb_hid_keyboard_report(input_slots[slot].hid_handle,
                                    report_buffer, length);
            int key_activity = length >= 8U && report_buffer[0] != 0U;
            for (uint32_t key = 2U; key < 8U && key < length; ++key)
                if (report_buffer[key] != 0U)
                    key_activity = 1;
            if (key_activity && !keyboard_input_observed) {
                keyboard_input_observed = 1;
                serial_write("VANTAOS_USB_HID_KEYBOARD_INPUT_OK\n");
            }
        } else if (input_slots[slot].kind == 2U) {
            usb_hid_mouse_report(input_slots[slot].hid_handle,
                                 report_buffer, length);
            int mouse_activity = length >= 3U &&
                ((report_buffer[0] & 0x1FU) != 0U ||
                 (int8_t)report_buffer[1] != 0 ||
                 (int8_t)report_buffer[2] != 0 ||
                 (length >= 4U && (int8_t)report_buffer[3] != 0));
            if (mouse_activity && !mouse_input_observed) {
                mouse_input_observed = 1;
                serial_write("VANTAOS_USB_HID_MOUSE_INPUT_OK\n");
            }
        }
    }
}
