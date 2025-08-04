#include "interrupt/idt.h"
#include "panic.h"
#include "print.h"
#include "Paging.h"
#define PIT_FREC 1193180
extern void timer_stub();



static uint32_t ticks;


static inline void outb(uint16_t port, uint8_t value)
{
    asm volatile("outb %0, %1"::"a"(value),"Nd"(port));
} 

void time_handler()
{
    ticks ++;

    if(ticks % 100 == 0)
        
        print_warning("[TIMER] Tick! \n");
}       

void init_PIT(uint32_t frecuency)
{
    // abro el gate para que el timer acceda a las interrupciones
    idt_set_gate(32, (uint32_t)timer_stub, IDT_INTERRUPT_GATE_KERNEL);

    // hago la división para obtener la frecuencia
    uint32_t divisor = PIT_FREC / frecuency;

    
    // entonces ya le puedo mandar cosas al PIT
    outb(0x43, 0x36); // modo 3, canal 0
    outb(0x40, divisor & 0xFF); // mando los bajos
    outb(0x40, (divisor >> 8) & 0xFF); // mando los bits altos

}
