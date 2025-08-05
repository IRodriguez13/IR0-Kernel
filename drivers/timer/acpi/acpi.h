#pragma once

#include "../../../includes/stdint.h"
#include "../../../includes/stddef.h"


/*
 Bueno, esta es una función que me sirve para encontrar la firma ASCII de la tabla HPET (High Precission Event Timer)
 se buca a través de memo mapeada y, según la BIOS, anda por algún lugar de la memoria RAM entre 0x000E0000 – 0x000FFFFF. 
 Hay que llegar a esa región, iterar y leer la HPET
*/
void* find_hpet_table();

typedef struct {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed)) rsdp_t;

typedef struct {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_sdt_header_t;
