/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef USB_H
#define USB_H

/* Initialize root-port USB HID boot-protocol devices after input_initialize(). */
void usb_initialize(void);

/* Poll xHCI completions and deliver ready HID reports. */
void usb_poll(void);

#endif
