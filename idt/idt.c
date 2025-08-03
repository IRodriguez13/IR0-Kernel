#include "idt.h"

idt_entry_t idt[IDT_ENTRIES]; // Tabla de descriptores de interrupciones (256 entradas de 8 bytes)
idt_ptr_t idt_ptr;            // y mi puntero al idt

extern void idt_flush(uint32_t); // esta es la funcion que carga el idt en asm
extern void isr_default();
extern void isr_page_fault();


void idt_set_gate(int n, uint32_t handler, uint32_t flags) // crea una entrada de interrupción en la IDT en la posición n a 255
{
    idt[n].offset_low = handler & 0xFFFF;
    idt[n].selector = 0x08; // segmento de código en la GDT
    idt[n].zero = 0;
    idt[n].type_attr = flags; // Presente, ring 0, 32-bit interrupt gate
    idt[n].offset_high = (handler >> 16) & 0xFFFF;
}

void idt_init()
{
    // Inicializo la base y el límite del puntero del idt
    idt_ptr.limit = sizeof(idt_entry_t) * IDT_ENTRIES - 1;
    idt_ptr.base = (uint32_t)&idt;

    for (int i = 0; i < IDT_ENTRIES; i++)
    {
        idt_set_gate(i, (uint32_t)isr_default, IDT_INTERRUPT_GATE_KERNEL);// Inicializa todas las entradas de la IDT con un handler genérico por defecto
    }

    // declaro al bit 14 del idt como el de page faults para manejar desde su handler en asm
    idt_set_gate(14, (uint32_t)isr_page_fault, IDT_INTERRUPT_GATE_KERNEL);

    // cargo el idt desde el asm
    idt_flush((uint32_t)&idt_ptr);
}