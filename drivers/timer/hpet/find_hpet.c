#include "find_hpet.h"
#include "../../../includes/stdint.h"



/* VERSIÓN MEJORADA - Con validaciones y implementación propia */
// Implementación propia de memcmp para freestanding
static int memcmp(const void* s1, const void* s2, size_t n) 
{
    const unsigned char* p1 = (const unsigned char*)s1;
    const unsigned char* p2 = (const unsigned char*)s2;
    
    for (size_t i = 0; i < n; i++) 
    {
        if (p1[i] != p2[i]) 
        {
            return p1[i] - p2[i];
        }
    }
    return 0;
}

// Validar si una dirección parece válida para ACPI
static int is_valid_acpi_address(uintptr_t addr) 
{
    // Verificar que esté en rangos válidos de memoria
    if (addr < 0x100000)  // Menor a 1MB
        return 0;
    if (addr > 0xFFFFFFFF)  // Mayor a 4GB (en 32-bit)
        return 0;
    if (addr & 0xF)  // No alineado a 16 bytes
        return 0;
    return 1;
}

// Validar checksum de RSDP
static int validate_rsdp_checksum(rsdp_t* rsdp) 
{
    uint8_t* bytes = (uint8_t*)rsdp;
    uint8_t sum = 0;
    
    for (int i = 0; i < 20; i++) 
    {  // RSDP v1.0 tiene 20 bytes
        sum += bytes[i];
    }
    
    return sum == 0; // Checksum válido si suma es 0
}

void* find_hpet_table() 
{
    for (uint32_t addr = RSDP_SEARCH_START; addr < RSDP_SEARCH_END; addr += 16) 
    {
        char* sig = (char*)addr;
        
        //  Usar implementación propia de memcmp
        if (memcmp(sig, ACPI_RSDP_SIGNATURE, 8) == 0) 
        {
            rsdp_t* rsdp = (rsdp_t*)addr;
            
            //  Validar checksum de RSDP
            if (!validate_rsdp_checksum(rsdp)) 
                continue; // RSDP corrupto, seguir buscando
            
            
            //  Validar que rsdt_address sea válido
            if (!is_valid_acpi_address(rsdp->rsdt_address))
                continue;
            
            
            acpi_sdt_header_t* rsdt = (acpi_sdt_header_t*)(uintptr_t)rsdp->rsdt_address;
            
            //  Validar longitud de RSDT
            if (rsdt->length < sizeof(acpi_sdt_header_t)) 
                continue;
            
            
            //  Verificar signature de RSDT
            if (memcmp(rsdt->signature, "RSDT", 4) != 0)     
                continue;
        
            
            int entries = (rsdt->length - sizeof(acpi_sdt_header_t)) / 4;
            
            // Limitar número de entradas para evitar loops infinitos
            if (entries > 64) 
                continue;
            
            
            uint32_t* table_ptrs = (uint32_t*)((uint8_t*)rsdt + sizeof(acpi_sdt_header_t));

            for (int i = 0; i < entries; i++) 
            {
                // Validar cada puntero de tabla
                if (!is_valid_acpi_address(table_ptrs[i])) 
                    continue;
                                
                acpi_sdt_header_t* table = (acpi_sdt_header_t*)(uintptr_t)table_ptrs[i];
                
                // Verificar que el puntero no sea NULL
                if (table == NULL) 
                    continue;
                
                
                // Comparar signature completa, no solo el primer uint32_t
                if (memcmp(table->signature, "HPET", 4) == 0) 
                    return table;
                
            }
        }
    }

    return NULL;
}