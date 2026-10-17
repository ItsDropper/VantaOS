#ifndef NVME_H
#define NVME_H

/* Probe PCI NVMe controllers and register supported namespaces as block devices. */
void nvme_initialize(void);

#endif
