/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef USB_INIT_H
#define USB_INIT_H

/* Initialize the USB host-controller layer after PCI and memory setup. */
void usb_host_controller_initialize(void);

#endif
