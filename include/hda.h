/*
 * Copyright (c) 2026 ItsDropper
 * Licensed under the MIT License. See LICENSE.
 */
#ifndef VANTA_HDA_H
#define VANTA_HDA_H
#include <stdint.h>
#include "pci.h"
typedef enum {
 HDA_PROBE_OK=0, HDA_PROBE_INVALID_ARGUMENT, HDA_PROBE_UNSUPPORTED_FUNCTION,
 HDA_PROBE_DECODE_DISABLED, HDA_PROBE_INVALID_BAR, HDA_PROBE_MAPPING_FAILED,
 HDA_PROBE_INVALID_REGISTERS, HDA_PROBE_UNMAP_FAILED
} hda_probe_status_t;
typedef struct {
 uint64_t register_base;
 uint16_t capabilities;
 uint16_t codec_presence;
 uint8_t version_major, version_minor, controller_in_reset;
} hda_controller_info_t;
/* Passive inventory only: no PCI writes, controller reset, register writes or DMA. */
hda_probe_status_t hda_probe_controller(const struct pci_device *device,
                                         hda_controller_info_t *out_info);
void hda_initialize(void);
#endif
