#include "print.h"

void kernel_main() // Literal este es el punto de entrada del kernel desde GRUB de booteo que se conforma en la rutina asm
{
    print("Holis :-)");

    /* yo podría dormir la cpu sin instrucciones con asm volatile ("hlt"); de manera infinita, pero no me sirve si después
    quiero meter mas funcionalidades como el coordinador, interrupciones, paginado */
    while (1)
    { } // Loop eterno
}

void ShutDown() // No la voy a llamar porque no tengo drivers para poder manejar lógica de encendido
{
    asm volatile("mov al, 0xFE; out 0x64, al");
}



