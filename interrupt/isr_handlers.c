#include "isr_handlers.h"

// Acá manejo las interrupciones que dan page fault según el idt.
void page_fault_handler()
{
    uint32_t fault_adrss;
    asm volatile("mov %%cr2, %0": "=r"(fault_adrss)); // le asigno al registro de ctrol 2 el valor de la direccion que esta en PF

    print_error("==== PAGE FAULT ====\n");
    // Por acá tengo que pasar la dirección que dió PF.
    print_hex_compact(fault_adrss);
    print("\n");    
    // No sé si es un evento de kernel panic pero tengo que dormir la cpu
    cpu_relax();
}

// manejo de interrupciones por default por parte del bajo nivel 

void default_interrupt_handler()
{
    print_warning("Interrupción por defecto \n");
    
    cpu_relax();
}

