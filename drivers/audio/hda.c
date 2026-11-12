/*
 * Copyright (c) 2026 ItsDropper
 * Licensed under the MIT License. See LICENSE.
 */
#include "hda.h"
#include <stddef.h>
#include <stdint.h>
#include "mmio.h"
#define HDA_REGISTER_WINDOW 0x1000U
#define HDA_PCI_COMMAND_MEMORY 0x0002U
#define HDA_REG_GCAP 0x00U
#define HDA_REG_VMIN 0x02U
#define HDA_REG_VMAJ 0x03U
#define HDA_REG_GCTL 0x08U
#define HDA_REG_STATESTS 0x0EU
#define HDA_GCTL_CRST 0x00000001U
static uint16_t hda_mmio_read16(void *context,uint32_t offset)
{
 volatile uint8_t *regs=(volatile uint8_t *)context;
 return *(volatile uint16_t *)(regs+offset);
}
static void hda_mmio_write16(void *context,uint32_t offset,uint16_t value)
{
 volatile uint8_t *regs=(volatile uint8_t *)context;
 *(volatile uint16_t *)(regs+offset)=value;
 __asm__ volatile("" ::: "memory");
}
static uint32_t hda_mmio_read32(void *context,uint32_t offset)
{
 volatile uint8_t *regs=(volatile uint8_t *)context;
 return *(volatile uint32_t *)(regs+offset);
}
static void hda_mmio_write32(void *context,uint32_t offset,uint32_t value)
{
 volatile uint8_t *regs=(volatile uint8_t *)context;
 *(volatile uint32_t *)(regs+offset)=value;
 __asm__ volatile("" ::: "memory");
}
static void clear_info(hda_controller_info_t *info)
{
 uint8_t *p=(uint8_t *)info;
 for(size_t i=0;i<sizeof(*info);++i) p[i]=0U;
}
hda_probe_status_t hda_probe_controller(const struct pci_device *device,
                                         hda_controller_info_t *out_info)
{
 uint64_t base=0U; int is_io=0, is_64bit=0; uint16_t command;
 volatile uint8_t *regs; hda_controller_info_t info;
 if(!out_info) return HDA_PROBE_INVALID_ARGUMENT;
 clear_info(out_info);
 if(!device) return HDA_PROBE_INVALID_ARGUMENT;
 if(device->class_code!=0x04U || device->subclass!=0x03U ||
    device->prog_if!=0x00U) return HDA_PROBE_UNSUPPORTED_FUNCTION;
 command=pci_read_config16(device,0x04U);
 if(command==0xFFFFU) return HDA_PROBE_INVALID_BAR;
 /* Never change firmware decode state or disturb another audio driver. */
 if((command&HDA_PCI_COMMAND_MEMORY)==0U) return HDA_PROBE_DECODE_DISABLED;
 if(!pci_get_bar(device,0U,&base,&is_io,&is_64bit) || is_io!=0 ||
    base==0U || (base&0xFFFU)!=0U ||
    base>UINT64_MAX-(HDA_REGISTER_WINDOW-1U))
  return HDA_PROBE_INVALID_BAR;
 regs=(volatile uint8_t *)mmio_map_region(base,HDA_REGISTER_WINDOW);
 if(!regs) return HDA_PROBE_MAPPING_FAILED;
 clear_info(&info);
 info.capabilities=*(volatile uint16_t *)(regs+HDA_REG_GCAP);
 info.version_minor=regs[HDA_REG_VMIN];
 info.version_major=regs[HDA_REG_VMAJ];
 {
  uint32_t gctl=*(volatile uint32_t *)(regs+HDA_REG_GCTL);
  info.controller_in_reset=(uint8_t)((gctl&HDA_GCTL_CRST)==0U);
  info.codec_presence=*(volatile uint16_t *)(regs+HDA_REG_STATESTS);
  info.register_base=base;
  if(info.capabilities==0xFFFFU || info.version_minor==0xFFU ||
     info.version_major==0xFFU || gctl==0xFFFFFFFFU) {
   if(!mmio_unmap_region((void *)regs,HDA_REGISTER_WINDOW))
    return HDA_PROBE_UNMAP_FAILED;
   return HDA_PROBE_INVALID_REGISTERS;
  }
 }
 if(!mmio_unmap_region((void *)regs,HDA_REGISTER_WINDOW))
  return HDA_PROBE_UNMAP_FAILED;
 *out_info=info;
 (void)is_64bit;
 return HDA_PROBE_OK;
}

hda_probe_status_t hda_query_controller_codecs(const struct pci_device *device,
                                                hda_controller_info_t *info)
{
 uint64_t base=0U;int is_io=0,is_64bit=0;uint16_t command;
 volatile uint8_t *regs;hda_controller_info_t candidate;
 hda_verb_status_t verb_status;
 if(!device||!info||info->register_base==0U)
  return HDA_PROBE_INVALID_ARGUMENT;
 if(device->class_code!=0x04U||device->subclass!=0x03U||
    device->prog_if!=0x00U)
  return HDA_PROBE_UNSUPPORTED_FUNCTION;
 if(info->controller_in_reset||info->codec_presence==0U)
  return HDA_PROBE_OK;
 command=pci_read_config16(device,0x04U);
 if(command==0xFFFFU) return HDA_PROBE_INVALID_BAR;
 if((command&HDA_PCI_COMMAND_MEMORY)==0U)
  return HDA_PROBE_DECODE_DISABLED;
 if(!pci_get_bar(device,0U,&base,&is_io,&is_64bit)||is_io!=0||
    base==0U||(base&0xFFFU)!=0U||base!=info->register_base||
    base>UINT64_MAX-(HDA_REGISTER_WINDOW-1U))
  return HDA_PROBE_INVALID_BAR;
 (void)is_64bit;
 regs=(volatile uint8_t *)mmio_map_region(base,HDA_REGISTER_WINDOW);
 if(!regs) return HDA_PROBE_MAPPING_FAILED;
 uint32_t gctl=*(volatile uint32_t *)(regs+HDA_REG_GCTL);
 uint16_t presence=*(volatile uint16_t *)(regs+HDA_REG_STATESTS);
 if(gctl==UINT32_MAX)
 {
  int unmap_ok=mmio_unmap_region((void *)regs,HDA_REGISTER_WINDOW);
  return unmap_ok?HDA_PROBE_INVALID_REGISTERS:HDA_PROBE_UNMAP_FAILED;
 }
 if((gctl&HDA_GCTL_CRST)==0U||presence==0U)
 {
  if(!mmio_unmap_region((void *)regs,HDA_REGISTER_WINDOW))
   return HDA_PROBE_UNMAP_FAILED;
  info->controller_in_reset=(uint8_t)((gctl&HDA_GCTL_CRST)==0U);
  info->codec_presence=presence;
  info->codec_query_mask=0U;
  for(uint32_t i=0U;i<16U;++i)
  {
   info->codec_vendor_id[i]=0U;
   info->codec_revision_id[i]=0U;
  }
  return HDA_PROBE_OK;
 }
 candidate=*info;
 candidate.codec_presence=presence;
 const hda_verb_ops_t ops={
  .context=(void *)regs,.read16=hda_mmio_read16,.write16=hda_mmio_write16,
  .read32=hda_mmio_read32,.write32=hda_mmio_write32
 };
 verb_status=hda_query_codecs(&ops,presence,&candidate);
 if(!mmio_unmap_region((void *)regs,HDA_REGISTER_WINDOW))
  return HDA_PROBE_UNMAP_FAILED;
 *info=candidate;
 return verb_status==HDA_VERB_OK?HDA_PROBE_OK:HDA_PROBE_QUERY_FAILED;
}
