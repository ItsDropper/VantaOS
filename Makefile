CC = gcc
AS = nasm
LD = ld

CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -Iinclude
LDFLAGS = -m elf_i386 --no-warn-rwx-segments -T linker.ld

OBJS = boot.o \
       isr.o \
       kernel.o \
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
       pci.o

all: kernel.bin

boot.o: boot.asm
	$(AS) -f elf32 boot.asm -o boot.o

isr.o: kernel/isr.asm
	$(AS) -f elf32 kernel/isr.asm -o isr.o

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

gdt.o: kernel/gdt.c
	$(CC) $(CFLAGS) -c kernel/gdt.c -o gdt.o

terminal.o: kernel/terminal.c
	$(CC) $(CFLAGS) -c kernel/terminal.c -o terminal.o

keyboard.o: kernel/keyboard.c
	$(CC) $(CFLAGS) -c kernel/keyboard.c -o keyboard.o

mouse.o: kernel/mouse.c
	$(CC) $(CFLAGS) -c kernel/mouse.c -o mouse.o

interrupts.o: kernel/interrupts.c
	$(CC) $(CFLAGS) -c kernel/interrupts.c -o interrupts.o

timer.o: kernel/timer.c
	$(CC) $(CFLAGS) -c kernel/timer.c -o timer.o

pmm.o: kernel/pmm.c
	$(CC) $(CFLAGS) -c kernel/pmm.c -o pmm.o

paging.o: kernel/paging.c
	$(CC) $(CFLAGS) -c kernel/paging.c -o paging.o

graphics.o: kernel/graphics.c
	$(CC) $(CFLAGS) -c kernel/graphics.c -o graphics.o

heap.o: kernel/heap.c
	$(CC) $(CFLAGS) -c kernel/heap.c -o heap.o

shell.o: kernel/shell.c
	$(CC) $(CFLAGS) -c kernel/shell.c -o shell.o

pci.o: kernel/pci.c
	$(CC) $(CFLAGS) -c kernel/pci.c -o pci.o

kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o kernel.bin

clean:
	rm -f *.o kernel.bin
