#include "scheduler.h"
#include "task.h"

static task_t *current_task = NULL;
static task_t *ready_queue = NULL;
extern void switch_task(task_t *current, task_t *next);

void scheduler_init()
{
    current_task = NULL;
    ready_queue = NULL;
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

    task->state = TASK_READY; // DECLARO A TASK COMO READY

    // Pregunto si el proceso está listo

    if (!ready_queue)
    {
        // Primera tarea: crear lista circular de 1 elemento
        ready_queue = task;
        task->next = task;
        LOG_OK("First task added to scheduler");
    }
    else
    {
        // Buscar el último nodo de la lista circular y lo marco como el que sigue
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

        // Insertar nueva tarea al final si todo funcó
        last->next = task;
        task->next = ready_queue;
    }
}

void scheduler_tick()
{
    // si no hay proceso actual o el que sigue está corrupto, corto.
    if (!current_task || !current_task->next)
    {
        LOG_WARN("Se detectaton procesos corruptos");
        return;
    }

    task_t *next_task = current_task->next; // Candidato siguiente

    while (next_task->state != TASK_READY && next_task != current_task)
    {
        next_task = next_task->next;
    }

    // Si la que sigue no es la actual o es diferente, cambio de contexto.
    if (next_task != current_task)
    {
        switch_task(current_task, next_task);
        current_task = next_task;
    }
}