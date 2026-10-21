# VantaOS build orchestrator. Subsystem manifests live beside their code.
CC       = gcc
AS       = nasm
LD       = ld
OBJCOPY  = objcopy
OBJDIR   := build/obj
UEFI_OBJDIR := build/uefi
EFI_CRT := $(firstword $(wildcard /usr/lib/gnuefi/crt0-efi-x86_64.o /usr/lib/crt0-efi-x86_64.o /usr/lib/x86_64-linux-gnu/gnuefi/crt0-efi-x86_64.o))
EFI_LIBDIR := $(patsubst %/,%,$(dir $(EFI_CRT)))
EFI_LDSCRIPT := $(firstword $(wildcard $(EFI_LIBDIR)/elf_x86_64_efi.lds /usr/lib/gnuefi/elf_x86_64_efi.lds /usr/lib/elf_x86_64_efi.lds))

CFLAGS   := -m64 -march=x86-64 -mtune=generic -ffreestanding -fno-pie \
            -fno-stack-protector -mno-red-zone -mgeneral-regs-only \
            -nostdlib -Iinclude -MMD -MP \
            -Werror=implicit-function-declaration \
            -Werror=incompatible-pointer-types \
            -Werror=int-conversion -Werror=int-to-pointer-cast
ASFLAGS  := -f elf64
LDFLAGS  := -m elf_x86_64 --no-warn-rwx-segments -T build/linker.ld

ifneq ($(VANTAOS_SELF_TEST),)
CFLAGS += -DVANTAOS_SELF_TEST
endif

ifneq ($(VANTAOS_PANIC_QEMU_TEST),)
CFLAGS += -DVANTAOS_PANIC_QEMU_TEST
endif

ifneq ($(VANTAOS_BOOT_LOG_TO_FAT32),)
CFLAGS += -DVANTAOS_BOOT_LOG_TO_FAT32
endif

include arch/x86_64/boot/Makefile
include arch/Makefile
include kernel/Makefile
include mm/Makefile
include drivers/Makefile
include net/Makefile
include fs/Makefile
include os/Makefile

ASM_SOURCES := $(VANTAOS_BOOT_ASM) $(VANTAOS_ARCH_ASM)
C_SOURCES   := $(VANTAOS_KERNEL_C) $(VANTAOS_ARCH_C) $(VANTAOS_MM_C) \
               $(VANTAOS_PROC_C) $(VANTAOS_SCHED_C) $(VANTAOS_TIME_C) \
               $(VANTAOS_DRIVER_C) $(VANTAOS_NET_DRIVER_C) $(VANTAOS_NET_C) $(VANTAOS_GRAPHICS_C) $(VANTAOS_FS_C) \
               $(VANTAOS_NET_SELFTEST_C) \
               $(VANTAOS_OS_C) $(VANTAOS_TERMINAL_C) $(VANTAOS_DESKTOP_C)
OBJECTS     := $(patsubst %.asm,$(OBJDIR)/%.o,$(ASM_SOURCES)) \
               $(patsubst %.c,$(OBJDIR)/%.o,$(C_SOURCES))
DEPFILES    := $(OBJECTS:.o=.d)

.PHONY: all clean list-sources uefi setup
all: kernel.bin

# A separate ELF entry keeps the existing Multiboot/GRUB image untouched.
kernel-uefi.elf: $(OBJECTS) build/uefi-linker.ld
	$(LD) -m elf_x86_64 --no-warn-rwx-segments -T build/uefi-linker.ld -e uefi_kernel_entry $(OBJECTS) -o $@

$(UEFI_OBJDIR)/loader.o: boot/uefi/loader.c include/vanta_boot_info.h
	@test -n "$(EFI_CRT)" -a -n "$(EFI_LDSCRIPT)" || (echo "gnu-efi is required (install gnu-efi)."; exit 1)
	@mkdir -p $(@D)
	$(CC) -Iinclude -I/usr/include/efi -I/usr/include/efi/x86_64 -I/usr/include/efi/protocol \
		-fpic -fshort-wchar -mno-red-zone -fno-stack-protector -fno-strict-aliasing \
		-fno-builtin -DEFI_FUNCTION_WRAPPER -c $< -o $@

BOOTX64.EFI: $(UEFI_OBJDIR)/loader.o
	$(LD) -nostdlib -znocombreloc -T $(EFI_LDSCRIPT) -shared -Bsymbolic \
		$(EFI_CRT) $< -L$(EFI_LIBDIR) -lefi -lgnuefi -o $(UEFI_OBJDIR)/loader.so
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
		-j .rel -j .rela -j .reloc --target=efi-app-x86_64 $(UEFI_OBJDIR)/loader.so $@

$(UEFI_OBJDIR)/setup.o: boot/uefi/setup.c include/vanta_installer_fat32.h include/vanta_installer_gpt.h include/vanta_setup_config.h
	@test -n "$(EFI_CRT)" -a -n "$(EFI_LDSCRIPT)" || (echo "gnu-efi is required (install gnu-efi)."; exit 1)
	@mkdir -p $(@D)
	$(CC) -Iinclude -I/usr/include/efi -I/usr/include/efi/x86_64 -I/usr/include/efi/protocol \
		-fpic -fshort-wchar -mno-red-zone -fno-stack-protector -fno-strict-aliasing \
		-fno-builtin -DEFI_FUNCTION_WRAPPER -Wall -Wextra -Werror -c $< -o $@

$(UEFI_OBJDIR)/installer-layout.o: installer/layout.c include/vanta_installer_layout.h
	@mkdir -p $(@D)
	$(CC) -Iinclude -fpic -mno-red-zone -fno-stack-protector -fno-builtin -Wall -Wextra -Werror -c $< -o $@

$(UEFI_OBJDIR)/installer-gpt.o: installer/gpt_writer.c include/vanta_installer_gpt.h include/vanta_installer_layout.h
	@mkdir -p $(@D)
	$(CC) -Iinclude -fpic -mno-red-zone -fno-stack-protector -fno-builtin -Wall -Wextra -Werror -c $< -o $@

$(UEFI_OBJDIR)/installer-fat32.o: installer/fat32_format.c include/vanta_installer_fat32.h
	@mkdir -p $(@D)
	$(CC) -Iinclude -fpic -mno-red-zone -fno-stack-protector -fno-builtin -Wall -Wextra -Werror -c $< -o $@

$(UEFI_OBJDIR)/setup-config.o: installer/setup_config.c include/vanta_setup_config.h
	@mkdir -p $(@D)
	$(CC) -Iinclude -fpic -mno-red-zone -fno-stack-protector -fno-builtin -Wall -Wextra -Werror -c $< -o $@

VANTASETUP.EFI: $(UEFI_OBJDIR)/setup.o $(UEFI_OBJDIR)/installer-layout.o $(UEFI_OBJDIR)/installer-gpt.o $(UEFI_OBJDIR)/installer-fat32.o $(UEFI_OBJDIR)/setup-config.o
	$(LD) -nostdlib -znocombreloc -T $(EFI_LDSCRIPT) -shared -Bsymbolic \
		$(EFI_CRT) $(UEFI_OBJDIR)/setup.o $(UEFI_OBJDIR)/installer-layout.o \
		$(UEFI_OBJDIR)/installer-gpt.o $(UEFI_OBJDIR)/installer-fat32.o \
		$(UEFI_OBJDIR)/setup-config.o -L$(EFI_LIBDIR) -lefi -lgnuefi -o $(UEFI_OBJDIR)/setup.so
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
		-j .rel -j .rela -j .reloc --target=efi-app-x86_64 $(UEFI_OBJDIR)/setup.so $@

VantaOS-Setup.img: VANTASETUP.EFI BOOTX64.EFI kernel-uefi.elf vantaos.img
	@command -v mkfs.fat >/dev/null && command -v mmd >/dev/null && command -v mcopy >/dev/null && command -v sgdisk >/dev/null || \
		(echo "dosfstools, mtools, and gdisk are required to build the setup image."; exit 1)
	@test -s /tmp/vantaos-terminal.elf -a -s /tmp/vantaos-settings.elf -a -s /tmp/vantaos-explorer.elf || \
		(echo "Run build.sh first so the ring-3 application ELFs are available."; exit 1)
	rm -rf /tmp/vantaos-setup-staging
	mkdir -p /tmp/vantaos-setup-staging/system/drivers /tmp/vantaos-setup-staging/system/devices /tmp/vantaos-setup-staging/etc
	for file in system/terminal.vx system/drivers/ata.sys system/drivers/keyboard.sys system/drivers/mouse.sys system/drivers/pci.sys system/drivers/fat32.sys system/devices/disk0.dev system/devices/keyboard0.dev system/devices/mouse0.dev etc/system.conf; do \
		mcopy -i vantaos.img "::/$${file}" "/tmp/vantaos-setup-staging/$${file}" >/dev/null || exit $$?; \
	done
	rm -f $@
	truncate -s 1024M $@
	sgdisk --clear --new=1:2048:0 --typecode=1:EF00 --change-name=1:VANTA_SETUP_MEDIA $@
	mkfs.fat -F 32 --offset=2048 $@ >/dev/null
	mmd -i $@@@1048576 ::/EFI
	mmd -i $@@@1048576 ::/EFI/BOOT
	mmd -i $@@@1048576 ::/EFI/VANTA
	mmd -i $@@@1048576 ::/SYSTEM
	mmd -i $@@@1048576 ::/SYSTEM/DRIVERS
	mmd -i $@@@1048576 ::/SYSTEM/DEVICES
	mmd -i $@@@1048576 ::/ETC
	mmd -i $@@@1048576 ::/HOME
	mmd -i $@@@1048576 ::/HOME/USER
	mcopy -i $@@@1048576 VANTASETUP.EFI ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $@@@1048576 BOOTX64.EFI ::/EFI/VANTA/VANTAOS.EFI
	mcopy -i $@@@1048576 kernel-uefi.elf ::/EFI/VANTA/KERNEL.ELF
	mcopy -i $@@@1048576 /tmp/vantaos-terminal.elf ::/TERMINAL.ELF
	mcopy -i $@@@1048576 /tmp/vantaos-settings.elf ::/SYSTEM/SETTINGS.ELF
	mcopy -i $@@@1048576 /tmp/vantaos-explorer.elf ::/EXPLORER.ELF
	mcopy -i $@@@1048576 /tmp/vantaos-setup-staging/system/terminal.vx ::/SYSTEM/TERMINAL.VX
	mcopy -i $@@@1048576 /tmp/vantaos-setup-staging/system/drivers/ata.sys ::/SYSTEM/DRIVERS/ATA.SYS
	mcopy -i $@@@1048576 /tmp/vantaos-setup-staging/system/drivers/keyboard.sys ::/SYSTEM/DRIVERS/KEYBOARD.SYS
	mcopy -i $@@@1048576 /tmp/vantaos-setup-staging/system/drivers/mouse.sys ::/SYSTEM/DRIVERS/MOUSE.SYS
	mcopy -i $@@@1048576 /tmp/vantaos-setup-staging/system/drivers/pci.sys ::/SYSTEM/DRIVERS/PCI.SYS
	mcopy -i $@@@1048576 /tmp/vantaos-setup-staging/system/drivers/fat32.sys ::/SYSTEM/DRIVERS/FAT32.SYS
	mcopy -i $@@@1048576 /tmp/vantaos-setup-staging/system/devices/disk0.dev ::/SYSTEM/DEVICES/DISK0.DEV
	mcopy -i $@@@1048576 /tmp/vantaos-setup-staging/system/devices/keyboard0.dev ::/SYSTEM/DEVICES/KEYBOARD0.DEV
	mcopy -i $@@@1048576 /tmp/vantaos-setup-staging/system/devices/mouse0.dev ::/SYSTEM/DEVICES/MOUSE0.DEV
	mcopy -i $@@@1048576 /tmp/vantaos-setup-staging/etc/system.conf ::/ETC/SYSTEM.CONF

VantaOS-UEFI.img: BOOTX64.EFI kernel-uefi.elf
	@command -v mkfs.fat >/dev/null && command -v mmd >/dev/null && command -v mcopy >/dev/null && command -v sgdisk >/dev/null || \
		(echo "dosfstools, mtools, and gdisk are required to build the UEFI image."; exit 1)
	rm -f $@
	truncate -s 128M $@
	sgdisk --clear --new=1:2048:0 --typecode=1:EF00 --change-name=1:VANTAESP $@
	mkfs.fat -F 32 --offset=2048 $@ >/dev/null
	mmd -i $@@@1048576 ::/EFI
	mmd -i $@@@1048576 ::/EFI/BOOT
	mmd -i $@@@1048576 ::/EFI/VANTA
	mcopy -i $@@@1048576 BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $@@@1048576 kernel-uefi.elf ::/EFI/VANTA/KERNEL.ELF

uefi: VantaOS-UEFI.img

setup: VantaOS-Setup.img

kernel.bin: $(OBJECTS) build/linker.ld
	$(LD) $(LDFLAGS) $(OBJECTS) -o $@

$(OBJDIR)/arch/x86_64/ap_trampoline.bin: arch/x86_64/ap_trampoline.asm
	@mkdir -p $(@D)
	$(AS) -f bin $< -o $@

$(OBJDIR)/arch/x86_64/isr.o: $(OBJDIR)/arch/x86_64/ap_trampoline.bin

$(OBJDIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.asm
	@mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

clean:
	rm -rf $(OBJDIR) $(UEFI_OBJDIR) kernel.bin kernel-uefi.elf BOOTX64.EFI VANTASETUP.EFI VantaOS-UEFI.img VantaOS-Setup.img *.o

list-sources:
	@printf '%s\n' 'Assembly sources:' $(ASM_SOURCES) 'C sources:' $(C_SOURCES)

-include $(DEPFILES)
