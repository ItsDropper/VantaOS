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
            -nostdlib -Iinclude -MMD -MP
ASFLAGS  := -f elf64
LDFLAGS  := -m elf_x86_64 --no-warn-rwx-segments -T build/linker.ld

ifneq ($(VANTAOS_SELF_TEST),)
CFLAGS += -DVANTAOS_SELF_TEST
endif

include arch/x86_64/boot/Makefile
include arch/Makefile
include kernel/Makefile
include mm/Makefile
include drivers/Makefile
include fs/Makefile
include os/Makefile

ASM_SOURCES := $(VANTAOS_BOOT_ASM) $(VANTAOS_ARCH_ASM)
C_SOURCES   := $(VANTAOS_KERNEL_C) $(VANTAOS_ARCH_C) $(VANTAOS_MM_C) \
               $(VANTAOS_PROC_C) $(VANTAOS_SCHED_C) $(VANTAOS_TIME_C) \
               $(VANTAOS_DRIVER_C) $(VANTAOS_GRAPHICS_C) $(VANTAOS_FS_C) \
               $(VANTAOS_OS_C) $(VANTAOS_TERMINAL_C) $(VANTAOS_DESKTOP_C)
OBJECTS     := $(patsubst %.asm,$(OBJDIR)/%.o,$(ASM_SOURCES)) \
               $(patsubst %.c,$(OBJDIR)/%.o,$(C_SOURCES))
DEPFILES    := $(OBJECTS:.o=.d)

.PHONY: all clean list-sources uefi
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

VantaOS-UEFI.img: BOOTX64.EFI kernel-uefi.elf
	@command -v mkfs.fat >/dev/null && command -v mmd >/dev/null && command -v mcopy >/dev/null || \
		(echo "dosfstools and mtools are required to build the UEFI image."; exit 1)
	rm -f $@
	truncate -s 128M $@
	mkfs.fat -F 32 $@ >/dev/null
	mmd -i $@ ::/EFI
	mmd -i $@ ::/EFI/BOOT
	mmd -i $@ ::/EFI/VANTA
	mcopy -i $@ BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $@ kernel-uefi.elf ::/EFI/VANTA/KERNEL.ELF

uefi: VantaOS-UEFI.img

kernel.bin: $(OBJECTS) build/linker.ld
	$(LD) $(LDFLAGS) $(OBJECTS) -o $@

$(OBJDIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.asm
	@mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

clean:
	rm -rf $(OBJDIR) $(UEFI_OBJDIR) kernel.bin kernel-uefi.elf BOOTX64.EFI VantaOS-UEFI.img *.o

list-sources:
	@printf '%s\n' 'Assembly sources:' $(ASM_SOURCES) 'C sources:' $(C_SOURCES)

-include $(DEPFILES)
