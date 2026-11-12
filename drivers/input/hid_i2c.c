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
#include "timer.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define HID_I2C_MAX_DEVICES 8U
#define HID_I2C_DESC_BYTES 30U
#define HID_I2C_MAX_REPORT 4096U
#define HID_I2C_POLL_BUDGET 2U
#define HID_I2C_ERROR_LIMIT 10U
#define HID_I2C_MAX_TOUCH_CONTACTS 8U

typedef struct
{
    uint8_t active;
    uint8_t has_keyboard;
    uint8_t has_pointer;
    uint8_t touchpad;
    uint8_t last_xy_valid;
    uint8_t last_touch_valid;
    uint8_t last_contact_count;
    uint8_t buttons;
    uint8_t errors;
    uint8_t online;
    uint32_t retry_after_tick;
    uint32_t retry_delay_ticks;
    uint16_t address;
    uint16_t descriptor_register;
    uint16_t input_register;
    uint16_t command_register;
    uint16_t max_input_length;
    uint32_t speed_hz;
    uint32_t keyboard_id;
    uint32_t pointer_id;
    int32_t last_x;
    int32_t last_y;
    int32_t last_touch_y;
    int32_t touch_scroll_remainder_y;
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

static int hid_register_write(i2c_bus_t *bus,uint16_t address,
                              uint32_t speed,uint16_t reg,
                              const uint8_t *data,size_t length)
{
    uint8_t command[8];
    if(!bus||!bus->ready||!bus->transfer||!data||length==0U||
       length>sizeof(command)-2U) return 0;
    command[0]=(uint8_t)reg;
    command[1]=(uint8_t)(reg>>8U);
    memcpy(command+2U,data,length);
    return bus->transfer(bus->context,address,speed,command,length+2U,NULL,0U);
}

static int hid_set_power(i2c_bus_t *bus,uint16_t address,uint32_t speed,
                         uint16_t command_register,uint8_t power_state)
{
    const uint8_t command[]={0x00U,0x08U,power_state};
    return hid_register_write(bus,address,speed,command_register,
                              command,sizeof(command));
}

static int hid_reset_device(i2c_bus_t *bus,uint16_t address,uint32_t speed,
                            uint16_t command_register,uint16_t input_register,
                            uint16_t max_input_length)
{
    if(!hid_set_power(bus,address,speed,command_register,0U))
        return 0;
    sleep_ms(60U);
    const uint8_t reset_command[]={0x00U,0x01U};
    if(!hid_register_write(bus,address,speed,command_register,
                           reset_command,sizeof(reset_command)))
    {
        (void)hid_set_power(bus,address,speed,command_register,1U);
        return 0;
    }

    uint8_t response[HID_I2C_MAX_REPORT];
    int acknowledged=0;
    for(uint32_t elapsed=0U;elapsed<1000U;elapsed+=10U)
    {
        sleep_ms(10U);
        if(!hid_register_read(bus,address,speed,input_register,response,
                              max_input_length))
            continue;
        uint16_t response_length=hid_u16(response);
        if(response_length==0U||
           (response_length>=2U&&response_length<=max_input_length))
        {
            acknowledged=1;
            break;
        }
    }
    if(!acknowledged)
        serial_write("VANTAOS_I2C_HID_RESET_ACK_TIMEOUT_CONTINUING\n");
    if(!hid_set_power(bus,address,speed,command_register,0U))
        return 0;
    sleep_ms(60U);
    return 1;
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
           hid_u16(d+16U)!=0U&&max_input>=3U&&
           max_input<=HID_I2C_MAX_REPORT;
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

    /* Apply modifiers first so same-report key presses observe Shift/Ctrl. */
    for(uint32_t usage=0xE0U;usage<=0xE7U;++usage)
        if(managed[usage]&&next[usage]!=device->keys[usage])
            hid_emit_key(device,(uint16_t)usage,next[usage]);
    for(uint32_t usage=1U;usage<256U;++usage)
    {
        if(usage>=0xE0U&&usage<=0xE7U) continue;
        if(!managed[usage]||next[usage]==device->keys[usage]) continue;
        hid_emit_key(device,(uint16_t)usage,next[usage]);
    }
    memcpy(device->keys,next,sizeof(next));
}

typedef struct
{
    uint16_t collection_id;
    uint8_t tip_switch_seen;
    uint8_t tip_switch;
    uint8_t have_x;
    uint8_t have_y;
    int32_t x;
    int32_t y;
    int32_t x_range;
    int32_t y_range;
} hid_touch_contact_t;

static hid_touch_contact_t *hid_touch_contact_get(
    hid_touch_contact_t contacts[HID_I2C_MAX_TOUCH_CONTACTS],
    uint32_t *count,uint16_t collection_id)
{
    if(collection_id==0U) return NULL;
    for(uint32_t i=0U;i<*count;++i)
        if(contacts[i].collection_id==collection_id) return &contacts[i];
    if(*count>=HID_I2C_MAX_TOUCH_CONTACTS) return NULL;
    hid_touch_contact_t *contact=&contacts[(*count)++];
    memset(contact,0,sizeof(*contact));
    contact->collection_id=collection_id;
    return contact;
}

static int32_t hid_clamp_delta(int64_t value)
{
    if(value>4096) return 4096;
    if(value< -4096) return -4096;
    return (int32_t)value;
}

static void hid_decode_pointer(hid_i2c_device_t *device,uint8_t report_id,
                               const uint8_t *payload,size_t payload_length)
{
    int32_t x=0,y=0,wheel=0;
    uint8_t buttons=device->buttons;
    uint8_t have_x=0U,have_y=0U;
    uint8_t contact_switch_seen=0U;
    uint8_t contact_count_hint=0U,contact_count_valid=0U;
    hid_touch_contact_t contacts[HID_I2C_MAX_TOUCH_CONTACTS]={0};
    uint32_t contact_slots=0U;

    for(uint32_t f=0U;f<device->layout.field_count;++f)
    {
        const hid_input_field_t *field=&device->layout.fields[f];
        if(field->report_id!=report_id||(field->flags&1U)!=0U||
           (field->flags&2U)==0U||field->usage_max<field->usage_min||
           (field->usage_min==0U&&field->usage_max==0U))
            continue;
        for(uint32_t i=0U;i<field->report_count;++i)
        {
            uint32_t usage=(uint32_t)field->usage_min+i;
            uint32_t raw=hid_extract_bits(payload,payload_length,
                field->bit_offset+i*field->bit_size,field->bit_size,1U);

            if(field->usage_page==0x09U&&usage>=1U&&usage<=5U)
            {
                uint8_t mask=(uint8_t)(1U<<(usage-1U));
                if(raw) buttons|=mask; else buttons&=(uint8_t)~mask;
                continue;
            }

            if(device->touchpad)
            {
                if(field->usage_page==0x0DU&&usage==0x54U)
                {
                    contact_count_hint=(uint8_t)(raw>HID_I2C_MAX_TOUCH_CONTACTS?
                        HID_I2C_MAX_TOUCH_CONTACTS:raw);
                    contact_count_valid=1U;
                    continue;
                }
                if(field->usage_page==0x0DU&&usage==0x42U)
                {
                    contact_switch_seen=1U;
                    hid_touch_contact_t *contact=hid_touch_contact_get(
                        contacts,&contact_slots,field->collection_id);
                    if(contact)
                    {
                        contact->tip_switch_seen=1U;
                        contact->tip_switch=(uint8_t)(raw!=0U);
                    }
                    continue;
                }
                if(field->usage_page==0x01U&&
                   (usage==0x30U||usage==0x31U))
                {
                    hid_touch_contact_t *contact=hid_touch_contact_get(
                        contacts,&contact_slots,field->collection_id);
                    if(contact)
                    {
                        int32_t coordinate=hid_signed_value(
                            raw,field->bit_size,field->logical_min);
                        int64_t range=(int64_t)field->logical_max-
                                      field->logical_min;
                        if(usage==0x30U)
                        {
                            contact->x=coordinate;
                            contact->have_x=1U;
                            if(range>0&&range<=INT32_MAX)
                                contact->x_range=(int32_t)range;
                        }
                        else
                        {
                            contact->y=coordinate;
                            contact->have_y=1U;
                            if(range>0&&range<=INT32_MAX)
                                contact->y_range=(int32_t)range;
                        }
                    }
                    continue;
                }
            }
            else if(field->usage_page==0x01U&&usage==0x30U&&!have_x)
            {
                x=hid_signed_value(raw,field->bit_size,field->logical_min);
                have_x=1U;
            }
            else if(field->usage_page==0x01U&&usage==0x31U&&!have_y)
            {
                y=hid_signed_value(raw,field->bit_size,field->logical_min);
                have_y=1U;
            }

            if(field->usage_page==0x01U&&usage==0x38U)
                wheel=hid_signed_value(raw,field->bit_size,field->logical_min);
        }
    }

    if(device->touchpad)
    {
        uint32_t active_contacts=0U;
        int64_t sum_y=0;
        int32_t primary_x=0,primary_y=0;
        int32_t coordinate_range=0;
        for(uint32_t i=0U;i<contact_slots;++i)
        {
            const hid_touch_contact_t *contact=&contacts[i];
            if(!contact->tip_switch_seen||!contact->tip_switch||
               !contact->have_x||!contact->have_y)
                continue;
            if(active_contacts==0U)
            {
                primary_x=contact->x;
                primary_y=contact->y;
            }
            ++active_contacts;
            sum_y+=contact->y;
            if(contact->y_range>coordinate_range)
                coordinate_range=contact->y_range;
        }
        if(!contact_switch_seen) active_contacts=0U;
        if(contact_count_valid&&contact_count_hint!=active_contacts)
            active_contacts=0U;

        if(active_contacts==1U)
        {
            if(device->last_contact_count==1U&&device->last_xy_valid)
            {
                int64_t dx=(int64_t)primary_x-device->last_x;
                int64_t dy=(int64_t)primary_y-device->last_y;
                int32_t x_range=0,y_range=0;
                for(uint32_t i=0U;i<contact_slots;++i)
                {
                    if(contacts[i].tip_switch_seen&&contacts[i].tip_switch)
                    {
                        x_range=contacts[i].x_range;
                        y_range=contacts[i].y_range;
                        break;
                    }
                }
                if(x_range>0) dx=(dx*128)/x_range;
                if(y_range>0) dy=(dy*128)/y_range;
                x=hid_clamp_delta(dx);
                y=hid_clamp_delta(dy);
                have_x=1U;
                have_y=1U;
            }
            device->last_x=primary_x;
            device->last_y=primary_y;
            device->last_xy_valid=1U;
            device->last_touch_valid=0U;
            device->touch_scroll_remainder_y=0;
        }
        else if(active_contacts==2U)
        {
            int32_t centroid_y=(int32_t)(sum_y/2);
            if(device->last_contact_count==2U&&device->last_touch_valid)
            {
                int64_t delta=(int64_t)centroid_y-device->last_touch_y;
                if(delta>2048) delta=2048;
                if(delta< -2048) delta= -2048;
                int32_t threshold=coordinate_range>0?
                    coordinate_range/100:64;
                if(threshold<8) threshold=8;
                if(threshold>1024) threshold=1024;
                int64_t remainder=(int64_t)device->touch_scroll_remainder_y+
                                  delta;
                int64_t steps=remainder/threshold;
                if(steps>20) steps=20;
                if(steps< -20) steps= -20;
                if(steps!=0)
                {
                    wheel=(int32_t)-steps;
                    remainder-=steps*threshold;
                }
                if(remainder>INT32_MAX) remainder=INT32_MAX;
                if(remainder<INT32_MIN) remainder=INT32_MIN;
                device->touch_scroll_remainder_y=(int32_t)remainder;
            }
            else
                device->touch_scroll_remainder_y=0;
            device->last_touch_y=centroid_y;
            device->last_touch_valid=1U;
            device->last_xy_valid=0U;
        }
        else
        {
            device->last_xy_valid=0U;
            device->last_touch_valid=0U;
            device->touch_scroll_remainder_y=0;
        }
        device->last_contact_count=(uint8_t)active_contacts;
    }

    if(have_x||have_y)
    {
        input_event_t motion={.device_id=device->pointer_id,
            .type=INPUT_EVENT_RELATIVE_MOTION,.code=0,
            .value=have_x?x:0,.value2=have_y?y:0};
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
    uint16_t command_register=hid_u16(descriptor+16U);
    static uint8_t report_descriptors[HID_I2C_MAX_DEVICES][HID_I2C_MAX_REPORT];
    if(!hid_reset_device(bus,address,speed_hz,command_register,
                         input_register,max_input))
        return 0;
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
    uint8_t keyboard_fields=0U;
    uint8_t relative_x=0U,relative_y=0U;
    uint8_t absolute_x=0U,absolute_y=0U;
    uint8_t contact_switch=0U,buttons_present=0U,wheel_present=0U;
    for(uint32_t i=0U;i<candidate.layout.field_count;++i)
    {
        const hid_input_field_t *field=&candidate.layout.fields[i];
        if((field->flags&1U)!=0U) continue;
        if(field->usage_page==0x07U&&field->usage_max>=1U)
            keyboard_fields=1U;
        if(field->usage_page==0x09U&&field->usage_min<=5U&&
           field->usage_max>=1U)
            buttons_present=1U;
        if(field->usage_page==0x0DU&&field->usage_min<=0x42U&&
           field->usage_max>=0x42U)
        {contact_switch=1U;}
        if(field->usage_page==0x01U&&field->usage_min<=0x30U&&
           field->usage_max>=0x30U)
        {
            if(field->logical_min<0) relative_x=1U;
            else absolute_x=1U;
        }
        if(field->usage_page==0x01U&&field->usage_min<=0x31U&&
           field->usage_max>=0x31U)
        {
            if(field->logical_min<0) relative_y=1U;
            else absolute_y=1U;
        }
        if(field->usage_page==0x01U&&field->usage_min<=0x38U&&
           field->usage_max>=0x38U)
            wheel_present=1U;
    }
    candidate.has_keyboard=(uint8_t)(candidate.layout.has_keyboard&&
                                     keyboard_fields);
    candidate.touchpad=(uint8_t)(candidate.layout.has_touchpad&&
                                 absolute_x&&absolute_y&&contact_switch);
    candidate.has_pointer=(uint8_t)(
        (candidate.layout.has_mouse&&relative_x&&relative_y)||
        candidate.touchpad);
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
    candidate.command_register=command_register;
    candidate.max_input_length=max_input;
    candidate.online=1U;
    candidate.retry_delay_ticks=10U;
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
                          (buttons_present?INPUT_CAP_MOUSE_BUTTONS:0U)|
                          ((wheel_present||candidate.touchpad)?
                           INPUT_CAP_MOUSE_WHEEL:0U)};
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
    device->last_touch_valid=0U;
    device->last_contact_count=0U;
    device->touch_scroll_remainder_y=0;
}

static void hid_poll_device(hid_i2c_device_t *device,uint32_t tick)
{
    uint8_t report[HID_I2C_MAX_REPORT];
    if(!hid_register_read(device->bus,device->address,device->speed_hz,
                          device->input_register,report,
                          device->max_input_length))
    {
        if(device->errors<HID_I2C_ERROR_LIMIT) ++device->errors;
        if(device->errors>=HID_I2C_ERROR_LIMIT)
        {
            if(device->online)
            {
                hid_release_all(device);
                if(device->keyboard_id)
                    (void)input_set_device_online(device->keyboard_id,false);
                if(device->pointer_id)
                    (void)input_set_device_online(device->pointer_id,false);
                device->online=0U;
                device->retry_delay_ticks=10U;
                serial_write("VANTAOS_I2C_HID_TRANSPORT_OFFLINE\n");
            }
            else if(device->retry_delay_ticks<200U)
            {
                device->retry_delay_ticks*=2U;
                if(device->retry_delay_ticks>200U)
                    device->retry_delay_ticks=200U;
            }
            device->retry_after_tick=tick+device->retry_delay_ticks;
        }
        return;
    }
    device->errors=0U;
    device->retry_delay_ticks=10U;
    if(!device->online)
    {
        int restored=1;
        if(device->keyboard_id)
            restored=input_set_device_online(device->keyboard_id,true)&&restored;
        if(device->pointer_id)
            restored=input_set_device_online(device->pointer_id,true)&&restored;
        if(!restored) return;
        device->online=1U;
        device->last_xy_valid=0U;
        device->last_touch_valid=0U;
        device->last_contact_count=0U;
        device->touch_scroll_remainder_y=0;
        device->buttons=0U;
        memset(device->keys,0,sizeof(device->keys));
        serial_write("VANTAOS_I2C_HID_TRANSPORT_RESTORED\n");
    }
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
        if(!devices[slot].active)
            continue;
        if(!devices[slot].online&&
           (int32_t)(tick-devices[slot].retry_after_tick)<0)
            continue;
        hid_poll_device(&devices[slot],tick);
        ++visited;
    }
}
