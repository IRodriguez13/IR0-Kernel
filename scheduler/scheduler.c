#include "scheduler.h"
#include "task.h"
#include "print.h"
#include "panic.h" 

static task_t *current_task = NULL;
static task_t *ready_queue = NULL;
extern void switch_task(task_t *current, task_t *next);

void scheduler_init()
{
    current_task = NULL;
    ready_queue = NULL;
    LOG_OK("Scheduler inicializado");
}

void add_task(task_t *task)
{
    // Validaciones defensivas
    if (!task)
    {
        LOG_ERR("add_task: task is NULL");
        return;
    }

    if (task->state == TASK_TERMINATED)
    {
        LOG_WARN("add_task: trying to add terminated task");
        return;
    }

    task->state = TASK_READY;

    if (!ready_queue)
    {
        // Primera tarea: crear lista circular de 1 elemento
        ready_queue = task;
        task->next = task;
        LOG_OK("Primera tarea agregada al scheduler");
    }
    else
    {
        // Buscar el último nodo de la lista circular
        task_t *last = ready_queue;

        while (last->next != ready_queue)
        {
            last = last->next;

            // Protección contra listas corruptas
            if (!last || !last->next)
            {
                LOG_ERR("Corrupted ready queue detected!");
                panic("Scheduler corruption detected");
                return;
            }
        }

        // Insertar nueva tarea al final
        last->next = task;
        task->next = ready_queue;
        LOG_OK("Nueva tarea agregada al scheduler");
    }
}

void scheduler_start()
{
    if (!ready_queue) {
        LOG_ERR("No hay procesos para ejecutar!");
        panic("Scheduler vacío");
        return;
    }
    
    // Inicializar current_task con el primer proceso
    current_task = ready_queue;
    current_task->state = TASK_RUNNING;
    
    LOG_OK("Scheduler iniciado, saltando al primer proceso...");
    
    // Saltar al primer proceso manualmente
    asm volatile(
        "mov %0, %%esp\n"
        "mov %1, %%ebp\n"  
        "jmp *%2"
        :
        : "r"(current_task->esp), "r"(current_task->ebp), "r"(current_task->eip)
        : "memory"
    );
}

void scheduler_tick()
{
    // NUEVO: Si no hay current_task, inicializar scheduler
    if (!current_task) {
        scheduler_start();
        return;
    }

    // Validación de corrupción
    if (!current_task->next)
    {
        LOG_WARN("Proceso actual corrupto");
        return;
    }

    task_t *next_task = current_task->next; // Candidato siguiente

    // Buscar próximo proceso READY
    while (next_task->state != TASK_READY && next_task != current_task)
    {
        next_task = next_task->next;
    }

    // Si encontramos un proceso diferente y listo, cambiar contexto
    if (next_task != current_task)
    {
        current_task->state = TASK_READY;  // El actual vuelve a READY
        next_task->state = TASK_RUNNING;   // El nuevo está RUNNING
        
        switch_task(current_task, next_task);
        current_task = next_task;
    }
}