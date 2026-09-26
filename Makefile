CC = gcc
AS = nasm
LD = ld

CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -Iinclude
LDFLAGS = -m elf_i386 -T linker.ld

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

interrupts.o: kernel/interrupts.c
	$(CC) $(CFLAGS) -c kernel/interrupts.c -o interrupts.o

kernel.bin: boot.o isr.o kernel.o gdt.o terminal.o keyboard.o interrupts.o
	$(LD) $(LDFLAGS) boot.o isr.o kernel.o gdt.o terminal.o keyboard.o interrupts.o -o kernel.bin

clean:
	rm -f *.o kernel.bin