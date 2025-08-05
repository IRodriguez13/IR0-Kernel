#include "../includes/print.h"
#include "../Paging/Paging.h"        // ← Ruta completa
#include "../interrupt/idt.h"
#include "../panic/panic.h"          // ← Ruta completa
#include "../drivers/timer/clock_system.h"  // ← En lugar de "time.h"
#include "../scheduler/scheduler.h"  // ← Ruta completa
#include "../scheduler/task.h"       // ← Ruta completa
#include "../test/task_demo.h"



#define STACK_SIZE 4096

uint8_t stack1[STACK_SIZE];
uint8_t stack2[STACK_SIZE];
// Tasks estáticas para testing  
task_t task1_struct;
task_t task2_struct;

//====================

void kernel_main()
{
    clear_screen();
    print_colored("=== IR0 KERNEL BOOT === :-)\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    idt_init();
    LOG_OK("IDT cargado correctamente");

    init_paging();
    LOG_OK("Paginación inicializada");

    scheduler_init();
    LOG_OK("Scheduler inicializado");

    // Crear task1
    task1_struct.pid = 1;
    task1_struct.eip = (uint32_t)task1;
    task1_struct.esp = (uint32_t)(stack1 + STACK_SIZE - 4); // Dejar espacio
    task1_struct.ebp = task1_struct.esp;
    task1_struct.state = TASK_READY;
    task1_struct.priority = 10;
    
    // Obtener page directory actual
    asm volatile("mov %%cr3, %0" : "=r"(task1_struct.cr3));
    
    add_task(&task1_struct);

    // Crear task2
    task2_struct.pid = 2;
    task2_struct.eip = (uint32_t)task2;
    task2_struct.esp = (uint32_t)(stack2 + STACK_SIZE - 4);
    task2_struct.ebp = task2_struct.esp;
    task2_struct.state = TASK_READY;
    task2_struct.priority = 10;
    task2_struct.cr3 = task1_struct.cr3; // Mismo page directory por ahora
    
    add_task(&task2_struct);

    LOG_OK("Tareas creadas");

    // Activar interrupciones
    asm volatile("sti");
    LOG_OK("Interrupciones habilitadas");

    // Iniciar scheduler (va a saltar al primer proceso)
    scheduler_start();

    // No debería llegar aquí
    panic("Scheduler returned unexpectedly!");
}

void ShutDown() // Como no tengo drivers para apagar la máquina, no la puedo usar.
{
    uint8_t reset_value = 0xFE;
    asm volatile(
       "outb %%al, $0x64"
       :                    // outputs (ninguno)
       : "a"(reset_value)   // inputs: reset_value en AL
       : "memory"           // clobbers: indica que puede modificar memoria
    );
}