#include "acpi.h"
#include "print.h"

#define ACPI_RSDP_SIGNATURE "RSD PTR "
#define ACPI_HPET_SIGNATURE 0x54455048 // "HPET" en little endian
#define RSDP_SEARCH_START 0x000E0000   // direccion tope de la BIOS donde esta HPET
#define RSDP_SEARCH_END 0x000FFFFF     // Direccion inicial de la BIOS donde esta HPET

void *find_hpet_table()
{
    // Itero entre los límites de RSDP de a 16 por su alineación

    for (uint32_t direcc = RSDP_SEARCH_START; direcc < RSDP_SEARCH_END; direcc += 16)
    {
        char *siguiente = (char *)direcc;
        // en cada vuelta, pregunto si la direccion coincide con el str RSDP pointer que busco
        if (__builtin_memcmp(siguiente, ACPI_RSDP_SIGNATURE, 8) == 0) 
        {
            // si la encuentro, la guardo en la struct
            rsdp_t *rsdp = (rsdp_t *)direcc;
            acpi_sdt_header_t *rsdt = (acpi_sdt_header_t *)(uintptr_t)rsdp->rsdt_address;

            // verifico la cantidad de tablas rsdt
            int entries = (rsdt->length - sizeof(acpi_sdt_header_t)) / 4;
            uint32_t *table_ptrs = (uint32_t *)((uint8_t *)rsdt + sizeof(acpi_sdt_header_t));

            // Itero en las tablas HPET y pregunto si alguna tiene la firma ASCII que busco
            int i = 0;
            while (i < entries)
            {
                i++;
                acpi_sdt_header_t *table = (acpi_sdt_header_t *)(uintptr_t)table_ptrs[i];
               
                if (*(uint32_t *)table == ACPI_HPET_SIGNATURE)
                    return table;
                
            }
        }
    }
    return NULL;
}
