#include "print.h"
#include "Paging.h"
#include "interrupt/idt.h"
#include "panic.h"
#include "time.h"
#include "scheduler.h"
#include "task.h"

//=== TEST ====
#include "task_demo.h"


#define STACK_SIZE 4096

uint8_t stack1[STACK_SIZE];
uint8_t stack2[STACK_SIZE];

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

    scheduler_init();
// ===== TEST ======
    // Crear task1
    task_t *t1 = (task_t *)0x300000; // Asumamos esta dirección libre
    t1->eip = (uint32_t)task1;
    t1->esp = (uint32_t)(stack1 + STACK_SIZE);
    t1->ebp = t1->esp;
    t1->state = TASK_READY;
    t1->next = NULL;
    add_task(t1);

    // Crear task2
    task_t *t2 = (task_t *)0x300100; // Otra dirección, o mejor si usás kmalloc a futuro
    t2->eip = (uint32_t)task2;
    t2->esp = (uint32_t)(stack2 + STACK_SIZE);
    t2->ebp = t2->esp;
    t2->state = TASK_READY;
    t2->next = NULL;
    add_task(t2);

    print("[KERNEL] Tareas inicializadas.\n");

    // Iniciar con un primer switch manual
    scheduler_tick(); // Va a llamar a switch_task(t1, t2)

    // inicio el reloj que esté disponible. La prioridad es de LAPIC
    init_clock();

    print_colored("\nSistema en estado operativo.\n", VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    cpu_relax();
}

void ShutDown() // No la voy a llamar porque no tengo drivers para poder manejar lógica de encendido
{
    asm volatile("mov al, 0xFE; out 0x64, al");
}
