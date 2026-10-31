/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_DEVICE_H
#define VANTA_DEVICE_H

#include <stdint.h>

#define DEVICE_NAME_MAX 48U
#define DEVICE_MAX 2048U
#define DEVICE_DRIVER_MAX 128U
#define DEVICE_ID_ANY16 0xFFFFU
#define DEVICE_ID_ANY8 0xFFU

typedef enum
{
    DEVICE_BUS_NONE = 0,
    DEVICE_BUS_PLATFORM,
    DEVICE_BUS_PCI,
    DEVICE_BUS_USB,
    DEVICE_BUS_VIRTUAL
} device_bus_t;

typedef enum
{
    DEVICE_STATE_UNREGISTERED = 0,
    DEVICE_STATE_REGISTERED,
    DEVICE_STATE_PROBING,
    DEVICE_STATE_BOUND,
    DEVICE_STATE_FAILED,
    DEVICE_STATE_REMOVING
} device_state_t;

typedef enum
{
    DEVICE_PROBE_NO_MATCH = 0,
    DEVICE_PROBE_BOUND = 1,
    DEVICE_PROBE_FAILED = -1
} device_probe_result_t;

typedef enum
{
    DEVICE_BIND_INVALID = -2,
    DEVICE_BIND_FAILED = -1,
    DEVICE_BIND_NO_MATCH = 0,
    DEVICE_BIND_BOUND = 1
} device_bind_result_t;

typedef struct device device_t;
typedef struct device_driver device_driver_t;

typedef struct
{
    uint16_t vendor_id; /* DEVICE_ID_ANY16 matches any vendor. */
    uint16_t device_id; /* DEVICE_ID_ANY16 matches any device. */
    uint8_t class_code; /* DEVICE_ID_ANY8 matches any class. */
    uint8_t subclass;   /* DEVICE_ID_ANY8 matches any subclass. */
    uint8_t prog_if;    /* DEVICE_ID_ANY8 matches any programming interface. */
} device_id_t;

struct device
{
    char name[DEVICE_NAME_MAX];
    device_bus_t bus;
    device_t* parent;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    void* bus_data;
    /* Non-NULL when a subsystem owns the device's public lifecycle. */
    const void* owner;
    const device_driver_t* driver;
    device_state_t state;
};

struct device_driver
{
    const char* name;
    device_bus_t bus;
    const device_id_t* ids;
    uint32_t id_count;
    device_probe_result_t (*probe)(device_t*);
    void (*remove)(device_t*);
    void* context;
};

/* Device and driver storage remain caller-owned until unregister succeeds. */
int device_register(device_t* device);
int device_unregister(device_t* device);
/* Subsystem-owned devices can only be removed by their registered owner. */
int device_unregister_owned(device_t* device, const void* owner);
int device_acquire(device_t* device);
void device_release(device_t* device);

int device_driver_register(const device_driver_t* driver);
int device_driver_unregister(const device_driver_t* driver);

/* Probe and remove callbacks always execute without the registry lock. */
device_bind_result_t device_bind(device_t* device,
                                 const device_driver_t* driver);
/* Unbind is refused unless the bound driver supplies a hardware-safe remove callback. */
int device_unbind(device_t* device);
uint32_t device_probe_all(void);

uint32_t device_count(void);
/* Returned pointers are borrowed and remain valid only while registered. */
device_t* device_get(uint32_t index);
device_t* device_find(device_bus_t bus, const char* name);

#endif
