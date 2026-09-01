#!/usr/bin/env python3
"""
Generate dynamic Makefile based on selected subsystems from .config
This script reads .config and subsystems.json to generate a Makefile
that compiles only the selected subsystems.
"""

import json
import os
import sys
from pathlib import Path


def load_config(config_file):
    """Load configuration from .config file with validation"""
    selected_subsystems = []
    seen_subsystems = set()
    
    if not os.path.exists(config_file):
        print("Warning: .config file not found", file=sys.stderr)
        return selected_subsystems
    
    with open(config_file, 'r') as f:
        for line_num, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            
            # Validate line format
            if '=' not in line:
                print(f"Warning: Invalid line {line_num} in .config: {line}", file=sys.stderr)
                continue
            
            key, value = line.split('=', 1)
            key = key.strip()
            value = value.strip()
            
            # Only process SUBSYSTEM_ entries
            if key.startswith('SUBSYSTEM_'):
                subsystem_id = key.replace('SUBSYSTEM_', '')
                
                # Validate subsystem ID (alphanumeric and underscore only)
                if not subsystem_id.replace('_', '').isalnum():
                    print(f"Warning: Invalid subsystem ID at line {line_num}: {subsystem_id}", file=sys.stderr)
                    continue
                
                # Check for duplicates
                if subsystem_id in seen_subsystems:
                    print(f"Warning: Duplicate subsystem at line {line_num}: {subsystem_id}", file=sys.stderr)
                    continue
                
                # Only add if enabled (=y)
                if value == 'y':
                    selected_subsystems.append(subsystem_id)
                    seen_subsystems.add(subsystem_id)
    
    return selected_subsystems


def load_subsystems_json(subsystems_json):
    """Load subsystems definition from JSON"""
    if not os.path.exists(subsystems_json):
        return None
    
    with open(subsystems_json, 'r') as f:
        return json.load(f)


def get_subsystem_files(subsystems_data, subsystem_id, arch):
    """Get list of files for a subsystem"""
    if not subsystems_data or 'subsystems' not in subsystems_data:
        return []
    
    subsystem = subsystems_data['subsystems'].get(subsystem_id)
    if not subsystem:
        return []
    
    files_dict = subsystem.get('files', {})
    return files_dict.get(arch, [])


def resolve_dependencies(subsystems_data, selected_subsystems):
    """Resolve dependencies for selected subsystems"""
    resolved = set(selected_subsystems)
    to_process = list(selected_subsystems)
    
    while to_process:
        subsystem_id = to_process.pop(0)
        if subsystem_id not in subsystems_data.get('subsystems', {}):
            continue
        
        subsystem = subsystems_data['subsystems'][subsystem_id]
        deps = subsystem.get('dependencies', [])
        
        for dep in deps:
            if dep not in resolved:
                resolved.add(dep)
                to_process.append(dep)
    
    return list(resolved)


def migrate_subsystem_name(old_name):
    """Migrate old subsystem names to new names"""
    migrations = {
        "DRIVERS": None,  # Removed - use specific driver subsystems
        "KEYBOARD": "PS2_KEYBOARD",  # Migrated to PS2_KEYBOARD
        "AUDIO": "AUDIO_SB16",  # Migrated to AUDIO_SB16 (default)
    }
    return migrations.get(old_name, old_name)


def validate_subsystems(subsystems_data, selected_subsystems):
    """Validate that all selected subsystems exist in subsystems.json"""
    valid_subsystems = []
    invalid_subsystems = []
    migrated_subsystems = []
    
    available = set(subsystems_data.get('subsystems', {}).keys())
    
    for subsystem_id in selected_subsystems:
        # Try migration first
        migrated_name = migrate_subsystem_name(subsystem_id)
        
        if migrated_name is None:
            # Subsystem was removed
            print(f"Warning: Subsystem '{subsystem_id}' has been removed. Please use specific driver subsystems.", file=sys.stderr)
            invalid_subsystems.append(subsystem_id)
        elif migrated_name != subsystem_id:
            # Subsystem was migrated
            if migrated_name in available:
                valid_subsystems.append(migrated_name)
                migrated_subsystems.append((subsystem_id, migrated_name))
            else:
                invalid_subsystems.append(subsystem_id)
        elif subsystem_id in available:
            valid_subsystems.append(subsystem_id)
        else:
            invalid_subsystems.append(subsystem_id)
    
    if migrated_subsystems:
        for old, new in migrated_subsystems:
            print(f"Info: Migrated '{old}' -> '{new}'", file=sys.stderr)
    
    if invalid_subsystems:
        print(f"Warning: Invalid subsystems in .config: {', '.join(invalid_subsystems)}", file=sys.stderr)
        print(f"Available subsystems: {', '.join(sorted(available))}", file=sys.stderr)
    
    return valid_subsystems


def generate_makefile(config_file, subsystems_json, arch, kernel_root, output_path):
    """Generate dynamic Makefile with validation"""
    # Load configuration
    selected_subsystems = load_config(config_file)
    
    if not selected_subsystems:
        print("Error: No subsystems selected in .config", file=sys.stderr)
        return False
    
    # Load subsystems data
    subsystems_data = load_subsystems_json(subsystems_json)
    if not subsystems_data:
        print(f"Error: Failed to load subsystems.json: {subsystems_json}", file=sys.stderr)
        return False
    
    # Validate subsystems
    valid_subsystems = validate_subsystems(subsystems_data, selected_subsystems)
    if not valid_subsystems:
        print("Error: No valid subsystems found in .config", file=sys.stderr)
        return False
    
    # Resolve dependencies
    all_subsystems = resolve_dependencies(subsystems_data, valid_subsystems)
    
    # Collect all files and validate they exist
    all_files = []
    missing_files = []
    for subsystem_id in all_subsystems:
        files = get_subsystem_files(subsystems_data, subsystem_id, arch)
        for file_path in files:
            full_path = os.path.join(kernel_root, file_path)
            if os.path.exists(full_path):
                all_files.append(file_path)
            else:
                missing_files.append((subsystem_id, file_path))
                print(f"Warning: File '{file_path}' from subsystem '{subsystem_id}' does not exist, skipping", file=sys.stderr)
    
    if missing_files:
        print(f"\nWarning: {len(missing_files)} file(s) from subsystems.json do not exist and were skipped", file=sys.stderr)
    
    # Create output directory
    output_dir = os.path.dirname(output_path)
    os.makedirs(output_dir, exist_ok=True)
    
    # Generate Makefile
    with open(output_path, 'w') as f:
        f.write("# ===============================================================================\n")
        f.write("# IR0 KERNEL DYNAMIC MAKEFILE\n")
        f.write("# ===============================================================================\n")
        f.write("# Auto-generated by generate_dynamic_makefile.py\n")
        f.write("# DO NOT EDIT MANUALLY - This file is regenerated on each configuration change\n")
        f.write("# ===============================================================================\n\n")
        
        f.write(f"KERNEL_ROOT := {kernel_root}\n")
        f.write(f"ARCH := {arch}\n\n")
        
        # Compiler configuration
        f.write("# Compiler configuration\n")
        f.write("CC = gcc\n")
        f.write("LD = ld\n")
        f.write("ASM = nasm\n")
        f.write("NASM = nasm\n\n")
        
        # Flags
        f.write("# Flags\n")
        f.write("CFLAGS = -m64 -ffreestanding -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -nostdlib -lgcc -I./includes -I./ -g -Wall -Wextra -fno-stack-protector -fno-builtin\n")
        f.write("LDFLAGS = -T kernel/linker.ld -z max-page-size=0x1000\n")
        f.write("NASMFLAGS = -f elf64\n")
        f.write("ASMFLAGS = -f elf64\n\n")
        
        # Include paths
        f.write("# Include paths\n")
        f.write("CFLAGS += -I$(KERNEL_ROOT)\n")
        f.write("CFLAGS += -I$(KERNEL_ROOT)/includes\n")
        f.write("CFLAGS += -I$(KERNEL_ROOT)/includes/ir0\n")
        f.write("CFLAGS += -I$(KERNEL_ROOT)/mm\n")
        f.write("CFLAGS += -I$(KERNEL_ROOT)/arch/common\n")
        f.write("CFLAGS += -I$(KERNEL_ROOT)/arch/$(ARCH)/include\n")
        f.write("CFLAGS += -I$(KERNEL_ROOT)/kernel\n")
        f.write("CFLAGS += -I$(KERNEL_ROOT)/drivers\n")
        f.write("CFLAGS += -I$(KERNEL_ROOT)/fs\n")
        f.write("CFLAGS += -I$(KERNEL_ROOT)/interrupt\n")
        f.write("CFLAGS += -I$(KERNEL_ROOT)/mm\n")
        f.write("CFLAGS += -I$(KERNEL_ROOT)/scheduler\n\n")
        
        # Object files
        f.write("# Object files from selected subsystems\n")
        f.write("OBJS =\n")
        
        # Emit only files that passed the existence check above: warning about a
        # stale subsystems.json entry and then emitting its .o anyway just moves
        # the failure to link time.
        existing_files = set(all_files)

        total_files = 0
        for subsystem_id in all_subsystems:
            files = [f for f in get_subsystem_files(subsystems_data, subsystem_id, arch)
                     if f in existing_files]
            if files:
                f.write(f"\n# Subsystem: {subsystem_id} ({len(files)} files)\n")
                for file_path in files:
                    # Convert source to object file
                    if file_path.endswith('.c'):
                        obj_file = file_path[:-2] + '.o'
                    elif file_path.endswith('.cpp'):
                        obj_file = file_path[:-4] + '.o'
                    elif file_path.endswith('.asm'):
                        obj_file = file_path[:-4] + '.o'
                    else:
                        continue
                    
                    f.write(f"OBJS += {obj_file}\n")
                    total_files += 1
        
        f.write(f"\n# Total: {total_files} object file(s) from {len(all_subsystems)} subsystem(s)\n")
        f.write(f"# Selected: {len(valid_subsystems)} subsystem(s)\n")
        f.write(f"# With dependencies: {len(all_subsystems)} subsystem(s)\n\n")
        
        # Build rules
        f.write("# Build rules\n")
        f.write(".PHONY: all clean\n\n")
        
        f.write("all: $(OBJS)\n")
        f.write(f"\t@echo \"✓ Compiled {total_files} object file(s)\"\n\n")
        
        # Compile C files
        f.write("# Compile C files\n")
        f.write("%%.o: %%.c\n")
        f.write("\t@echo \"  CC      $<\"\n")
        f.write("\t@$(CC) $(CFLAGS) -c $< -o $@\n\n")
        
        # Compile C++ files
        f.write("# Compile C++ files\n")
        f.write("%%.o: %%.cpp\n")
        f.write("\t@echo \"  CXX     $<\"\n")
        f.write("\t@g++ -m64 -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics \\\n")
        f.write("\t\t-mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \\\n")
        f.write("\t\t-nostdlib -lgcc -g -Wall -Wextra -fno-stack-protector -fno-builtin \\\n")
        f.write("\t\t$(CFLAGS) -c $< -o $@\n\n")
        
        # Compile ASM files
        f.write("# Compile ASM files\n")
        f.write("%%.o: %%.asm\n")
        f.write("\t@echo \"  ASM     $<\"\n")
        f.write("\t@$(ASM) $(ASMFLAGS) $< -o $@\n\n")
        
        # Clean rule
        f.write("# Clean rule\n")
        f.write("clean:\n")
        f.write("\t@echo \"Cleaning object files...\"\n")
        f.write("\t@rm -f $(OBJS)\n")
        f.write("\t@echo \"✓ Clean complete\"\n\n")
    
    print(f"Generated Makefile with {total_files} files from {len(all_subsystems)} subsystems", file=sys.stderr)
    return True


def main():
    if len(sys.argv) < 5:
        print("Usage: generate_dynamic_makefile.py <config_file> <subsystems_json> <arch> <kernel_root>", file=sys.stderr)
        sys.exit(1)
    
    config_file = sys.argv[1]
    subsystems_json = sys.argv[2]
    arch = sys.argv[3]
    kernel_root = sys.argv[4]
    
    # Generate output path
    output_path = os.path.join(kernel_root, 'setup', '.build', 'Makefile.dynamic')
    
    success = generate_makefile(config_file, subsystems_json, arch, kernel_root, output_path)
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()

