/*
 * Copyright (c) 2026 ItsDropper
 * Licensed under the MIT License. See LICENSE.
 */
#include "hda.h"
#include <stddef.h>
#include <stdint.h>

#define HDA_IC_REG_ICOI 0x60U
#define HDA_IC_REG_ICII 0x64U
#define HDA_IC_REG_ICIS 0x68U
#define HDA_ICIS_BUSY 0x01U
#define HDA_ICIS_VALID 0x02U
#define HDA_IMMEDIATE_WAIT_LIMIT 100000U

static hda_verb_status_t hda_wait_idle(const hda_verb_ops_t *ops,
                                      uint32_t *status)
{
 for(uint32_t spin=0U;spin<HDA_IMMEDIATE_WAIT_LIMIT;++spin)
 {
  uint32_t value=ops->read16(ops->context,HDA_IC_REG_ICIS);
  if(value==UINT16_MAX)return HDA_VERB_REGISTER_FAULT;
  if((value&HDA_ICIS_BUSY)==0U){*status=value;return HDA_VERB_OK;}
  __asm__ volatile("pause");
 }
 return HDA_VERB_CONTROLLER_BUSY_TIMEOUT;
}

hda_verb_status_t hda_codec_get_parameter(const hda_verb_ops_t *ops,
                                           uint8_t codec_address,
                                           uint8_t parameter,
                                           uint32_t *response)
{
 if(!ops||!ops->read16||!ops->write16||!ops->read32||!ops->write32||!response||codec_address>15U)
  return HDA_VERB_INVALID_ARGUMENT;
 uint32_t status=0U;
 hda_verb_status_t idle_status=hda_wait_idle(ops,&status);
 if(idle_status!=HDA_VERB_OK)return idle_status;
 if((status&HDA_ICIS_VALID)!=0U)
  ops->write16(ops->context,HDA_IC_REG_ICIS,HDA_ICIS_VALID);
 uint32_t command=((uint32_t)codec_address<<28U)|(0xF00U<<8U)|parameter;
 ops->write32(ops->context,HDA_IC_REG_ICOI,command);
 ops->write16(ops->context,HDA_IC_REG_ICIS,HDA_ICIS_BUSY);
 for(uint32_t spin=0U;spin<HDA_IMMEDIATE_WAIT_LIMIT;++spin)
 {
  status=ops->read16(ops->context,HDA_IC_REG_ICIS);
  if(status==UINT16_MAX)return HDA_VERB_REGISTER_FAULT;
  if((status&HDA_ICIS_BUSY)==0U&&(status&HDA_ICIS_VALID)!=0U)
  {
   uint32_t value=ops->read32(ops->context,HDA_IC_REG_ICII);
   ops->write32(ops->context,HDA_IC_REG_ICIS,HDA_ICIS_VALID);
   if(value==UINT32_MAX)return HDA_VERB_REGISTER_FAULT;
   *response=value;
   return HDA_VERB_OK;
  }
  __asm__ volatile("pause");
 }
 return HDA_VERB_RESPONSE_TIMEOUT;
}

hda_verb_status_t hda_query_codecs(const hda_verb_ops_t *ops,
                                    uint16_t codec_presence,
                                    hda_controller_info_t *info)
{
 if(!ops||!info)return HDA_VERB_INVALID_ARGUMENT;
 info->codec_query_mask=0U;
 for(uint32_t i=0U;i<16U;++i)
 {
  info->codec_vendor_id[i]=0U;
  info->codec_revision_id[i]=0U;
 }
 for(uint8_t codec=0U;codec<16U;++codec)
 {
  if((codec_presence&(uint16_t)(1U<<codec))==0U)continue;
  uint32_t vendor=0U,revision=0U;
  hda_verb_status_t status=hda_codec_get_parameter(ops,codec,0x00U,&vendor);
  if(status!=HDA_VERB_OK)return status;
  if(vendor==0U||vendor==UINT32_MAX)continue;
  info->codec_vendor_id[codec]=vendor;
  info->codec_query_mask|=(uint16_t)(1U<<codec);
  status=hda_codec_get_parameter(ops,codec,0x02U,&revision);
  if(status!=HDA_VERB_OK)return status;
  info->codec_revision_id[codec]=revision;
 }
 return HDA_VERB_OK;
}
