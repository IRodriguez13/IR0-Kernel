#include "Paging.h"

// Tamaño std para directiorio/tablas en 32-bit paging

#define PAGE_ENTRIES 1024 // esto es 2 a la 10 bits por cada nivel, ya que el nvl1 procesa 10 bits, el segundo tambien y el tercero contiene el offset de los 12 restantes
#define PAGE_SIZE 4096    // 4kb que es lo que suele medir cada tabla (porque son páginas también)

// Flags

#define PAGE_PRESENT 0x1 // Con este bit, me aseguro de que la página es mapeable. sino page fault
#define PAGE_WRITE 0x2   // Con este, que puedo escribir en la página, sino page fault
#define PAGE_USER 0x4    // Cpn este bit (el 2), le digo  al sistema que tanto kernel como programas de usuario pueden acceder acá.

// Alineo a 4 kb por necesidad de la cpu. Estos son mis dos niveles de paginación en caada tabla que arme.

__attribute__((aligned(PAGE_SIZE))) uint32_t page_directory[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t first_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t second_page_table[PAGE_ENTRIES];

void init_paging()
{

    // Ahora debería llenar una página (tabla de paginación) de páginas mapeables a el directorio de páginas.

    for (uint32_t i = 0; i < PAGE_ENTRIES; i++)
    {
        first_page_table[i] = (i * 4096) | PAGE_PRESENT | PAGE_WRITE;
    }

    // Ahora le digo al directorio de páginas que esta tabla es usable
    page_directory[0] = ((uint32_t)first_page_table) | PAGE_PRESENT | PAGE_WRITE;

    // La CPU va a seguir buscando los 1023 tablas restantes que no están creadas asi que las tengo que inicialiozar en 0x00

    for (uint32_t i = 1; i < PAGE_ENTRIES; i++)
    {
        page_directory[i] = 0x00;
    }

    // Le tengoque decir a la CPU cómo estoy paginando y que inicie la paginación


}
