[bits 32]

global isr_default
global isr_page_fault
global idt_flush

extern page_fault_handler
extern default_interrupt_handler

idt_flush:
    mov eax, [esp+4] ; parámetro pasado
    lidt [eax]
    ret

isr_default:
    pusha
    call default_interrupt_handler
    popa
    iret

isr_page_fault:
    pusha
    call page_fault_handler
    popa
    add esp, 4 ; salteo el código de error
    iret
