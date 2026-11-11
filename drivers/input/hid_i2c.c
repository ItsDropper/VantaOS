/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "hid_i2c.h"

#include "hid_report.h"
#include "input.h"
#include "keyboard.h"
#include "interrupts.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define HID_I2C_MAX_DEVICES 8U
#define HID_I2C_DESC_BYTES 30U
#define HID_I2C_MAX_REPORT 4096U
#define HID_I2C_POLL_BUDGET 2U
#define HID_I2C_ERROR_LIMIT 10U

typedef struct
{
    uint8_t active;
    uint8_t has_keyboard;
    uint8_t has_pointer;
    uint8_t touchpad;
    uint8_t last_xy_valid;
    uint8_t buttons;
    uint8_t errors;
    uint16_t address;
    uint16_t descriptor_register;
    uint16_t input_register;
    uint16_t max_input_length;
    uint32_t speed_hz;
    uint32_t keyboard_id;
    uint32_t pointer_id;
    int32_t last_x;
    int32_t last_y;
    uint8_t keys[256];
    i2c_bus_t *bus;
    hid_report_layout_t layout;
    char firmware_id[16];
} hid_i2c_device_t;

static hid_i2c_device_t devices[HID_I2C_MAX_DEVICES];
static uint32_t poll_cursor;
static uint32_t last_poll_tick=UINT32_MAX;
static const char *const keyboard_names[HID_I2C_MAX_DEVICES]={
    "I2C HID keyboard 0","I2C HID keyboard 1","I2C HID keyboard 2",
    "I2C HID keyboard 3","I2C HID keyboard 4","I2C HID keyboard 5",
    "I2C HID keyboard 6","I2C HID keyboard 7"
};
static const char *const pointer_names[HID_I2C_MAX_DEVICES]={
    "I2C HID pointer 0","I2C HID pointer 1","I2C HID pointer 2",
    "I2C HID pointer 3","I2C HID pointer 4","I2C HID pointer 5",
    "I2C HID pointer 6","I2C HID pointer 7"
};

static uint16_t hid_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8U));
}

static int hid_register_read(i2c_bus_t *bus,uint16_t address,
                             uint32_t speed,uint16_t reg,
                             uint8_t *data,size_t length)
{
    uint8_t command[2]={(uint8_t)reg,(uint8_t)(reg>>8U)};
    return bus&&bus->ready&&bus->transfer&&data&&length!=0U&&
           bus->transfer(bus->context,address,speed,command,sizeof(command),
                         data,length);
}

static int hid_descriptor_valid(const uint8_t *d)
{
    uint16_t descriptor_length=hid_u16(d);
    uint16_t version=hid_u16(d+2U);
    uint16_t report_length=hid_u16(d+4U);
    uint16_t report_register=hid_u16(d+6U);
    uint16_t input_register=hid_u16(d+8U);
    uint16_t max_input=hid_u16(d+10U);
    return descriptor_length>=HID_I2C_DESC_BYTES&&descriptor_length<=64U&&
           version>=0x0100U&&version<=0x0101U&&
           report_length!=0U&&report_length<=HID_I2C_MAX_REPORT&&
           report_register!=0U&&input_register!=0U&&
           max_input>=3U&&max_input<=HID_I2C_MAX_REPORT;
}

static uint32_t hid_extract_bits(const uint8_t *bytes,size_t length,
                                 uint32_t bit_offset,uint8_t bit_size,
                                 uint32_t count)
{
    if(!bytes||bit_size==0U||bit_size>32U||count>32U||
       bit_offset>length*8U||bit_size>length*8U-bit_offset)
        return 0U;
    uint32_t value=0U;
    for(uint32_t bit=0U;bit<bit_size;++bit)
    {
        uint32_t position=bit_offset+bit;
        if((bytes[position>>3U]&(uint8_t)(1U<<(position&7U)))!=0U)
            value|=1U<<bit;
    }
    return value;
}

static int32_t hid_signed_value(uint32_t value,uint8_t bits,int32_t logical_min)
{
    if(logical_min>=0||bits==0U||bits>=32U) return (int32_t)value;
    uint32_t sign=1U<<(bits-1U);
    if((value&sign)!=0U) value|=(~0U)<<bits;
    return (int32_t)value;
}

static int hid_find_report(const hid_report_layout_t *layout,uint8_t report_id,
                           uint16_t actual_bytes)
{
    for(uint32_t i=0U;i<layout->report_count;++i)
        if(layout->reports[i].report_id==report_id&&
           actual_bytes>=layout->reports[i].input_bytes)
            return 1;
    return 0;
}

static void hid_emit_key(hid_i2c_device_t *device,uint16_t usage,int pressed)
{
    if(usage==0U||usage>255U) return;
    input_event_t event={.device_id=device->keyboard_id,
        .type=INPUT_EVENT_KEY,.code=usage,.value=pressed?1:0,.value2=0};
    (void)input_report_event(&event);
    keyboard_inject_hid_usage(usage,pressed!=0);
}

static void hid_decode_keyboard(hid_i2c_device_t *device,uint8_t report_id,
                                const uint8_t *payload,size_t payload_length)
{
    uint8_t next[256];
    uint8_t managed[256];
    memcpy(next,device->keys,sizeof(next));
    memset(managed,0,sizeof(managed));

    for(uint32_t f=0U;f<device->layout.field_count;++f)
    {
        const hid_input_field_t *field=&device->layout.fields[f];
        if(field->report_id!=report_id||field->usage_page!=0x07U||
           (field->flags&1U)!=0U||field->usage_max<field->usage_min||
           (field->usage_min==0U&&field->usage_max==0U))
            continue;
        uint32_t base=field->bit_offset;
        if(field->report_count>256U) return;
        if((field->flags&2U)!=0U)
        {
            for(uint32_t i=0U;i<field->report_count;++i)
            {
                uint32_t usage=(uint32_t)field->usage_min+i;
                if(usage>255U||usage>field->usage_max) continue;
                managed[usage]=1U;
                uint32_t value=hid_extract_bits(payload,payload_length,
                    base+i*field->bit_size,field->bit_size,1U);
                next[usage]=(uint8_t)(value!=0U);
            }
        }
        else
        {
            uint32_t maximum=field->usage_max;
            if(maximum>255U) maximum=255U;
            uint32_t minimum=field->usage_min==0U?1U:field->usage_min;
            for(uint32_t usage=minimum;usage<=maximum;++usage)
            {
                managed[usage]=1U;
                next[usage]=0U;
            }
            uint8_t rollover=0U;
            for(uint32_t i=0U;i<field->report_count;++i)
            {
                uint32_t usage=hid_extract_bits(payload,payload_length,
                    base+i*field->bit_size,field->bit_size,1U);
                if(usage>=1U&&usage<=3U) rollover=1U;
                if(usage>=field->usage_min&&usage<=maximum)
                    next[usage]=1U;
            }
            if(rollover) return;
        }
    }

    for(uint32_t usage=1U;usage<256U;++usage)
    {
        if(!managed[usage]||next[usage]==device->keys[usage]) continue;
        hid_emit_key(device,(uint16_t)usage,next[usage]);
    }
    memcpy(device->keys,next,sizeof(next));
}

static void hid_decode_pointer(hid_i2c_device_t *device,uint8_t report_id,
                               const uint8_t *payload,size_t payload_length)
{
    int32_t x=0,y=0,wheel=0;
    uint8_t buttons=device->buttons;
    uint8_t have_x=0U,have_y=0U;
    for(uint32_t f=0U;f<device->layout.field_count;++f)
    {
        const hid_input_field_t *field=&device->layout.fields[f];
        if(field->report_id!=report_id||(field->flags&1U)!=0U||
           (field->flags&2U)==0U) continue;
        for(uint32_t i=0U;i<field->report_count;++i)
        {
            uint32_t usage=(uint32_t)field->usage_min+i;
            uint32_t raw=hid_extract_bits(payload,payload_length,
                field->bit_offset+i*field->bit_size,field->bit_size,1U);
            if(field->usage_page==0x09U&&usage>=1U&&usage<=5U)
            {
                uint8_t mask=(uint8_t)(1U<<(usage-1U));
                if(raw) buttons|=mask; else buttons&=(uint8_t)~mask;
            }
            else if(field->usage_page==0x0DU&&usage==0x42U)
            {
                if(raw) buttons|=1U; else buttons&=(uint8_t)~1U;
            }
            else if(field->usage_page==0x01U&&usage==0x30U)
            {
                x=hid_signed_value(raw,field->bit_size,field->logical_min);
                have_x=1U;
            }
            else if(field->usage_page==0x01U&&usage==0x31U)
            {
                y=hid_signed_value(raw,field->bit_size,field->logical_min);
                have_y=1U;
            }
            else if(field->usage_page==0x01U&&usage==0x38U)
                wheel=hid_signed_value(raw,field->bit_size,field->logical_min);
        }
    }

    if(device->touchpad)
    {
        /* Touch coordinates are absolute. Only move while the first contact
         * is active, and reset the baseline between contacts to avoid jumps. */
        if((buttons&1U)==0U)
        {
            device->last_xy_valid=0U;
            have_x=have_y=0U;
        }
        else if(have_x&&have_y)
        {
            int32_t absolute_x=x,absolute_y=y;
            if(device->last_xy_valid)
            {
                int64_t dx=(int64_t)absolute_x-device->last_x;
                int64_t dy=(int64_t)absolute_y-device->last_y;
                if(dx>4096) dx=4096;
                if(dx< -4096) dx= -4096;
                if(dy>4096) dy=4096;
                if(dy< -4096) dy= -4096;
                x=(int32_t)dx;
                y=(int32_t)dy;
            }
            else x=y=0;
            device->last_x=absolute_x;
            device->last_y=absolute_y;
            device->last_xy_valid=1U;
        }
        else
            have_x=have_y=0U;
    }

    if(have_x||have_y)
    {
        input_event_t motion={.device_id=device->pointer_id,
            .type=INPUT_EVENT_RELATIVE_MOTION,.code=0,
            .value=have_x?x:0,.value2=have_y?-y:0};
        if(motion.value!=0||motion.value2!=0)
            (void)input_report_event(&motion);
    }
    uint8_t changed=(uint8_t)(buttons^device->buttons);
    for(uint32_t bit=0U;bit<5U;++bit)
    {
        uint8_t mask=(uint8_t)(1U<<bit);
        if((changed&mask)==0U) continue;
        input_event_t event={.device_id=device->pointer_id,
            .type=INPUT_EVENT_BUTTON,.code=(int32_t)bit,
            .value=(buttons&mask)?1:0,.value2=0};
        (void)input_report_event(&event);
    }
    device->buttons=buttons;
    if(wheel!=0)
    {
        input_event_t event={.device_id=device->pointer_id,
            .type=INPUT_EVENT_WHEEL,.code=0,.value=wheel,.value2=0};
        (void)input_report_event(&event);
    }
}

int hid_i2c_attach(i2c_bus_t *bus,uint16_t address,uint32_t speed_hz,
                   uint16_t hid_descriptor_register,const char *firmware_id)
{
    if(!bus||!bus->ready||address==0U||address>0x7FU||
       speed_hz==0U||speed_hz>400000U||hid_descriptor_register==0U)
        return 0;
    uint32_t slot=HID_I2C_MAX_DEVICES;
    for(uint32_t i=0U;i<HID_I2C_MAX_DEVICES;++i)
    {
        if(devices[i].active&&devices[i].bus==bus&&
           devices[i].address==address&&
           devices[i].descriptor_register==hid_descriptor_register)
            return 0;
        if(!devices[i].active&&slot==HID_I2C_MAX_DEVICES) slot=i;
    }
    if(slot==HID_I2C_MAX_DEVICES) return 0;

    uint8_t descriptor[HID_I2C_DESC_BYTES];
    if(!hid_register_read(bus,address,speed_hz,hid_descriptor_register,
                          descriptor,sizeof(descriptor))||
       !hid_descriptor_valid(descriptor))
        return 0;
    uint16_t report_length=hid_u16(descriptor+4U);
    uint16_t report_register=hid_u16(descriptor+6U);
    uint16_t input_register=hid_u16(descriptor+8U);
    uint16_t max_input=hid_u16(descriptor+10U);
    static uint8_t report_descriptors[HID_I2C_MAX_DEVICES][HID_I2C_MAX_REPORT];
    if(!hid_register_read(bus,address,speed_hz,report_register,
                          report_descriptors[slot],report_length))
        return 0;

    hid_i2c_device_t candidate;
    memset(&candidate,0,sizeof(candidate));
    if(!hid_report_parse(report_descriptors[slot],report_length,
                         &candidate.layout))
        return 0;
    uint16_t largest_report=0U;
    for(uint32_t i=0U;i<candidate.layout.report_count;++i)
        if(candidate.layout.reports[i].input_bytes>largest_report)
            largest_report=candidate.layout.reports[i].input_bytes;
    if((uint32_t)largest_report+2U>max_input)
        return 0;
    candidate.has_keyboard=candidate.layout.has_keyboard;
    candidate.has_pointer=(uint8_t)(candidate.layout.has_mouse||
                                    candidate.layout.has_touchpad);
    candidate.touchpad=candidate.layout.has_touchpad;
    if(!candidate.has_keyboard&&!candidate.has_pointer)
    {
        serial_write("VANTAOS_I2C_HID_UNSUPPORTED_REPORT_COLLECTION\n");
        return 0;
    }
    candidate.bus=bus;
    candidate.address=address;
    candidate.descriptor_register=hid_descriptor_register;
    candidate.speed_hz=speed_hz;
    candidate.input_register=input_register;
    candidate.max_input_length=max_input;
    if(firmware_id)
    {
        size_t n=strlen(firmware_id);
        if(n>=sizeof(candidate.firmware_id)) n=sizeof(candidate.firmware_id)-1U;
        memcpy(candidate.firmware_id,firmware_id,n);
        candidate.firmware_id[n]='\0';
    }

    if(candidate.has_keyboard)
    {
        input_device_descriptor_t input={.name=keyboard_names[slot],
            .bus=INPUT_BUS_I2C,.kind=INPUT_DEVICE_KEYBOARD,
            .capabilities=INPUT_CAP_KEYBOARD_KEYS};
        if(!input_register_device(&input,&candidate.keyboard_id)) return 0;
    }
    if(candidate.has_pointer)
    {
        input_device_descriptor_t input={.name=pointer_names[slot],
            .bus=INPUT_BUS_I2C,.kind=INPUT_DEVICE_MOUSE,
            .capabilities=INPUT_CAP_MOUSE_RELATIVE|
                          INPUT_CAP_MOUSE_BUTTONS|INPUT_CAP_MOUSE_WHEEL};
        if(!input_register_device(&input,&candidate.pointer_id))
        {
            if(candidate.keyboard_id) (void)input_unregister_device(candidate.keyboard_id);
            return 0;
        }
    }
    candidate.active=1U;
    devices[slot]=candidate;
    serial_write("VANTAOS_I2C_HID_ATTACHED id=");
    serial_write(candidate.firmware_id[0]?candidate.firmware_id:"unknown");
    serial_write(candidate.has_keyboard?" keyboard=1":" keyboard=0");
    serial_write(candidate.has_pointer?" pointer=1\n":" pointer=0\n");
    return 1;
}

static void hid_release_all(hid_i2c_device_t *device)
{
    if(device->keyboard_id)
        for(uint32_t usage=1U;usage<256U;++usage)
            if(device->keys[usage])
                hid_emit_key(device,(uint16_t)usage,0);
    memset(device->keys,0,sizeof(device->keys));
    if(device->pointer_id)
        for(uint32_t button=0U;button<5U;++button)
            if(device->buttons&(1U<<button))
            {
                input_event_t event={.device_id=device->pointer_id,
                    .type=INPUT_EVENT_BUTTON,.code=(int32_t)button,
                    .value=0,.value2=0};
                (void)input_report_event(&event);
            }
    device->buttons=0U;
    device->last_xy_valid=0U;
}

static void hid_poll_device(hid_i2c_device_t *device)
{
    uint8_t report[HID_I2C_MAX_REPORT];
    if(!hid_register_read(device->bus,device->address,device->speed_hz,
                          device->input_register,report,
                          device->max_input_length))
    {
        if(device->errors<HID_I2C_ERROR_LIMIT) ++device->errors;
        if(device->errors==HID_I2C_ERROR_LIMIT)
        {
            hid_release_all(device);
            if(device->keyboard_id) (void)input_set_device_online(device->keyboard_id,false);
            if(device->pointer_id) (void)input_set_device_online(device->pointer_id,false);
            serial_write("VANTAOS_I2C_HID_TRANSPORT_OFFLINE\n");
            ++device->errors;
        }
        return;
    }
    device->errors=0U;
    uint16_t total=hid_u16(report);
    if(total==0U||total==0xFFFFU) return;
    if(total<2U||total>device->max_input_length) return;
    uint16_t payload_length=(uint16_t)(total-2U);
    if(payload_length==0U) return;
    uint8_t report_id=0U;
    const uint8_t *payload=report+2U;
    size_t data_length=payload_length;
    int numbered=0;
    for(uint32_t i=0U;i<device->layout.report_count;++i)
        if(device->layout.reports[i].report_id!=0U) numbered=1;
    if(numbered)
    {
        if(data_length==0U) return;
        report_id=payload[0];
        if(report_id==0U) return;
    }
    if(!hid_find_report(&device->layout,report_id,payload_length)) return;
    if(numbered){++payload;--data_length;}
    if(device->has_keyboard)
        hid_decode_keyboard(device,report_id,payload,data_length);
    if(device->has_pointer)
        hid_decode_pointer(device,report_id,payload,data_length);
}

void hid_i2c_poll(void)
{
    uint32_t tick=interrupts_get_ticks();
    if(tick==last_poll_tick) return;
    last_poll_tick=tick;
    uint32_t visited=0U;
    uint32_t scanned=0U;
    while(visited<HID_I2C_POLL_BUDGET&&scanned<HID_I2C_MAX_DEVICES)
    {
        uint32_t slot=poll_cursor++%HID_I2C_MAX_DEVICES;
        ++scanned;
        if(!devices[slot].active||devices[slot].errors>HID_I2C_ERROR_LIMIT)
            continue;
        hid_poll_device(&devices[slot]);
        ++visited;
    }
}
