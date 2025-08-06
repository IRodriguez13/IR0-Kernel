;
; ===============================================================================
; switch_task - Cambio de contexto entre procesos
; ===============================================================================
;
; Como toda funcion que usa instrucciones privilegiadas, va en asm. En este caso cli y sti 
; El tema aca es: primero cargamos las structs del alto nivel en eax y ebx respectivamente, despues
; despues "guardamos" los datos de las tareas de la struct 1 y usamos el cr3 para que apunte 
; al directorio de paginacion de la segunda tarea.
; despues cargamos esp y ebp la segunda tarea. y saltamos ahí en el alto nivel. 
;

global switch_task

switch_task:

    cli                             ; Corto las interrupciones
    ; cargo las dos tareas en registros de prop. general.
    mov eax, [esp + 4]              ; old_task 
    mov ebx, [esp + 8]              ; new_task
    
    ; Guardar contexto mínimo de la tarea vieja

    mov [eax + 4], esp              ; old_task->esp
    mov [eax + 8], ebp              ; old_task->ebp
    mov ecx, [esp]                  ; ecx = return address
    mov [eax + 12], ecx             ; old_task->eip me guardo la direcc de retorno en el old task
    
    ; Cambiar memoria virtual
    mov ecx, [ebx + 16]             ; new_task->cr3 ahora la nueva tarea apunta al registro de control para que haga su magia
    mov cr3, ecx                    ; Nos vamos al directorio paginado de la segunda tarea.
    
    ; Cargar contexto nuevo
    mov esp, [ebx + 4]              ; new_task->esp
    mov ebp, [ebx + 8]              ; new_task->ebp
    
    sti                             ; Rehabilitar interrupciones
    jmp dword [ebx + 12]            ; Saltar a new_task-> eip
