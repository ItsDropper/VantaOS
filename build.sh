#!/bin/bash

set -e

echo "==> Cleaning..."
make clean

echo "==> Building kernel..."
make

echo "==> Preparing ISO..."
rm -rf isodir/boot/kernel.bin
cp kernel.bin isodir/boot/kernel.bin

echo "==> Creating ISO..."
grub-mkrescue -o VantaOS.iso --modules="video video_bochs vbe gfxterm" isodir

echo "==> Build complete!"
echo "    VantaOS.iso"

echo "==> Preparing persistent FAT32 disk image..."
if command -v mkfs.fat >/dev/null 2>&1; then
    if [ ! -f vantaos.img ]; then
        truncate -s 128M vantaos.img
        mkfs.fat -F 32 vantaos.img >/dev/null
        echo "    Created vantaos.img (128 MiB FAT32)"
    else
        echo "    Using existing vantaos.img"
    fi

    if command -v mmd >/dev/null 2>&1 && command -v mcopy >/dev/null 2>&1; then
        # Build the real on-disk VantaOS system namespace.
        for dir in system system/drivers system/devices home home/user etc; do
            mmd -i vantaos.img "::/$dir" 2>/dev/null || true
        done

        install_system_file() {
            local path="$1"
            shift
            local tmp="/tmp/vantaos-$(basename "$path")"
            printf '%s\\n' "$@" > "$tmp"
            mcopy -o -i vantaos.img "$tmp" "::/$path" >/dev/null
            rm -f "$tmp"
            echo "    Installed /$path"
        }

        install_system_file "system/terminal.vx" \
            "VANTA EXECUTABLE" \
            "name=Terminal" \
            "entry=terminal" \
            "version=1"

        install_system_file "system/drivers/ata.sys" \
            "VANTA SYSTEM DRIVER" \
            "name=ATA Storage" \
            "module=kernel" \
            "status=active" \
            "version=1"

        install_system_file "system/drivers/keyboard.sys" \
            "VANTA SYSTEM DRIVER" \
            "name=Keyboard" \
            "module=kernel" \
            "status=active" \
            "version=1"

        install_system_file "system/drivers/mouse.sys" \
            "VANTA SYSTEM DRIVER" \
            "name=Mouse" \
            "module=kernel" \
            "status=active" \
            "version=1"

        install_system_file "system/drivers/pci.sys" \
            "VANTA SYSTEM DRIVER" \
            "name=PCI Bus" \
            "module=kernel" \
            "status=active" \
            "version=1"

        install_system_file "system/drivers/fat32.sys" \
            "VANTA SYSTEM DRIVER" \
            "name=FAT32 Filesystem" \
            "module=kernel" \
            "status=active" \
            "version=1"

        install_system_file "system/devices/disk0.dev" \
            "VANTA DEVICE" \
            "name=Primary Storage" \
            "driver=ata.sys" \
            "filesystem=fat32" \
            "status=online"

        install_system_file "system/devices/keyboard0.dev" \
            "VANTA DEVICE" \
            "name=Keyboard" \
            "driver=keyboard.sys" \
            "status=online"

        install_system_file "system/devices/mouse0.dev" \
            "VANTA DEVICE" \
            "name=Mouse" \
            "driver=mouse.sys" \
            "status=online"

        install_system_file "etc/system.conf" \
            "VANTA SYSTEM CONFIGURATION" \
            "version=1" \
            "kernel=kernel.bin" \
            "os=vantaos"
    else
        echo "    mtools not found; /system/terminal.vx was not installed."
        echo "    Install mtools to populate system files in vantaos.img."
    fi
else
    echo "    mkfs.fat not found; disk image was not created."
    echo "    Install dosfstools to create the persistent FAT32 image."
fi
