/* linker.ld */
OUTPUT_FORMAT(elf32-i386)
ENTRY(start)

SECTIONS {
    . = 1M;

    .text : {
        *(.multiboot)
        *(.text)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(.bss)
        *(COMMON)
    }
}
