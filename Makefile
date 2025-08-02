# Makefile

CFLAGS=-m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -fno-pic -nodefaultlibs
LDFLAGS=-m elf_i386

all: kernel.iso

boot.o: boot.asm
	nasm -f elf boot.asm -o boot.o

kernel.o: kernel.c
	gcc $(CFLAGS) -c kernel.c -o kernel.o

kernel.bin: boot.o kernel.o linker.ld
	ld $(LDFLAGS) -T linker.ld -o kernel.bin boot.o kernel.o

kernel.iso: kernel.bin
	mkdir -p iso/boot/grub
	cp kernel.bin iso/boot/kernel.bin
	echo 'set timeout=0' > iso/boot/grub/grub.cfg
	echo 'set default=0' >> iso/boot/grub/grub.cfg
	echo 'menuentry "Mi Kernel" { multiboot /boot/kernel.bin }' >> iso/boot/grub/grub.cfg
	grub-mkrescue -o kernel.iso iso/ > /dev/null 2>&1

clean:
	rm -rf *.o *.bin iso kernel.iso
