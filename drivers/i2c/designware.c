/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "designware.h"

#include <string.h>

#define DW_IC_CON             0x00U
#define DW_IC_TAR             0x04U
#define DW_IC_DATA_CMD        0x10U
#define DW_IC_SS_SCL_HCNT     0x14U
#define DW_IC_SS_SCL_LCNT     0x18U
#define DW_IC_FS_SCL_HCNT     0x1CU
#define DW_IC_FS_SCL_LCNT     0x20U
#define DW_IC_INTR_MASK       0x30U
#define DW_IC_RAW_INTR_STAT   0x34U
#define DW_IC_CLR_TX_ABRT     0x54U
#define DW_IC_ENABLE          0x6CU
#define DW_IC_STATUS          0x70U
#define DW_IC_TXFLR           0x74U
#define DW_IC_RXFLR           0x78U
#define DW_IC_ENABLE_STATUS   0x9CU
#define DW_IC_COMP_PARAM_1    0xF4U
#define DW_IC_COMP_TYPE       0xFCU

#define DW_CON_MASTER         (1U << 0U)
#define DW_CON_SPEED_MASK     (3U << 1U)
#define DW_CON_SPEED_STANDARD (1U << 1U)
#define DW_CON_SPEED_FAST     (2U << 1U)
#define DW_CON_RESTART_EN     (1U << 5U)
#define DW_CON_SLAVE_DISABLE  (1U << 6U)
#define DW_CMD_READ           (1U << 8U)
#define DW_CMD_STOP           (1U << 9U)
#define DW_CMD_RESTART        (1U << 10U)
#define DW_STATUS_ACTIVITY    (1U << 0U)
#define DW_STATUS_MASTER_ACTIVITY (1U << 5U)
#define DW_STATUS_SLAVE_ACTIVITY  (1U << 6U)
#define DW_STATUS_TFNF        (1U << 1U)
#define DW_STATUS_TFE         (1U << 2U)
#define DW_STATUS_RFNE        (1U << 3U)
#define DW_INTR_TX_ABRT       (1U << 6U)
#define DW_TIMEOUT_DEFAULT    1000000U

static uint32_t dw_read(const i2c_dw_bus_t *bus, uint32_t offset)
{
    return bus->read32(bus->context, offset);
}

static void dw_write(const i2c_dw_bus_t *bus, uint32_t offset, uint32_t value)
{
    bus->write32(bus->context, offset, value);
}

static int dw_wait_enabled(const i2c_dw_bus_t *bus, uint32_t expected)
{
    uint32_t timeout = bus->timeout_spins;
    while (timeout-- != 0U)
    {
        uint32_t value = dw_read(bus, DW_IC_ENABLE_STATUS);
        if (value == expected)
            return 1;
        if (value == UINT32_MAX)
            return 0;
        __asm__ volatile ("pause" ::: "memory");
    }
    return 0;
}

static int dw_wait_idle(const i2c_dw_bus_t *bus)
{
    uint32_t timeout = bus->timeout_spins;
    while (timeout-- != 0U)
    {
        uint32_t status = dw_read(bus, DW_IC_STATUS);
        uint32_t tx_level = dw_read(bus, DW_IC_TXFLR);
        if (status == UINT32_MAX || tx_level == UINT32_MAX)
            return 0;
        if ((status & (DW_STATUS_ACTIVITY | DW_STATUS_MASTER_ACTIVITY |
                       DW_STATUS_SLAVE_ACTIVITY)) == 0U && tx_level == 0U)
            return 1;
        __asm__ volatile ("pause" ::: "memory");
    }
    return 0;
}

static int dw_restore_controller(i2c_dw_bus_t *bus,uint32_t old_con,
                                 uint32_t old_enable)
{
    dw_write(bus,DW_IC_ENABLE,0U);
    if(!dw_wait_enabled(bus,0U)) return 0;
    dw_write(bus,DW_IC_CON,old_con);
    dw_write(bus,DW_IC_ENABLE,old_enable&1U);
    return dw_wait_enabled(bus,old_enable&1U);
}

static int dw_drain_rx(const i2c_dw_bus_t *bus)
{
    for(uint32_t i=0U;i<bus->rx_fifo_depth;++i)
    {
        uint32_t status=dw_read(bus,DW_IC_STATUS);
        if(status==UINT32_MAX) return 0;
        if((status&DW_STATUS_RFNE)==0U) return 1;
        (void)dw_read(bus,DW_IC_DATA_CMD);
    }
    return (dw_read(bus,DW_IC_STATUS)&DW_STATUS_RFNE)==0U;
}

int i2c_dw_initialize(i2c_dw_bus_t *bus)
{
    uint32_t comp_type, comp_param, old_con, old_enable;
    uint32_t ss_high, ss_low, fs_high, fs_low;
    uint32_t tx_depth, rx_depth;

    if (!bus || !bus->read32 || !bus->write32)
        return 0;
    bus->ready = 0U;
    bus->timeout_spins = bus->timeout_spins != 0U ?
                         bus->timeout_spins : DW_TIMEOUT_DEFAULT;

    comp_type = dw_read(bus, DW_IC_COMP_TYPE);
    comp_param = dw_read(bus, DW_IC_COMP_PARAM_1);
    if (comp_type != I2C_DW_COMP_TYPE || comp_param == UINT32_MAX)
        return 0;

    tx_depth = ((comp_param >> 16U) & 0xFFU) + 1U;
    rx_depth = ((comp_param >> 8U) & 0xFFU) + 1U;
    if (tx_depth == 0U || rx_depth == 0U ||
        tx_depth > 64U || rx_depth > 64U)
        return 0;

    ss_high = dw_read(bus, DW_IC_SS_SCL_HCNT);
    ss_low = dw_read(bus, DW_IC_SS_SCL_LCNT);
    fs_high = dw_read(bus, DW_IC_FS_SCL_HCNT);
    fs_low = dw_read(bus, DW_IC_FS_SCL_LCNT);
    if (ss_high == 0U || ss_low == 0U ||
        ss_high == UINT32_MAX || ss_low == UINT32_MAX)
        return 0;

    old_con = dw_read(bus, DW_IC_CON);
    old_enable = dw_read(bus, DW_IC_ENABLE);
    if (old_con == UINT32_MAX || old_enable == UINT32_MAX ||
        (old_enable & ~1U) != 0U)
        return 0;

    /* Never seize a controller while a master transaction is active. */
    if ((dw_read(bus, DW_IC_STATUS) & (DW_STATUS_ACTIVITY |
        DW_STATUS_MASTER_ACTIVITY | DW_STATUS_SLAVE_ACTIVITY)) != 0U)
        return 0;

    dw_write(bus, DW_IC_INTR_MASK, 0U);
    if (old_enable & 1U)
    {
        dw_write(bus,DW_IC_ENABLE,0U);
        if(!dw_wait_enabled(bus,0U))
        {
            if(!dw_restore_controller(bus,old_con,old_enable))
                bus->ready=0U;
            return 0;
        }
    }

    /* Keep firmware timing counts intact; reject a speed with no timing data. */
    bus->standard_speed_valid = 1U;
    bus->fast_speed_valid = (fs_high != 0U && fs_low != 0U &&
                             fs_high != UINT32_MAX && fs_low != UINT32_MAX);
    bus->tx_fifo_depth = (uint16_t)tx_depth;
    bus->rx_fifo_depth = (uint16_t)rx_depth;

    uint32_t con = old_con;
    con &= ~(DW_CON_SPEED_MASK);
    con |= DW_CON_MASTER | DW_CON_SPEED_STANDARD |
           DW_CON_RESTART_EN | DW_CON_SLAVE_DISABLE;
    dw_write(bus, DW_IC_CON, con);
    dw_write(bus, DW_IC_ENABLE, 1U);
    if(!dw_wait_enabled(bus,1U))
    {
        if(!dw_restore_controller(bus,old_con,old_enable))
            bus->ready=0U;
        bus->standard_speed_valid=0U;
        bus->fast_speed_valid=0U;
        return 0;
    }
    bus->ready = 1U;
    return 1;
}

static int dw_set_speed(i2c_dw_bus_t *bus, uint32_t speed_hz)
{
    uint32_t con;
    uint32_t mode;
    if (speed_hz == 0U || speed_hz > 400000U)
        return 0;
    if (speed_hz <= 100000U)
    {
        if (!bus->standard_speed_valid)
            return 0;
        mode = DW_CON_SPEED_STANDARD;
    }
    else
    {
        if (!bus->fast_speed_valid)
            return 0;
        mode = DW_CON_SPEED_FAST;
    }
    con = dw_read(bus, DW_IC_CON);
    if (con == UINT32_MAX)
        return 0;
    if ((con & DW_CON_SPEED_MASK) != mode)
    {
        uint32_t old_enable=dw_read(bus,DW_IC_ENABLE);
        if(old_enable==UINT32_MAX||(old_enable&1U)==0U||
           !dw_wait_idle(bus))
            return 0;
        dw_write(bus,DW_IC_ENABLE,0U);
        if(!dw_wait_enabled(bus,0U))
        {
            bus->ready=0U;
            return 0;
        }
        con=(con&~DW_CON_SPEED_MASK)|mode;
        dw_write(bus,DW_IC_CON,con);
        dw_write(bus,DW_IC_ENABLE,1U);
        if(!dw_wait_enabled(bus,1U))
        {
            if(!dw_restore_controller(bus,con&~DW_CON_SPEED_MASK|
                                      (mode==DW_CON_SPEED_FAST?
                                       DW_CON_SPEED_STANDARD:DW_CON_SPEED_FAST),
                                      old_enable))
                bus->ready=0U;
            return 0;
        }
    }
    return 1;
}

static int dw_check_abort(const i2c_dw_bus_t *bus)
{
    uint32_t raw = dw_read(bus, DW_IC_RAW_INTR_STAT);
    if (raw == UINT32_MAX)
        return -1;
    if ((raw & DW_INTR_TX_ABRT) == 0U)
        return 0;
    (void)dw_read(bus, DW_IC_CLR_TX_ABRT);
    return -1;
}

int i2c_dw_transfer(i2c_dw_bus_t *bus, uint16_t address,
                    uint32_t speed_hz, const uint8_t *write_data,
                    size_t write_length, uint8_t *read_data,
                    size_t read_length)
{
    size_t write_sent = 0U;
    size_t read_queued = 0U;
    size_t read_received = 0U;
    uint32_t timeout;

    if (!bus || !bus->ready || address == 0U || address > 0x7FU ||
        (write_length != 0U && !write_data) ||
        (read_length != 0U && !read_data) ||
        (write_length == 0U && read_length == 0U) ||
        write_length > I2C_DW_MAX_WRITE || read_length > I2C_DW_MAX_READ)
        return 0;
    if(!dw_wait_idle(bus)||!dw_drain_rx(bus)||!dw_set_speed(bus,speed_hz))
        return 0;

    if(dw_read(bus,DW_IC_ENABLE_STATUS)!=1U)
    {bus->ready=0U;return 0;}
    dw_write(bus,DW_IC_TAR,address);
    (void)dw_read(bus, 0x40U); /* Clear accumulated controller interrupts. */

    timeout = bus->timeout_spins;
    while (timeout-- != 0U)
    {
        if(dw_check_abort(bus)!=0)
        {
            if(!dw_wait_idle(bus)||!dw_drain_rx(bus))
                bus->ready=0U;
            return 0;
        }

        uint32_t status = dw_read(bus, DW_IC_STATUS);
        uint32_t tx_level = dw_read(bus, DW_IC_TXFLR);
        uint32_t rx_level = dw_read(bus, DW_IC_RXFLR);
        if (status == UINT32_MAX || tx_level == UINT32_MAX ||
            rx_level == UINT32_MAX || tx_level > bus->tx_fifo_depth ||
            rx_level > bus->rx_fifo_depth)
            return 0;

        while (write_sent < write_length &&
               (status & DW_STATUS_TFNF) != 0U &&
               tx_level < bus->tx_fifo_depth)
        {
            uint32_t command = write_data[write_sent];
            if (write_sent + 1U == write_length && read_length == 0U)
                command |= DW_CMD_STOP;
            dw_write(bus, DW_IC_DATA_CMD, command);
            ++write_sent;
            ++tx_level;
            if (tx_level >= bus->tx_fifo_depth)
                status &= ~DW_STATUS_TFNF;
        }

        if (write_sent == write_length && read_length != 0U)
        {
            while (read_queued < read_length &&
                   (status & DW_STATUS_TFNF) != 0U &&
                   tx_level < bus->tx_fifo_depth &&
                   read_queued - read_received < bus->rx_fifo_depth)
            {
                uint32_t command = DW_CMD_READ;
                if (read_queued == 0U && write_length != 0U)
                    command |= DW_CMD_RESTART;
                if (read_queued + 1U == read_length)
                    command |= DW_CMD_STOP;
                dw_write(bus, DW_IC_DATA_CMD, command);
                ++read_queued;
                ++tx_level;
                if (tx_level >= bus->tx_fifo_depth)
                    status &= ~DW_STATUS_TFNF;
            }
        }

        while (read_received < read_length)
        {
            uint32_t current_status=dw_read(bus,DW_IC_STATUS);
            if(current_status==UINT32_MAX) return 0;
            if((current_status&DW_STATUS_RFNE)==0U) break;
            read_data[read_received++]=(uint8_t)dw_read(bus,DW_IC_DATA_CMD);
        }

        if (read_length == 0U && write_sent == write_length)
        {
            if(dw_wait_idle(bus)&&dw_drain_rx(bus))
                return 1;
        }
        else if(read_length!=0U&&read_received==read_length&&
                read_queued==read_length&&dw_wait_idle(bus)&&
                dw_drain_rx(bus))
            return 1;

        __asm__ volatile ("pause" ::: "memory");
    }

    /* A timed-out command is not safe to reuse until the master is quiescent. */
    if(!dw_wait_idle(bus)||!dw_drain_rx(bus))
        bus->ready=0U;
    return 0;
}
