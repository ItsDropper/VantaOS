#!/bin/bash
# Copyright (c) 2026 ItsDropper
#
# This file is part of VantaOS and is licensed under the MIT License.
# See the LICENSE file in the project root for the full license text.


set -e

if [ ! -f VantaOS.iso ]; then
    ./build.sh
fi

if [ ! -f vantaos.img ]; then
    echo "vantaos.img is missing."
    echo "Run ./build.sh first so the FAT32 disk image can be created."
    exit 1
fi

exec qemu-system-x86_64 \
    -m 256M \
    -drive file=VantaOS.iso,media=cdrom,format=raw \
    -drive file=vantaos.img,if=ide,format=raw \
    -boot order=d