# VantaOS top-level build orchestrator.
# Subsystems declare sources in nearby Makefiles; this file owns build policy.

CC       = gcc
AS       = nasm
LD       = ld
OBJDIR   := build/obj

CFLAGS   := -m64 -march=x86-64 -mtune=generic -ffreestanding -fno-pie \
            -fno-stack-protector -mno-red-zone -mgeneral-regs-only \
            -nostdlib -Iinclude -MMD -MP
ASFLAGS  := -f elf64
LDFLAGS  := -m elf_x86_64 --no-warn-rwx-segments -T build/linker.ld

ifneq ($(VANTAOS_SELF_TEST),)
CFLAGS += -DVANTAOS_SELF_TEST
endif

include src/boot/Makefile
include src/kernel/Makefile
include src/os/Makefile

ASM_SOURCES := $(VANTAOS_BOOT_ASM) $(VANTAOS_ARCH_ASM)
C_SOURCES   := $(VANTAOS_KERNEL_C) $(VANTAOS_ARCH_C) $(VANTAOS_CORE_C) \
               $(VANTAOS_DRIVER_C) $(VANTAOS_GRAPHICS_C) \
               $(VANTAOS_STORAGE_C) $(VANTAOS_OS_C) \
               $(VANTAOS_TERMINAL_C) $(VANTAOS_DESKTOP_C)
OBJECTS     := $(patsubst %.asm,$(OBJDIR)/%.o,$(ASM_SOURCES)) \
               $(patsubst %.c,$(OBJDIR)/%.o,$(C_SOURCES))
DEPFILES    := $(OBJECTS:.o=.d)

.PHONY: all clean list-sources
all: kernel.bin

kernel.bin: $(OBJECTS) build/linker.ld
	$(LD) $(LDFLAGS) $(OBJECTS) -o $@

$(OBJDIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.asm
	@mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

clean:
	rm -rf $(OBJDIR) kernel.bin *.o

list-sources:
	@printf '%s\n' 'Assembly sources:' $(ASM_SOURCES) 'C sources:' $(C_SOURCES)

-include $(DEPFILES)
