/* Hello Module - 3Com Packet Driver Modular Architecture Demo
 * Version: 1.0
 * Date: 2025-08-22
 * 
 * Demonstration module implementing the 64-byte ABI header and showing
 * hot/cold section separation, symbol export, and proper entry points.
 */

#include <stdio.h>
#include <string.h>
#include "../../include/module_abi.h"

/* Module header - exactly 64 bytes, will be placed at offset 0 */
const module_header_t module_header = {
    .signature = "MD64",
    .abi_version = MODULE_ABI_VERSION,
    .module_type = MODULE_TYPE_DIAGNOSTIC,
    .flags = MODULE_FLAG_DISCARD_COLD,
    
    .total_size_para = 32,      /* 512 bytes total (example) */
    .resident_size_para = 24,   /* 384 bytes resident */
    .cold_size_para = 8,        /* 128 bytes cold (discarded) */
    .alignment_para = 1,        /* 16-byte alignment */
    
    .init_offset = 64,          /* Offset to hello_init */
    .api_offset = 128,          /* Offset to hello_api */
    .isr_offset = 0,            /* No ISR */
    .unload_offset = 192,       /* Offset to hello_cleanup */
    
    .export_table_offset = 256, /* Offset to exports */
    .export_count = 3,          /* Number of exports */
    .reloc_table_offset = 292,  /* Offset to relocations */
    .reloc_count = 2,           /* Number of relocations */
    
    .bss_size_para = 1,         /* 16 bytes BSS */
    .required_cpu = CPU_TYPE_80286,
    .required_features = FEATURE_NONE,
    .module_id = MODULE_ID_DIAG,
    
    .module_name = "HELLO",
    .name_padding = 0,
    
    .header_checksum = 0,       /* Will be calculated */
    .image_checksum = 0,        /* Will be calculated */
    .vendor_id = 0x434C4155,    /* "CLAU" - Claude */
    .build_timestamp = 0,       /* Will be set by build */
    .reserved = {0, 0}
};

/* Hot data section - remains resident */
typedef struct {
    uint32_t call_count;        /* Number of API calls */
    uint16_t last_function;     /* Last function called */
    char status_message[32];    /* Current status */
} hot_data_t;

static hot_data_t hot_data = {
    .call_count = 0,
    .last_function = 0,
    .status_message = "Hello module initialized"
};

/* Cold data section - discarded after init */
typedef struct {
    char init_message[64];
    uint16_t init_sequence[16];
    uint8_t temp_buffer[64];
} cold_data_t;

static cold_data_t cold_data = {
    .init_message = "Hello module performing initialization...",
    .init_sequence = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},
    .temp_buffer = {0}
};

/* BSS section - uninitialized data, zeroed by loader */
static uint8_t bss_buffer[16];

/* Module entry points */

/* Initialization entry point - called once during loading */
int far hello_init(void) {
    printf("HELLO: %s\n", cold_data.init_message);
    
    /* Perform initialization using cold data */
    for (int i = 0; i < 16; i++) {
        cold_data.temp_buffer[i] = cold_data.init_sequence[i];
    }
    
    /* Initialize hot data */
    hot_data.call_count = 0;
    hot_data.last_function = 0;
    strcpy(hot_data.status_message, "Hello module ready");
    
    /* Verify BSS section was zeroed */
    for (int i = 0; i < sizeof(bss_buffer); i++) {
        if (bss_buffer[i] != 0) {
            printf("HELLO: ERROR - BSS section not zeroed at offset %d\n", i);
            return MODULE_ERROR_INIT_FAILED;
        }
    }
    
    printf("HELLO: Initialization complete, cold section can be discarded\n");
    return MODULE_SUCCESS;
}

/* API entry point - main module interface */
int far hello_api(uint16_t function, void far *params) {
    hot_data.call_count++;
    hot_data.last_function = function;
    
    switch (function) {
        case 0: /* Get info */
            printf("HELLO: Hello from modular 3Com packet driver!\n");
            printf("HELLO: Call count: %lu\n", hot_data.call_count);
            printf("HELLO: Status: %s\n", hot_data.status_message);
            return MODULE_SUCCESS;
            
        case 1: /* Set status message */
            if (params) {
                strncpy(hot_data.status_message, (char far*)params, 
                       sizeof(hot_data.status_message) - 1);
                hot_data.status_message[sizeof(hot_data.status_message) - 1] = '\0';
                printf("HELLO: Status updated to: %s\n", hot_data.status_message);
            }
            return MODULE_SUCCESS;
            
        case 2: /* Get statistics */
            if (params) {
                uint32_t far *stats = (uint32_t far*)params;
                stats[0] = hot_data.call_count;
                stats[1] = hot_data.last_function;
            }
            return MODULE_SUCCESS;
            
        default:
            printf("HELLO: Unknown function %u\n", function);
            return MODULE_ERROR_INVALID_PARAM;
    }
}

/* Cleanup entry point - called before module unload */
int far hello_cleanup(void) {
    printf("HELLO: Module cleanup, total API calls: %lu\n", hot_data.call_count);
    printf("HELLO: Final status: %s\n", hot_data.status_message);
    return MODULE_SUCCESS;
}

/* Exported functions for symbol resolution */
void far hello_print(void) {
    printf("HELLO: Print function called\n");
}

uint32_t far hello_get_version(void) {
    return 0x00010000;  /* Version 1.0 */
}

/* Export table - sorted alphabetically for binary search */
const export_entry_t export_table[3] = {
    {"cleanup", (uint16_t)hello_cleanup, SYMBOL_FLAG_FUNCTION | SYMBOL_FLAG_FAR_CALL},
    {"hello", (uint16_t)hello_print, SYMBOL_FLAG_FUNCTION | SYMBOL_FLAG_FAR_CALL},
    {"version", (uint16_t)hello_get_version, SYMBOL_FLAG_FUNCTION | SYMBOL_FLAG_FAR_CALL}
};

/* Relocation table - examples for demonstration */
const reloc_entry_t relocation_table[2] = {
    {RELOC_TYPE_SEGMENT, 0, 100},    /* Example segment relocation */
    {RELOC_TYPE_SEG_OFS, 0, 200}     /* Example far pointer relocation */
};