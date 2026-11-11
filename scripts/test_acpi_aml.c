/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "acpi_aml.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static acpi_aml_device_t captured;
static uint32_t callbacks;
static void capture(const acpi_aml_device_t* device, void* context)
{
    (void)context;
    captured = *device;
    ++callbacks;
}
static void append(uint8_t* out, uint32_t* length,
                   const uint8_t* bytes, uint32_t count)
{
    memcpy(out + *length, bytes, count);
    *length += count;
}
int main(void)
{
    uint8_t aml[512];
    uint32_t n = 0U;
    /* Scope(\\_SB_) { Device(TP0) { static IDs and a Buffer(_CRS). } } */
    const uint8_t scope_prefix[] = {0x10U,0x2BU,0x5CU,'_','S','B','_'};
    const uint8_t device_prefix[] = {0x5BU,0x82U,0x22U,'T','P','0','_'};
    const uint8_t hid[] = {0x08U,'_','H','I','D',0x0DU,
        'P','N','P','0','C','5','0',0x00U};
    const uint8_t cid[] = {0x08U,'_','C','I','D',0x12U,0x08U,0x02U,
        0x0DU,'A','C','P','I','0','0','1',0x00U,
        0x0DU,'P','N','P','0','C','5','0',0x00U};
    const uint8_t uid[] = {0x08U,'_','U','I','D',0x0AU,0x03U};
    const uint8_t sta[] = {0x08U,'_','S','T','A',0x0AU,0x0FU};
    const uint8_t crs[] = {
        0x08U,'_','C','R','S',0x11U,0x1AU,0x0EU,0x19U,
        0x8EU,0x0FU,0x00U,0x01U,0x00U,0x01U,0x00U,0x00U,
        0x01U,0x06U,0x00U,0xA0U,0x86U,0x01U,0x00U,
        0x15U,0x00U,'\\','_','S','B','_',0x79U,0x00U
    };
    const uint8_t device_suffix[] = {0xA3U};
    const uint8_t scope_suffix[] = {0xA3U};

    append(aml,&n,scope_prefix,sizeof(scope_prefix));
    append(aml,&n,device_prefix,sizeof(device_prefix));
    append(aml,&n,hid,sizeof(hid));
    append(aml,&n,cid,sizeof(cid));
    append(aml,&n,uid,sizeof(uid));
    append(aml,&n,sta,sizeof(sta));
    append(aml,&n,crs,sizeof(crs));
    append(aml,&n,device_suffix,sizeof(device_suffix));
    append(aml,&n,scope_suffix,sizeof(scope_suffix));

    /* Package lengths are encoded above for the fixed test payload. */
    callbacks=0U;
    uint32_t found=0U;
    assert(acpi_aml_parse_table(aml,n,capture,NULL,&found));
    assert(found==1U && callbacks==1U);
    assert(strcmp(captured.path,"\\_SB_.TP0_")==0);
    assert(strcmp(captured.hid,"PNP0C50")==0);
    assert(captured.cid_count==2U);
    assert(strcmp(captured.cids[0],"ACPI001")==0);
    assert(captured.uid_valid && captured.uid==3U);
    assert(captured.sta_valid && captured.sta==0x0FU);
    assert(captured.i2c_count==1U);
    assert(captured.resources[0].serial_bus_type==1U);
    assert(captured.resources[0].connection_speed_hz==100000U);
    assert(captured.resources[0].slave_address==0x15U);
    assert(strcmp(captured.resources[0].resource_source,"\\_SB_")==0);

    /* A malformed package length must fail closed without an out-of-bounds read. */
    const uint8_t malformed[]={0x5BU,0x82U,0x7FU,'T','E','S','T'};
    assert(!acpi_aml_parse_table(malformed,sizeof(malformed),NULL,NULL,&found));
    assert(found==0U);
    assert(!acpi_aml_parse_table(NULL,1U,NULL,NULL,&found));
    assert(!acpi_aml_parse_table(aml,0U,NULL,NULL,&found));
    puts("ACPI AML namespace and I2C resource tests passed");
    return 0;
}
