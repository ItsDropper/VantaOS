/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef USB_HID_H
#define USB_HID_H
#include <stdint.h>

/* Handles are one-based slot IDs returned by attach(). Reports must be
 * complete HID boot-protocol reports from an already-enumerated interface. */
int usb_hid_keyboard_attach(uint32_t *handle_out);
void usb_hid_keyboard_detach(uint32_t handle);
void usb_hid_keyboard_report(uint32_t handle, const uint8_t *report, uint32_t length);
int usb_hid_mouse_attach(uint32_t *handle_out);
void usb_hid_mouse_detach(uint32_t handle);
void usb_hid_mouse_report(uint32_t handle, const uint8_t *report, uint32_t length);

#endif
