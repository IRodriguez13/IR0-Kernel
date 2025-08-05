#include "../includes/print.h"
#include "../panic/panic.h"

void task1() 
{
    volatile int counter = 0;
    
    while (1) 
    {
        print_colored("[TASK1] Ejecutando...\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
        
        // Loop de delay básico
        for (volatile int i = 0; i < 1000000; i++) 
        {
            counter++;
        }
        
        // Yield voluntario (simulado con hlt)
        asm volatile("hlt");
    }
}

void task2()
{
    volatile int counter = 0;
    
    while (1) 
    {
        print_colored("[TASK2] Ejecutando...\n", VGA_COLOR_MAGENTA, VGA_COLOR_BLACK);
        
        // Loop de delay básico
        for (volatile int i = 0; i < 1500000; i++) 
        {
            counter++;
        }
        
        // Yield voluntario
        asm volatile("hlt");
    }
}