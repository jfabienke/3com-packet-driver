/* Module ABI v1.0 - 3Com Packet Driver Modular Architecture
 * Version: 1.0
 * Date: 2025-08-22
 * 
 * FINAL SPECIFICATION - Changes require RFC and version increment
 * All agents must implement this exact 64-byte layout
 */

#ifndef MODULE_ABI_H
#define MODULE_ABI_H

#include <stdint.h>

/* Module Header Layout - Exactly 64 bytes, little-endian */
typedef struct {
    /* 0x00: Module Identification (8 bytes) */
    char     signature[4];        /* "MD64" - Module Driver 64-byte header */
    uint8_t  abi_version;         /* ABI version (1 = v1.0) */
    uint8_t  module_type;         /* Module type (see MODULE_TYPE_*) */
    uint16_t flags;               /* Module flags (see MODULE_FLAG_*) */
    
    /* 0x08: Memory Layout (8 bytes) */
    uint16_t total_size_para;     /* Total module size in paragraphs (16 bytes) */
    uint16_t resident_size_para;  /* Resident size after cold discard (paragraphs) */
    uint16_t cold_size_para;      /* Cold section size to discard (paragraphs) */
    uint16_t alignment_para;      /* Required paragraph alignment (1=16 bytes) */
    
    /* 0x10: Entry Points (8 bytes) */
    uint16_t init_offset;         /* Module initialization entry point */
    uint16_t api_offset;          /* Hot API entry point offset */
    uint16_t isr_offset;          /* ISR entry point (0 if no ISR) */
    uint16_t unload_offset;       /* Module cleanup entry point */
    
    /* 0x18: Symbol Resolution (8 bytes) */
    uint16_t export_table_offset; /* Offset to export directory */
    uint16_t export_count;        /* Number of exported symbols */
    uint16_t reloc_table_offset;  /* Offset to relocation table */
    uint16_t reloc_count;         /* Number of relocation entries */
    
    /* 0x20: BSS and Requirements (8 bytes) */
    uint16_t bss_size_para;       /* Uninitialized data size (paragraphs) */
    uint16_t required_cpu;        /* Required CPU (see CPU_TYPE_*) */
    uint16_t required_features;   /* Required features (see FEATURE_*) */
    uint16_t module_id;           /* Unique module ID */
    
    /* 0x28: Module Name (12 bytes) */
    char     module_name[11];     /* 8.3 format uppercase, null-padded */
    uint8_t  name_padding;        /* Align to even boundary */
    
    /* 0x34: Integrity and Reserved (16 bytes) */
    uint16_t header_checksum;     /* Header checksum (excluding this field) */
    uint16_t image_checksum;      /* Image checksum */
    uint32_t vendor_id;           /* Vendor identifier */
    uint32_t build_timestamp;     /* Build timestamp */
    uint32_t reserved[2];         /* Reserved for future use - must be 0 */
    
} __attribute__((packed)) module_header_t;

/* Static assertion to ensure exact 64-byte size */
_Static_assert(sizeof(module_header_t) == 64, "Module header must be exactly 64 bytes");

/* Module Types */
#define MODULE_TYPE_NIC         0x01  /* Network interface card driver */
#define MODULE_TYPE_SERVICE     0x02  /* Core service module */
#define MODULE_TYPE_FEATURE     0x03  /* Optional feature module */
#define MODULE_TYPE_DIAGNOSTIC  0x04  /* Diagnostic and testing module */

/* Module Flags */
#define MODULE_FLAG_DISCARD_COLD    0x0001  /* Has cold section to discard */
#define MODULE_FLAG_HAS_ISR         0x0002  /* Provides interrupt handler */
#define MODULE_FLAG_NEEDS_DMA_SAFE  0x0004  /* Requires DMA-safe buffers */
#define MODULE_FLAG_XMS_OPTIONAL    0x0008  /* Can use XMS if available */
#define MODULE_FLAG_SMC_USED        0x0010  /* Uses self-modifying code */
#define MODULE_FLAG_NEEDS_TIMER     0x0020  /* Requires timer services */
#define MODULE_FLAG_PCMCIA_AWARE    0x0040  /* Supports PCMCIA hot-plug */
#define MODULE_FLAG_PCI_AWARE       0x0080  /* Supports PCI configuration */

/* CPU Types (minimum required) */
#define CPU_TYPE_8086           0x0086
#define CPU_TYPE_80286          0x0286
#define CPU_TYPE_80386          0x0386
#define CPU_TYPE_80486          0x0486
#define CPU_TYPE_PENTIUM        0x0586

/* Required Features */
#define FEATURE_NONE            0x0000
#define FEATURE_FPU             0x0001  /* Floating point unit required */
#define FEATURE_MMX             0x0002  /* MMX instructions (Pentium MMX+) */
#define FEATURE_CPUID           0x0004  /* CPUID instruction support */

/* Standard Module IDs */
#define MODULE_ID_PTASK         0x5054  /* 'PT' - Parallel Tasking */
#define MODULE_ID_CORKSCRW      0x434B  /* 'CK' - Corkscrew */
#define MODULE_ID_BOOMTEX       0x4254  /* 'BT' - Boomerang Extended */
#define MODULE_ID_MEMPOOL       0x4D50  /* 'MP' - Memory Pool */
#define MODULE_ID_PCCARD        0x5043  /* 'PC' - PC Card Services */
#define MODULE_ID_ROUTING       0x5254  /* 'RT' - Routing */
#define MODULE_ID_STATS         0x5354  /* 'ST' - Statistics */
#define MODULE_ID_DIAG          0x4447  /* 'DG' - Diagnostics */

/* Export Directory Entry - 12 bytes each */
typedef struct {
    char     symbol_name[8];      /* Symbol name, null-padded */
    uint16_t symbol_offset;       /* Offset from module base */
    uint16_t symbol_flags;        /* Symbol flags (see SYMBOL_FLAG_*) */
} __attribute__((packed)) export_entry_t;

/* Symbol Flags */
#define SYMBOL_FLAG_FUNCTION    0x0001  /* Symbol is a function */
#define SYMBOL_FLAG_DATA        0x0002  /* Symbol is data */
#define SYMBOL_FLAG_FAR_CALL    0x0004  /* Function uses far calling convention */
#define SYMBOL_FLAG_ISR_SAFE    0x0008  /* Function is ISR-safe */

/* Relocation Entry - 4 bytes each */
typedef struct {
    uint8_t  reloc_type;          /* Relocation type (see RELOC_TYPE_*) */
    uint8_t  reserved;            /* Reserved, must be 0 */
    uint16_t reloc_offset;        /* Offset from module base to patch */
} __attribute__((packed)) reloc_entry_t;

/* Relocation Types */
#define RELOC_TYPE_SEG_OFS      0x01  /* Segment:offset far pointer */
#define RELOC_TYPE_SEGMENT      0x02  /* Segment word only */
#define RELOC_TYPE_OFFSET       0x03  /* Offset word only */
#define RELOC_TYPE_REL_NEAR     0x04  /* Near relative jump/call */
#define RELOC_TYPE_REL_FAR      0x05  /* Far relative call */

/* Module Validation */
#define MODULE_SIGNATURE        "MD64"
#define MODULE_ABI_VERSION      1

/* Header validation function */
static inline int validate_module_header(const module_header_t *hdr) {
    if (!hdr) return 0;
    
    /* Check signature */
    if (hdr->signature[0] != 'M' || hdr->signature[1] != 'D' ||
        hdr->signature[2] != '6' || hdr->signature[3] != '4') {
        return 0;
    }
    
    /* Check ABI version */
    if (hdr->abi_version != MODULE_ABI_VERSION) {
        return 0;
    }
    
    /* Basic sanity checks */
    if (hdr->total_size_para == 0 || hdr->resident_size_para == 0) {
        return 0;
    }
    
    if (hdr->resident_size_para > hdr->total_size_para) {
        return 0;
    }
    
    /* Validate entry points are within module bounds */
    uint16_t module_size_bytes = hdr->total_size_para * 16;
    if (hdr->init_offset >= module_size_bytes ||
        hdr->api_offset >= module_size_bytes ||
        hdr->unload_offset >= module_size_bytes) {
        return 0;
    }
    
    return 1;
}

/* Checksum calculation (simple additive checksum) */
static inline uint16_t calculate_header_checksum(const module_header_t *hdr) {
    const uint8_t *bytes = (const uint8_t*)hdr;
    uint16_t checksum = 0;
    
    /* Sum all bytes except the checksum field itself */
    for (int i = 0; i < sizeof(module_header_t); i++) {
        if (i >= offsetof(module_header_t, header_checksum) &&
            i < offsetof(module_header_t, header_checksum) + sizeof(hdr->header_checksum)) {
            continue; /* Skip checksum field */
        }
        checksum += bytes[i];
    }
    
    return (~checksum) + 1; /* Two's complement */
}

/* Helper macros for module definition */
#define MODULE_HEADER_INIT(name, type, id, flags) { \
    .signature = MODULE_SIGNATURE, \
    .abi_version = MODULE_ABI_VERSION, \
    .module_type = (type), \
    .flags = (flags), \
    .module_id = (id), \
    .module_name = name, \
    .required_cpu = CPU_TYPE_80286, \
    .alignment_para = 1 \
}

/* Module Instance - Runtime tracking structure */
typedef struct {
    uint16_t module_segment;      /* Base segment of loaded module */
    uint16_t total_size_para;     /* Total allocated size in paragraphs */
    uint16_t resident_size_para;  /* Size after cold discard */
    void far *module_base;        /* Far pointer to module base */
    module_header_t *header;      /* Pointer to module header */
    uint8_t status;               /* Module status (see MODULE_STATUS_*) */
    uint8_t load_order;           /* Loading sequence number */
} module_instance_t;

/* Module Status */
#define MODULE_STATUS_UNLOADED      0x00
#define MODULE_STATUS_LOADING       0x01
#define MODULE_STATUS_LOADED        0x02
#define MODULE_STATUS_INITIALIZING  0x03
#define MODULE_STATUS_ACTIVE        0x04
#define MODULE_STATUS_ERROR         0x05

/* Symbol Resolution API */
typedef struct {
    char symbol_name[9];          /* Null-terminated symbol name */
    void far *symbol_address;     /* Far pointer to symbol */
    uint16_t symbol_flags;        /* Symbol attributes */
    uint16_t module_id;           /* Module providing this symbol */
} resolved_symbol_t;

/* Global symbol table for O(log N) binary search */
extern resolved_symbol_t g_symbol_table[];
extern uint16_t g_symbol_count;

/* Symbol resolution API - O(log N) binary search */
void far *resolve_symbol(const char *symbol_name);
int register_symbol(const resolved_symbol_t *symbol);
int unregister_module_symbols(uint16_t module_id);

/* Module loading API */
int load_module(const char *filename, module_instance_t *instance);
int initialize_module(module_instance_t *instance);
int discard_cold_section(module_instance_t *instance);
int unload_module(module_instance_t *instance);

/* Module entry point function types */
typedef int (far *module_init_func_t)(void);
typedef int (far *module_api_func_t)(uint16_t function, void far *params);
typedef void (far *module_isr_func_t)(void);
typedef int (far *module_cleanup_func_t)(void);

/* Error codes for module operations */
#define MODULE_SUCCESS                  0x0000
#define MODULE_ERROR_FILE_NOT_FOUND    0x0020
#define MODULE_ERROR_INVALID_MODULE    0x0021
#define MODULE_ERROR_INCOMPATIBLE      0x0022
#define MODULE_ERROR_LOAD_FAILED       0x0023
#define MODULE_ERROR_INIT_FAILED       0x0024
#define MODULE_ERROR_ALREADY_LOADED    0x0025
#define MODULE_ERROR_DEPENDENCY        0x0026
#define MODULE_ERROR_ABI_MISMATCH      0x0027
#define MODULE_ERROR_CHECKSUM          0x0028
#define MODULE_ERROR_RELOCATION        0x0029
#define MODULE_ERROR_SYMBOL            0x002A
#define MODULE_ERROR_OUT_OF_MEMORY     0x002B

/* Include timing measurement support */
#include "timemsr.h"

#endif /* MODULE_ABI_H */