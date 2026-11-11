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

static acpi_aml_device_t captured[4];
static uint32_t callbacks;
static void capture(const acpi_aml_device_t* device, void* context)
{
    (void)context;
    assert(callbacks < 4U);
    captured[callbacks++] = *device;
}
static void append(uint8_t* out, uint32_t* length,
                   const uint8_t* bytes, uint32_t count)
{
    assert(*length <= 1024U && count <= 1024U - *length);
    memcpy(out + *length, bytes, count);
    *length += count;
}
static void append_package(uint8_t* out, uint32_t* length,
                           const uint8_t* prefix, uint32_t prefix_length,
                           const uint8_t* body, uint32_t body_length)
{
    uint32_t follow = 0U;
    uint32_t package_length = 0U;
    for (;;)
    {
        package_length = body_length + follow + 1U;
        uint32_t required = package_length <= 0x3FU ? 0U :
                            package_length <= 0x0FFFU ? 1U :
                            package_length <= 0x0FFFFFU ? 2U : 3U;
        if (required == follow)
            break;
        follow = required;
    }
    assert(follow <= 3U);
    append(out, length, prefix, prefix_length);
    uint8_t encoded[4];
    if (follow == 0U)
        encoded[0] = (uint8_t)package_length;
    else
    {
        encoded[0] = (uint8_t)((follow << 6U) |
                               (package_length & 0x0FU));
        for (uint32_t i = 0U; i < follow; ++i)
            encoded[i + 1U] = (uint8_t)(package_length >>
                                         (4U + i * 8U));
    }
    append(out, length, encoded, follow + 1U);
    append(out, length, body, body_length);
}
int main(void)
{
    uint8_t aml[1024], device_body[512], scope_body[768], cid_body[64];
    uint8_t crs_body[64], buffer_body[64];
    uint32_t n=0U, device_n=0U, scope_n=0U, cid_n=0U, crs_n=0U, buffer_n=0U;

    /* Device(TP0_) static identity and I2C SerialBus(_CRS) descriptor. */
    const uint8_t hid[]={0x08U,'_','H','I','D',0x0DU,
        'P','N','P','0','C','5','0',0x00U};
    const uint8_t uid[]={0x08U,'_','U','I','D',0x0AU,0x03U};
    const uint8_t sta[]={0x08U,'_','S','T','A',0x0AU,0x0FU};
    const uint8_t adr[]={0x08U,'_','A','D','R',0x0CU,
        0x01U,0x00U,0x15U,0x00U};
    const uint8_t device_name[]={'T','P','0','_'};
    const uint8_t cid_name[]={0x08U,'_','C','I','D'};
    const uint8_t crs_name[]={0x08U,'_','C','R','S'};
    const uint8_t cid_count[]={0x02U};
    const uint8_t cid_a[]={0x0DU,'A','C','P','I','0','0','1',0x00U};
    const uint8_t cid_b[]={0x0DU,'P','N','P','0','C','5','0',0x00U};
    append(cid_body,&cid_n,cid_count,sizeof(cid_count));
    append(cid_body,&cid_n,cid_a,sizeof(cid_a));
    append(cid_body,&cid_n,cid_b,sizeof(cid_b));
    const uint8_t package_op[]={0x12U};
    uint8_t cid_package[80]; uint32_t cid_package_n=0U;
    append_package(cid_package,&cid_package_n,package_op,sizeof(package_op),
                   cid_body,cid_n);
    append(device_body,&device_n,device_name,sizeof(device_name));
    append(device_body,&device_n,hid,sizeof(hid));
    append(device_body,&device_n,cid_name,sizeof(cid_name));
    append(device_body,&device_n,cid_package,cid_package_n);
    append(device_body,&device_n,uid,sizeof(uid));
    append(device_body,&device_n,sta,sizeof(sta));
    append(device_body,&device_n,adr,sizeof(adr));

    /* _DSD standard Device Properties package with hid-descr-addr. */
    const uint8_t uuid_bytes[]={0x14U,0xD8U,0xFFU,0xDAU,0xBAU,0x6EU,
        0x8CU,0x4DU,0x8AU,0x91U,0xBCU,0x9BU,0xBFU,0x4AU,0xA3U,0x01U};
    uint8_t dsd_buffer_body[32], dsd_buffer[48];
    uint32_t dsd_buffer_body_n=0U, dsd_buffer_n=0U;
    const uint8_t dsd_buffer_size[]={0x0AU,0x10U};
    append(dsd_buffer_body,&dsd_buffer_body_n,dsd_buffer_size,sizeof(dsd_buffer_size));
    append(dsd_buffer_body,&dsd_buffer_body_n,uuid_bytes,sizeof(uuid_bytes));
    const uint8_t dsd_buffer_op[]={0x11U};
    append_package(dsd_buffer,&dsd_buffer_n,dsd_buffer_op,sizeof(dsd_buffer_op),
                   dsd_buffer_body,dsd_buffer_body_n);
    uint8_t property_body[64], property_package[80];
    uint32_t property_body_n=0U, property_package_n=0U;
    const uint8_t property_count[]={0x02U};
    const uint8_t property_name[]={0x0DU,'h','i','d','-','d','e','s','c','r','-','a','d','d','r',0x00U};
    const uint8_t property_value[]={0x0BU,0x20U,0x00U};
    append(property_body,&property_body_n,property_count,sizeof(property_count));
    append(property_body,&property_body_n,property_name,sizeof(property_name));
    append(property_body,&property_body_n,property_value,sizeof(property_value));
    const uint8_t dsd_package_op[]={0x12U};
    append_package(property_package,&property_package_n,dsd_package_op,sizeof(dsd_package_op),
                   property_body,property_body_n);
    uint8_t dsd_body[160], dsd_package[192];
    uint32_t dsd_body_n=0U, dsd_package_n=0U;
    const uint8_t dsd_count[]={0x02U};
    append(dsd_body,&dsd_body_n,dsd_count,sizeof(dsd_count));
    append(dsd_body,&dsd_body_n,dsd_buffer,dsd_buffer_n);
    append(dsd_body,&dsd_body_n,property_package,property_package_n);
    append_package(dsd_package,&dsd_package_n,dsd_package_op,sizeof(dsd_package_op),
                   dsd_body,dsd_body_n);
    const uint8_t dsd_name[]={0x08U,'_','D','S','D'};
    append(device_body,&device_n,dsd_name,sizeof(dsd_name));
    append(device_body,&device_n,dsd_package,dsd_package_n);

    /* I2C descriptor: 100 kHz, 7-bit slave 0x15, source \\_SB_. */
    const uint8_t i2c_descriptor[]={
        0x8EU,0x15U,0x00U,
        0x01U,0x00U,0x01U,0x00U,0x00U,0x00U,0x01U,0x06U,0x00U,
        0xA0U,0x86U,0x01U,0x00U,0x15U,0x00U,
        '\\','_','S','B','_',0x00U,
        0x22U,0x02U,0x00U,
        0x79U,0x00U
    };
    append(crs_body,&crs_n,i2c_descriptor,sizeof(i2c_descriptor));
    const uint8_t buffer_size[]={0x0AU,0x1DU};
    append(buffer_body,&buffer_n,buffer_size,sizeof(buffer_size));
    append(buffer_body,&buffer_n,crs_body,crs_n);
    const uint8_t buffer_op[]={0x11U};
    uint8_t buffer_object[80]; uint32_t buffer_object_n=0U;
    append_package(buffer_object,&buffer_object_n,buffer_op,sizeof(buffer_op),
                   buffer_body,buffer_n);
    append(device_body,&device_n,crs_name,sizeof(crs_name));
    append(device_body,&device_n,buffer_object,buffer_object_n);
    const uint8_t noop[]={0xA3U};
    append(device_body,&device_n,noop,sizeof(noop));

    const uint8_t device_op[]={0x5BU,0x82U};
    uint8_t device_object[300]; uint32_t device_object_n=0U;
    append_package(device_object,&device_object_n,device_op,sizeof(device_op),
                   device_body,device_n);
    const uint8_t scope_name[]={'\\','_','S','B','_'};
    append(scope_body,&scope_n,scope_name,sizeof(scope_name));
    append(scope_body,&scope_n,device_object,device_object_n);

    /* A second device exercises EISAID's AML byte order and numeric _HID. */
    const uint8_t second_name[]={'D','E','V','0'};
    const uint8_t numeric_hid[]={0x08U,'_','H','I','D',0x0CU,
        0x41U,0xD0U,0x0AU,0x03U};
    uint8_t second_body[32]; uint32_t second_n=0U;
    append(second_body,&second_n,second_name,sizeof(second_name));
    append(second_body,&second_n,numeric_hid,sizeof(numeric_hid));
    uint8_t second_object[64]; uint32_t second_object_n=0U;
    append_package(second_object,&second_object_n,device_op,sizeof(device_op),
                   second_body,second_n);
    append(scope_body,&scope_n,second_object,second_object_n);
    const uint8_t scope_op[]={0x10U};
    append_package(aml,&n,scope_op,sizeof(scope_op),scope_body,scope_n);

    callbacks=0U;
    uint32_t found=0U;
    assert(acpi_aml_parse_table(aml,n,capture,NULL,&found));
    assert(found==2U && callbacks==2U);
    assert(strcmp(captured[0].path,"\\_SB_.TP0_")==0);
    assert(strcmp(captured[0].parent_path,"\\_SB_")==0);
    assert(captured[0].adr_valid && captured[0].adr==0x00150001U);
    assert(strcmp(captured[0].hid,"PNP0C50")==0);
    assert(captured[0].cid_count==2U);
    assert(strcmp(captured[0].cids[0],"ACPI001")==0);
    assert(captured[0].uid_valid && captured[0].uid==3U);
    assert(captured[0].sta_valid && captured[0].sta==0x0FU);
    assert(captured[0].i2c_count==1U);
    assert(captured[0].hid_descriptor_address_valid==1U);
    assert(captured[0].hid_descriptor_address==0x0020U);
    assert(captured[0].resource_total==2U && captured[0].resource_count==2U);
    assert(captured[0].resources[0].resource_type==4U);
    assert(captured[0].resources[0].serial_bus_type==1U);
    assert(captured[0].resources[0].connection_speed_hz==100000U);
    assert(captured[0].resources[0].slave_address==0x15U);
    assert(strcmp(captured[0].resources[0].resource_source,"\\_SB_")==0);
    assert(captured[0].resources[1].resource_type==3U);
    assert(captured[0].resources[1].irq_mask==2U);
    assert(strcmp(captured[1].path,"\\_SB_.DEV0")==0);
    assert(strcmp(captured[1].hid,"PNP0A03")==0);

    const uint8_t malformed[]={0x5BU,0x82U,0x7FU,'T','E','S','T'};
    assert(!acpi_aml_parse_table(malformed,sizeof(malformed),NULL,NULL,&found));
    assert(found==0U);
    assert(!acpi_aml_parse_table(NULL,1U,NULL,NULL,&found));
    assert(!acpi_aml_parse_table(aml,0U,NULL,NULL,&found));
    puts("ACPI AML namespace and I2C resource tests passed");
    return 0;
}
