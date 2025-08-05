#pragma once

enum ClockType 
{
    CLOCK_HPET,
    CLOCK_LAPIC,
    CLOCK_PIT,
    CLOCK_RTC,
    CLOCK_NONE
};

// Función faltante
enum ClockType detect_best_clock();
void init_clock();