CC = gcc
AS = nasm
LD = ld

CFLAGS = -m32 -march=i386 -mtune=generic -ffreestanding -fno-pie -fno-stack-protector -mgeneral-regs-only -mstackrealign -nostdlib -Iinclude
LDFLAGS = -m elf_i386 --no-warn-rwx-segments -T build/linker.ld

OBJS = boot.o isr.o kernel.o os.o desktop.o desktop_process.o file_explorer.o file_explorer_process.o gdt.o idt.o pic.o terminal.o keyboard.o mouse.o ata.o interrupts.o panic.o fault_trace.o timer.o pmm.o paging.o graphics.o graphics_core.o graphics_text.o graphics_input.o graphics_ui.o heap.o shell.o shell_core.o shell_files.o shell_system.o shell_storage.o shell_commands.o pci.o filesystem.o fat32.o process.o scheduler.o vfs.o terminal_process.o

all: kernel.bin

boot.o:
	$(AS) -f elf32 src/boot/boot.asm -o boot.o

isr.o:
	$(AS) -f elf32 src/kernel/arch/x86/isr.asm -o isr.o

kernel.o:
	$(CC) $(CFLAGS) -c src/kernel/kernel.c -o kernel.o

os.o:
	$(CC) $(CFLAGS) -c src/os/os.c -o os.o

desktop.o:
	$(CC) $(CFLAGS) -c src/os/desktop/desktop.c -o desktop.o

desktop_process.o:
	$(CC) $(CFLAGS) -c src/os/desktop/desktop_process.c -o desktop_process.o

file_explorer_process.o:
	$(CC) $(CFLAGS) -c src/os/desktop/file_explorer_process.c -o file_explorer_process.o

file_explorer.o:
	$(CC) $(CFLAGS) -c src/os/desktop/file_explorer.c -o file_explorer.o

gdt.o:
	$(CC) $(CFLAGS) -c src/kernel/arch/x86/gdt.c -o gdt.o

idt.o:
	$(CC) $(CFLAGS) -c src/kernel/arch/x86/idt.c -o idt.o

pic.o:
	$(CC) $(CFLAGS) -c src/kernel/arch/x86/pic.c -o pic.o

terminal.o:
	$(CC) $(CFLAGS) -c src/os/terminal/terminal.c -o terminal.o

keyboard.o:
	$(CC) $(CFLAGS) -c src/kernel/drivers/input/keyboard.c -o keyboard.o

mouse.o:
	$(CC) $(CFLAGS) -c src/kernel/drivers/input/mouse.c -o mouse.o

ata.o:
	$(CC) $(CFLAGS) -c src/kernel/drivers/storage/ata.c -o ata.o

interrupts.o:
	$(CC) $(CFLAGS) -c src/kernel/arch/x86/interrupts.c -o interrupts.o

panic.o:
	$(CC) $(CFLAGS) -c src/kernel/panic.c -o panic.o

fault_trace.o:
	$(CC) $(CFLAGS) -c src/kernel/fault_trace.c -o fault_trace.o

timer.o:
	$(CC) $(CFLAGS) -c src/kernel/core/timer.c -o timer.o

pmm.o:
	$(CC) $(CFLAGS) -c src/kernel/core/pmm.c -o pmm.o

paging.o:
	$(CC) $(CFLAGS) -c src/kernel/core/paging.c -o paging.o

graphics.o:
	$(CC) $(CFLAGS) -c src/kernel/graphics/graphics.c -o graphics.o

graphics_core.o:
	$(CC) $(CFLAGS) -c src/kernel/graphics/graphics_core.c -o graphics_core.o

graphics_text.o:
	$(CC) $(CFLAGS) -c src/kernel/graphics/graphics_text.c -o graphics_text.o

graphics_input.o:
	$(CC) $(CFLAGS) -c src/kernel/graphics/graphics_input.c -o graphics_input.o

graphics_ui.o:
	$(CC) $(CFLAGS) -c src/kernel/graphics/graphics_ui.c -o graphics_ui.o

heap.o:
	$(CC) $(CFLAGS) -c src/kernel/core/heap.c -o heap.o

shell.o:
	$(CC) $(CFLAGS) -c src/os/terminal/shell.c -o shell.o

shell_core.o:
	$(CC) $(CFLAGS) -c src/os/terminal/shell_core.c -o shell_core.o

shell_files.o:
	$(CC) $(CFLAGS) -c src/os/terminal/shell_files.c -o shell_files.o

shell_system.o:
	$(CC) $(CFLAGS) -c src/os/terminal/shell_system.c -o shell_system.o

shell_commands.o:
	$(CC) $(CFLAGS) -c src/os/terminal/shell_commands.c -o shell_commands.o

pci.o:
	$(CC) $(CFLAGS) -c src/kernel/drivers/pci/pci.c -o pci.o

filesystem.o:
	$(CC) $(CFLAGS) -c src/kernel/storage/filesystem.c -o filesystem.o

fat32.o:
	$(CC) $(CFLAGS) -c src/kernel/storage/fat32.c -o fat32.o

process.o:
	$(CC) $(CFLAGS) -c src/kernel/core/process.c -o process.o

scheduler.o:
	$(CC) $(CFLAGS) -c src/kernel/core/scheduler.c -o scheduler.o

vfs.o:
	$(CC) $(CFLAGS) -c src/kernel/storage/vfs.c -o vfs.o

terminal_process.o:
	$(CC) $(CFLAGS) -c src/os/terminal/terminal_process.c -o terminal_process.o

kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o kernel.bin

clean:
	rm -f *.o kernel.bin
