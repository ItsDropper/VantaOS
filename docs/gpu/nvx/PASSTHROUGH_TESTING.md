# Testing NVX on a real GPU with VFIO passthrough

Status: procedure; **not yet executed on hardware by the project.** Last reviewed: 2026-11-13.

NVX has no NVIDIA GPU emulator, so the only way to test it is against a
physical card. The safest route is to run VantaOS in a QEMU/KVM guest with the
card passed through by VFIO, **on a machine that is not your daily PC**. The
IOMMU confines the card's DMA to the guest, but a misbehaving GPU can still
hang the host PCIe link or require a reboot.

## What works and what does not

| Setup | Real GPU visible to VantaOS? |
| --- | --- |
| Linux host + QEMU/KVM + VFIO | **Yes** |
| Linux host, bare metal boot of VantaOS | Yes (highest risk; see [First hardware boot](../../project/FIRST_HARDWARE_BOOT.md)) |
| QEMU on Windows / macOS | No: no PCI passthrough |
| WSL2 | No: GPU-PV exposes a paravirtual DirectX device, not a PCI device |
| Hyper-V GPU-P | No: paravirtual, not a PCI device |

## Requirements

- A Linux machine with VT-d/AMD-Vi and KVM, and a second GPU or iGPU for the
  host display (the passed-through card must not drive the host console).
- An RTX 3060 (`10de:2503`; the LHR variant is `10de:2504`).
- The card alone in its IOMMU group, or only with its HDMI audio function.
- The NVIDIA GSP firmware files you are entitled to use, pinned with a
  `SHA256SUMS`, placed on the guest's FAT32 disk (see below).

## Procedure

1. **Enable the IOMMU.** Turn on VT-d/AMD-Vi in firmware and add
   `intel_iommu=on iommu=pt` (Intel) or `amd_iommu=on iommu=pt` (AMD) to the
   kernel command line. Reboot.
2. **Identify the card and its group.**
   ```sh
   lspci -nnk | grep -A3 -i nvidia
   for g in /sys/kernel/iommu_groups/*; do echo "group ${g##*/}:"; ls "$g/devices"; done
   ```
3. **Hand it to vfio-pci** (the helper does this for the GPU and its audio function):
   ```sh
   sudo scripts/nvx_vfio_qemu.sh bind 0000:01:00.0
   ```
4. **Prepare firmware on the guest disk.** Mount `vantaos.img` and copy the five
   files into `/nvidia/ga106/gsp/`, then create the manifest:
   ```sh
   sudo mount -o loop vantaos.img /mnt   # raw FAT32 image; add offset= if you partitioned it
   sudo mkdir -p /mnt/nvidia/ga106/gsp && sudo cp <your files> /mnt/nvidia/ga106/gsp/
   (cd /mnt/nvidia/ga106/gsp && sudo sh -c 'sha256sum *.bin > SHA256SUMS')
   sudo umount /mnt
   ```
5. **Boot VantaOS with the card attached.**
   ```sh
   sudo scripts/nvx_vfio_qemu.sh run 0000:01:00.0
   ```
   Serial output is written to `nvx-serial.log`.
6. **Run the diagnostics in the guest shell**, in this order. Stop at the first
   unexpected result.
   ```
   nvx status
   nvx survey
   nvx vbios
   nvx fw <firmware-version>
   ```
7. **Analyze the survey** on the host and keep the log:
   ```sh
   python3 scripts/nvx_knowledge.py analyze nvx-serial.log
   python3 scripts/nvx_knowledge.py analyze nvx-serial.log --write-status   # updates facts.tsv
   ```
8. **Return the card to the host driver** when finished:
   ```sh
   sudo scripts/nvx_vfio_qemu.sh unbind 0000:01:00.0
   ```

## What a result proves

| Observation | Proves | Does not prove |
| --- | --- | --- |
| `nvx status` shows the expected chip ID | BAR0 is reachable; PMC identity decodes | Anything beyond identity |
| `nvx survey` matches predictions | Facts become `observed-consistent` | Facts are `hardware-verified` (needs a reviewed run on real hardware, per the policy) |
| `nvx vbios` finds FWSEC | The PROM read and image-chain parse work for that card | FWSEC executes |
| `nvx fw` passes | Files match your pinned hashes and parse | Files are NVIDIA-signed or compatible with the firmware ROM |

Attach the serial log when reporting results; unlogged observations are not
evidence under the [independence policy](INDEPENDENCE.md).

## Stop conditions

Stop and power-cycle the machine if the guest hangs, the host reports PCIe
errors (`dmesg | grep -i aer`), or the card disappears from `lspci`. Do not
retry without recording the exact state in the log. Never run commands beyond
the read-only diagnostics until the earlier ones have produced clean logs.
