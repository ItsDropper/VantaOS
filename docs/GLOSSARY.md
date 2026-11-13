# Glossary

Terms are listed alphabetically. Hardware terms are as used in this project;
they are not authoritative vendor definitions.

| Term | Meaning |
| --- | --- |
| **ADR** | Architecture decision record; see [decisions](decisions/README.md). |
| **AHCI** | Advanced Host Controller Interface, the SATA controller programming model. |
| **BAR** | PCI Base Address Register; assigns a device's MMIO/IO window. NVIDIA's BAR0 is the register aperture. |
| **Booter** | NVIDIA signed firmware (Booter Load/Unload) executed on SEC2 that loads and starts the GSP. |
| **CE** | Copy engine; the GPU DMA engine used for the first CPU-verifiable GPU work. |
| **FRTS** | Firmware Runtime Table Setup; FWSEC command that sets up the protected firmware region in VRAM. |
| **FWSEC** | Firmware section of the VBIOS, run on the GPU to establish the secure environment (including FRTS). |
| **GA10x / GA106** | NVIDIA Ampere GPU family / the chip in the GeForce RTX 3060. |
| **GPFIFO** | Ring of work-submission entries consumed by a GPU channel. |
| **GPUVM** | GPU virtual-memory layer (VA allocator and GPU page tables). |
| **GSP** | GPU System Processor, a RISC-V core on Turing and later that runs NVIDIA's closed resource-manager firmware. |
| **GSP-RM** | The resource manager firmware running on the GSP; the host talks to it with RPCs over shared queues. |
| **IFR** | Image File Registry header that may precede the PCI ROM in a GPU's VBIOS image. |
| **IOMMU** | VT-d / AMD-Vi; remaps device DMA so a passed-through device cannot touch arbitrary host memory. |
| **MSGQ** | The shared command/status message queues between host and GSP-RM. |
| **NVX** | VantaOS's independently engineered NVIDIA GPU driver; see [NVX overview](gpu/nvx/OVERVIEW.md). |
| **PCIR / NPDS / NPDE** | PCI ROM data structures used to walk the image chain and select the expansion ROM. |
| **PMC** | Master control block; `PMC_BOOT_0/42` identify the chip. |
| **PROM** | PCI ROM window in BAR0 through which the VBIOS can be read. |
| **PRAMIN** | BAR0 window into VRAM. |
| **SEC2** | Security engine (Falcon) that runs Booter and FWSEC-style signed code. |
| **VBIOS** | The GPU's firmware ROM image. |
| **VFIO** | Linux framework to hand a PCI device to a VM or userspace safely, using the IOMMU. |
| **VZA** | VantaOS RLE archive format; see [VZA format](VZA_ARCHIVE_FORMAT.md). |
| **WPR** | Write-Protected Region in VRAM that holds the GSP firmware after Booter authenticates it. |
