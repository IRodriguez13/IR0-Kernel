# Makefile para IR0 Kernel con Scheduler

# Configuración del compilador
CC = gcc
ASM = nasm
LD = ld

# Flags comunes
CFLAGS = -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
         -fno-pic -nodefaultlibs -Iinclude -Wall -Wextra -O1
ASMFLAGS = -f elf
LDFLAGS = -m elf_i386 -T linker.ld

# Archivos objeto necesarios (ordenados por dependencias)
OBJS = boot/boot.o \
       kernel/kernel.o \
       interrupt/idt.o \
       interrupt/interrupt.o \
       interrupt/isr_handlers.o \
       includes/print.o \
       Paging/Paging.o \
       panic/panic.o \
       drivers/timer/pit/pit.o \
       drivers/timer/clock_system.o \
       drivers/timer/best_clock.o \
       drivers/timer/acpi/acpi.o \
       drivers/timer/hpet/hpet.o \
       drivers/timer/lapic/lapic.o \
       scheduler/scheduler.o \
       scheduler/switch/switch.o \
       test/task_demo.o

# Reglas principales
all: kernel.iso

# Compilación de ASM
%.o: %.asm
	$(ASM) $(ASMFLAGS) $< -o $@

# Compilación de C (con dependencias automáticas)
%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
	@cp $*.d $*.P; \
	sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
	    -e '/^$$/ d' -e 's/$$/ :/' < $*.d >> $*.P; \
	rm -f $*.d

# Incluir dependencias automáticas
-include $(OBJS:.o=.P)

# Linkeo del kernel
kernel.bin: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

# Creación de ISO
kernel.iso: kernel.bin
	@mkdir -p iso/boot/grub
	@cp kernel.bin iso/boot/
	@echo 'set timeout=0\nset default=0\nmenuentry "IR0 Kernel" { multiboot /boot/kernel.bin }' > iso/boot/grub/grub.cfg
	@grub-mkrescue -o $@ iso/ >/dev/null 2>&1
	@echo "ISO generada: kernel.iso"

# Limpieza
clean:
	rm -f $(OBJS) $(OBJS:.o=.P) kernel.bin
	rm -rf iso

distclean: clean
	rm -f kernel.iso

# QEMU (opcional)
run: kernel.iso
	qemu-system-i386 -cdrom kernel.iso -m 512 -serial stdio

.PHONY: all clean distclean run