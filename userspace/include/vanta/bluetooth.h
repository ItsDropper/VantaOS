/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_USER_BLUETOOTH_H
#define VANTA_USER_BLUETOOTH_H

#include <usb_bluetooth.h>
#include <vanta/syscall.h>

/* Returns the number of currently bound USB Bluetooth HCI adapters. */
static inline long vanta_bluetooth_adapter_count(void)
{
    return vanta_syscall3(VANTA_SYS_BLUETOOTH_ADAPTER_COUNT, 0U, 0U, 0U);
}

/* Returns 1 and writes the USB port, 0 when index is out of range. */
static inline long vanta_bluetooth_adapter_port(uint32_t index,
                                                uint32_t *port)
{
    return vanta_syscall3(VANTA_SYS_BLUETOOTH_ADAPTER_PORT,
                          (uintptr_t)index, (uintptr_t)port, 0U);
}

/* Start a bounded Classic Bluetooth inquiry (length is in 1.28 s units). */
static inline long vanta_bluetooth_scan(uint32_t port)
{
    return vanta_syscall3(VANTA_SYS_BLUETOOTH_INQUIRY,
                          (uintptr_t)port, 0x08U, 0x10U);
}

static inline long vanta_bluetooth_cancel_scan(uint32_t port)
{
    return vanta_syscall3(VANTA_SYS_BLUETOOTH_INQUIRY_CANCEL,
                          (uintptr_t)port, 0U, 0U);
}

static inline long vanta_bluetooth_discovered_count(uint32_t port)
{
    return vanta_syscall3(VANTA_SYS_BLUETOOTH_DISCOVERED_COUNT,
                          (uintptr_t)port, 0U, 0U);
}

static inline long vanta_bluetooth_discovered_get(
    uint32_t port, uint32_t index, usb_bluetooth_inquiry_result_t *result)
{
    return vanta_syscall3(VANTA_SYS_BLUETOOTH_DISCOVERED_GET,
                          (uintptr_t)port, (uintptr_t)index,
                          (uintptr_t)result);
}

/* Connect to a selected inquiry result; no pairing/authentication is implied. */
static inline long vanta_bluetooth_connect(
    uint32_t port, const usb_bluetooth_inquiry_result_t *result)
{
    return vanta_syscall3(VANTA_SYS_BLUETOOTH_CONNECT,
                          (uintptr_t)port, (uintptr_t)result, 0U);
}

/* Returns 1 connected, 2 connecting, 0 idle/not connected, or negative errno. */
static inline long vanta_bluetooth_peer_state(
    uint32_t port, const uint8_t address[6], uint16_t *handle)
{
    return vanta_syscall3(VANTA_SYS_BLUETOOTH_PEER_STATE,
                          (uintptr_t)port, (uintptr_t)address,
                          (uintptr_t)handle);
}

static inline long vanta_bluetooth_cancel_connection(
    uint32_t port, const uint8_t address[6])
{
    return vanta_syscall3(VANTA_SYS_BLUETOOTH_CANCEL_CONNECTION,
                          (uintptr_t)port, (uintptr_t)address, 0U);
}

#endif
