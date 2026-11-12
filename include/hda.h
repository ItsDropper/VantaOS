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
 HDA_PROBE_INVALID_REGISTERS, HDA_PROBE_UNMAP_FAILED,
 HDA_PROBE_QUERY_FAILED
} hda_probe_status_t;
typedef struct {
 uint64_t register_base;
 uint16_t capabilities;
 uint16_t codec_presence;
 uint16_t codec_query_mask;
 uint8_t version_major, version_minor, controller_in_reset;
 uint32_t codec_vendor_id[16];
 uint32_t codec_revision_id[16];
} hda_controller_info_t;

typedef struct {
 void *context;
 uint16_t (*read16)(void *context,uint32_t offset);
 void (*write16)(void *context,uint32_t offset,uint16_t value);
 uint32_t (*read32)(void *context,uint32_t offset);
 void (*write32)(void *context,uint32_t offset,uint32_t value);
} hda_verb_ops_t;

typedef enum {
 HDA_VERB_OK=0,
 HDA_VERB_INVALID_ARGUMENT,
 HDA_VERB_CONTROLLER_BUSY_TIMEOUT,
 HDA_VERB_RESPONSE_TIMEOUT,
 HDA_VERB_REGISTER_FAULT
} hda_verb_status_t;

/* Passive inventory only: no PCI writes, controller reset, register writes or DMA. */
hda_probe_status_t hda_probe_controller(const struct pci_device *device,
                                         hda_controller_info_t *out_info);
/* Bounded GET_PARAMETER only; the controller must already be active. */
hda_probe_status_t hda_query_controller_codecs(const struct pci_device *device,
                                                hda_controller_info_t *info);
hda_verb_status_t hda_codec_get_parameter(const hda_verb_ops_t *ops,
                                           uint8_t codec_address,
                                           uint8_t parameter,
                                           uint32_t *response);
hda_verb_status_t hda_query_codecs(const hda_verb_ops_t *ops,
                                    uint16_t codec_presence,
                                    hda_controller_info_t *info);
void hda_initialize(void);
#endif
