#pragma once

// Flags para autorizar las tablas de paginacion

#define PAGE_PRESENT 0x1 // Con este bit, me aseguro de que la página es mapeable. sino page fault
#define PAGE_WRITE 0x2   // Con este, que puedo escribir en la página, sino page fault
#define PAGE_USER 0x4    // Con este bit (el 2), le digo  al sistema que tanto kernel como programas de usuario pueden acceder acá.


#include "../includes/stdint.h"

void init_paging();

