#pragma once
#include "../includes/print.h"

/*
 * ============ Panic() ================
 * Detiene el kernel completamente y muestra mensaje de error crítico.
 * Limpia pantalla, muestra mensaje en rojo, deshabilita interrupciones
 * y entra en loop infinito con hlt para mantener la cpu en modo de ahorro.
 * 
 * Cómo es un estado límite del sistema, la idea es frenar en seco la ejecución del sistema. 
 */
void panic(const char *message);
void cpu_relax();

