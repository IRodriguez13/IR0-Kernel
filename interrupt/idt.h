#pragma once // Evita tener que hacer ifdef, endif pq esta abstraído
#include <stdint.h>

#define IDT_ENTRIES 256 // cantidad de entradas de nuestro idt
#define IDT_INTERRUPT_GATE_KERNEL  0x8E  // DPL=0, solo kernel
#define IDT_INTERRUPT_GATE_USER    0xEE  // DPL=3, user puede usar
#define IDT_TRAP_GATE_KERNEL       0x8F  // trap gate, kernel
#define IDT_FLAG_GATE32   0x0E // 
#define IDT_FLAG_TRAP32   0x0F // es para pruebas 


// Descriptor de IDT (cada entrada tiene 8 bytes) Es como una tabla de instrucciones para manejar todas las interrupciones del kernel
typedef struct 
{
    uint16_t offset_low;  // offset bits 0..15
    uint16_t selector;    // selector de segmento en la dirección del loader donde dije que se inicia el kernel
    uint8_t  zero;        // reservado (tiene que ser 0 pq sino INTEL se enoja)
    uint8_t  type_attr;   // tipo y atributos
    uint16_t offset_high; // offset bits 16..31... Si, 31 porque se cuenta desde el bit 0

} __attribute__((packed)) idt_entry_t; // El " __attribute__((packed))" es para que el compilador nos deje exactamente 8 bytes, que es lo que debería pesar el IDT


// Puntero a la IDT que le damos a la CPU con lidt
typedef struct
{
    uint16_t limit; // limit = (256 * sizeof(idt_entry_t)) - 1 = (256 * 8) - 1 = 2047 el tamaño del idt
    uint32_t base; // la memo adrss donde empieza la struct de la idt (base = (uint32_t)&idt[0])

} __attribute__((packed)) idt_ptr_t;

// Funciones que van a consumir la IDT
void idt_init();

void idt_set_gate(int n, uint32_t handler, uint8_t flags);

