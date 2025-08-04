#include "print.h"
#include "Paging.h"
#include "interrupt/idt.h"
#include "panic.h"
#include "time.h"

void kernel_main()
{
    clear_screen();
    print_colored("=== IR0 KERNEL BOOT === :-)\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    idt_init();
    LOG_OK("[OK] IDT cargado correctamente.\n");

    init_paging();
    LOG_OK("[OK] Paginación inicializada.\n");
    
    // Activar interrupciones globalmente (como tengo handlers lo puedo hacer)
    asm volatile("sti");
    
    LOG_OK("[OK] Interrupciones habilitadas.\n");

    // inicio el reloj que esté disponible. La prioridad es de LAPIC
    init_clock();

    print_colored("\nSistema en estado operativo.\n", VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    cpu_relax();
}


void ShutDown() // No la voy a llamar porque no tengo drivers para poder manejar lógica de encendido
{
    asm volatile("mov al, 0xFE; out 0x64, al");
}



