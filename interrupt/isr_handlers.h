#pragma  once
#include "../includes/print.h"
#include "../includes/stdint.h"
#include "../panic/panic.h"


void default_interrupt_handler();
void page_fault_handler();