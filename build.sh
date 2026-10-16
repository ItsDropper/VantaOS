#!/bin/bash

set -e

echo "==> Cleaning..."
make clean

echo "==> Building kernel..."
make

echo "==> Building the ring-3 Terminal ELF..."
gcc -m64 -mcmodel=large -ffreestanding -fno-pie -fno-stack-protector \
    -mno-red-zone -fno-builtin -nostdlib -Wall -Wextra -Werror \
    -Iuserspace/include -c userspace/crt/crt0.S -o /tmp/vantaos-terminal-crt0.o
gcc -m64 -mcmodel=large -ffreestanding -fno-pie -fno-stack-protector \
    -mno-red-zone -fno-builtin -nostdlib -Wall -Wextra -Werror \
    -Iuserspace/include -c userspace/apps/terminal.c -o /tmp/vantaos-terminal.o
ld -m elf_x86_64 -static -nostdlib -Ttext-segment=0x8000000000 \
    -e _start -o /tmp/vantaos-terminal.elf \
    /tmp/vantaos-terminal-crt0.o /tmp/vantaos-terminal.o
gcc -m64 -mcmodel=large -ffreestanding -fno-pie -fno-stack-protector \
    -mno-red-zone -fno-builtin -nostdlib -Wall -Wextra -Werror \
    -Iuserspace/include -Iinclude -c userspace/apps/settings.c -o /tmp/vantaos-settings.o
ld -m elf_x86_64 -static -nostdlib -Ttext-segment=0x8000000000 \
    -e _start -o /tmp/vantaos-settings.elf \
    /tmp/vantaos-terminal-crt0.o /tmp/vantaos-settings.o
test -s /tmp/vantaos-terminal.elf
test -s /tmp/vantaos-settings.elf

echo "==> Preparing ISO..."
rm -rf isodir/boot/kernel.bin
cp kernel.bin isodir/boot/kernel.bin

echo "==> Creating ISO..."
grub-mkrescue -o VantaOS.iso isodir

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
            if ! mdir -i vantaos.img "::/$dir" >/dev/null 2>&1; then
                mmd -i vantaos.img "::/$dir"
            fi
        done

        install_system_file() {
            local path="$1"
            shift
            local tmp="/tmp/vantaos-$(basename "$path")"
            printf '%s\n' "$@" > "$tmp"
            mcopy -o -i vantaos.img "$tmp" "::/$path" >/dev/null
            rm -f "$tmp"
            echo "    Installed /$path"
        }

        mcopy -o -i vantaos.img /tmp/vantaos-terminal.elf ::/TERMINAL.ELF >/dev/null
        echo "    Installed /TERMINAL.ELF (static ring-3 application)"
        mcopy -o -i vantaos.img /tmp/vantaos-settings.elf ::/SETTINGS.ELF >/dev/null
        echo "    Installed /SETTINGS.ELF (static ring-3 Settings application)"

        install_system_file "system/terminal.vx" \
            "VANTA EXECUTABLE" \
            "name=Terminal" \
            "entry=/TERMINAL.ELF" \
            "format=ELF64" \
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
