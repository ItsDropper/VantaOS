/*
 * Copyright (c) 2026 ItsDropper
 * Licensed under the MIT License. See LICENSE.
 */
#include "hda.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HDA_IC_REG_ICOI 0x60U
#define HDA_IC_REG_ICII 0x64U
#define HDA_IC_REG_ICIS 0x68U
#define HDA_ICIS_BUSY 0x01U
#define HDA_ICIS_VALID 0x02U

typedef struct
{
 uint32_t status,response,command,reads,writes,busy_reads;
 uint8_t no_valid,read_fault,response_fault;
} fake_hda_t;

static uint16_t fake_read16(void *context,uint32_t offset)
{
 fake_hda_t *f=(fake_hda_t *)context;
 ++f->reads;
 if(offset!=HDA_IC_REG_ICIS)return UINT16_MAX;
 if(f->read_fault)return UINT16_MAX;
 if(f->busy_reads){--f->busy_reads;return HDA_ICIS_BUSY;}
 return (uint16_t)f->status;
}
static void fake_write16(void *context,uint32_t offset,uint16_t value)
{
 fake_hda_t *f=(fake_hda_t *)context;
 ++f->writes;
 if(offset!=HDA_IC_REG_ICIS)return;
 if(value==HDA_ICIS_VALID)f->status&=~HDA_ICIS_VALID;
 else if(value==HDA_ICIS_BUSY)f->status=f->no_valid?0U:HDA_ICIS_VALID;
}
static uint32_t fake_read32(void *context,uint32_t offset)
{
 fake_hda_t *f=(fake_hda_t *)context;
 ++f->reads;
 if(offset!=HDA_IC_REG_ICII||f->response_fault)return UINT32_MAX;
 uint8_t codec=(uint8_t)(f->command>>28U);
 uint8_t parameter=(uint8_t)f->command;
 return parameter==0U?0x10EC0256U+codec:0x00100100U+codec;
}
static void fake_write32(void *context,uint32_t offset,uint32_t value)
{
 fake_hda_t *f=(fake_hda_t *)context;
 ++f->writes;
 if(offset==HDA_IC_REG_ICOI)f->command=value;
}

static void test_single_codec_query(void)
{
 fake_hda_t f={.status=HDA_ICIS_VALID};
 hda_verb_ops_t ops={.context=&f,.read16=fake_read16,.write16=fake_write16,
 .read32=fake_read32,.write32=fake_write32};
 uint32_t response=0U;
 assert(hda_codec_get_parameter(&ops,3U,0U,&response)==HDA_VERB_OK);
 assert(response==0x10EC0259U&&f.command==0x300F0000U);
 assert(f.writes==4U&&(f.status&HDA_ICIS_VALID)==0U);
}
static void test_multi_codec_inventory(void)
{
 fake_hda_t f={0};
 hda_verb_ops_t ops={.context=&f,.read16=fake_read16,.write16=fake_write16,
 .read32=fake_read32,.write32=fake_write32};
 hda_controller_info_t info={0};
 assert(hda_query_codecs(&ops,0x0005U,&info)==HDA_VERB_OK);
 assert(info.codec_query_mask==0x0005U);
 assert(info.codec_vendor_id[0]==0x10EC0256U);
 assert(info.codec_vendor_id[2]==0x10EC0258U);
 assert(info.codec_revision_id[0]==0x00100100U);
 assert(info.codec_revision_id[2]==0x00100102U);
 assert(f.writes==12U);
}
static void test_bounded_failures(void)
{
 fake_hda_t f={.status=HDA_ICIS_BUSY,.busy_reads=100001U};
 hda_verb_ops_t ops={.context=&f,.read16=fake_read16,.write16=fake_write16,
 .read32=fake_read32,.write32=fake_write32};
 uint32_t response=0xA5A5A5A5U;
 assert(hda_codec_get_parameter(&ops,0U,0U,&response)==
        HDA_VERB_CONTROLLER_BUSY_TIMEOUT);
 assert(f.writes==0U&&response==0xA5A5A5A5U);
 memset(&f,0,sizeof(f));f.no_valid=1U;ops.context=&f;
 assert(hda_codec_get_parameter(&ops,0U,2U,&response)==
        HDA_VERB_RESPONSE_TIMEOUT);
 memset(&f,0,sizeof(f));f.read_fault=1U;ops.context=&f;
 assert(hda_codec_get_parameter(&ops,0U,0U,&response)==
        HDA_VERB_REGISTER_FAULT);
 assert(hda_codec_get_parameter(NULL,0U,0U,&response)==
        HDA_VERB_INVALID_ARGUMENT);
 assert(hda_codec_get_parameter(&ops,16U,0U,&response)==
        HDA_VERB_INVALID_ARGUMENT);
}
int main(void)
{
 test_single_codec_query();
 test_multi_codec_inventory();
 test_bounded_failures();
 puts("HDA bounded immediate codec-query tests passed");
 return 0;
}
