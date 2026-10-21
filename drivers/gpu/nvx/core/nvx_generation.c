/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_generation.h"
static const uint16_t ids[] = {0x2503U,0x2504U,0x2507U,0x2508U,0x2509U,0x2544U,0x2548U};
static const char *const chips[] = {"ga102","ga103","ga104","ga106","ga107"};
static const char *const components[] = {"gsp","ucodes","booter_load","booter_unload","bootloader"};
static const nvx_generation_descriptor_t ga10x = {
 .family=NVX_FAMILY_AMPERE,.profile=NVX_PROFILE_GEFORCE_RTX_3060,
 .pci_device_ids=ids,.pci_device_id_count=sizeof(ids)/sizeof(ids[0]),
 .firmware_chips=chips,.firmware_chip_count=sizeof(chips)/sizeof(chips[0]),
 .firmware_directory="nvidia",.firmware_components=components,
 .firmware_component_count=sizeof(components)/sizeof(components[0]),
 .boot_sequence=NVX_BOOT_SEQUENCE_GA10X_SEC2_GSP,
 .pmc_boot_0_offset=0x0000U,.pmc_boot_1_offset=0x0004U,
 .pmc_boot_42_offset=0x0A00U,.gsp_mailbox_base=0x110804U,
 .gsp_queue_head_base=0x110C00U,.gsp_queue_tail_base=0x110C04U
};
const nvx_generation_descriptor_t *nvx_generation_for_pci(uint16_t id)
{
 for(size_t i=0U;i<ga10x.pci_device_id_count;++i)
  if(ga10x.pci_device_ids[i]==id)return &ga10x;
 return 0;
}
int nvx_generation_supports_firmware_chip(const char *chip)
{
 if(!chip||!chip[0])return 0;
 for(size_t i=0U;i<ga10x.firmware_chip_count;++i){
  size_t j=0U;
  while(chip[j]&&ga10x.firmware_chips[i][j]==chip[j])++j;
  if(!chip[j]&&!ga10x.firmware_chips[i][j])return 1;
 }
 return 0;
}
