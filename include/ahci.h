/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef AHCI_H
#define AHCI_H

/* Safely identify AHCI PCI functions with decoding and bus mastering disabled; this does not start the HBA or do I/O. */
void ahci_initialize(void);

#endif
