/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef XHCI_H
#define XHCI_H

#include <stdint.h>
#include "device.h"

/* Initialize the first usable PCI xHCI host controller. Safe to call once. */
void xhci_initialize(void);
int xhci_submit_command(uint32_t type, uint64_t parameter,
                        uint32_t status, uint32_t control_flags,
                        uint32_t* completion_code, uint32_t* slot_id);
uint32_t xhci_port_count(void);
uint32_t xhci_port_status(uint32_t port);
int xhci_reset_port(uint32_t port);
void xhci_ack_port_changes(uint32_t port);
uint32_t xhci_port_speed(uint32_t port);
int xhci_enable_device(uint32_t port, uint32_t speed, uint16_t ep0_packet,
                       uint32_t* slot_id_out);
int xhci_disable_device(uint32_t slot_id);
int xhci_set_ep0_packet(uint32_t slot_id, uint16_t packet_size);
int xhci_control_transfer(uint32_t slot_id, const uint8_t setup[8],
                          void* data, uint16_t length);
int xhci_configure_bulk_endpoint(uint32_t slot_id, uint8_t endpoint_address,
                                 uint16_t packet_size);
int xhci_bulk_transfer(uint32_t slot_id, uint8_t endpoint_address,
                       void* buffer, uint32_t length, uint32_t* actual_length);
/* Nonblocking bulk-IN receive: submit, then retrieve completed packets.
 * take returns 1 for a packet, 0 while pending, -1 for a failed/invalid
 * transfer, or -2 when capacity is too small. On -2, actual_length is the
 * required size and the completed packet remains available for retry. */
int xhci_start_bulk_in(uint32_t slot_id, uint8_t endpoint_address);
int xhci_take_bulk_in(uint32_t slot_id, uint8_t endpoint_address,
                      void* buffer, uint32_t capacity, uint32_t* actual_length);
int xhci_configure_interrupt_in(uint32_t slot_id, uint8_t endpoint_address,
                                uint16_t packet_size, uint8_t interval);
int xhci_start_interrupt_in(uint32_t slot_id);
void xhci_poll(void);
/* take returns 1 for a report, 0 when no report is ready, -1 for invalid
 * arguments/state, or -2 when capacity is too small. On -2, length_out is
 * the required size and the pending report is not consumed. */
int xhci_take_interrupt_report(uint32_t slot_id, uint8_t* output,
                               uint32_t capacity, uint32_t* length_out);

/* True after controller reset, rings, and Run/Stop have completed. */
int xhci_controller_online(void);
/* Generic device node for the active host controller, if any. */
device_t* xhci_controller_device(void);

/* Bit N is set when a port-status-change event was observed for port N. */
unsigned int xhci_take_port_events(void);

#endif
