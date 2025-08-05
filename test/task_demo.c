#include "print.h"

void task1() 
{
    while (1) 
    {
        print_colored("[TASK1] Hola mundo!\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
        cpu_relax();
    }
}

void task2()
{
    while (1) 
    {
        print_colored("[TASK2] Chau mundo!\n", VGA_COLOR_MAGENTA, VGA_COLOR_BLACK);
        cpu_relax();
    }
}
