#pragma once
#include <stdint.h>

#define IDT_ENTRIES 256

// Descriptor de IDT (cada entrada tiene 8 bytes)
typedef struct {
    uint16_t offset_low;  // offset bits 0..15
    uint16_t selector;    // selector de segmento (usamos código)
    uint8_t  zero;        // reservado (tiene que ser 0)
    uint8_t  type_attr;   // tipo y atributos
    uint16_t offset_high; // offset bits 16..31
} __attribute__((packed)) idt_entry_t;

// Puntero a la IDT que le damos a la CPU con lidt
typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

// Funciones
void idt_init();
void idt_set_gate(int n, uint32_t handler);
