#!/bin/bash
# Copyright (c) 2026 ItsDropper
#
# This file is part of VantaOS and is licensed under the MIT License.
# See the LICENSE file in the project root for the full license text.


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
    -Iuserspace/include -Iinclude -c userspace/apps/terminal.c -o /tmp/vantaos-terminal.o
ld -m elf_x86_64 -static -nostdlib -Ttext-segment=0x8000000000 \
    -e _start -o /tmp/vantaos-terminal.elf \
    /tmp/vantaos-terminal-crt0.o /tmp/vantaos-terminal.o
gcc -m64 -mcmodel=large -ffreestanding -fno-pie -fno-stack-protector \
    -mno-red-zone -fno-builtin -nostdlib -Wall -Wextra -Werror \
    -Iuserspace/include -Iinclude -c userspace/apps/settings.c -o /tmp/vantaos-settings.o
ld -m elf_x86_64 -static -nostdlib -Ttext-segment=0x8000000000 \
    -e _start -o /tmp/vantaos-settings.elf \
    /tmp/vantaos-terminal-crt0.o /tmp/vantaos-settings.o
gcc -m64 -mcmodel=large -ffreestanding -fno-pie -fno-stack-protector \
    -mno-red-zone -fno-builtin -nostdlib -Wall -Wextra -Werror \
    -Iuserspace/include -Iinclude -c userspace/apps/explorer.c -o /tmp/vantaos-explorer.o
ld -m elf_x86_64 -static -nostdlib -Ttext-segment=0x8000000000 \
    -e _start -o /tmp/vantaos-explorer.elf \
    /tmp/vantaos-terminal-crt0.o /tmp/vantaos-explorer.o
echo "==> Building the ring-3 Explorer end-to-end test ELF..."
gcc -m64 -mcmodel=large -ffreestanding -fno-pie -fno-stack-protector \
    -mno-red-zone -fno-builtin -nostdlib -Wall -Wextra -Werror \
    -DVANTAOS_EXPLORER_E2E_TEST -Iuserspace/include -Iinclude \
    -c userspace/apps/explorer.c -o /tmp/vantaos-explorer-e2e.o
ld -m elf_x86_64 -static -nostdlib -Ttext-segment=0x8000000000 \
    -e _start -o /tmp/vantaos-explorer-e2e.elf \
    /tmp/vantaos-terminal-crt0.o /tmp/vantaos-explorer-e2e.o
test -s /tmp/vantaos-terminal.elf
test -s /tmp/vantaos-settings.elf
test -s /tmp/vantaos-explorer.elf
test -s /tmp/vantaos-explorer-e2e.elf

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
        mcopy -o -i vantaos.img /tmp/vantaos-settings.elf ::/system/SETTINGS.ELF >/dev/null
        echo "    Installed /system/SETTINGS.ELF (static ring-3 Settings application)"
        mcopy -o -i vantaos.img /tmp/vantaos-explorer.elf ::/EXPLORER.ELF >/dev/null
        echo "    Installed /EXPLORER.ELF (static ring-3 File Explorer application)"
        mcopy -o -i vantaos.img /tmp/vantaos-explorer-e2e.elf ::/EXPLORER-E2E.ELF >/dev/null
        echo "    Installed /EXPLORER-E2E.ELF (automated ring-3 Explorer end-to-end tests)"

        # Firmware is redistributable only under its upstream terms and is not
        # checked into this repository. Supply a locally obtained matching
        # RTL8188EU firmware image explicitly when building a bootable disk.
        if [ -n "${RTL8188EU_FIRMWARE:-}" ]; then
            if [ ! -f "$RTL8188EU_FIRMWARE" ]; then
                echo "ERROR: RTL8188EU_FIRMWARE does not name a readable file." >&2
                exit 1
            fi
            firmware_size=$(wc -c < "$RTL8188EU_FIRMWARE")
            if [ "$firmware_size" -le 32 ] || [ "$firmware_size" -gt 32768 ]; then
                echo "ERROR: RTL8188EU firmware image size is outside the supported bounds." >&2
                exit 1
            fi
            mcopy -o -i vantaos.img "$RTL8188EU_FIRMWARE" ::/SYSTEM/RTL8188EU.BIN >/dev/null
            echo "    Installed /SYSTEM/RTL8188EU.BIN ($firmware_size bytes)"
        else
            echo "    RTL8188EU firmware not supplied; USB Wi-Fi firmware bring-up will fail."
        fi

        # The register profile is board/cut-specific and is not synthesized
        # from guessed values or bundled from third-party driver tables.
        if [ -n "${RTL8188EU_PHY_PROFILE:-}" ]; then
            if [ ! -f "$RTL8188EU_PHY_PROFILE" ]; then
                echo "ERROR: RTL8188EU_PHY_PROFILE does not name a readable file." >&2
                exit 1
            fi
            profile_size=$(wc -c < "$RTL8188EU_PHY_PROFILE")
            if [ "$profile_size" -lt 24 ] || [ "$profile_size" -gt 8192 ]; then
                echo "ERROR: RTL8188EU PHY profile size is outside the supported bounds." >&2
                exit 1
            fi
            mcopy -o -i vantaos.img "$RTL8188EU_PHY_PROFILE" ::/SYSTEM/RTL8188EU.PHY >/dev/null
            echo "    Installed /SYSTEM/RTL8188EU.PHY ($profile_size bytes)"
        else
            echo "    RTL8188EU PHY profile not supplied; radio initialization and TX remain disabled."
        fi

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

# The installer must carry the same required system metadata as the persistent image.
echo "==> Building the bootable UEFI VantaOS setup image..."
make setup
