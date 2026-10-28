/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "usb.h"

#include "../input/usb/hid.h"
#include "xhci.h"
#include "rtl8188eu.h"
#include "usb_bluetooth.h"
#include "serial.h"

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
    uint32_t acl_packet_count;
    uint8_t reset_complete_observed;
    uint8_t reset_failed_observed;
} usb_bluetooth_slot_t;

static usb_bluetooth_slot_t bluetooth_slots[USB_MAX_XHCI_SLOTS + 1U];
static uint8_t bluetooth_event_buffer[64] __attribute__((aligned(64)));
static uint8_t bluetooth_acl_buffer[USB_BLUETOOTH_ACL_MAX_PACKET]
    __attribute__((aligned(64)));
static usb_bluetooth_acl_queue_t bluetooth_acl_queues[USB_MAX_XHCI_SLOTS + 1U];
static uint32_t bluetooth_acl_dropped[USB_MAX_XHCI_SLOTS + 1U];
static uint8_t bluetooth_acl_tx_buffers[USB_MAX_XHCI_SLOTS + 1U][4096]
    __attribute__((aligned(64)));
static uint8_t bluetooth_acl_tx_busy[USB_MAX_XHCI_SLOTS + 1U];
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
int usb_bluetooth_acl_send(uint32_t port, const uint8_t *packet,
                           uint32_t length)
{
    if (port == 0U || packet == 0 || length < 4U ||
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
        !usb_bluetooth_transport_ready(
            bluetooth_slots[slot].reset_complete_observed,
            bluetooth_slots[slot].reset_failed_observed))
        return 0;

    if (__atomic_test_and_set(&bluetooth_acl_tx_busy[slot], __ATOMIC_ACQUIRE))
        return 0;

    int success = 0;
    if (bluetooth_slots[slot].active &&
        bluetooth_slots[slot].port == port) {
        for (uint32_t i = 0U; i < length; ++i)
            bluetooth_acl_tx_buffers[slot][i] = packet[i];

        uint32_t actual_length = 0U;
        success = xhci_bulk_transfer(slot,
                                     bluetooth_slots[slot].acl_out_endpoint,
                                     bluetooth_acl_tx_buffers[slot], length,
                                     &actual_length) &&
                  actual_length == length;
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
    bluetooth_slots[slot].interface_number = 0U;
    bluetooth_slots[slot].event_endpoint = 0U;
    bluetooth_slots[slot].acl_in_endpoint = 0U;
    bluetooth_slots[slot].acl_out_endpoint = 0U;
    bluetooth_slots[slot].event_count = 0U;
    bluetooth_slots[slot].acl_packet_count = 0U;
    bluetooth_slots[slot].reset_complete_observed = 0U;
    bluetooth_slots[slot].reset_failed_observed = 0U;
    usb_bluetooth_acl_queue_reset(&bluetooth_acl_queues[slot]);
    bluetooth_acl_dropped[slot] = 0U;
    __atomic_store_n(&bluetooth_acl_tx_busy[slot], 0U, __ATOMIC_RELEASE);
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

        /* HCI Reset opcode 0x0C03, sent over the USB class control pipe. */
        if (!usb_control(slot, 0x20U, 0U, 0x0C03U,
                         bluetooth_candidate.interface_number, 0, 0))
            goto cleanup;
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
        bluetooth_slots[slot].acl_packet_count = 0U;
        bluetooth_slots[slot].reset_complete_observed = 0U;
        bluetooth_slots[slot].reset_failed_observed = 0U;
        usb_bluetooth_acl_queue_reset(&bluetooth_acl_queues[slot]);
        bluetooth_acl_dropped[slot] = 0U;
        __atomic_store_n(&bluetooth_acl_tx_busy[slot], 0U, __ATOMIC_RELEASE);
        if (!xhci_start_interrupt_in(slot) ||
            !xhci_start_bulk_in(slot, bluetooth_candidate.acl_in_endpoint)) {
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
    if (!success)
        (void)xhci_disable_device(slot);
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
                if (slot_disconnected)
                    (void)xhci_disable_device(slot);
            }
        } else {
            int already_enumerated = rtl8188eu_port_is_bound(port);
            for (uint32_t slot = 1; slot <= USB_MAX_XHCI_SLOTS; ++slot) {
                if ((input_slots[slot].active && input_slots[slot].port == port) ||
                    (bluetooth_slots[slot].active &&
                     bluetooth_slots[slot].port == port))
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

        uint32_t event_length = 0U;
        if (xhci_take_interrupt_report(slot, bluetooth_event_buffer,
                                       sizeof(bluetooth_event_buffer),
                                       &event_length) &&
            usb_bluetooth_hci_event_valid(bluetooth_event_buffer,
                                          event_length)) {
            bluetooth->event_count++;
            if (bluetooth->event_count == 1U)
                serial_write("VANTAOS_BLUETOOTH_HCI_EVENT_RECEIVED\n");
            int reset_transition = usb_bluetooth_update_reset_state(
                bluetooth_event_buffer, event_length,
                &bluetooth->reset_complete_observed,
                &bluetooth->reset_failed_observed);
            if (reset_transition > 0)
                serial_write("VANTAOS_BLUETOOTH_HCI_RESET_COMPLETE\\n");
            else if (reset_transition < 0)
                serial_write("VANTAOS_BLUETOOTH_HCI_RESET_FAILED\\n");
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
        } else if (acl_result < 0) {
            (void)xhci_start_bulk_in(slot, bluetooth->acl_in_endpoint);
        }
    }

    for (uint32_t slot = 1; slot <= USB_MAX_XHCI_SLOTS; ++slot) {
        if (!input_slots[slot].active)
            continue;
        uint32_t length = 0;
        if (!xhci_take_interrupt_report(slot, report_buffer,
                                        sizeof(report_buffer), &length))
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
