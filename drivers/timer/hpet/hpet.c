#include "hpet.h"
#include "print.h"
#include "interrupt/idt.h"
#include "panic.h"
#include "pit.h" // para outb, inb 

#define HPET_GENERAL_CAPABILITY       0x00
#define HPET_GENERAL_CONFIGURATION    0x10
#define HPET_MAIN_COUNTER             0xF0

#define HPET_TIMER0_CONFIG            0x100
#define HPET_TIMER0_COMPARATOR        0x108
#define HPET_TIMER0_FSB_ROUTE         0x110 // para MSI (a futuro)

#define HPET_ENABLE_CNF               (1 << 0) // Habilita el contador general
#define HPET_LEGACY_REPLACEMENT       (1 << 1) // Usa IRQ0 para HPET (reemplaza PIT)
#define HPET_TN_INT_ENB_CNF           (1 << 2) // Habilita interrupciones en el timer
#define HPET_TN_TYPE_CNF              (1 << 3) // 1 = periódica, 0 = una sola vez
#define HPET_TN_VAL_SET_CNF           (1 << 6) // Usar valor inmediato en comparador
#define HPET_TN_32MODE_CNF            (1 << 8) // Usar modo 32-bit si querés

volatile uint64_t* hpet_base = 0; // Puntero base mapeado a la tabla HPET

static void* hpet_mmio_base = NULL;

void hpet_set_address(void* addr) 
{
    hpet_mmio_base = addr;
}

void hpet_init()
{
     if (hpet_mmio_base == NULL) 
       
     panic("HPET address not set before hpet_init");
    
    // Guardamos la dirección base (mapeada por ACPI)
    hpet_base = (volatile uint64_t*)hpet_mmio_base;

    // 1. Deshabilitamos el contador general antes de modificarlo
    *((volatile uint64_t*)((uint8_t*)hpet_base + HPET_GENERAL_CONFIGURATION)) = 0;

    // 2. Resetear el contador principal
    *((volatile uint64_t*)((uint8_t*)hpet_base + HPET_MAIN_COUNTER)) = 0;

    // 3. Leer las capacidades de HPET
    uint64_t capabilities = *((volatile uint64_t*)((uint8_t*)hpet_base + HPET_GENERAL_CAPABILITY));

    // Extraer la resolución en femtosegundos por tick
    uint32_t clock_period_fs = (uint32_t)(capabilities >> 32);

    // Convertir a Hz: (1e15 fs / periodo en fs = Hz)
    uint64_t freq_hz = 1000000000000000ULL / clock_period_fs;

    print("HPET Frequency: ");
    print_hex_compact(freq_hz);
    print("\n");

    // 4. Configurar el Timer 0 como periódico, con interrupciones habilitadas
    uint64_t config = 0;
    config |= HPET_TN_TYPE_CNF;       // Periodic mode
    config |= HPET_TN_INT_ENB_CNF;    // Enable IRQ
    config |= HPET_TN_VAL_SET_CNF;    // Usar valor inmediato

    *((volatile uint64_t*)((uint8_t*)hpet_base + HPET_TIMER0_CONFIG)) = config;

    // 5. Setear comparador (cada cuánto lanzar interrupción)
    // Frecuencia deseada: 100 Hz → cada 10ms → equivalente en ticks HPET
    uint64_t ticks_per_irq = freq_hz / 100;

    *((volatile uint64_t*)((uint8_t*)hpet_base + HPET_TIMER0_COMPARATOR)) = ticks_per_irq;

    // 6. Habilitar modo legacy (usa IRQ 0, el mismo que el PIT)
    uint64_t gen_config = 0;
    gen_config |= HPET_ENABLE_CNF;         // Enable HPET
    gen_config |= HPET_LEGACY_REPLACEMENT; // Reemplaza PIT en IRQ0

    *((volatile uint64_t*)((uint8_t*)hpet_base + HPET_GENERAL_CONFIGURATION)) = gen_config;

    LOG_OK("[HPET] Inicializado con interrupciones periódicas en IRQ0.\n");
}
