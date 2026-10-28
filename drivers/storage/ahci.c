/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "ahci.h"

#include "ahci_pci.h"
#include "mmio.h"
#include "pci.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define AHCI_PCI_CLASS       0x01U
#define AHCI_PCI_SUBCLASS    0x06U
#define AHCI_PCI_PROG_IF     0x01U

#define AHCI_REG_CAP         0x00U
#define AHCI_REG_GHC         0x04U
#define AHCI_REG_PI          0x0CU
#define AHCI_REG_VS          0x10U

static int initialized;

static uint32_t ahci_mmio_read32(volatile uint8_t* registers, uint32_t offset)
{
    return *(volatile uint32_t*)(registers + offset);
}

/*
 * This deliberately only probes controllers whose PCI command register has
 * I/O decode, memory decode and bus mastering disabled. BAR sizing temporarily
 * writes all ones to the BAR, so it must never run against an active function.
 * This discovery stage does not reset the HBA or enable DMA.
 */
static int ahci_probe_inactive_pci_function(const struct pci_device* pci)
{
    uint16_t saved_command;
    uint64_t bar = 0U;
    uint64_t bar_size = 0U;
    int is_io = 0;
    int is_64bit = 0;
    int memory_enabled = 0;
    volatile uint8_t* registers = 0;
    int ok = 0;

    if (pci == 0)
        return 0;

    saved_command = pci_read_config16(pci, 0x04U);
    if (!ahci_pci_bar_sizing_allowed(saved_command))
        return 0;

    if (!pci_get_bar_size(pci, 5U, &bar_size) ||
        !pci_get_bar(pci, 5U, &bar, &is_io, &is_64bit) ||
        !ahci_pci_abar_valid(bar, bar_size, is_io, is_64bit) ||
        bar_size < AHCI_ABAR_MIN_SIZE)
        return 0;

    if (!pci_enable_memory_space(pci))
        goto cleanup;
    memory_enabled = 1;

    registers = (volatile uint8_t*)mmio_map_region(
        bar, (size_t)AHCI_ABAR_MIN_SIZE);
    if (registers == 0)
        goto cleanup;

    {
        uint32_t cap = ahci_mmio_read32(registers, AHCI_REG_CAP);
        uint32_t ports = ahci_mmio_read32(registers, AHCI_REG_PI);
        uint32_t version = ahci_mmio_read32(registers, AHCI_REG_VS);
        uint32_t port_count = (cap & 0x1FU) + 1U;
        uint32_t valid_ports = port_count == 32U ? UINT32_MAX :
                               ((1U << port_count) - 1U);

        if (cap == 0U || cap == UINT32_MAX || ports == UINT32_MAX ||
            version == 0U || version == UINT32_MAX ||
            (ports & ~valid_ports) != 0U)
            goto cleanup;

        /* GHC is read only here to confirm the mapped register block responds. */
        (void)ahci_mmio_read32(registers, AHCI_REG_GHC);
        ok = 1;
    }

cleanup:
    if (registers != 0) {
        if (!mmio_unmap_region((void*)registers,
                               (size_t)AHCI_ABAR_MIN_SIZE))
            ok = 0;
    }
    if (memory_enabled || pci_read_config16(pci, 0x04U) != saved_command) {
        if (!pci_write_config16(pci, 0x04U, saved_command) ||
            pci_read_config16(pci, 0x04U) != saved_command)
            ok = 0;
    }

    return ok;
}

void ahci_initialize(void)
{
    if (initialized)
        return;
    initialized = 1;

    int index = -1;
    int candidate_found = 0;
    while ((index = pci_find_class(AHCI_PCI_CLASS, AHCI_PCI_SUBCLASS,
                                   AHCI_PCI_PROG_IF, index + 1)) >= 0) {
        const struct pci_device* pci = pci_get_device(index);
        if (pci == 0)
            continue;
        candidate_found = 1;
        if (ahci_probe_idle_function(pci)) {
            serial_write("VANTAOS_AHCI_CONTROLLER_DISCOVERED_READ_ONLY\n");
        } else {
            serial_write("VANTAOS_AHCI_CONTROLLER_PROBE_REJECTED\n");
        }
    }

    if (!candidate_found)
        serial_write("VANTAOS_AHCI_CONTROLLER_NOT_FOUND\n");
}
