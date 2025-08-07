; boot.asm - Punto de entrada Multiboot2
bits 32                         ; GRUB inicia en modo 32-bit

section .multiboot_header
header_start:
    ; Encabezado Multiboot2
    dd 0xe85250d6               ; Número mágico Multiboot2
    dd 0                        ; Arquitectura 0 (i386 protegido)
    dd header_end - header_start ; Longitud del encabezado
    ; Checksum
    dd 0x100000000 - (0xe85250d6 + 0 + (header_end - header_start))

    ; Tags requeridos
    ; Tag de información de memoria (opcional)
    dw 2                        ; Type: information request
    dw 0                        ; Flags
    dd 24                       ; Size
    dd 4                        ; Memory map tag
    dd 6                        ; Framebuffer tag
    dd 0                        ; Terminator

    ; Tag de terminación
    dw 0                        ; Type
    dw 0                        ; Flags
    dd 8                        ; Size
header_end:

section .text
global _start
extern kernel_main              ; Definido en main.c

_start:

    ; Configurar stack
    mov esp, stack_top

    ; Verificar que GRUB nos dejó en modo 32-bit
    call check_multiboot
    call check_cpuid
    call check_long_mode

    ; Configurar paginación (temporal para el salto a 64-bit)
    call setup_page_tables
    call enable_paging

    ; Cargar GDT 64-bit
    lgdt [gdt64.pointer]
    
    ; Saltar a modo 64-bit
    jmp gdt64.code:long_mode_start

bits 64
long_mode_start:
    ; Limpiar segmentos de datos
    mov ax, gdt64.data
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Cargo el kernel
    call kernel_main

    ; En caso de retorno, el típico fallback donde  duerme la cpu.
    cli
.halt_fallback:
    hlt
    jmp .halt_fallback

; Funciones de verificación y configuración (32-bit)
bits 32
check_multiboot:
    cmp eax, 0x36d76289        ; Magic number de Multiboot2
    jne .no_multiboot
    ret
.no_multiboot:
    mov al, "0"
    jmp error

check_cpuid:
    ; ... (implementación similar a la anterior)
    ret

check_long_mode:
    ; ... (implementación similar a la anterior)
    ret

setup_page_tables:
    ; ... (configuración básica de paginación para el salto a 64-bit)
    ret

enable_paging:
    ; ... (habilitar PAE y paginación)
    ret

error:
    ; Manejo básico de errores
    mov dword [0xb8000], 0x4f524f45
    mov dword [0xb8004], 0x4f3a4f52
    mov dword [0xb8008], 0x4f204f20
    mov byte  [0xb800a], al
    hlt

section .bss
align 16
stack_bottom:
    resb 16384                  ; 16KB de stack
stack_top:

section .rodata
gdt64:
    dq 0                        ; Entrada nula
.code: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53) ; Segmento de código
.data: equ $ - gdt64
    dq (1<<44) | (1<<47)        ; Segmento de datos
.pointer:
    dw $ - gdt64 - 1
    dq gdt64
