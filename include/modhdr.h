/**
 * @file module_header.h
 * @brief Standardized 64-byte module header for hot/cold separation
 *
 * This header structure MUST be present at the start of every module
 * to support self-modifying code patching and hot/cold section management.
 *
 * Constraints:
 * - Exactly 64 bytes total size
 * - Aligned on paragraph boundary (16 bytes)
 * - Compatible with DOS real mode addressing
 */

#ifndef MODULE_HEADER_H
#define MODULE_HEADER_H

#include <stdint.h>
#include "cpudet.h"  /* For cpu_info_t */

/* Module signature - identifies valid packet driver module */
#define MODULE_SIGNATURE    "PKTDRV"
#define MODULE_SIG_SIZE     7

/* Module version */
#define MODULE_VERSION_MAJOR 1
#define MODULE_VERSION_MINOR 0

/* Patch types for SMC framework */
typedef enum {
    PATCH_TYPE_COPY        = 0x01,  /* Memory copy operations */
    PATCH_TYPE_IO          = 0x02,  /* Port I/O operations */
    PATCH_TYPE_CHECKSUM    = 0x03,  /* Checksum calculations */
    PATCH_TYPE_ISR         = 0x04,  /* Interrupt handlers */
    PATCH_TYPE_BRANCH      = 0x05,  /* Conditional branches */
    PATCH_TYPE_DMA_CHECK   = 0x06,  /* DMA boundary validation */
    PATCH_TYPE_CACHE_PRE   = 0x07,  /* Pre-DMA cache management */
    PATCH_TYPE_CACHE_POST  = 0x08,  /* Post-DMA cache management */
    PATCH_TYPE_BOUNCE_COPY = 0x09,  /* Bounce buffer operations */
    PATCH_TYPE_ENDIAN      = 0x0A,  /* Endianness conversion (BSWAP) */
    PATCH_TYPE_NOP         = 0xFF   /* Remove code (NOP fill) */
} patch_type_t;

/* Module header structure - EXACTLY 64 bytes */
typedef struct {
    /* Identification (9 bytes) */
    char     signature[MODULE_SIG_SIZE];  /* "PKTDRV\0" */
    uint8_t  version_major;                /* Major version */
    uint8_t  version_minor;                /* Minor version */
    
    /* Section pointers (8 bytes) */
    uint16_t hot_start;                    /* Hot section start offset */
    uint16_t hot_end;                      /* Hot section end offset */
    uint16_t cold_start;                   /* Cold section start offset */
    uint16_t cold_end;                     /* Cold section end offset */
    
    /* Patch table (4 bytes) */
    uint16_t patch_table_offset;           /* Offset to patch table */
    uint16_t patch_count;                   /* Number of patch entries */
    
    /* Module info (6 bytes) */
    uint16_t module_size;                  /* Total module size */
    uint16_t required_memory;              /* Required resident memory */
    uint8_t  cpu_requirements;             /* Minimum CPU (2=286, 3=386, etc) */
    uint8_t  nic_type;                     /* NIC type (0=any, 1=3C509, 2=3C515) */

    /* Capability flags (2 bytes) - JIT module selection criteria */
    uint16_t cap_flags;                    /* Capability requirement flags */

    /* Reserved for alignment (35 bytes) */
    uint8_t  reserved[35];                 /* Future use / padding */
} module_header_t;

/* Verify structure is exactly 64 bytes */
#if defined(__WATCOMC__)
    #pragma aux module_header_size = \
        "mov ax, 64" \
        value [ax];
#endif

/* Safety requirement flags for patches */
typedef enum {
    SAFETY_FLAG_NONE       = 0x00,  /* No special safety requirements */
    SAFETY_FLAG_ISA_DMA    = 0x01,  /* Requires ISA DMA boundary check */
    SAFETY_FLAG_CACHE_MGMT = 0x02,  /* Requires cache management */
    SAFETY_FLAG_BUS_MASTER = 0x04,  /* Only if bus master enabled */
    SAFETY_FLAG_BOUNCE_BUF = 0x08,  /* May use bounce buffers */
    SAFETY_FLAG_CLFLUSH    = 0x10,  /* Can use CLFLUSH if available */
    SAFETY_FLAG_WBINVD     = 0x20,  /* Can use WBINVD if available */
} safety_flags_t;

/* Module capability requirement flags (cap_flags field in module_header_t) */
#define MOD_CAP_ISA_DMA         0x0001  /* Requires ISA DMA channel */
#define MOD_CAP_BUSMASTER_DMA   0x0002  /* Requires bus-master DMA */
#define MOD_CAP_WBINVD          0x0004  /* Requires WBINVD (486+) */
#define MOD_CAP_CLFLUSH         0x0008  /* Requires CLFLUSH (Pentium+) */
#define MOD_CAP_PCI_BUS         0x0010  /* Requires PCI bus */
#define MOD_CAP_BOUNCE_BUF      0x0020  /* Requires bounce buffer support */
#define MOD_CAP_VDS             0x0040  /* Requires VDS services */
#define MOD_CAP_XMS             0x0080  /* Requires XMS memory */
#define MOD_CAP_DESC_RING       0x0100  /* Requires DMA descriptor rings */
#define MOD_CAP_SNOOP           0x0200  /* Requires PCI cache snoop */
#define MOD_CAP_CORE            0x8000  /* Core module (always selected) */

/* NIC type identifiers for module selection */
#define MOD_NIC_ANY             0x00
#define MOD_NIC_3C509B          0x01
#define MOD_NIC_3C515           0x02
#define MOD_NIC_VORTEX          0x03    /* 3C590/595/597 PCI PIO */
#define MOD_NIC_BOOMERANG       0x04    /* 3C900/905 PCI DMA */
#define MOD_NIC_CYCLONE         0x05    /* 3C905B/920 PCI DMA+csum */
#define MOD_NIC_TORNADO         0x06    /* 3C905C/556 PCI DMA+SG+VLAN */

/* Module IDs for selection */
typedef enum {
    /* Core JIT modules (always included) */
    MOD_ISR = 0,
    MOD_IRQ,
    MOD_PKTBUF,
    MOD_DATA,
    /* NIC-specific (mutually exclusive) */
    MOD_3C509B,
    MOD_3C515,
    MOD_VORTEX,
    MOD_BOOMERANG,
    MOD_CYCLONE,
    MOD_TORNADO,
    /* DMA/Transfer */
    MOD_PIO,
    MOD_DMA_ISA,
    MOD_DMA_BUSMASTER,
    MOD_DMA_DESCRING,
    MOD_DMA_BOUNCE,
    /* Cache coherency */
    MOD_CACHE_NONE,
    MOD_CACHE_WBINVD,
    MOD_CACHE_CLFLUSH,
    MOD_CACHE_SNOOP,
    /* CPU-optimized copy */
    MOD_COPY_8086,
    MOD_COPY_286,
    MOD_COPY_386,
    MOD_COPY_PENT,
    /* Core ASM modules (always included, Stage 2 TSR) */
    MOD_CORE_PKTAPI,
    MOD_CORE_NICIRQ,
    MOD_CORE_HWSMC,
    MOD_CORE_PCMISR,
    MOD_CORE_FLOWRT,
    MOD_CORE_DIRPIO,
    MOD_CORE_PKTOPS,
    MOD_CORE_PKTCOPY,
    MOD_CORE_TSRCOM,
    MOD_CORE_TSRWRAP,
    MOD_CORE_PCI_IO,
    MOD_CORE_PCIISR,
    MOD_CORE_LINKASM,
    MOD_CORE_HWPKT,
    MOD_CORE_HWCFG,
    MOD_CORE_HWCOORD,
    MOD_CORE_HWINIT,
    MOD_CORE_HWEEP,
    MOD_CORE_HWDMA,
    MOD_CORE_CACHEOPS,
    MOD_CORE_TSR_CRT,
    /* Sentinel */
    MOD_COUNT
} module_id_t;

/* First and last core module IDs (for iteration) */
#define MOD_CORE_FIRST  MOD_CORE_PKTAPI
#define MOD_CORE_LAST   MOD_CORE_TSR_CRT

/* JIT patch types (extends existing patch_type_t) */
#define PATCH_TYPE_IMM16        0x06    /* 16-bit immediate (IRQ, DMA ch) */
#define PATCH_TYPE_IMM8         0x07    /* 8-bit immediate */
#define PATCH_TYPE_RELOC_NEAR   0x08    /* Near CALL/JMP relocation */

/* Patch table entry structure */
typedef struct {
    uint16_t patch_offset;      /* Offset from module start to patch point */
    uint8_t  patch_type;        /* Type of patch (patch_type_t) */
    uint8_t  patch_size;        /* Size of patch area in bytes */
    uint8_t  cpu_8086[5];       /* 8086 code (up to 5 bytes) */
    uint8_t  cpu_286[5];        /* 286 code */
    uint8_t  cpu_386[5];        /* 386 code */
    uint8_t  cpu_486[5];        /* 486 code */
    uint8_t  cpu_pentium[5];    /* Pentium code */
} patch_entry_t;

/* Enhanced patch table entry with safety support */
typedef struct {
    uint16_t patch_offset;      /* Offset from module start to patch point */
    uint8_t  patch_type;        /* Type of patch (patch_type_t) */
    uint8_t  patch_size;        /* Size of patch area in bytes */
    uint8_t  safety_flags;      /* Safety requirements (safety_flags_t) */
    uint8_t  reserved;          /* Alignment padding */
    
    /* CPU variants with safety considerations */
    uint8_t  cpu_8086[5];       /* 8086 code */
    uint8_t  cpu_286_pio[5];    /* 286 PIO fallback */
    uint8_t  cpu_286_dma[5];    /* 286 DMA if safe */
    uint8_t  cpu_386_pio[5];    /* 386 PIO fallback */
    uint8_t  cpu_386_dma[5];    /* 386 DMA if safe */
    uint8_t  cpu_486[5];        /* 486 code */
    uint8_t  cpu_pentium[5];    /* Pentium code */
    uint8_t  cpu_p4_clflush[5]; /* Pentium 4+ with CLFLUSH */
} enhanced_patch_entry_t;

/* Helper macros for assembly modules */
#ifdef __ASSEMBLER__

; Module header macro for assembly files
.macro MODULE_HEADER name, ver_maj, ver_min, nic
module_header_\name:
    .ascii  "PKTDRV"            ; Signature
    .byte   0                    ; Null terminator
    .byte   \ver_maj             ; Major version
    .byte   \ver_min             ; Minor version
    .word   hot_start_\name      ; Hot section start
    .word   hot_end_\name        ; Hot section end
    .word   cold_start_\name     ; Cold section start
    .word   cold_end_\name       ; Cold section end
    .word   patch_table_\name    ; Patch table offset
    .word   patch_count_\name    ; Number of patches
    .word   module_size_\name    ; Module size
    .word   required_mem_\name   ; Required memory
    .byte   2                    ; CPU requirement (286 minimum)
    .byte   \nic                 ; NIC type
    .word   0                    ; cap_flags (set per module)
    .space  35, 0                ; Reserved padding
.endm

; Patch point macro for assembly
.macro PATCH_POINT name, default_code, size
patch_point_\name:
    \default_code                ; Default 8086 code
    .if (\size - (. - patch_point_\name)) > 0
        .space (\size - (. - patch_point_\name)), 0x90  ; NOP padding
    .endif
.endm

#endif /* __ASSEMBLER__ */

/* Function prototypes for module management */
int validate_module_header(const module_header_t* header);
int apply_module_patches(module_header_t* header, const cpu_info_t* cpu_info);
uint16_t calculate_resident_size(const module_header_t* header);

#endif /* MODULE_HEADER_H */