#!/bin/bash
# Copyright (c) 2026 ItsDropper
#
# This file is part of VantaOS and is licensed under the MIT License.
# See the LICENSE file in the project root for the full license text.
#
# Helper for testing NVX against a real NVIDIA GPU through VFIO passthrough.
# See docs/gpu/nvx/PASSTHROUGH_TESTING.md. Run only on a dedicated Linux test
# machine whose host display does not use the card.
#
#   nvx_vfio_qemu.sh bind   <bdf>   bind the GPU (and same-slot functions) to vfio-pci
#   nvx_vfio_qemu.sh run    <bdf>   boot VantaOS with the GPU attached
#   nvx_vfio_qemu.sh unbind <bdf>   return the functions to their previous driver
set -euo pipefail

usage() { sed -n '9,13p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
[ $# -eq 2 ] || usage
action=$1
bdf=$2
[[ $bdf =~ ^[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-7]$ ]] || { echo "bad PCI address: $bdf" >&2; exit 2; }
slot=${bdf%.*}

functions() { for d in /sys/bus/pci/devices/${slot}.*; do [ -e "$d" ] && basename "$d"; done; }

require_nvidia() {
    local vendor
    vendor=$(cat "/sys/bus/pci/devices/$bdf/vendor")
    [ "$vendor" = "0x10de" ] || { echo "$bdf is not an NVIDIA device (vendor $vendor)" >&2; exit 1; }
}

check_iommu() {
    [ -e "/sys/bus/pci/devices/$bdf/iommu_group" ] || {
        echo "No IOMMU group for $bdf: enable VT-d/AMD-Vi and intel_iommu=on / amd_iommu=on" >&2; exit 1; }
}

state_dir=/run/nvx-vfio

case "$action" in
bind)
    [ "$(id -u)" -eq 0 ] || { echo "run as root" >&2; exit 1; }
    require_nvidia; check_iommu
    modprobe vfio-pci
    mkdir -p "$state_dir"
    for f in $(functions); do
        d=/sys/bus/pci/devices/$f
        if [ -e "$d/driver" ]; then
            basename "$(readlink "$d/driver")" > "$state_dir/$f"
            echo "$f" > "$d/driver/unbind"
        fi
        echo vfio-pci > "$d/driver_override"
        echo "$f" > /sys/bus/pci/drivers_probe
        echo "bound $f to vfio-pci"
    done
    ;;
run)
    require_nvidia; check_iommu
    [ -f VantaOS.iso ] && [ -f vantaos.img ] || { echo "run ./build.sh first" >&2; exit 1; }
    : "${VANTAOS_MEMORY:=1G}"
    args=()
    for f in $(functions); do args+=(-device "vfio-pci,host=$f"); done
    # q35 provides a PCIe root complex; the guest sees a real BAR0 for the GPU.
    exec qemu-system-x86_64 -machine q35,accel=kvm -cpu host -smp "${VANTAOS_CPUS:-2}" \
        -m "$VANTAOS_MEMORY" \
        -drive file=VantaOS.iso,media=cdrom,format=raw \
        -drive file=vantaos.img,format=raw,if=ide -boot order=d -vga std \
        "${args[@]}" \
        -serial file:nvx-serial.log -display gtk
    ;;
unbind)
    [ "$(id -u)" -eq 0 ] || { echo "run as root" >&2; exit 1; }
    for f in $(functions); do
        d=/sys/bus/pci/devices/$f
        [ -e "$d/driver" ] && echo "$f" > "$d/driver/unbind" || true
        echo > "$d/driver_override"
        echo "$f" > /sys/bus/pci/drivers_probe
        echo "released $f"
    done
    rm -rf "$state_dir"
    ;;
*) usage ;;
esac
