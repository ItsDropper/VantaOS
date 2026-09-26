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
grub-mkrescue -o VantaOS.iso isodir

echo "==> Build complete!"
echo "    VantaOS.iso"