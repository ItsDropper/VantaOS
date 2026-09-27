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
    -cdrom VantaOS.iso \
    -hda vantaos.img
