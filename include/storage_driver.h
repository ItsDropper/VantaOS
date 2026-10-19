/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef STORAGE_DRIVER_H
#define STORAGE_DRIVER_H
#include "pci.h"
#include <stdint.h>
#define STORAGE_DRIVER_NAME_MAX 32U
#define STORAGE_DRIVER_MAX 16U
typedef enum { STORAGE_PROBE_NO_MATCH=0, STORAGE_PROBE_BOUND=1, STORAGE_PROBE_FAILED=-1 } storage_probe_result_t;
typedef struct {
 uint16_t vendor_id; /* 0xFFFF = wildcard */
 uint16_t device_id; /* 0xFFFF = wildcard */
 uint8_t class_code; /* 0xFF = wildcard */
 uint8_t subclass; /* 0xFF = wildcard */
 uint8_t prog_if; /* 0xFF = wildcard */
} storage_pci_id_t;
typedef struct storage_driver {
 const char* name;
 const storage_pci_id_t* ids;
 uint32_t id_count;
 storage_probe_result_t (*probe)(const struct pci_device*);
} storage_driver_t;
int storage_driver_register(const storage_driver_t*);
uint32_t storage_driver_count(void);
/* Call after pci_initialize(). Probe callbacks run without the registry lock. */
uint32_t storage_probe_pci_devices(void);
const char* storage_driver_bound_name(const struct pci_device*);
#endif
