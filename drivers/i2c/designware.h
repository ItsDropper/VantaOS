/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_I2C_DESIGNWARE_H
#define VANTA_I2C_DESIGNWARE_H

#include <stddef.h>
#include <stdint.h>

#define I2C_DW_MAX_WRITE 64U
#define I2C_DW_MAX_READ 4096U
#define I2C_DW_COMP_TYPE 0x44570140U

typedef uint32_t (*i2c_dw_read32_fn)(void *context, uint32_t offset);
typedef void (*i2c_dw_write32_fn)(void *context, uint32_t offset,
                                  uint32_t value);

typedef struct
{
    void *context;
    i2c_dw_read32_fn read32;
    i2c_dw_write32_fn write32;
    uint32_t timeout_spins;
    uint16_t tx_fifo_depth;
    uint16_t rx_fifo_depth;
    uint8_t standard_speed_valid;
    uint8_t fast_speed_valid;
    uint8_t ready;
} i2c_dw_bus_t;

/* Validate the DesignWare signature/FIFO/timing registers and take ownership. */
int i2c_dw_initialize(i2c_dw_bus_t *bus);
/* Synchronous, bounded PIO transfer; no DMA or interrupt dependency. */
int i2c_dw_transfer(i2c_dw_bus_t *bus, uint16_t address,
                    uint32_t speed_hz, const uint8_t *write_data,
                    size_t write_length, uint8_t *read_data,
                    size_t read_length);

#endif
