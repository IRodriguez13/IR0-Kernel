#include "../../../includes/stdint.h"
#ifndef TIMER_H
#define TIMER_H
/*
                        ==== El timer =====
Básicamente, le tengo que hacer saber al kernel que está trabajando con noción del tiempo, para saber cuando lanzar interrupciones al IDT 
y cómo manejarlas. Se le dice PIT (Programmable Interval Timer)

*/

// Sirve para hacerle saber al kernel sobre el paso interno del tiempo.
void init_PIT(uint32_t frecuency);


// Acá arranco el contador del timer
void time_handler();

/*
    El PIT es un chip físico en la MOBO que se comunic
    a por puertos, y para enviar info tenemos que hablarle mediante interrupciones IRQ

    Como este bicho es parte del hard del sistema, tengo que interrumpir y manejar específicamente sus interrupciones (asm con iret)
    Además, los datos que espera son de a 8 bytes, por lo que tengo que mandar primero los 8 bajos y después los 8 altos.

    
*/

#endif