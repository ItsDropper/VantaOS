/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef USB_BLUETOOTH_H
#define USB_BLUETOOTH_H

#include <stdint.h>

#define USB_BLUETOOTH_ACL_MAX_PACKET 4096U
#define USB_BLUETOOTH_ACL_QUEUE_DEPTH 2U

#define USB_BLUETOOTH_HCI_EVENT_MAX 64U
#define USB_BLUETOOTH_HCI_EVENT_QUEUE_DEPTH 4U

typedef struct {
    uint32_t head;
    uint32_t count;
    uint32_t lengths[USB_BLUETOOTH_HCI_EVENT_QUEUE_DEPTH];
    uint8_t packets[USB_BLUETOOTH_HCI_EVENT_QUEUE_DEPTH]
                   [USB_BLUETOOTH_HCI_EVENT_MAX];
} usb_bluetooth_hci_event_queue_t;


typedef struct {
    uint32_t head;
    uint32_t count;
    uint32_t lengths[USB_BLUETOOTH_ACL_QUEUE_DEPTH];
    uint8_t packets[USB_BLUETOOTH_ACL_QUEUE_DEPTH][USB_BLUETOOTH_ACL_MAX_PACKET];
} usb_bluetooth_acl_queue_t;

/*
 * A descriptor-level match only. It does not mean that the controller has
 * been initialized or that Bluetooth services are available.
 */
typedef struct {
    uint16_t handle;
    uint8_t packet_boundary;
    uint8_t broadcast;
    const uint8_t *payload;
    uint32_t payload_length;
} usb_bluetooth_acl_view_t;

typedef struct {
    uint16_t channel_id;
    const uint8_t *payload;
    uint16_t payload_length;
} usb_bluetooth_l2cap_view_t;
typedef struct {
    uint8_t code;
    uint8_t identifier;
    const uint8_t *payload;
    uint16_t payload_length;
} usb_bluetooth_l2cap_signal_command_t;

typedef struct {
    uint8_t active;
    uint8_t complete;
    uint16_t handle;
    uint32_t expected_length;
    uint32_t received_length;
    uint8_t packet[USB_BLUETOOTH_ACL_MAX_PACKET];
} usb_bluetooth_acl_reassembly_t;
#define USB_BLUETOOTH_LINK_MAX 8U

typedef struct {
    uint8_t active;
    uint16_t handle;
    usb_bluetooth_acl_reassembly_t reassembly;
} usb_bluetooth_link_t;

/* Bounded HCI command-credit state, owned by the USB polling CPU. */
typedef struct {
    uint16_t opcode;
    uint8_t credits;
    uint8_t outstanding;
    uint8_t completed;
    uint8_t status;
    uint8_t event_kind; /* 1 Command Complete, 2 Command Status */
    uint8_t faulted; /* transport outcome ambiguous; Reset required */
} usb_bluetooth_hci_command_state_t;

typedef struct {
    uint8_t active;
    uint8_t address[6]; /* HCI wire byte order */
} usb_bluetooth_connection_attempt_t;

/* Inquiry lifecycle state, owned by the serialized USB polling CPU. */
typedef struct {
    uint8_t active;
    uint8_t cancel_state; /* 0 idle, 1 awaiting command status, 2 accepted */
} usb_bluetooth_inquiry_state_t;

void usb_bluetooth_inquiry_state_reset(usb_bluetooth_inquiry_state_t *state);
int usb_bluetooth_inquiry_state_begin(usb_bluetooth_inquiry_state_t *state);
int usb_bluetooth_inquiry_state_cancel_begin(usb_bluetooth_inquiry_state_t *state);
int usb_bluetooth_inquiry_state_cancel_result(usb_bluetooth_inquiry_state_t *state,
                                               int accepted);
void usb_bluetooth_inquiry_state_command_rejected(
    usb_bluetooth_inquiry_state_t *state);
void usb_bluetooth_inquiry_state_complete(usb_bluetooth_inquiry_state_t *state);

void usb_bluetooth_connection_attempt_reset(
    usb_bluetooth_connection_attempt_t *attempt);
int usb_bluetooth_connection_attempt_begin(
    usb_bluetooth_connection_attempt_t *attempt, const uint8_t address[6]);
int usb_bluetooth_connection_attempt_matches(
    const usb_bluetooth_connection_attempt_t *attempt,
    const uint8_t address[6]);
int usb_bluetooth_connection_attempt_finish(
    usb_bluetooth_connection_attempt_t *attempt, const uint8_t address[6]);

#define USB_BLUETOOTH_L2CAP_CHANNEL_MAX 8U
#define USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MIN 0x0040U
#define USB_BLUETOOTH_L2CAP_CID_DYNAMIC_MAX 0xFFFFU
typedef struct {
    uint8_t active;
    uint8_t state; /* 1 pending, 2 connected, 3 configuring, 4 open, 5 closing */
    uint8_t identifier;
    uint8_t config_identifier;
    uint16_t local_cid;
    uint16_t remote_cid;
    uint16_t psm;
    uint16_t local_mtu;
    uint16_t remote_mtu;
    uint32_t deadline;
} usb_bluetooth_l2cap_channel_t;
typedef struct {
    uint16_t next_local_cid;
    uint8_t next_identifier;
    usb_bluetooth_l2cap_channel_t channels[USB_BLUETOOTH_L2CAP_CHANNEL_MAX];
} usb_bluetooth_l2cap_table_t;

typedef struct {
    usb_bluetooth_link_t links[USB_BLUETOOTH_LINK_MAX];
} usb_bluetooth_link_table_t;

typedef struct {
    uint8_t address[6]; /* HCI wire byte order */
    uint8_t page_scan_repetition_mode;
    uint32_t class_of_device; /* low 24 bits are valid */
    uint16_t clock_offset; /* includes the controller's validity bit */
    int8_t rssi;
    uint8_t has_rssi;
} usb_bluetooth_inquiry_result_t;

typedef struct {
    uint8_t interface_number;
    uint8_t event_endpoint;
    uint8_t acl_in_endpoint;
    uint8_t acl_out_endpoint;
    uint8_t event_interval;
    uint16_t event_max_packet;
    uint16_t acl_in_max_packet;
    uint16_t acl_out_max_packet;
} usb_bluetooth_hci_candidate_t;

/*
 * Scan a complete configuration descriptor. Returns 1 only when one
 * Bluetooth HCI interface (class E0/subclass 01/protocol 01) has an
 * interrupt-IN event endpoint and both bulk-IN and bulk-OUT ACL endpoints.
 * Malformed descriptor streams and incomplete interfaces return 0.
 */
int usb_bluetooth_find_hci_interface(const uint8_t *configuration,
                                     uint16_t length,
                                     usb_bluetooth_hci_candidate_t *candidate);

/* HCI command-credit tracking. begin refuses a second outstanding command;
 * event consumes only matching Command Complete/Status and updates credits. */
void usb_bluetooth_hci_command_state_reset(
    usb_bluetooth_hci_command_state_t *state);
int usb_bluetooth_hci_command_begin(usb_bluetooth_hci_command_state_t *state,
                                    uint16_t opcode);
int usb_bluetooth_hci_command_event(usb_bluetooth_hci_command_state_t *state,
                                    const uint8_t *event, uint32_t length);
/* Build standard HCI Read Local Version, Read BD_ADDR, Read Buffer Size,
 * and Inquiry command packets; this does not submit them to a controller. */
int usb_bluetooth_hci_build_read_local_version(uint8_t *out, uint32_t capacity);
int usb_bluetooth_hci_build_read_bd_addr(uint8_t *out, uint32_t capacity);
int usb_bluetooth_hci_build_read_buffer_size(uint8_t *out, uint32_t capacity);
int usb_bluetooth_hci_build_inquiry(uint8_t *out, uint32_t capacity,
                                    uint8_t inquiry_length, uint8_t responses);
/* Parse one indexed result from HCI Inquiry Result (0x02) or Inquiry Result
 * with RSSI (0x22). Returns 1 for a result, 0 if index is out of range,
 * and -22 for malformed/unsupported event data.
 */
int usb_bluetooth_hci_inquiry_result(const uint8_t *event, uint32_t length,
                                     uint8_t index,
                                     usb_bluetooth_inquiry_result_t *result);
/* Build BR/EDR connection commands. BD_ADDR is in HCI wire byte order. */
int usb_bluetooth_hci_build_create_connection(
    const uint8_t address[6], uint16_t packet_type,
    uint8_t page_scan_repetition_mode, uint16_t clock_offset,
    uint8_t allow_role_switch, uint8_t *out, uint32_t capacity);
int usb_bluetooth_hci_build_disconnect(uint16_t handle, uint8_t reason,
                                       uint8_t *out, uint32_t capacity);
int usb_bluetooth_hci_build_create_connection_cancel(
    const uint8_t address[6], uint8_t *out, uint32_t capacity);

/* Submit connection-management commands on a bound USB Bluetooth port.
 * Return 1 when the USB control transfer was accepted, 0 on failure.
 * Call only from the serialized USB polling/owner CPU context.
 */
int usb_bluetooth_inquiry(uint32_t port, uint8_t inquiry_length,
                          uint8_t responses);
int usb_bluetooth_inquiry_cancel(uint32_t port);
int usb_bluetooth_connect(uint32_t port, const uint8_t address[6],
                          uint16_t packet_type,
                          uint8_t page_scan_repetition_mode,
                          uint16_t clock_offset, uint8_t allow_role_switch);
int usb_bluetooth_cancel_connection(uint32_t port,
                                    const uint8_t address[6]);
int usb_bluetooth_disconnect(uint32_t port, uint16_t handle, uint8_t reason);
/* Bounded L2CAP channel lifecycle primitives; signaling dispatch remains
 * the responsibility of a controller/connection manager. */
void usb_bluetooth_l2cap_table_reset(usb_bluetooth_l2cap_table_t *table);
int usb_bluetooth_l2cap_channel_begin(usb_bluetooth_l2cap_table_t *table,
                                      uint16_t psm, uint16_t local_mtu,
                                      uint32_t deadline,
                                      uint16_t *local_cid,
                                      uint8_t *identifier);
int usb_bluetooth_l2cap_channel_response(usb_bluetooth_l2cap_table_t *table,
                                         uint8_t identifier, uint16_t remote_cid,
                                         uint16_t result, uint16_t remote_mtu);
/* Apply one validated L2CAP Connection Response command (code 0x03).
 * Checks the signaling identifier and echoed local CID before changing state. */
int usb_bluetooth_l2cap_connection_response(
    usb_bluetooth_l2cap_table_t *table,
    const uint8_t *command, uint32_t length, uint16_t default_remote_mtu);
int usb_bluetooth_l2cap_channel_configured(usb_bluetooth_l2cap_table_t *table,
                                           uint16_t local_cid);
/* Record a nonzero identifier for a Configure Request on a configuring CID. */
int usb_bluetooth_l2cap_channel_configure_begin(
    usb_bluetooth_l2cap_table_t *table, uint16_t local_cid,
    uint8_t identifier);
/* Apply a parsed Configure Response; flags!=0 means continuation (returns 0).
 * A successful final response opens the channel. */
int usb_bluetooth_l2cap_channel_configure_response(
    usb_bluetooth_l2cap_table_t *table, uint16_t local_cid,
    uint8_t identifier, uint16_t flags, uint16_t result,
    uint16_t remote_mtu);
/* Parse and apply one complete L2CAP Configure Response signaling command. */
int usb_bluetooth_l2cap_configuration_response(
    usb_bluetooth_l2cap_table_t *table, const uint8_t *command,
    uint32_t length);
int usb_bluetooth_l2cap_channel_close(usb_bluetooth_l2cap_table_t *table,
                                      uint16_t local_cid);
uint32_t usb_bluetooth_l2cap_expire(usb_bluetooth_l2cap_table_t *table,
                                    uint32_t now);
/* Track legacy BR/EDR Connection Complete and Disconnection Complete events.
 * Returns 1 on link state change, 0 for unrelated event, or negative errno. */
void usb_bluetooth_link_table_reset(usb_bluetooth_link_table_t *table);
int usb_bluetooth_link_table_event(usb_bluetooth_link_table_t *table,
                                   const uint8_t *event, uint32_t length);
/* True only when handle is the sole active link (L2CAP ownership guard). */
int usb_bluetooth_link_table_is_single_handle(
    const usb_bluetooth_link_table_t *table, uint16_t handle);
uint32_t usb_bluetooth_link_table_active_count(
    const usb_bluetooth_link_table_t *table);
int usb_bluetooth_link_table_has_handle(
    const usb_bluetooth_link_table_t *table, uint16_t handle);
int usb_bluetooth_link_table_acl_feed(usb_bluetooth_link_table_t *table,
                                      const uint8_t *packet, uint32_t length);
int usb_bluetooth_link_table_acl_take(usb_bluetooth_link_table_t *table,
                                      uint16_t handle, uint8_t *packet,
                                      uint32_t capacity);
/* Parse one complete HCI ACL packet without copying its payload.
 * Rejects reserved connection handle zero and reserved broadcast flag 3. */
int usb_bluetooth_acl_parse(const uint8_t *packet, uint32_t length,
                            usb_bluetooth_acl_view_t *view);
/* Parse one complete, unfragmented L2CAP PDU carried in an ACL packet. */
int usb_bluetooth_l2cap_parse(const uint8_t *packet, uint32_t length,
                              usb_bluetooth_l2cap_view_t *view);
/* Validate and frame a complete CID 0x0001 signaling command list. */
int usb_bluetooth_l2cap_build_signaling_acl_packet(
    uint16_t handle, const uint8_t *commands, uint32_t command_length,
    uint8_t *output, uint32_t capacity);
/* Wrap an L2CAP payload in a first-fragment HCI ACL packet (PB=2).
 * Returns framed byte count, -22 for invalid fields, -90 for short output. */
int usb_bluetooth_l2cap_build_acl_packet(uint16_t handle, uint16_t cid,
                                         const uint8_t *payload,
                                         uint32_t payload_length,
                                         uint8_t *output, uint32_t capacity);
/* Assemble one ACL stream per connection handle. feed: 0 incomplete,
 * 1 complete (retained until take), negative errno-style code on failure.
 * An unexpected new start drops the stale partial PDU and returns -16.
 * take returns framed HCI ACL bytes; -90 preserves a completed packet if the
 * caller's output buffer is too small. Owner-CPU context only. */
void usb_bluetooth_acl_reassembly_reset(usb_bluetooth_acl_reassembly_t *state);
int usb_bluetooth_acl_reassembly_feed(usb_bluetooth_acl_reassembly_t *state,
                                      const uint8_t *packet, uint32_t length);
int usb_bluetooth_acl_reassembly_take(usb_bluetooth_acl_reassembly_t *state,
                                      uint8_t *packet, uint32_t capacity);
/* Iterate validated signaling commands: 1 command, 0 end, -22 malformed.
 * offset starts at zero and advances only after a valid command. */
int usb_bluetooth_l2cap_signaling_next(
    const uint8_t *payload, uint32_t length, uint32_t *offset,
    usb_bluetooth_l2cap_signal_command_t *command);
/* Build one signaling command; returns bytes written or -90 if too small. */
int usb_bluetooth_l2cap_signaling_build(uint8_t code, uint8_t identifier,
                                        const uint8_t *parameters,
                                        uint16_t parameter_length,
                                        uint8_t *output, uint32_t capacity);
/* Build standard fixed-shape BR/EDR L2CAP signaling commands. Return bytes
 * written, -22 for invalid fields, or -90 for insufficient output capacity. */
int usb_bluetooth_l2cap_build_connection_request(uint8_t identifier,
                                                  uint16_t psm,
                                                  uint16_t local_cid,
                                                  uint8_t *output,
                                                  uint32_t capacity);
int usb_bluetooth_l2cap_build_configuration_request(uint8_t identifier,
                                                     uint16_t remote_cid,
                                                     uint8_t *output,
                                                     uint32_t capacity);
int usb_bluetooth_l2cap_build_connection_response(
    uint8_t identifier, uint16_t destination_cid, uint16_t source_cid,
    uint16_t result, uint16_t status, uint8_t *output, uint32_t capacity);
int usb_bluetooth_l2cap_build_configuration_response(
    uint8_t identifier, uint16_t source_cid, uint16_t flags,
    uint16_t result, uint16_t mtu, uint8_t *output, uint32_t capacity);
int usb_bluetooth_l2cap_build_disconnection_request(uint8_t identifier,
                                                     uint16_t remote_cid,
                                                     uint16_t local_cid,
                                                     uint8_t *output,
                                                     uint32_t capacity);
/* Parse a complete reassembled ACL PDU and dispatch CID 0x0001 responses.
 * Non-signaling L2CAP channels are left to their own consumers (returns 0).
 */
int usb_bluetooth_l2cap_dispatch_acl_responses(
    usb_bluetooth_l2cap_table_t *table, const uint8_t *packet,
    uint32_t length);
/* Handle-bound variant: rejects ACL packets from any other connection. */
int usb_bluetooth_l2cap_dispatch_acl_responses_for_handle(
    usb_bluetooth_l2cap_table_t *table, uint16_t expected_handle,
    const uint8_t *packet, uint32_t length);
/* Dispatch a validated signaling list to supported channel responses.
 * Returns number of recognized response commands or a negative errno. */
int usb_bluetooth_l2cap_dispatch_responses(
    usb_bluetooth_l2cap_table_t *table, const uint8_t *payload,
    uint32_t length);
/* Validate a complete L2CAP signaling command list on CID 0x0001. */
int usb_bluetooth_l2cap_signaling_valid(const uint8_t *payload,
                                        uint32_t length);
/* Strict packet-framing checks for HCI event and ACL-IN transfers. */
int usb_bluetooth_hci_event_valid(const uint8_t *packet, uint32_t length);
int usb_bluetooth_hci_reset_status(const uint8_t *packet, uint32_t length);
int usb_bluetooth_hci_reset_complete(const uint8_t *packet, uint32_t length);
int usb_bluetooth_acl_packet_valid(const uint8_t *packet, uint32_t length);
/* True only after a successful HCI Reset and before any reset failure. */
int usb_bluetooth_transport_ready(int reset_complete, int reset_failed);
/* Apply reset completion; returns 1 new success, -1 new failure, 0 otherwise. */
int usb_bluetooth_update_reset_state(const uint8_t *packet, uint32_t length,
                                     uint8_t *reset_complete,
                                     uint8_t *reset_failed);
void usb_bluetooth_hci_event_queue_reset(
    usb_bluetooth_hci_event_queue_t *queue);
int usb_bluetooth_hci_event_queue_push(
    usb_bluetooth_hci_event_queue_t *queue, const uint8_t *event,
    uint32_t length);
int usb_bluetooth_hci_event_queue_pop(
    usb_bluetooth_hci_event_queue_t *queue, uint8_t *event,
    uint32_t capacity);
void usb_bluetooth_acl_queue_reset(usb_bluetooth_acl_queue_t *queue);
int usb_bluetooth_acl_queue_push(usb_bluetooth_acl_queue_t *queue,
                                 const uint8_t *packet, uint32_t length);
int usb_bluetooth_acl_queue_pop(usb_bluetooth_acl_queue_t *queue,
                                uint8_t *packet, uint32_t capacity);
/* Pop the oldest queued ACL packet for one connection handle. */
int usb_bluetooth_acl_queue_pop_handle(usb_bluetooth_acl_queue_t *queue,
                                       uint16_t handle, uint8_t *packet,
                                       uint32_t capacity);

/* Encode HCI command data-stage bytes: opcode LE16, parameter length,
 * then parameters. Returns encoded length, -22 for invalid input, -90 if
 * capacity is insufficient. Maximum encoded length is 258 bytes. */
int usb_bluetooth_hci_command_encode(uint16_t opcode,
                                     const uint8_t *parameters,
                                     uint8_t parameter_length,
                                     uint8_t *output, uint32_t capacity);
/* Send one HCI command through the USB class control pipe. Submission is
 * gated by controller-reported command credits and one outstanding opcode.
 * The caller must wait for the matching Command Complete/Status event before
 * retrying after a busy result. Owner-CPU context only. */
int usb_bluetooth_hci_command(uint32_t port, uint16_t opcode,
                              const uint8_t *parameters, uint8_t length);
/* Receive a queued complete HCI event; -11 empty, -90 too small (preserved),
 * -9 unbound. Events are queued independently of reset-state processing. */
int usb_bluetooth_hci_event_receive(uint32_t port, uint8_t *event,
                                    uint32_t capacity);
/* Receive one completed reassembled HCI ACL packet for a live BR/EDR
 * handle; -11 incomplete/empty, -90 output too small (preserved), -9 unknown. */
int usb_bluetooth_reassembled_acl_receive(uint32_t port, uint16_t handle,
                                          uint8_t *packet, uint32_t capacity);
/* Send one complete framed ACL packet through a bound device port.
 * Returns 1 only if xHCI reports a full transfer; caller-context limits are
 * documented in docs/USB_BLUETOOTH_BRINGUP.md. */
int usb_bluetooth_acl_send(uint32_t port, const uint8_t *packet,
                           uint32_t length);
/* Receive one queued raw ACL packet; -11 empty, -90 too small (not consumed),
 * -9 when no bound device exists. Owner-CPU context only. */
int usb_bluetooth_acl_receive(uint32_t port, uint8_t *packet,
                              uint32_t capacity);

#endif
