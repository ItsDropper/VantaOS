#!/bin/bash
# Copyright (c) 2026 ItsDropper
#
# This file is part of VantaOS and is licensed under the MIT License.
# See the LICENSE file in the project root for the full license text.

set -euo pipefail

if [ ! -f VantaOS.iso ]; then
    ./build.sh
fi

if [ ! -f vantaos.img ]; then
    echo "vantaos.img is missing."
    echo "Run ./build.sh first so the FAT32 disk image can be created."
    exit 1
fi

# Prefer hardware virtualization when the host exposes it. CI and unsupported
# hosts retain the portable TCG path; VANTAOS_ACCEL can override auto-detection.
qemu_accel="${VANTAOS_ACCEL:-auto}"
if [ "$qemu_accel" = "auto" ]; then
    case "$(uname -s)" in
        Linux)
            if [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
                qemu_accel="kvm"
            else
                qemu_accel="tcg"
            fi
            ;;
        Darwin)
            if [ "$(sysctl -n kern.hv_support 2>/dev/null || printf '0')" = "1" ]; then
                qemu_accel="hvf"
            else
                qemu_accel="tcg"
            fi
            ;;
        MINGW*|MSYS*|CYGWIN*)
            if qemu-system-x86_64 -accel help 2>&1 | grep -qi whpx; then
                qemu_accel="whpx"
            else
                qemu_accel="tcg"
            fi
            ;;
        *)
            qemu_accel="tcg"
            ;;
    esac
fi

case "$qemu_accel" in
    kvm|hvf) qemu_cpu="${VANTAOS_CPU:-host}" ;;
    whpx|tcg) qemu_cpu="${VANTAOS_CPU:-max}" ;;
    *)
        echo "Unsupported VANTAOS_ACCEL: $qemu_accel (use kvm, hvf, whpx, or tcg)."
        exit 2
        ;;
esac

qemu_cpus="${VANTAOS_CPUS:-4}"
case "$qemu_cpus" in
    ''|*[!0-9]*)
        echo "VANTAOS_CPUS must be an integer from 1 to 32."
        exit 2
        ;;
esac
if [ "$qemu_cpus" -lt 1 ] || [ "$qemu_cpus" -gt 32 ]; then
    echo "VANTAOS_CPUS must be an integer from 1 to 32."
    exit 2
fi

exec qemu-system-x86_64 \
    -accel "$qemu_accel" \
    -cpu "$qemu_cpu" \
    -smp "$qemu_cpus" \
    -m "${VANTAOS_MEMORY:-512M}" \
    -vga std \
    -name VantaOS \
    -drive file=VantaOS.iso,media=cdrom,format=raw \
    -drive file=vantaos.img,if=ide,format=raw \
    -boot order=d
