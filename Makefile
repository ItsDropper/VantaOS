CC = gcc
AS = nasm
LD = ld

CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -Iinclude
LDFLAGS = -m elf_i386 --no-warn-rwx-segments -T build/linker.ld

OBJS = boot.o \
       isr.o \
       kernel.o \
       desktop.o \
       file_explorer.o \
       gdt.o \
       idt.o \
       pic.o \
       terminal.o \
       keyboard.o \
       mouse.o \
       ata.o \
       interrupts.o \
       panic.o \
       fault_trace.o \
       timer.o \
       pmm.o \
       paging.o \
       graphics.o \
       graphics_core.o \
       graphics_text.o \
       graphics_input.o \
       graphics_ui.o \
       heap.o \
       shell.o \
       shell_core.o \
       shell_files.o \
       shell_system.o \
       shell_commands.o \
       pci.o \
       filesystem.o \
       fat32.o \
       process.o \
       scheduler.o \
       vfs.o \
       terminal_process.o

all: kernel.bin

boot.o: src/boot/boot.asm
	$(AS) -f elf32 src/boot/boot.asm -o boot.o

isr.o: src/kernel/arch/x86/isr.asm
	$(AS) -f elf32 src/kernel/arch/x86/isr.asm -o isr.o

kernel.o: src/kernel/kernel.c
	$(CC) $(CFLAGS) -c src/kernel/kernel.c -o kernel.o

desktop.o: src/kernel/desktop/desktop.c
	$(CC) $(CFLAGS) -c src/kernel/desktop/desktop.c -o desktop.o

file_explorer.o: src/kernel/desktop/file_explorer.c
	$(CC) $(CFLAGS) -c src/kernel/desktop/file_explorer.c -o file_explorer.o

idt.o: src/kernel/arch/x86/idt.c
	$(CC) $(CFLAGS) -c src/kernel/arch/x86/idt.c -o idt.o

pic.o: src/kernel/arch/x86/pic.c
	$(CC) $(CFLAGS) -c src/kernel/arch/x86/pic.c -o pic.o

gdt.o: src/kernel/arch/x86/gdt.c
	$(CC) $(CFLAGS) -c src/kernel/arch/x86/gdt.c -o gdt.o

terminal.o: src/kernel/terminal/terminal.c
	$(CC) $(CFLAGS) -c src/kernel/terminal/terminal.c -o terminal.o

keyboard.o: src/kernel/drivers/input/keyboard.c
	$(CC) $(CFLAGS) -c src/kernel/drivers/input/keyboard.c -o keyboard.o

mouse.o: src/kernel/drivers/input/mouse.c
	$(CC) $(CFLAGS) -c src/kernel/drivers/input/mouse.c -o mouse.o

ata.o: src/kernel/drivers/storage/ata.c
	$(CC) $(CFLAGS) -c src/kernel/drivers/storage/ata.c -o ata.o

interrupts.o: src/kernel/arch/x86/interrupts.c
	$(CC) $(CFLAGS) -c src/kernel/arch/x86/interrupts.c -o interrupts.o

panic.o: src/kernel/panic.c
	$(CC) $(CFLAGS) -c src/kernel/panic.c -o panic.o

timer.o: src/kernel/core/timer.c
	$(CC) $(CFLAGS) -c src/kernel/core/timer.c -o timer.o

pmm.o: src/kernel/core/pmm.c
	$(CC) $(CFLAGS) -c src/kernel/core/pmm.c -o pmm.o

paging.o: src/kernel/core/paging.c
	$(CC) $(CFLAGS) -c src/kernel/core/paging.c -o paging.o

graphics.o: src/kernel/graphics/graphics.c
	$(CC) $(CFLAGS) -c src/kernel/graphics/graphics.c -o graphics.o

heap.o: src/kernel/core/heap.c
	$(CC) $(CFLAGS) -c src/kernel/core/heap.c -o heap.o

shell.o: src/kernel/terminal/shell.c
	$(CC) $(CFLAGS) -c src/kernel/terminal/shell.c -o shell.o

pci.o: src/kernel/drivers/pci/pci.c
	$(CC) $(CFLAGS) -c src/kernel/drivers/pci/pci.c -o pci.o

filesystem.o: src/kernel/storage/filesystem.c
	$(CC) $(CFLAGS) -c src/kernel/storage/filesystem.c -o filesystem.o

fat32.o: src/kernel/storage/fat32.c
	$(CC) $(CFLAGS) -c src/kernel/storage/fat32.c -o fat32.o

process.o: src/kernel/core/process.c
	$(CC) $(CFLAGS) -c src/kernel/core/process.c -o process.o

scheduler.o: src/kernel/core/scheduler.c
	$(CC) $(CFLAGS) -c src/kernel/core/scheduler.c -o scheduler.o

vfs.o: src/kernel/storage/vfs.c
	$(CC) $(CFLAGS) -c src/kernel/storage/vfs.c -o vfs.o

terminal_process.o: src/kernel/terminal/terminal_process.c
	$(CC) $(CFLAGS) -c src/kernel/terminal/terminal_process.c -o terminal_process.o

kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o kernel.bin

clean:
	rm -f *.o kernel.bin

graphics_core.o: src/kernel/graphics/graphics_core.c
	$(CC) $(CFLAGS) -c src/kernel/graphics/graphics_core.c -o graphics_core.o

graphics_text.o: src/kernel/graphics/graphics_text.c
	$(CC) $(CFLAGS) -c src/kernel/graphics/graphics_text.c -o graphics_text.o

graphics_input.o: src/kernel/graphics/graphics_input.c
	$(CC) $(CFLAGS) -c src/kernel/graphics/graphics_input.c -o graphics_input.o

graphics_ui.o: src/kernel/graphics/graphics_ui.c
	$(CC) $(CFLAGS) -c src/kernel/graphics/graphics_ui.c -o graphics_ui.o

shell_core.o: src/kernel/terminal/shell_core.c
	$(CC) $(CFLAGS) -c src/kernel/terminal/shell_core.c -o shell_core.o

shell_files.o: src/kernel/terminal/shell_files.c
	$(CC) $(CFLAGS) -c src/kernel/terminal/shell_files.c -o shell_files.o

shell_system.o: src/kernel/terminal/shell_system.c
	$(CC) $(CFLAGS) -c src/kernel/terminal/shell_system.c -o shell_system.o

shell_commands.o: src/kernel/terminal/shell_commands.c
	$(CC) $(CFLAGS) -c src/kernel/terminal/shell_commands.c -o shell_commands.o

fault_trace.o: src/kernel/fault_trace.c
	gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -Iinclude -c src/kernel/fault_trace.c -o fault_trace.o
