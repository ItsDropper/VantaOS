#!/bin/bash

set -e

if [ ! -f VantaOS.iso ]; then
    ./build.sh
fi

if [ ! -f vantaos.img ]; then
    echo "vantaos.img is missing."
    echo "Run ./build.sh first so the FAT32 disk image can be created."
    exit 1
fi

exec qemu-system-i386 \
    -m 256M \
    -drive file=VantaOS.iso,media=cdrom,format=raw \
    -drive file=vantaos.img,if=ide,format=raw \
    -boot order=d