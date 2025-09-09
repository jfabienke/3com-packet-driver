/**
 * @file cpu_detect.h
 * @brief CPU detection definitions and structures
 *
 * Defines CPU types, features, and detection interfaces for
 * the self-modifying code patch system.
 */

#ifndef CPU_DETECT_H
#define CPU_DETECT_H

#include <stdint.h>

/* Success/error codes */
#define SUCCESS             0
#define ERROR_CPU_UNKNOWN   -1

/* CPU type identifiers */
typedef enum {
    CPU_TYPE_8086       = 0,     /* 8086/8088 - no cache management */
    CPU_TYPE_80186      = 1,     /* 80186/80188 - ENTER/LEAVE, PUSHA/POPA */
    CPU_TYPE_80286      = 2,     /* 80286 - no cache management */
    CPU_TYPE_80386      = 3,     /* 80386 - external cache, no coherency */
    CPU_TYPE_80486      = 4,     /* 80486 - WBINVD available */
    CPU_TYPE_CPUID_CAPABLE = 5,  /* Has CPUID - use family/model for details */
    CPU_TYPE_UNKNOWN    = 0xFF
} cpu_type_t;

/* CPU vendor identifiers */
typedef enum {
    VENDOR_INTEL        = 0,
    VENDOR_AMD          = 1,
    VENDOR_CYRIX        = 2,
    VENDOR_NEXGEN       = 3,
    VENDOR_UMC          = 4,
    VENDOR_TRANSMETA    = 5,
    VENDOR_RISE         = 6,
    VENDOR_VIA          = 7,
    VENDOR_UNKNOWN      = 0xFF
} cpu_vendor_t;

/* CPU feature flags */
#define CPU_FEATURE_NONE            0x0000
#define CPU_FEATURE_PROTECTED_MODE  0x0001  /* 286+ protected mode */
#define CPU_FEATURE_32BIT           0x0002  /* 386+ 32-bit registers */
#define CPU_FEATURE_PAGING          0x0004  /* 386+ paging support */
#define CPU_FEATURE_CACHE           0x0008  /* 486+ internal cache */
#define CPU_FEATURE_FPU             0x0010  /* FPU present */
#define CPU_FEATURE_MMX             0x0020  /* MMX instructions */
#define CPU_FEATURE_MSR             0x0040  /* Model-specific registers */
#define CPU_FEATURE_CPUID           0x0080  /* CPUID instruction */

/* 486+ Instruction Set Features for PCI Optimizations */
#define CPU_FEATURE_BSWAP           0x0100  /* 486+ BSWAP instruction */
#define CPU_FEATURE_BT_OPS          0x0200  /* 486+ BT/BTS/BTR/BTC instructions */
#define CPU_FEATURE_MOVZX           0x0400  /* 486+ MOVZX/MOVSX instructions */
#define CPU_FEATURE_XADD            0x0800  /* 486+ XADD instruction */
#define CPU_FEATURE_CMPXCHG         0x1000  /* 486+ CMPXCHG instruction */

/* GPT-5 Critical: Cache management features */
#define CPU_FEATURE_WBINVD          0x2000  /* 486+ WBINVD instruction */
#define CPU_FEATURE_CLFLUSH         0x4000  /* P4+ CLFLUSH instruction */
#define CPU_FEATURE_V86_MODE        0x8000  /* Running in V86 mode */
#define CPU_FEATURE_WBINVD_SAFE     0x10000 /* WBINVD safe to use (not V86) */

/* CPU information structure */
typedef struct {
    cpu_type_t  cpu_type;       /* CPU type identifier */
    cpu_vendor_t cpu_vendor;     /* CPU vendor identifier */
    uint32_t    features;        /* Feature flags (32-bit) */
    uint16_t    cpu_mhz;         /* Approximate CPU speed in MHz */
    uint8_t     speed_confidence; /* Speed measurement confidence (0-100%) */
    uint8_t     addr_bits;       /* Address bus width (20/24/32) */
    char        cpu_name[32];    /* CPU name string (expanded from 16) */
    char        cpu_codename[20]; /* CPU internal codename */
    char        vendor_string[13]; /* Vendor ID string from CPUID */
    uint8_t     cpu_family;      /* CPU family from CPUID */
    uint8_t     cpu_model;       /* CPU model from CPUID */
    uint8_t     stepping;        /* CPU stepping from CPUID */
    /* Cache information */
    uint16_t    l1_data_size;    /* L1 data cache size in KB */
    uint16_t    l1_code_size;    /* L1 instruction cache size in KB */
    uint16_t    l2_size;         /* L2 cache size in KB */
    uint8_t     cache_line_size; /* Cache line size in bytes */
    /* Boolean flags for features */
    bool        has_clflush;     /* CLFLUSH available (checked via CPUID) */
    bool        has_wbinvd;      /* WBINVD available (486+) */
    bool        has_cpuid;       /* CPUID instruction available */
    bool        in_v86_mode;     /* Running in V86 mode */
    /* GPT-5 Critical: Privilege level detection for WBINVD safety */
    uint8_t     current_cpl;     /* Current Privilege Level (0-3) */
    bool        in_ring0;        /* Convenient boolean for CPL == 0 */
    bool        can_wbinvd;      /* CPL 0 AND family >= 4 AND not V86 */
    bool        has_cyrix_ext;   /* Cyrix CPU extensions detected */
    bool        is_hypervisor;   /* Running under hypervisor/VM */
} cpu_info_t;

/* Function prototypes */
int cpu_detect_init(void);
const cpu_info_t* cpu_get_info(void);
void detect_cpu_speed(cpu_info_t* info);
const char* cpu_type_to_string(cpu_type_t type);
uint8_t cpu_get_family(void);

/* Assembly helpers (implemented in cpu_detect.asm) */
extern int cpu_detect_main(void);
extern int asm_detect_cpu_type(void);
extern uint32_t asm_get_cpu_flags(void);
extern uint8_t asm_get_cpu_family(void);
extern uint32_t asm_get_cpuid_max_level(void);
extern int asm_is_v86_mode(void);
extern int asm_get_interrupt_flag(void);
extern uint16_t asm_check_cpu_flags(void);
extern int asm_has_cpuid(void);
extern void asm_get_cpuid_info(uint32_t level, uint32_t* eax, uint32_t* ebx, 
                               uint32_t* ecx, uint32_t* edx);
extern uint8_t asm_get_cpu_vendor(void);
extern char far* asm_get_cpu_vendor_string(void);
extern int asm_has_cyrix_extensions(void);
extern uint8_t asm_get_cpu_model(void);
extern uint8_t asm_get_cpu_stepping(void);
extern uint8_t asm_is_hypervisor(void);

/* CPU database functions (implemented in cpu_database.c) */
int intel_486_has_cpuid(const char* s_spec);
const char* intel_486_get_model(const char* s_spec);
int amd_k5_has_pge_bug(uint8_t model);
int cyrix_needs_cpuid_enable(const cpu_info_t* info);
int nexgen_nx586_detected(void);
void log_cpu_database_info(const cpu_info_t* info);

#endif /* CPU_DETECT_H */