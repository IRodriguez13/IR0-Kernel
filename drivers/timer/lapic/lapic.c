#include "lapic.h"
#include "print.h"
#include "panic.h"

#define LAPIC_BASE 0xFEE00000
#define LAPIC_TIMER_REG 0x320
#define LAPIC_TIMER_DIV 0x3E0
#define LAPIC_TIMER_INIT_COUNT 0x380
#define LAPIC_TIMER_CURR_COUNT 0x390

// Registra valor en MMIO del LAPIC
static inline void lapic_write(uint32_t reg, uint32_t value)
{
    *((volatile uint32_t *)(LAPIC_BASE + reg)) = value;
}

// Lee valor actual del LAPIC MMIO
static inline uint32_t lapic_read(uint32_t reg)
{
    return *((volatile uint32_t *)(LAPIC_BASE + reg));
}

void lapic_init_timer()
{
    print_colored("[LAPIC] Inicializando temporizador local...\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    // Configurar divisor del reloj del LAPIC → /16 por ejemplo
    lapic_write(LAPIC_TIMER_DIV, 0x3); // divisor 16 (ver tabla del datasheet Intel)

    // Modo de interrupción: 0x20020 = periodic mode, IRQ 32
    lapic_write(LAPIC_TIMER_REG, 0x20020); // vector 32, periodic

    // Cargar el contador inicial
    lapic_write(LAPIC_TIMER_INIT_COUNT, 10000000); // este valor define frecuencia

    print_success("[LAPIC] Temporizador local configurado.\n");
}
