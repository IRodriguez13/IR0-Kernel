# Makefile para IR0 Kernel - Panic Handler
# Compilación para arquitectura i386

# Compiladores y herramientas
CC = gcc
AS = nasm
LD = ld
OBJCOPY = objcopy

# Directorios
SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin

# Flags del compilador
CFLAGS = -m32 -ffreestanding -nostdlib -nostartfiles -nodefaultlibs \
         -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
         -Wall -Wextra -Werror -std=c99 \
         -I$(INCLUDE_DIR) -O2

# Flags del assembler
ASFLAGS = -f elf32

# Flags del linker
LDFLAGS = -m elf_i386 -nostdlib -T linker.ld

# Archivos fuente
C_SOURCES = $(SRC_DIR)/panic.c \
            $(SRC_DIR)/print.c \
            $(SRC_DIR)/memory.c \
            $(SRC_DIR)/kernel.c

ASM_SOURCES = $(SRC_DIR)/boot.asm

# Archivos objeto
C_OBJECTS = $(C_SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
ASM_OBJECTS = $(ASM_SOURCES:$(SRC_DIR)/%.asm=$(OBJ_DIR)/%.o)
OBJECTS = $(C_OBJECTS) $(ASM_OBJECTS)

# Archivos de salida
KERNEL_BIN = $(BIN_DIR)/kernel.bin
KERNEL_ELF = $(BIN_DIR)/kernel.elf
ISO_FILE = $(BIN_DIR)/ir0.iso

# Target principal
all: directories $(KERNEL_BIN)

# Crear directorios necesarios
directories:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR)

# Compilar kernel ELF
$(KERNEL_ELF): $(OBJECTS) linker.ld
	@echo "Linking kernel..."
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)
	@echo "Kernel ELF created: $@"

# Crear binario plano del kernel
$(KERNEL_BIN): $(KERNEL_ELF)
	@echo "Creating kernel binary..."
	$(OBJCOPY) -O binary $< $@
	@echo "Kernel binary created: $@"

# Compilar archivos C
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Compilar archivos ASM
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.asm
	@echo "Assembling $<..."
	$(AS) $(ASFLAGS) $< -o $@

# Compilar solo el panic handler
panic: directories $(OBJ_DIR)/panic.o $(OBJ_DIR)/print.o
	@echo "Compiling panic handler..."
	$(CC) $(CFLAGS) -c $(SRC_DIR)/panic.c -o $(OBJ_DIR)/panic.o
	@echo "Panic handler compiled successfully"

# Test básico del panic handler
test-panic: panic
	@echo "Testing panic handler compilation..."
	@nm $(OBJ_DIR)/panic.o | grep panic_advanced && echo "✓ panic_advanced found" || echo "✗ panic_advanced missing"
	@nm $(OBJ_DIR)/panic.o | grep dump_registers && echo "✓ dump_registers found" || echo "✗ dump_registers missing"
	@nm $(OBJ_DIR)/panic.o | grep dump_stack_trace && echo "✓ dump_stack_trace found" || echo "✗ dump_stack_trace missing"
	@echo "Panic handler test completed"

# Ejecutar en QEMU
run: $(KERNEL_BIN)
	@echo "Starting QEMU..."
	qemu-system-i386 -kernel $(KERNEL_BIN) -m 256M

# Debug con GDB
debug: $(KERNEL_ELF)
	@echo "Starting QEMU with GDB server..."
	qemu-system-i386 -kernel $(KERNEL_BIN) -m 256M -s -S &
	@echo "Starting GDB..."
	gdb $(KERNEL_ELF) -ex "target remote localhost:1234"

# Crear imagen ISO booteable
iso: $(KERNEL_BIN)
	@echo "Creating ISO image..."
	@mkdir -p $(BUILD_DIR)/iso/boot/grub
	@cp $(KERNEL_BIN) $(BUILD_DIR)/iso/boot/
	@echo 'menuentry "IR0 Kernel" { multiboot /boot/kernel.bin }' > $(BUILD_DIR)/iso/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO_FILE) $(BUILD_DIR)/iso
	@echo "ISO created: $(ISO_FILE)"

# Análisis del código
analyze: $(C_SOURCES)
	@echo "Running static analysis..."
	cppcheck --enable=all --std=c99 $(C_SOURCES)
	@echo "Analysis completed"

# Información del binario
info: $(KERNEL_ELF)
	@echo "=== KERNEL INFO ==="
	@file $(KERNEL_ELF)
	@size $(KERNEL_ELF)
	@echo
	@echo "=== SYMBOLS ==="
	@nm $(KERNEL_ELF) | head -20
	@echo
	@echo "=== SECTIONS ==="
	@readelf -S $(KERNEL_ELF)

# Dump de assembly
disasm: $(KERNEL_ELF)
	@echo "Disassembling kernel..."
	objdump -d $(KERNEL_ELF) > $(BUILD_DIR)/kernel_disasm.txt
	@echo "Disassembly saved to $(BUILD_DIR)/kernel_disasm.txt"

# Limpiar archivos compilados
clean:
	@echo "Cleaning build files..."
	@rm -rf $(BUILD_DIR)
	@echo "Clean completed"

# Limpiar completamente
distclean: clean
	@echo "Removing all generated files..."
	@rm -f *.o *.bin *.elf *.iso
	@echo "Distribution clean completed"

# Verificar dependencias
check-deps:
	@echo "Checking dependencies..."
	@which $(CC) >/dev/null || (echo "ERROR: $(CC) not found" && exit 1)
	@which $(AS) >/dev/null || (echo "ERROR: $(AS) not found" && exit 1)
	@which $(LD) >/dev/null || (echo "ERROR: $(LD) not found" && exit 1)
	@which qemu-system-i386 >/dev/null || (echo "WARNING: qemu-system-i386 not found")
	@which gdb >/dev/null || (echo "WARNING: gdb not found")
	@echo "Dependencies check completed"

# Mostrar ayuda
help:
	@echo "IR0 Kernel Makefile"
	@echo "=================="
	@echo "Available targets:"
	@echo "  all        - Compile complete kernel"
	@echo "  panic      - Compile only panic handler"
	@echo "  test-panic - Test panic handler compilation"
	@echo "  run        - Run kernel in QEMU"
	@echo "  debug      - Debug kernel with GDB"
	@echo "  iso        - Create bootable ISO"
	@echo "  analyze    - Run static code analysis"
	@echo "  info       - Show kernel binary information"
	@echo "  disasm     - Disassemble kernel"
	@echo "  clean      - Remove build files"
	@echo "  distclean  - Remove all generated files"
	@echo "  check-deps - Check required dependencies"
	@echo "  help       - Show this help"

# Declarar targets que no generan archivos
.PHONY: all directories panic test-panic run debug iso analyze info disasm clean distclean check-deps help

# Configuración por defecto
.DEFAULT_GOAL := panic
