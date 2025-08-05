#include "acpi/acpi.h"
#include "lapic/lapic.h"
#include "clock_system.h" // donde definiste ClockType enum
#include "hpet/hpet.h"

enum ClockType detect_best_clock()
{
    void* hpet_addr = find_hpet_table();

    if (hpet_addr != NULL) 
    {
        hpet_set_address(hpet_addr); // guardado globalmente
        return CLOCK_HPET;
    }

    if (lapic_available()) 
    {
        return CLOCK_LAPIC;
    }

    return CLOCK_PIT;
}
