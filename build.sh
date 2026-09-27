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
else
    echo "    mkfs.fat not found; disk image was not created."
    echo "    Install dosfstools to create the persistent FAT32 image."
fi
