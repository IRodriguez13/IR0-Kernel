#pragma once
#include "../includes/stdint.h"

typedef enum {
    TASK_READY, //0, listo para ejecutar
    TASK_RUNNING, //1 en ejecución
    TASK_BLOCKED, // 2 esperando IO, mutex, etc
    TASK_TERMINATED // proceso terminado.
} task_state_t;

/*

Todos los registros que voy a manjear en 32 bits por ahora

*/

typedef struct task {
    uint32_t pid;    // Process ID único
    uint32_t esp;    // Stack Pointer - dónde está la pila del proceso
    uint32_t ebp;    // Base Pointer - base del stack frame actual  
    uint32_t eip;    // Instruction Pointer - próxima instrucción a ejecutar
    uint32_t cr3;    // Page Directory - espacio de memoria virtual del proceso
    uint8_t priority;// Prioridad (0-255, mayor número = mayor prioridad)
    task_state_t state; // Estado actual del proceso
    struct task* next;  // Puntero al siguiente proceso (lista circular)
} task_t;

