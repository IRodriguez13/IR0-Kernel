; Básicamente, acá tengo funciones que se encargan de orquestar llamados a los diferentes handlers de interrupciones.
; Lo hago en asm porque la funcion flush requiere de lidt que me permite cargar el idt que hice en el alto nivel. 
; Aparte, iret no existe en alto nivel y retornar con los registros puhseados al interrumpir corrompe todo

[bits 32]

global isr_default
global isr_page_fault
global idt_flush
global timer_stub

extern idt_flush
extern page_fault_handler
extern default_interrupt_handler
extern time_handler

idt_flush:
    mov eax, [esp+4] ; parámetro pasado (el puntero a idt_ptr, que también es un puntero)
    lidt [eax] ; cargo la idt usando la memo adrss de su puntero
    ret ; vuelvo a alto nivel

isr_template: ; Es para testear el tema del byte que quiero mostrar por consola
    pusha
    push byte 14 ; la idea es loggear este PF.
    call default_interrupt_handler
    add esp, 4
    popa
    iret

isr_default:
    pusha ; Guarda TODOS los registros (eax, ebx, ecx, etc.)
    call default_interrupt_handler ; Llama a la funcion en C para manejar la interrupcion normal
    popa ; Restaura los valores que se guardaron
    iret ; Retorna de la interrupción (especial, no ret normal)

isr_page_fault:
    pusha ; me guardo todos los registros
    call page_fault_handler ; llamo a funcion de alto nivel para manejar interrupciones por page faults
    popa ; restauro los valores
    add esp, 4 ; Limpio el error code automático generado en PF haciendo que el puntero de pila apunte a 4 bytes del err
    iret 

timer_stub: ; lo tengo que hacer porque la cpu no sabe hacer iret en C
    pusha
    call time_handler
    popa
    iret


; Al entrar a la interrupción, CPU hace automáticamente:
;pushf        ; Guarda EFLAGS (incluyendo IF = interrupt flag)
;cli          ; Deshabilita interrupciones (IF = 0)
;push cs      ; Guarda segmento
;push eip     ; Guarda posición actual
;CON error code: 8, 10, 11, 12, 13, 14, 17, 21
;SIN error code: Todas las demás

;el iret me popea todos esos datos desde el momento en que finaliza la interrupcion
;iret
; CPU hace internamente:
; pop eip       ; eip = [esp], esp += 4
; pop cs        ; cs = [esp], esp += 4  
; pop eflags    ; eflags = [esp], esp += 4
;
;
;al momento de hacer iret con errores
;iret
; CPU hace internamente:
; pop eip       ; eip = [esp], esp += 4
; pop cs        ; cs = [esp], esp += 4  
; pop eflags    ; eflags = [esp], esp += 4 
;
; Stack después de una interrupción con error code:
;
;ESP+12 -> [eflags ]  (4 bytes)
;ESP+8  -> [cs     ]  (4 bytes)  
;ESP+4  -> [eip    ]  (4 bytes) <- ESP debería apuntar acá
;ESP+0  -> [error  ]  (4 bytes) <- ESP apunta aquí
;
;La idea es correrlo 4 bytes "arriba" para que el puntero de pila apunte al instruction pointer como debe ser.