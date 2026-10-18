#ifndef PCI_H
#define PCI_H

#include <stdint.h>

struct pci_device
{
    uint8_t bus;
    uint8_t slot;
    uint8_t function;

    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
};

void pci_initialize(void);
uint8_t pci_read_config8(const struct pci_device *device, uint8_t offset);
uint16_t pci_read_config16(const struct pci_device *device, uint8_t offset);
uint32_t pci_read_config32(const struct pci_device *device, uint8_t offset);

int pci_get_bar(const struct pci_device* device, uint8_t index,
                uint64_t* address, int* is_io, int* is_64bit);
int pci_enable_memory_space(const struct pci_device* device);
int pci_enable_io_space(const struct pci_device* device);
/* Enable bus mastering only after DMA buffers and device state are ready. */
int pci_enable_bus_mastering(const struct pci_device* device);
int pci_find_class(uint8_t class_code, uint8_t subclass, int prog_if, int start_index);
int pci_get_bar0(const struct pci_device* device, uint32_t* bar);

int pci_get_device_count(void);

const struct pci_device* pci_get_device(int index);

const char* pci_get_vendor_name(uint16_t vendor_id);

const char* pci_get_device_name(
    uint16_t vendor_id,
    uint16_t device_id
);

const char* pci_get_class_name(
    uint8_t class_code,
    uint8_t subclass
);

#endif