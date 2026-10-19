/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_IRQ_CAPS_H
#define VANTA_NVX_IRQ_CAPS_H

#include <stdint.h>
#include "nvx_resource.h"

typedef enum nvx_irq_caps_status {
    NVX_IRQ_CAPS_OK = 0,
    NVX_IRQ_CAPS_INVALID_ARGUMENT,
    NVX_IRQ_CAPS_MALFORMED_MSI,
    NVX_IRQ_CAPS_MALFORMED_MSIX,
    NVX_IRQ_CAPS_INVALID_BAR,
    NVX_IRQ_CAPS_RANGE_INVALID
} nvx_irq_caps_status_t;

typedef struct nvx_msi_capability {
    uint8_t present;
    uint8_t enabled;
    uint8_t supports_64bit_address;
    uint8_t per_vector_masking;
    uint8_t max_messages_log2;
    uint8_t multiple_message_enable_log2;
    uint8_t capability_offset;
} nvx_msi_capability_t;

typedef struct nvx_msix_capability {
    uint8_t present;
    uint8_t enabled;
    uint8_t function_masked;
    uint8_t table_bar_index;
    uint8_t pba_bar_index;
    uint8_t capability_offset;
    uint16_t vector_count;
    uint32_t table_offset;
    uint32_t pba_offset;
    uint64_t table_bytes;
    uint64_t pba_bytes;
} nvx_msix_capability_t;

typedef struct nvx_irq_capabilities {
    nvx_msi_capability_t msi;
    nvx_msix_capability_t msix;
} nvx_irq_capabilities_t;

/*
 * Read and validate interrupt capability metadata only. This never enables
 * MSI/MSI-X, writes the PCI command register, maps the table/PBA, or installs
 * an interrupt handler. MSI-X range checks require trusted BAR size metadata.
 */
nvx_irq_caps_status_t nvx_irq_read_capabilities(
    const struct pci_device *device,
    const nvx_bar_resource_t bars[6],
    nvx_irq_capabilities_t *out_caps);

#endif
