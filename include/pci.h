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