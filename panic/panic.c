#include "panic.h"
#include "print.h"

void panic(const char *message)
{
    // Termino las interrupciones inmediatamente para que no se ejecuten eventos que no espero o no puedo controlar
    asm volatile("cli");

    print_colored("=== KERNEL PANIC ===\n\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
    print_error(message);
    print_colored("\n\nSistema detenido. Reinicie la máquina.", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    // Acá puede haber stacktrace más custom del error

    cpu_relax();
}

// como estoy en un estado del sistema crítico, tengo que "dormir" la cpu con halt para poder mantener un estado controlado
void cpu_relax()
{
    for(;;) // parece más pro si uso el for asi que uso el while(1).
    
      asm volatile("hlt");
}
