[BITS 16] ; le aviso a NASM que voy a usar código de 16 bits
[ORG 0x7C00] ; Arranco en la direccion 0x7C00 por que yo lo digo

start:
    cli ; Clear Interrupts (deshabilitar interrupciones)
    xor ax, ax     ; AX = 0 (más rápido que mov ax, 0) pq xor de lo mismo niega el bit original
    mov ds, ax     ; Data Segment = 0
    mov es, ax     ; Extra Segment = 0  
    mov ss, ax     ; Stack Segment = 0
    mov sp, 0x7C00 ; Stack Pointer justo antes del bootloader
    
    ; Habilitar A20 line (necesario para acceder >1MB)
    call enable_a20
    
    ; Cargar kernel en 1MB usando BIOS extended read
    call load_kernel_1mb
    
    ; Cambiar a modo protegido
    lgdt [gdt_descriptor]
    mov  eax, cr0
    or   eax, 1
    mov  cr0, eax
    
    ; Saltar a modo protegido para pasar de 16 a 32 bits
    jmp CODE_SEG:protected_mode

;       --- Inicio teórico de la ejecución de 32 bit ---

; ===== HABILITAR A20 =====
enable_a20:

    ; Método 1: Keyboard controller
    call a20_wait
    mov  al,   0xAD ; Disable keyboard
    out  0x64, al
    
    call a20_wait
    mov  al,   0xD0 ; Read output port
    out  0x64, al
    
    call a20_wait2
    in   al, 0x60
    push eax
    
    call a20_wait
    mov  al,   0xD1 ; Write output port
    out  0x64, al
    
    call a20_wait
    pop  eax
    or   al,   2  ; Enable A20
    out  0x60, al
    
    call a20_wait
    mov  al,   0xAE ; Enable keyboard
    out  0x64, al
    
    call a20_wait
    ret

a20_wait:
    in   al, 0x64
    test al, 2
    jnz  a20_wait
    ret

a20_wait2:
    in   al, 0x64
    test al, 1
    jz   a20_wait2
    ret

; ===== CARGAR KERNEL EN 1MB =====
load_kernel_1mb:
    ; Usar INT 15h, AH=87h para copiar a memoria extendida
    ; Primero cargar en memoria convencional, luego copiar
    
    ; Cargar sectores en buffer temporal (0x8000)
    mov ah, 0x02   ; Read sectors
    mov al, 20     ; 20 sectores (10KB)
    mov ch, 0      ; Cylinder 0
    mov dh, 0      ; Head 0
    mov cl, 2      ; Sector 2
    mov bx, 0x8000 ; Buffer temporal
    int 0x13
    jc  .error
    
    ; Copiar de 0x8000 a 0x100000 usando BIOS
    mov ah, 0x87     ; Move extended memory
    mov cx, 0x1400   ; Mover 10KB (20 sectores * 512 / 2)
    mov si, gdt_copy ; Descriptor table para la copia
    int 0x15
    jc  .error
    
    ret

.error:
    mov  si, load_error
    call print_string
    hlt

print_string:
    mov ah, 0x0E
.loop:
    lodsb
    cmp al, 0
    je  .done
    int 0x10
    jmp .loop
.done:
    ret

load_error db "Error arrancaando kernel en 1MB", 0

; Descriptor table para copiar memoria
gdt_copy:
    times 16 db 0 ; Null descriptor
    
    ; Source: 0x8000 (memoria convencional)
    dw 0xFFFF ; Limit
    dw 0x8000 ; Base low
    db 0x00   ; Base mid
    db 0x93   ; Access
    dw 0x0000 ; Base high + limit high
    
    ; Destination: 0x100000 (1MB)
    dw 0xFFFF ; Limit  
    dw 0x0000 ; Base low
    db 0x10   ; Base mid
    db 0x93   ; Access
    dw 0x0000 ; Base high + limit high
    
    times 16 db 0 ; Más descriptores nulos

[BITS 32]
protected_mode:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Saltar al kernel en 1MB 
    jmp 0x100000

; ===== GDT =====
gdt_start:
    dd 0x0, 0x0 ; Null descriptor

gdt_code: ; Código ejecutable
    dw 0xFFFF    ; Limit (0-15)
    dw 0x0000    ; Base (0-15)
    db 0x00      ; Base (16-23)
    db 10011010b ; Access: Present, Ring0, Executable, Readable
    db 11001111b ; Granularity: 4KB blocks, 32-bit
    db 0x00      ; Base (24-31)

gdt_data: ; Datos
    dw 0xFFFF    ; Limit (0-15)
    dw 0x0000    ; Base (0-15)  
    db 0x00      ; Base (16-23)
    db 10010010b ; Access: Present, Ring0, Writable
    db 11001111b ; Granularity: 4KB blocks, 32-bit
    db 0x00      ; Base (24-31)

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG         equ gdt_code - gdt_start
DATA_SEG         equ gdt_data - gdt_start

times 510-($-$$) db  0
dw                   0xAA55