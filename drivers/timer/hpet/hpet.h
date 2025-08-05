#ifndef HPET_H
#define HPET_H

#include "../../../includes/stdint.h"
#include "../../../includes/stddef.h"

void hpet_init();
void hpet_set_address(void* addr);

#endif
