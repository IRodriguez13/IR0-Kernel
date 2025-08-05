#include "Paging.h"

extern void idt_flush(void);

// Tamaño std para directiorio/tablas en 32-bit paging

#define PAGE_ENTRIES 1024 // esto es 2 a la 10 bits por cada nivel, ya que el nvl1 procesa 10 bits, el segundo tambien y el tercero contiene el offset de los 12 restantes
#define PAGE_SIZE 4096    // 4kb que es lo que suele medir cada tabla (porque son páginas también)
#define TABLE_RANGE_SIZE (PAGE_ENTRIES * PAGE_SIZE) // 4MB por tabla


// Alineo a 4 kb por necesidad de la cpu. Estos son mis dos niveles de paginación en caada tabla que arme.

__attribute__((aligned(PAGE_SIZE))) uint32_t page_directory[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t first_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t second_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t third_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Fouth_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Five_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Six_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Seven_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Eight_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Nine_page_table[PAGE_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint32_t Ten_page_table[PAGE_ENTRIES];

void Fill_Table_Page(uint32_t Directory_index, uint32_t *table, uint32_t start_adress)
{
    for (uint32_t i = 0; i < PAGE_ENTRIES; i++)
    {
        table[i] = (start_adress + i * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITE;
    }

    // Ahora le digo al directorio de páginas que esta tabla es usable
    page_directory[Directory_index] = ((uint32_t)table) | PAGE_PRESENT | PAGE_WRITE;
}

void Clean_Remaining_Tables(uint32_t dir_index)
{
    // La CPU va a seguir buscando los 1023 tablas restantes que no están creadas asi que las tengo que inicialiozar en 0x00
    for (uint32_t i = dir_index; i < PAGE_ENTRIES; i++)
    {
        page_directory[i] = 0x00; // Convierto en NULL las próximas 1018 páginas. O las que falten dependiendo de la cantidad de tablas que tenga.
    }
}

void init_paging()
{
    // Ahora debería llenar una página (tabla de paginación) de páginas mapeables a el directorio de páginas.
    Fill_Table_Page(0, first_page_table, 0 *TABLE_RANGE_SIZE);
    Fill_Table_Page(1, second_page_table, 1 *TABLE_RANGE_SIZE);
    Fill_Table_Page(2, third_page_table, 2 *TABLE_RANGE_SIZE);
    Fill_Table_Page(3, Fouth_page_table, 3 *TABLE_RANGE_SIZE);
    Fill_Table_Page(4, Five_page_table, 4 *TABLE_RANGE_SIZE);
    Fill_Table_Page(5, Six_page_table, 5 *TABLE_RANGE_SIZE);
    Fill_Table_Page(6, Seven_page_table, 6 *TABLE_RANGE_SIZE);
    Fill_Table_Page(7, Eight_page_table, 7 *TABLE_RANGE_SIZE);
    Fill_Table_Page(8, Nine_page_table, 8 *TABLE_RANGE_SIZE);
    Fill_Table_Page(9, Ten_page_table, 9 *TABLE_RANGE_SIZE);

    Clean_Remaining_Tables(10);

    // Le tengoque decir a la CPU cómo estoy paginando y que inicie la paginación
    asm volatile("mov %0, %%cr3" ::"r"(page_directory));

    // Le digo a la CPU que active el bit de paginación
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));

    cr0 |= 0x80000000;

    asm volatile("mov %0, %%cr0" ::"r"(cr0));
}
