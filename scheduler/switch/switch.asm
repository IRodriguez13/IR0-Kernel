global switch_task

switch_task:
    cli                             ; Deshabilitar interrupciones
    
    mov eax, [esp + 4]              ; old_task
    mov ebx, [esp + 8]              ; new_task
    
    ; Guardar contexto mínimo
    mov [eax + 4], esp              ; old_task->esp
    mov [eax + 8], ebp              ; old_task->ebp
    mov ecx, [esp]                  ; ecx = return address
    mov [eax + 12], ecx             ; old_task->eip
    
    ; Cambiar memoria virtual
    mov ecx, [ebx + 16]             ; new_task->cr3
    mov cr3, ecx                    ; Cambiar page directory
    
    ; Cargar contexto nuevo
    mov esp, [ebx + 4]              ; new_task->esp
    mov ebp, [ebx + 8]              ; new_task->ebp
    
    sti                             ; Rehabilitar interrupciones
    jmp dword [ebx + 12]            ; Saltar a new_task->eip