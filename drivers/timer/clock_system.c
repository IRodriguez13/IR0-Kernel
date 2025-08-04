#include "print.h"
#include "acpi.h"
#include "pit.h"
#include "lapic.h"
#include "hpet.h"
#include "clock_system.h"
#include "panic.h"

void init_clock()
{
    enum ClockType selected = detect_best_clock();

    switch (selected)
    {
        case CLOCK_HPET:
            LOG_OK("[CLOCK] Using HPET\n");
            hpet_init(); 
            break;

        case CLOCK_LAPIC:
            LOG_OK("[CLOCK] Using LAPIC Timer\n");
            lapic_init_timer();
            break;

        case CLOCK_PIT:
            LOG_OK("[CLOCK] HPET/LAPIC unavailable, using PIT\n");
            init_PIT(100);
            break;

        case CLOCK_RTC:
            LOG_OK("[CLOCK] Using legacy RTC timer\n");
            rtc_timer_init(); // No la tengo
            break;

        default:
            LOG_ERR("[CLOCK] No suitable timer found!\n");
            panic("No timer available!");
    }
}
