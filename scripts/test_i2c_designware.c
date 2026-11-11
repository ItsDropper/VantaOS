/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "designware.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define REG_CON 0x00U
#define REG_TAR 0x04U
#define REG_DATA_CMD 0x10U
#define REG_SS_H 0x14U
#define REG_SS_L 0x18U
#define REG_FS_H 0x1CU
#define REG_FS_L 0x20U
#define REG_RAW 0x34U
#define REG_CLR_TX_ABRT 0x54U
#define REG_ENABLE 0x6CU
#define REG_STATUS 0x70U
#define REG_TXFLR 0x74U
#define REG_RXFLR 0x78U
#define REG_ENABLE_STATUS 0x9CU
#define REG_COMP_PARAM 0xF4U
#define REG_COMP_TYPE 0xFCU
#define FIFO_CAPACITY 64U

typedef struct
{
    uint32_t regs[64];
    uint8_t rx[FIFO_CAPACITY];
    uint32_t rx_read, rx_write;
    uint32_t writes[128];
    uint32_t write_count;
    uint16_t target;
    uint8_t stuck_tfnf;
    uint8_t abort;
} mock_dw_t;

static uint32_t mock_read(void *context, uint32_t offset)
{
    mock_dw_t *mock=(mock_dw_t*)context;
    if(offset==REG_COMP_TYPE) return mock->regs[offset/4U];
    if(offset==REG_COMP_PARAM) return mock->regs[offset/4U];
    if(offset==REG_ENABLE_STATUS) return mock->regs[REG_ENABLE/4U]&1U;
    if(offset==REG_STATUS)
    {
        uint32_t status=0U;
        if(!mock->stuck_tfnf) status|=1U<<1U;
        status|=1U<<2U;
        if(mock->rx_read!=mock->rx_write) status|=1U<<3U;
        return status;
    }
    if(offset==REG_TXFLR) return 0U;
    if(offset==REG_RXFLR) return mock->rx_write-mock->rx_read;
    if(offset==REG_RAW) return mock->abort?(1U<<6U):0U;
    if(offset==REG_CLR_TX_ABRT){mock->abort=0U;return 0U;}
    if(offset==REG_DATA_CMD)
    {
        assert(mock->rx_read!=mock->rx_write);
        return mock->rx[mock->rx_read++%FIFO_CAPACITY];
    }
    return mock->regs[offset/4U];
}

static void mock_write(void *context, uint32_t offset, uint32_t value)
{
    mock_dw_t *mock=(mock_dw_t*)context;
    if(offset==REG_TAR) mock->target=(uint16_t)value;
    if(offset==REG_DATA_CMD)
    {
        if(mock->write_count<128U) mock->writes[mock->write_count++]=value;
        if(value&(1U<<8U))
        {
            uint32_t index=mock->rx_write++;
            mock->rx[index%FIFO_CAPACITY]=(uint8_t)(0xA0U+index);
        }
    }
    if(offset==REG_ENABLE) mock->regs[offset/4U]=value&1U;
    else if(offset/4U<64U) mock->regs[offset/4U]=value;
}

static void prepare(mock_dw_t *mock, i2c_dw_bus_t *bus)
{
    memset(mock,0,sizeof(*mock));
    mock->regs[REG_COMP_TYPE/4U]=I2C_DW_COMP_TYPE;
    mock->regs[REG_COMP_PARAM/4U]=(15U<<16U)|(15U<<8U);
    mock->regs[REG_SS_H/4U]=400U;
    mock->regs[REG_SS_L/4U]=470U;
    mock->regs[REG_FS_H/4U]=80U;
    mock->regs[REG_FS_L/4U]=100U;
    mock->regs[REG_CON/4U]=(1U<<0U)|(1U<<5U)|(1U<<6U)|(1U<<1U);
    mock->regs[REG_ENABLE/4U]=1U;
    memset(bus,0,sizeof(*bus));
    bus->context=mock;
    bus->read32=mock_read;
    bus->write32=mock_write;
    bus->timeout_spins=64U;
}

int main(void)
{
    mock_dw_t mock;
    i2c_dw_bus_t bus;
    prepare(&mock,&bus);
    assert(i2c_dw_initialize(&bus));
    assert(bus.ready && bus.tx_fifo_depth==16U && bus.rx_fifo_depth==16U);
    assert(mock.regs[REG_ENABLE/4U]==1U);

    const uint8_t address_register[]={0x20U,0x00U};
    assert(i2c_dw_transfer(&bus,0x15U,100000U,address_register,
                           sizeof(address_register),NULL,0U));
    assert(mock.target==0x15U);
    assert(mock.write_count==2U);
    assert(mock.writes[0]==0x20U);
    assert(mock.writes[1]==(0x00U|(1U<<9U)));

    /* A stale RX byte from an interrupted transfer is discarded before the
     * next transaction rather than being mistaken for its first response. */
    mock.rx[mock.rx_write++%FIFO_CAPACITY]=0xEEU;
    mock.write_count=0U;
    uint8_t received[4]={0};
    assert(i2c_dw_transfer(&bus,0x15U,400000U,address_register,
                           sizeof(address_register),received,sizeof(received)));
    assert(mock.target==0x15U);
    assert(mock.write_count==6U);
    assert(mock.writes[0]==0x20U && mock.writes[1]==0x00U);
    assert(mock.writes[2]==((1U<<8U)|(1U<<10U)));
    assert(mock.writes[5]==((1U<<8U)|(1U<<9U)));
    for(uint32_t i=0U;i<4U;++i) assert(received[i]!=0U);
    assert(mock.rx_read==mock.rx_write);

    assert(!i2c_dw_transfer(&bus,0U,100000U,address_register,2U,NULL,0U));
    assert(!i2c_dw_transfer(&bus,0x80U,100000U,address_register,2U,NULL,0U));
    mock.abort=1U;
    assert(!i2c_dw_transfer(&bus,0x15U,100000U,address_register,2U,NULL,0U));
    assert(mock.abort==0U);
    assert(i2c_dw_transfer(&bus,0x15U,100000U,address_register,2U,NULL,0U));
    assert(!i2c_dw_transfer(&bus,0x15U,1000000U,address_register,2U,NULL,0U));
    assert(!i2c_dw_transfer(&bus,0x15U,100000U,NULL,1U,NULL,0U));

    prepare(&mock,&bus);
    mock.regs[REG_COMP_TYPE/4U]=0U;
    assert(!i2c_dw_initialize(&bus));
    prepare(&mock,&bus);
    mock.regs[REG_FS_H/4U]=0U;
    mock.regs[REG_FS_L/4U]=0U;
    assert(i2c_dw_initialize(&bus));
    assert(!bus.fast_speed_valid);
    assert(!i2c_dw_transfer(&bus,0x15U,400000U,address_register,2U,NULL,0U));

    prepare(&mock,&bus);
    mock.stuck_tfnf=1U;
    assert(i2c_dw_initialize(&bus));
    assert((mock_read(&mock,REG_STATUS)&(1U<<1U))==0U);
    assert(!i2c_dw_transfer(&bus,0x15U,100000U,address_register,2U,NULL,0U));
    assert(mock.write_count==0U);
    puts("DesignWare I2C PIO transfer tests passed");
    return 0;
}
