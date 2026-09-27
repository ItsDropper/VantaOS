CC = gcc
AS = nasm
LD = ld

CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -Iinclude
LDFLAGS = -m elf_i386 --no-warn-rwx-segments -T build/linker.ld

OBJS = boot.o \
       isr.o \
       kernel.o \
       desktop.o \
       gdt.o \
       terminal.o \
       keyboard.o \
       mouse.o \
       interrupts.o \
       timer.o \
       pmm.o \
       paging.o \
       graphics.o \
       heap.o \
       shell.o \
       pci.o \
       filesystem.o \
       process.o

all: kernel.bin

boot.o: src/boot/boot.asm
	$(AS) -f elf32 src/boot/boot.asm -o boot.o

isr.o: src/kernel/arch/x86/isr.asm
	$(AS) -f elf32 src/kernel/arch/x86/isr.asm -o isr.o

kernel.o: src/kernel/kernel.c
	$(CC) $(CFLAGS) -c src/kernel/kernel.c -o kernel.o

desktop.o: src/kernel/desktop/desktop.c
	$(CC) $(CFLAGS) -c src/kernel/desktop.c -o desktop.o

gdt.o: src/kernel/arch/x86/gdt.c
	$(CC) $(CFLAGS) -c src/kernel/arch/x86/gdt.c -o gdt.o

terminal.o: src/kernel/terminal/terminal.c
	$(CC) $(CFLAGS) -c src/kernel/terminal/terminal.c -o terminal.o

keyboard.o: src/kernel/drivers/input/keyboard.c
	$(CC) $(CFLAGS) -c src/kernel/drivers/input/keyboard.c -o keyboard.o

mouse.o: src/kernel/drivers/input/mouse.c
	$(CC) $(CFLAGS) -c src/kernel/drivers/input/mouse.c -o mouse.o

interrupts.o: src/kernel/arch/x86/interrupts.c
	$(CC) $(CFLAGS) -c src/kernel/arch/x86/interrupts.c -o interrupts.o

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

process.o: src/kernel/core/process.c
	$(CC) $(CFLAGS) -c src/kernel/core/process.c -o process.o

kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o kernel.bin

clean:
	rm -f *.o kernel.bin
