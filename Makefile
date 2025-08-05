# Makefile para IR0 con Scheduler

CFLAGS=-m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -fno-pic -nodefaultlibs -Iinclude
LDFLAGS=-m elf_i386

# Archivos objeto necesarios
OBJS = boot/boot.o \
       kernel/kernel.o \
       IO/print.o \
       Paging/Paging.o \
       interrupt/idt.o \
       interrupt/interrupt.o \
       interrupt/isr_handlers.o \
       panic/panic.o \
       scheduler/scheduler.o \
       scheduler/switch/switch.o \
       test/task_demo.o \
       drivers/timer/pit/pit.o \
       drivers/timer/clock_system.o \
       drivers/timer/best_clock.o \
       drivers/timer/acpi/acpi.o \
       drivers/timer/hpet/hpet.o \
       drivers/timer/lapic/lapic.o

all: kernel.iso

# Compilar archivos ASM
%.o: %.asm
	nasm -f elf $< -o $@

# Compilar archivos C
%.o: %.c
	gcc $(CFLAGS) -c $< -o $@

# Linkear kernel
kernel.bin: $(OBJS) linker.ld
	ld $(LDFLAGS) -T linker.ld -o kernel.bin $(OBJS)

# Crear ISO
kernel.iso: kernel.bin
	mkdir -p iso/boot/grub
	cp kernel.bin iso/boot/kernel.bin
	echo 'set timeout=0' > iso/boot/grub/grub.cfg
	echo 'set default=0' >> iso/boot/grub/grub.cfg
	echo 'menuentry "IR0 Kernel" { multiboot /boot/kernel.bin }' >> iso/boot/grub/grub.cfg
	grub-mkrescue -o kernel.iso iso/ > /dev/null 2>&1

test-kernel:
	rm -rf $(OBJS) kernel.bin iso kernel.iso

.PHONY: all clean