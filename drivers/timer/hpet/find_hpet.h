#include "acpi.h"
#include "../../../includes/print.h"
#include "../../../includes/stddef.h"
#include "../../../includes/stdint.h"


#define ACPI_RSDP_SIGNATURE "RSD PTR "
#define ACPI_HPET_SIGNATURE 0x54455048 
#define RSDP_SEARCH_START 0x000E0000
#define RSDP_SEARCH_END   0x000FFFFF

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
