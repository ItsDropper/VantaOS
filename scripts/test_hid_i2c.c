/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "hid_i2c.h"
#include "input.h"
#include "keyboard.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const uint8_t keyboard_descriptor[]={
    0x05,0x01,0x09,0x06,0xA1,0x01,0x05,0x07,
    0x19,0xE0,0x29,0xE7,0x15,0x00,0x25,0x01,
    0x75,0x01,0x95,0x08,0x81,0x02,0x95,0x01,
    0x75,0x08,0x81,0x01,0x95,0x06,0x75,0x08,
    0x15,0x00,0x25,0x65,0x05,0x07,0x19,0x00,
    0x29,0x65,0x81,0x00,0xC0
};

static const uint8_t touchpad_descriptor[]={
    0x05,0x0D,0x09,0x05,0xA1,0x01,
    0x09,0x22,0xA1,0x02,
    0x09,0x42,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x01,0x81,0x02,
    0x09,0x51,0x15,0x00,0x25,0x0F,0x75,0x08,0x95,0x01,0x81,0x02,
    0x05,0x01,0x09,0x30,0x15,0x00,0x26,0xFF,0x0F,
    0x75,0x10,0x95,0x01,0x81,0x02,
    0x09,0x31,0x81,0x02,0xC0,
    0x05,0x0D,0x09,0x22,0xA1,0x02,
    0x09,0x42,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x01,0x81,0x02,
    0x09,0x51,0x15,0x00,0x25,0x0F,0x75,0x08,0x95,0x01,0x81,0x02,
    0x05,0x01,0x09,0x30,0x15,0x00,0x26,0xFF,0x0F,
    0x75,0x10,0x95,0x01,0x81,0x02,
    0x09,0x31,0x81,0x02,0xC0,
    0x05,0x0D,0x09,0x54,0x15,0x00,0x25,0x02,
    0x75,0x08,0x95,0x01,0x81,0x02,0xC0
};

typedef struct
{
    uint8_t hid_descriptor[30];
    uint8_t touchpad_hid_descriptor[30];
    uint8_t input_report[10];
    uint8_t touchpad_input_report[14];
    uint32_t tick;
    uint32_t transfer_count;
    uint32_t registered_count;
    uint32_t capabilities[8];
    input_event_t events[32];
    uint32_t event_count;
    uint16_t injected_usage[32];
    uint8_t injected_pressed[32];
    uint32_t injected_count;
    uint8_t online;
    uint8_t reset_ack_pending;
    uint8_t fail_input_reads;
    uint32_t power_on_commands;
    uint32_t reset_commands;
} hid_test_state_t;

static hid_test_state_t state;

static void put16(uint8_t *p,uint16_t value)
{p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8U);}

static int fake_transfer(void *context,uint16_t address,uint32_t speed,
                         const uint8_t *write_data,size_t write_length,
                         uint8_t *read_data,size_t read_length)
{
    hid_test_state_t *s=(hid_test_state_t*)context;
    assert((address==0x15U||address==0x16U)&&speed==100000U&&write_length>=2U);
    uint16_t reg=(uint16_t)(write_data[0]|((uint16_t)write_data[1]<<8U));
    ++s->transfer_count;
    if(read_length!=0U) memset(read_data,0,read_length);
    if(reg==0x60U&&read_length==0U&&write_length==5U&&
       write_data[2]==0U&&write_data[3]==0x08U)
    {
        if(write_data[4]==0U) ++s->power_on_commands;
        return 1;
    }
    if(reg==0x60U&&read_length==0U&&write_length==4U&&
       write_data[2]==0U&&write_data[3]==0x01U)
    {
        ++s->reset_commands;
        s->reset_ack_pending=1U;
        return 1;
    }
    if(reg==0x20U)
    {
        assert(read_length==30U);
        memcpy(read_data,address==0x15U?s->hid_descriptor:s->touchpad_hid_descriptor,30U);
        return 1;
    }
    if(reg==0x30U)
    {
        if(address==0x15U)
        {
            assert(read_length==sizeof(keyboard_descriptor));
            memcpy(read_data,keyboard_descriptor,sizeof(keyboard_descriptor));
        }
        else
        {
            assert(read_length==sizeof(touchpad_descriptor));
            memcpy(read_data,touchpad_descriptor,sizeof(touchpad_descriptor));
        }
        return 1;
    }
    if(reg==0x40U)
    {
        assert(read_length==(address==0x15U?10U:14U));
        if(address==0x15U&&s->fail_input_reads) return 0;
        if(s->reset_ack_pending)
        {
            s->reset_ack_pending=0U;
            return 1;
        }
        if(address==0x15U) memcpy(read_data,s->input_report,10U);
        else memcpy(read_data,s->touchpad_input_report,14U);
        return 1;
    }
    return 0;
}

static void set_bits(uint8_t *bytes,uint32_t offset,uint8_t size,
                      uint32_t value)
{
    for(uint32_t bit=0U;bit<size;++bit)
    {
        uint32_t position=offset+bit;
        uint8_t mask=(uint8_t)(1U<<(position&7U));
        if((value&(1U<<bit))!=0U) bytes[position>>3U]|=mask;
        else bytes[position>>3U]&=(uint8_t)~mask;
    }
}

static void set_touchpad_report(uint8_t report[14],
                                uint8_t tip1,uint16_t x1,uint16_t y1,
                                uint8_t tip2,uint16_t x2,uint16_t y2,
                                uint8_t contacts)
{
    memset(report,0,14U);
    put16(report,14U);
    uint8_t *payload=report+2U;
    set_bits(payload,0U,1U,tip1);
    set_bits(payload,1U,8U,1U);
    set_bits(payload,9U,16U,x1);
    set_bits(payload,25U,16U,y1);
    set_bits(payload,41U,1U,tip2);
    set_bits(payload,42U,8U,2U);
    set_bits(payload,50U,16U,x2);
    set_bits(payload,66U,16U,y2);
    set_bits(payload,82U,8U,contacts);
}

bool input_register_device(const input_device_descriptor_t *descriptor,
                           uint32_t *device_id_out)
{
    assert(descriptor&&descriptor->bus==INPUT_BUS_I2C);
    state.capabilities[state.registered_count]=descriptor->capabilities;
    ++state.registered_count;
    if(device_id_out) *device_id_out=100U+state.registered_count;
    return true;
}
bool input_unregister_device(uint32_t id){(void)id;return true;}
bool input_set_device_online(uint32_t id,bool online)
{(void)id;state.online=(uint8_t)online;return true;}
bool input_report_event(const input_event_t *event)
{
    assert(event&&state.event_count<32U);
    state.events[state.event_count++]=*event;
    return true;
}
void keyboard_inject_hid_usage(uint16_t usage,bool pressed)
{
    assert(state.injected_count<32U);
    state.injected_usage[state.injected_count]=usage;
    state.injected_pressed[state.injected_count]=(uint8_t)pressed;
    ++state.injected_count;
}
uint32_t interrupts_get_ticks(void){return state.tick;}
void sleep_ms(uint32_t milliseconds){(void)milliseconds;}
void serial_write(const char *message){(void)message;}

int main(void)
{
    memset(&state,0,sizeof(state));
    put16(state.hid_descriptor+0U,30U);
    put16(state.hid_descriptor+2U,0x0100U);
    put16(state.hid_descriptor+4U,sizeof(keyboard_descriptor));
    put16(state.hid_descriptor+6U,0x30U);
    put16(state.hid_descriptor+8U,0x40U);
    put16(state.hid_descriptor+10U,10U);
    put16(state.hid_descriptor+12U,0x50U);
    put16(state.hid_descriptor+14U,0U);
    put16(state.hid_descriptor+16U,0x60U);
    put16(state.hid_descriptor+18U,0x70U);
    put16(state.hid_descriptor+20U,0x1234U);
    put16(state.hid_descriptor+22U,0x5678U);
    put16(state.hid_descriptor+24U,0x0100U);

    i2c_bus_t bus={.name="test I2C",.context=&state,
                   .transfer=fake_transfer,.ready=1U};
    assert(hid_i2c_attach(&bus,0x15U,100000U,0x20U,"PNP0C50"));
    assert(state.registered_count==1U);
    state.online=1U;
    assert(state.transfer_count==6U);
    assert(state.power_on_commands==2U&&state.reset_commands==1U);
    assert(!hid_i2c_attach(&bus,0x15U,100000U,0x20U,"PNP0C50"));

    put16(state.input_report,10U);
    state.input_report[2U]=0x02U; /* Left Shift modifier. */
    state.input_report[4U]=0x04U; /* HID usage A. */
    state.tick=1U;
    hid_i2c_poll();
    assert(state.injected_count==2U);
    assert(state.injected_usage[0]==0xE1U&&state.injected_pressed[0]==1U);
    assert(state.injected_usage[1]==0x04U&&state.injected_pressed[1]==1U);
    assert(state.event_count==2U);
    assert(state.events[0].type==INPUT_EVENT_KEY&&state.events[0].value==1);

    memset(state.input_report+2U,0,8U);
    state.tick=2U;
    hid_i2c_poll();
    assert(state.injected_count==4U);
    assert(state.injected_pressed[2]==0U&&state.injected_pressed[3]==0U);
    assert(state.events[2].type==INPUT_EVENT_KEY&&state.events[2].value==0);

    /* A persistent bus failure must release held keys and take the device
       offline, then bounded retries must restore it when the bus recovers. */
    state.input_report[2U]=0x02U;
    state.input_report[4U]=0x04U;
    state.tick=3U;
    hid_i2c_poll();
    assert(state.injected_count==6U);
    assert(state.injected_pressed[4U]==1U&&state.injected_pressed[5U]==1U);
    state.fail_input_reads=1U;
    for(uint32_t i=0U;i<10U;++i)
    {
        ++state.tick;
        hid_i2c_poll();
    }
    assert(state.online==0U);
    assert(state.injected_count==8U);
    assert(state.injected_pressed[6U]==0U&&state.injected_pressed[7U]==0U);

    state.fail_input_reads=0U;
    state.tick+=10U;
    hid_i2c_poll();
    assert(state.online==1U);

    state.input_report[2U]=0x02U;
    state.input_report[4U]=0x04U;
    ++state.tick;
    hid_i2c_poll();
    assert(state.injected_count==10U);
    assert(state.injected_pressed[8U]==1U&&state.injected_pressed[9U]==1U);

    memset(state.input_report+2U,0,8U);
    ++state.tick;
    hid_i2c_poll();
    assert(state.injected_count==12U);

    put16(state.touchpad_hid_descriptor+0U,30U);
    put16(state.touchpad_hid_descriptor+2U,0x0100U);
    put16(state.touchpad_hid_descriptor+4U,sizeof(touchpad_descriptor));
    put16(state.touchpad_hid_descriptor+6U,0x30U);
    put16(state.touchpad_hid_descriptor+8U,0x40U);
    put16(state.touchpad_hid_descriptor+10U,14U);
    put16(state.touchpad_hid_descriptor+16U,0x60U);
    assert(hid_i2c_attach(&bus,0x16U,100000U,0x20U,"PNP0C50"));
    assert(state.registered_count==2U);
    assert((state.capabilities[1]&INPUT_CAP_MOUSE_WHEEL)!=0U);
    assert((state.capabilities[1]&INPUT_CAP_MOUSE_BUTTONS)==0U);

    set_touchpad_report(state.touchpad_input_report,1U,1000U,1000U,
                        1U,3000U,1000U,2U);
    uint32_t event_start=state.event_count;
    ++state.tick;
    hid_i2c_poll();
    assert(state.event_count==event_start);

    set_touchpad_report(state.touchpad_input_report,1U,1000U,700U,
                        1U,3000U,700U,2U);
    ++state.tick;
    hid_i2c_poll();
    assert(state.event_count==event_start+1U);
    assert(state.events[event_start].type==INPUT_EVENT_WHEEL);
    assert(state.events[event_start].value==7);

    set_touchpad_report(state.touchpad_input_report,1U,1000U,700U,
                        0U,3000U,700U,1U);
    ++state.tick;
    hid_i2c_poll();
    set_touchpad_report(state.touchpad_input_report,1U,1100U,700U,
                        0U,3000U,700U,1U);
    ++state.tick;
    hid_i2c_poll();
    assert(state.events[state.event_count-1U].type==
           INPUT_EVENT_RELATIVE_MOTION);
    assert(state.events[state.event_count-1U].value==3);
    assert(state.events[state.event_count-1U].value2==0);
    for(uint32_t i=0U;i<state.event_count;++i)
        assert(state.events[i].type!=INPUT_EVENT_BUTTON);

    puts("HID-over-I2C keyboard recovery and two-finger touchpad tests passed");
    return 0;
}
