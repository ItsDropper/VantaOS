/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "usb_init.h"

#include "xhci.h"

/* USB transport/controller bring-up belongs to the USB subsystem. */
void usb_host_controller_initialize(void)
{
    xhci_initialize();
}
