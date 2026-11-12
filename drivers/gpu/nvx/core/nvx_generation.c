/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_generation.h"

static const uint16_t ga10x_ids[] = {0x2503U,0x2504U,0x2507U,0x2508U,0x2509U,0x2544U,0x2548U};
static const char *const ga10x_chips[] = {"ga102","ga103","ga104","ga106","ga107"};
static const char *const ga10x_components[] = {"gsp","ucodes","booter_load","booter_unload","bootloader"};
static const uint16_t mx130_ids[] = {0x174DU};
static const uint16_t mx150_ids[] = {0x1D10U,0x1D12U};

static const nvx_generation_descriptor_t ga10x = {
 .family=NVX_FAMILY_AMPERE,.profile=NVX_PROFILE_GEFORCE_RTX_3060,
 .pci_device_ids=ga10x_ids,.pci_device_id_count=sizeof(ga10x_ids)/sizeof(ga10x_ids[0]),
 .firmware_chips=ga10x_chips,.firmware_chip_count=sizeof(ga10x_chips)/sizeof(ga10x_chips[0]),
 .firmware_directory="nvidia",.firmware_components=ga10x_components,
 .firmware_component_count=sizeof(ga10x_components)/sizeof(ga10x_components[0]),
 .boot_sequence=NVX_BOOT_SEQUENCE_GA10X_SEC2_GSP,
 .pmc_boot_0_offset=0U,.pmc_boot_1_offset=4U,.pmc_boot_42_offset=0xA00U,
 .gsp_mailbox_base=0x110804U,.gsp_queue_head_base=0x110C00U,.gsp_queue_tail_base=0x110C04U
};

/* These profiles reuse NVX PCI/resource/MMIO/identity code, never GA10x GSP. */
static const nvx_generation_descriptor_t mx130 = {
 .family=NVX_FAMILY_MAXWELL,.profile=NVX_PROFILE_GEFORCE_MX130,
 .pci_device_ids=mx130_ids,.pci_device_id_count=sizeof(mx130_ids)/sizeof(mx130_ids[0]),
 .boot_sequence=NVX_BOOT_SEQUENCE_UNSUPPORTED,
 .pmc_boot_0_offset=0U,.pmc_boot_1_offset=4U,.pmc_boot_42_offset=UINT32_MAX
};
static const nvx_generation_descriptor_t mx150 = {
 .family=NVX_FAMILY_PASCAL,.profile=NVX_PROFILE_GEFORCE_MX150,
 .pci_device_ids=mx150_ids,.pci_device_id_count=sizeof(mx150_ids)/sizeof(mx150_ids[0]),
 .boot_sequence=NVX_BOOT_SEQUENCE_UNSUPPORTED,
 .pmc_boot_0_offset=0U,.pmc_boot_1_offset=4U,.pmc_boot_42_offset=UINT32_MAX
};
static const nvx_generation_descriptor_t *const generations[]={&ga10x,&mx130,&mx150};

const nvx_generation_descriptor_t *nvx_generation_for_pci(uint16_t id)
{
 for(size_t g=0U;g<sizeof(generations)/sizeof(generations[0]);++g)
  for(size_t i=0U;i<generations[g]->pci_device_id_count;++i)
   if(generations[g]->pci_device_ids[i]==id)return generations[g];
 return 0;
}
const nvx_generation_descriptor_t *nvx_generation_for_firmware_chip(const char *chip)
{
 if(!chip||!chip[0])return 0;
 for(size_t i=0U;i<ga10x.firmware_chip_count;++i){
  size_t j=0U;
  while(chip[j]&&ga10x.firmware_chips[i][j]==chip[j])++j;
  if(!chip[j]&&!ga10x.firmware_chips[i][j])return &ga10x;
 }
 return 0;
}
int nvx_generation_supports_firmware_chip(const char *chip)
{
 return nvx_generation_for_firmware_chip(chip)!=0;
}
