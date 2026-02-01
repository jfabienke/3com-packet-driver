/**
 * @file cpu_detect.c
 * @brief Centralized CPU detection interface for loader (cold section)
 *
 * This module interfaces with the Assembly CPU detection routines to gather
 * CPU type, features, and vendor information during initialization. It is
 * discarded after TSR installation. Results are used for one-time SMC patching.
 *
 * Constraints:
 * - DOS real mode only
 * - Must work on 8086+ processors (8086/8088 support added)
 * - Results used for one-time SMC patching
 * - Entire module discarded after init (cold section)
 * - ALL CPU detection is performed by Assembly module
 */

#include "dos_io.h"
#include <string.h>
#include <dos.h>
#include <stdint.h>
#include "../include/cpudet.h"
#include "../include/platform_probe.h"
#include "../include/logging.h"
#include "../include/prod.h"

/* Additional assembly routines not declared in cpudet.h */
extern void asm_get_cache_info(uint16_t* l1d, uint16_t* l1i, uint16_t* l2, uint8_t* line);
extern uint16_t asm_get_cpu_speed(void);
extern uint8_t asm_get_speed_confidence(void);
extern uint8_t asm_has_invariant_tsc(void);

/* Mark entire file for cold section */
#pragma code_seg("COLD_TEXT", "CODE")

/* CPU type strings for logging - matches enum order */
static const char* cpu_names[] = {
    "8086/8088",
    "80186/80188",
    "80286",
    "80386",
    "80486",
    "CPUID-capable",
    "Unknown"
};

/* CPU vendor strings for logging - matches enum order */
static const char* vendor_names[] = {
    "Intel",
    "AMD",
    "Cyrix",
    "NexGen",
    "UMC",
    "Transmeta",
    "Rise",
    "VIA/Centaur",
    "Unknown"
};

/* CPU model identification structure */
typedef struct {
    uint8_t family;
    uint8_t model;
    const char* name;
    const char* codename;
} cpu_model_entry_t;

/* Intel CPU models with codenames */
static const cpu_model_entry_t intel_cpus[] = {
    /* Pentium Family */
    {5, 1, "Pentium", "P5"},
    {5, 2, "Pentium", "P54C"},
    {5, 4, "Pentium MMX", "P55C"},
    {5, 7, "Pentium MMX Mobile", "Tillamook"},
    
    /* Pentium Pro/II/III Family */
    {6, 1, "Pentium Pro", "P6"},
    {6, 3, "Pentium II", "Klamath"},
    {6, 5, "Pentium II", "Deschutes"},
    {6, 6, "Celeron", "Mendocino"},
    {6, 7, "Pentium III", "Katmai"},
    {6, 8, "Pentium III", "Coppermine"},
    {6, 9, "Pentium M", "Banias"},
    {6, 10, "Pentium III Xeon", "Cascades"},
    {6, 11, "Pentium III", "Tualatin"},
    {6, 13, "Pentium M", "Dothan"},
    {6, 14, "Core Solo/Duo", "Yonah"},
    {6, 15, "Core 2", "Conroe"},
    {6, 22, "Core 2", "Penryn"},
    {6, 23, "Core 2", "Wolfdale"},
    {6, 26, "Core i7", "Nehalem"},
    {6, 28, "Atom", "Bonnell"},
    {6, 30, "Core i7", "Lynnfield"},
    {6, 37, "Core i5", "Westmere"},
    {6, 42, "Core i7", "Sandy Bridge"},
    {6, 58, "Core i7", "Ivy Bridge"},
    
    /* Pentium 4 Family */
    {15, 0, "Pentium 4", "Willamette"},
    {15, 1, "Pentium 4", "Willamette"},
    {15, 2, "Pentium 4", "Northwood"},
    {15, 3, "Pentium 4", "Prescott"},
    {15, 4, "Pentium 4", "Prescott"},
    {15, 6, "Pentium 4", "Cedar Mill"},
    {0, 0, NULL, NULL}
};

/* AMD CPU models with codenames */
static const cpu_model_entry_t amd_cpus[] = {
    /* K5/K6 Family */
    {5, 0, "K5", "SSA5"},
    {5, 1, "K5", "5k86"},
    {5, 2, "K5", "5k86"},
    {5, 3, "K5", "5k86"},
    {5, 6, "K6", "Little Foot"},
    {5, 7, "K6", "Little Foot"},
    {5, 8, "K6-2", "Chomper"},
    {5, 9, "K6-III", "Sharptooth"},
    {5, 13, "K6-2+/III+", "Sharptooth"},
    
    /* Athlon Family */
    {6, 1, "Athlon", "Argon"},
    {6, 2, "Athlon", "Pluto"},
    {6, 3, "Duron", "Spitfire"},
    {6, 4, "Athlon", "Thunderbird"},
    {6, 6, "Athlon XP", "Palomino"},
    {6, 7, "Duron", "Morgan"},
    {6, 8, "Athlon XP", "Thoroughbred"},
    {6, 10, "Athlon XP", "Barton"},
    
    /* Athlon 64 Family */
    {15, 4, "Athlon 64", "Clawhammer"},
    {15, 5, "Athlon 64", "Sledgehammer"},
    {15, 7, "Athlon 64", "Clawhammer"},
    {15, 8, "Athlon 64", "Newcastle"},
    {15, 11, "Athlon 64", "Newcastle"},
    {15, 12, "Athlon 64", "Winchester"},
    {15, 15, "Athlon 64", "Winchester"},
    {15, 27, "Athlon 64", "San Diego"},
    {15, 31, "Athlon 64", "San Diego"},
    {15, 35, "Athlon 64 X2", "Manchester"},
    {15, 43, "Athlon 64 X2", "Windsor"},
    {15, 72, "Turion 64", "Lancaster"},
    {15, 75, "Turion 64", "Lancaster"},
    {0, 0, NULL, NULL}
};

/* Cyrix CPU models with codenames */
static const cpu_model_entry_t cyrix_cpus[] = {
    {4, 4, "5x86", "M1sc"},
    {5, 2, "6x86", "M1"},
    {5, 4, "6x86MX/MII", "M2"},
    {6, 0, "MII", "Cayenne"},
    {6, 5, "VIA Cyrix III", "Joshua"},
    {0, 0, NULL, NULL}
};

/* VIA CPU models with codenames */
static const cpu_model_entry_t via_cpus[] = {
    {5, 4, "WinChip C6", "C6"},
    {5, 8, "WinChip 2", "C6+"},
    {5, 9, "WinChip 3", "C6++"},
    {6, 6, "C3", "Samuel"},
    {6, 7, "C3", "Samuel 2/Ezra"},
    {6, 8, "C3", "Ezra-T"},
    {6, 9, "C3", "Nehemiah"},
    {6, 10, "C7", "Esther"},
    {6, 13, "C7-M", "Esther"},
    {6, 15, "Nano", "Isaiah"},
    {0, 0, NULL, NULL}
};

/* Transmeta CPU models with codenames */
static const cpu_model_entry_t transmeta_cpus[] = {
    {5, 4, "Crusoe", "TM3x00"},
    {5, 7, "Crusoe", "TM5x00"},
    {15, 2, "Efficeon", "TM8x00"},
    {0, 0, NULL, NULL}
};

/**
 * @brief Detect Current Privilege Level (CPL)
 * @return Current CPL (0-3), where 0 = ring 0 (kernel)
 *
 * GPT-5 Critical: WBINVD requires CPL 0, not just real mode
 */
static uint8_t detect_current_cpl(void) {
    uint16_t cs_selector;

    _asm {
        mov ax, cs
        mov cs_selector, ax
    }

    /* CPL is in the bottom 2 bits of CS selector */
    return (uint8_t)(cs_selector & 3);
}

/**
 * @brief Detect V86 mode by checking EFLAGS VM bit
 * @return true if running in Virtual 8086 mode
 *
 * GPT-5 Critical: V86 mode prevents privileged instructions on 486
 */
static bool detect_v86_mode(void) {
    uint32_t eflags;

    /* Only meaningful on 386+ */
    if (g_cpu_info.cpu_type < CPU_TYPE_80386) {
        return false;
    }

    _asm {
        .386
        pushfd              ; Push EFLAGS onto stack (32-bit)
        pop eax             ; Pop into EAX
        mov eflags, eax     ; Store in variable
    }

    /* VM bit is bit 17 (0x20000) */
    return (eflags & 0x20000) != 0;
}

/* Global CPU info structure */
cpu_info_t g_cpu_info = {0};

/**
 * @brief Identify specific CPU model from family/model/vendor
 * @param info CPU info structure to update
 */
static void identify_cpu_model(cpu_info_t* info) {
    const cpu_model_entry_t* table = NULL;
    const cpu_model_entry_t* entry;
    
    /* Select appropriate table based on vendor */
    switch (info->cpu_vendor) {
        case VENDOR_INTEL:
            table = intel_cpus;
            break;
        case VENDOR_AMD:
            table = amd_cpus;
            break;
        case VENDOR_CYRIX:
            table = cyrix_cpus;
            break;
        case VENDOR_VIA:
            table = via_cpus;
            break;
        case VENDOR_TRANSMETA:
            table = transmeta_cpus;
            break;
        default:
            /* Unknown vendor */
            snprintf(info->cpu_name, sizeof(info->cpu_name), 
                     "Unknown CPU");
            snprintf(info->cpu_codename, sizeof(info->cpu_codename),
                     "Unknown");
            return;
    }
    
    /* Search for matching family/model */
    if (table) {
        for (entry = table; entry->name != NULL; entry++) {
            if (entry->family == info->cpu_family && 
                entry->model == info->cpu_model) {
                strncpy(info->cpu_name, entry->name, 
                        sizeof(info->cpu_name) - 1);
                strncpy(info->cpu_codename, entry->codename,
                        sizeof(info->cpu_codename) - 1);
                return;
            }
        }
    }
    
    /* No match found - use generic description */
    snprintf(info->cpu_name, sizeof(info->cpu_name),
             "Family %d Model %d", 
             info->cpu_family, info->cpu_model);
    snprintf(info->cpu_codename, sizeof(info->cpu_codename),
             "Unknown");
}

/**
 * @brief Convert CPU type to string
 * @param type CPU type identifier
 * @return CPU type name string
 */
const char* cpu_type_to_string(cpu_type_t type) {
    if (type <= CPU_TYPE_CPUID_CAPABLE) {
        return cpu_names[type];
    }
    return "Unknown";
}

/**
 * @brief Main CPU detection function
 * @return SUCCESS or error code
 *
 * This function is called once during initialization and uses Assembly
 * routines to gather CPU information for SMC patching.
 */
int cpu_detect_init(void) {
    char far* vendor_str;
    int i;
    platform_probe_result_t platform;

    LOG_DEBUG("Starting CPU detection...");

    /* Clear CPU info structure */
    memset(&g_cpu_info, 0, sizeof(cpu_info_t));

    /* Get CPU type from Assembly module - TRUST COMPLETELY */
    g_cpu_info.cpu_type = (cpu_type_t)asm_detect_cpu_type();
    
    /* Check minimum requirement - now accepts 8086/8088 */
    if (g_cpu_info.cpu_type == CPU_TYPE_UNKNOWN) {
        LOG_ERROR("CPU detection failed: unknown CPU type");
        return ERROR_CPU_UNKNOWN;
    }

    /* Log 8086/8088 detection with appropriate warnings */
    if (g_cpu_info.cpu_type < CPU_TYPE_80286) {
        LOG_INFO("8086/8088 CPU detected - using simplified boot path");
        LOG_INFO("Features: 3C509B PIO only, no XMS/VDS/bus-mastering");
    }

    /* For pre-CPUID CPUs, use the basic type name */
    if (g_cpu_info.cpu_type < CPU_TYPE_CPUID_CAPABLE) {
        strncpy(g_cpu_info.cpu_name,
                cpu_type_to_string(g_cpu_info.cpu_type),
                sizeof(g_cpu_info.cpu_name) - 1);
        strncpy(g_cpu_info.cpu_codename, "Legacy", 
                sizeof(g_cpu_info.cpu_codename) - 1);
    }

    /* Get CPU features from Assembly module - full 32-bit flags */
    g_cpu_info.features = asm_get_cpu_flags();

    /* Set boolean flags based on features */
    g_cpu_info.has_cpuid = (g_cpu_info.features & CPU_FEATURE_CPUID) != 0;
    g_cpu_info.has_clflush = (g_cpu_info.features & CPU_FEATURE_CLFLUSH) != 0;
    g_cpu_info.has_wbinvd = (g_cpu_info.features & CPU_FEATURE_WBINVD) != 0;
    /* V86 detection removed - VDS presence is better indicator */
    g_cpu_info.in_v86_mode = false;  /* Deprecated field */
    
    /* GPT-5 Critical: Detect Current Privilege Level for WBINVD safety */
    g_cpu_info.current_cpl = detect_current_cpl();
    g_cpu_info.in_ring0 = (g_cpu_info.current_cpl == 0);
    
    /* V86 mode detection - check EFLAGS VM bit */
    g_cpu_info.in_v86_mode = detect_v86_mode();
    
    /* Determine if WBINVD can be safely used */
    g_cpu_info.can_wbinvd = (g_cpu_info.cpu_family >= 4) &&          /* 486+ */
                            g_cpu_info.in_ring0 &&                   /* CPL 0 */ 
                            !g_cpu_info.in_v86_mode &&               /* Not V86 */
                            g_cpu_info.has_wbinvd;                    /* Instruction available */

    /* Set address bits based on CPU type */
    switch (g_cpu_info.cpu_type) {
    case CPU_TYPE_8086:
    case CPU_TYPE_80186:
        g_cpu_info.addr_bits = 20;  /* 1MB address space */
        break;
    case CPU_TYPE_80286:
        g_cpu_info.addr_bits = 24;  /* 16MB address space */
        break;
    case CPU_TYPE_80386:
    case CPU_TYPE_80486:
    case CPU_TYPE_CPUID_CAPABLE:
        g_cpu_info.addr_bits = 32;  /* 4GB address space */
        break;
    default:
        g_cpu_info.addr_bits = 20;
        break;
    }

    /* Get CPU vendor from Assembly module */
    g_cpu_info.cpu_vendor = (cpu_vendor_t)asm_get_cpu_vendor();
    
    /* Get vendor string if CPUID is available */
    if (g_cpu_info.has_cpuid) {
        vendor_str = asm_get_cpu_vendor_string();
        if (vendor_str) {
            /* Copy vendor string from far pointer */
            for (i = 0; i < 12 && vendor_str[i]; i++) {
                g_cpu_info.vendor_string[i] = vendor_str[i];
            }
            g_cpu_info.vendor_string[i] = '\0';
        }
        
        /* Get CPU family/model/stepping if available */
        g_cpu_info.cpu_family = asm_get_cpu_family();
        g_cpu_info.cpu_model = asm_get_cpu_model();
        g_cpu_info.stepping = asm_get_cpu_stepping();
        
        /* Map vendor string to vendor enum if needed */
        if (g_cpu_info.cpu_vendor == VENDOR_UNKNOWN && g_cpu_info.vendor_string[0]) {
            if (strncmp(g_cpu_info.vendor_string, "CentaurHauls", 12) == 0) {
                g_cpu_info.cpu_vendor = VENDOR_VIA;
            }
        }
        
        /* Identify specific CPU model and codename for CPUID-capable CPUs */
        if (g_cpu_info.cpu_type == CPU_TYPE_CPUID_CAPABLE) {
            identify_cpu_model(&g_cpu_info);
        }
    }
    
    /* Check for Cyrix extensions */
    g_cpu_info.has_cyrix_ext = asm_has_cyrix_extensions() != 0;
    
    /* Get cache information if CPU has cache */
    if (g_cpu_info.features & CPU_FEATURE_CACHE) {
        asm_get_cache_info(&g_cpu_info.l1_data_size, 
                          &g_cpu_info.l1_code_size,
                          &g_cpu_info.l2_size,
                          &g_cpu_info.cache_line_size);
    }

    /* Detect CPU speed (rough estimate based on timing loop) */
    detect_cpu_speed(&g_cpu_info);
    
    /* Check if running under hypervisor */
    g_cpu_info.is_hypervisor = asm_is_hypervisor() != 0;

    /* Log detection results with new format */
    if (g_cpu_info.cpu_type == CPU_TYPE_CPUID_CAPABLE && g_cpu_info.cpu_codename[0]) {
        LOG_INFO("CPU: %s %s \"%s\"",
                 vendor_names[g_cpu_info.cpu_vendor <= VENDOR_VIA ? 
                             g_cpu_info.cpu_vendor : 8],
                 g_cpu_info.cpu_name,
                 g_cpu_info.cpu_codename);
        LOG_INFO("Family: %d · Model: %d · Stepping: %d",
                 g_cpu_info.cpu_family,
                 g_cpu_info.cpu_model,
                 g_cpu_info.stepping);
    } else {
        LOG_INFO("CPU: %s %s",
                 vendor_names[g_cpu_info.cpu_vendor <= VENDOR_VIA ? 
                             g_cpu_info.cpu_vendor : 8],
                 g_cpu_info.cpu_name);
    }
    
    LOG_INFO("Speed: %d MHz (Confidence: %d%%)", 
             g_cpu_info.cpu_mhz, g_cpu_info.speed_confidence);
    
    if (g_cpu_info.vendor_string[0]) {
        LOG_DEBUG("Vendor ID: %s", g_cpu_info.vendor_string);
    }
    
    LOG_DEBUG("Features: 0x%08lX", g_cpu_info.features);
    LOG_DEBUG("Address bits: %d", g_cpu_info.addr_bits);
    
    /* Check and log TSC characteristics if TSC is available */
    if (g_cpu_info.features & CPU_FEATURE_MSR) {
        uint8_t invariant = asm_has_invariant_tsc();
        if (invariant) {
            LOG_INFO("TSC is invariant (power management safe)");
        } else if (g_cpu_info.features & CPU_FEATURE_MSR) {
            LOG_WARNING("TSC may vary with power states (non-invariant)");
        }
    }
    
    /* Log cache information if available */
    if (g_cpu_info.l1_data_size || g_cpu_info.l1_code_size || g_cpu_info.l2_size) {
        LOG_INFO("Cache: L1=%dKB (%dD+%dI) · L2=%dKB · Line=%dB",
                 g_cpu_info.l1_data_size + g_cpu_info.l1_code_size,
                 g_cpu_info.l1_data_size, 
                 g_cpu_info.l1_code_size,
                 g_cpu_info.l2_size, 
                 g_cpu_info.cache_line_size);
    }
    
    /* Log if running under hypervisor */
    if (g_cpu_info.is_hypervisor) {
        LOG_WARNING("Running under hypervisor/virtual machine");
    }

    /* Log special cases */
    if (g_cpu_info.cpu_vendor == VENDOR_INTEL && g_cpu_info.cpu_type == CPU_TYPE_80486) {
        if (g_cpu_info.has_cpuid) {
            LOG_INFO("Intel 486 with CPUID support (DX4 or SL Enhanced model)");
        } else {
            LOG_INFO("Early Intel 486 without CPUID (pre-1992 model)");
        }
    }
    
    if (g_cpu_info.cpu_vendor == VENDOR_CYRIX) {
        if (g_cpu_info.cpu_type == CPU_TYPE_CPUID_CAPABLE) {
            LOG_INFO("Cyrix 6x86 detected - may require CCR4 register manipulation");
        }
        if (g_cpu_info.has_cyrix_ext) {
            LOG_INFO("Cyrix-specific extensions (DIR0) detected");
        }
    }
    
    if (g_cpu_info.cpu_vendor == VENDOR_NEXGEN) {
        LOG_WARNING("NexGen Nx586 detected - CPUID without ID flag support");
    }
    
    /* Platform detection now handled by platform_probe module */
    platform = platform_detect();
    
    LOG_INFO("Platform environment: %s", platform.environment_desc);
    LOG_INFO("DMA policy: %s", platform_get_policy_desc(platform.recommended_policy));
    
    if (!platform.safe_for_busmaster) {
        LOG_WARNING("Bus-master DMA disabled due to unsafe environment");
        LOG_WARNING("3C515-TX will not be supported, only 3C509B (PIO)");
    }

    #ifdef PRODUCTION
    /* In production, compact format */
    if (g_cpu_info.cpu_type == CPU_TYPE_CPUID_CAPABLE && g_cpu_info.cpu_codename[0]) {
        printf("%s %s \"%s\" %dMHz\n",
               vendor_names[g_cpu_info.cpu_vendor <= VENDOR_VIA ? 
                           g_cpu_info.cpu_vendor : 8],
               g_cpu_info.cpu_name,
               g_cpu_info.cpu_codename,
               g_cpu_info.cpu_mhz);
    } else {
        printf("%s %s %dMHz\n",
               vendor_names[g_cpu_info.cpu_vendor <= VENDOR_VIA ? 
                           g_cpu_info.cpu_vendor : 8],
               g_cpu_info.cpu_name,
               g_cpu_info.cpu_mhz);
    }
    #endif

    return SUCCESS;
}

/**
 * @brief Get CPU speed from Assembly module
 * @param info CPU info structure to update
 *
 * Calls Assembly module which uses PIT or RDTSC for accurate timing
 */
void detect_cpu_speed(cpu_info_t* info) {
    /* Get speed from Assembly module - it handles all detection */
    info->cpu_mhz = asm_get_cpu_speed();
    info->speed_confidence = asm_get_speed_confidence();
    
    /* If confidence is very low or speed is invalid, use fallback */
    if (info->speed_confidence < 25 || info->cpu_mhz == 0) {
        /* Apply fallback based on CPU type if Assembly failed */
        switch (info->cpu_type) {
        case CPU_TYPE_8086:
            info->cpu_mhz = 5;  /* Typical 4.77-8 MHz */
            break;
        case CPU_TYPE_80186:
            info->cpu_mhz = 8;  /* Typical 6-12 MHz */
            break;
        case CPU_TYPE_80286:
            info->cpu_mhz = 12; /* Typical 6-20 MHz */
            break;
        case CPU_TYPE_80386:
            info->cpu_mhz = 33; /* Typical 16-40 MHz */
            break;
        case CPU_TYPE_80486:
            info->cpu_mhz = 66; /* Typical 25-100 MHz */
            break;
        case CPU_TYPE_CPUID_CAPABLE:
            info->cpu_mhz = 133; /* Typical Pentium */
            break;
        default:
            info->cpu_mhz = 0;  /* Unknown */
            break;
        }
    }
}

/**
 * @brief Get detected CPU information
 * @return Pointer to global CPU info structure
 */
const cpu_info_t* cpu_get_info(void) {
    return &g_cpu_info;
}

/**
 * @brief Get CPU family ID if CPUID is available
 * @return CPU family (0 if no CPUID)
 */
uint8_t cpu_get_family(void) {
    return g_cpu_info.cpu_family;
}

/**
 * @brief Check if CPU supports 32-bit operations
 * @return 1 if 32-bit supported, 0 otherwise
 */
int cpu_supports_32bit(void) {
    return (g_cpu_info.features & CPU_FEATURE_32BIT) ? 1 : 0;
}

/**
 * @brief Get CPU optimization level for runtime code path selection
 * @return CPU_OPT_* value indicating which instruction sets are available
 *
 * Returns the appropriate optimization level based on CPU type:
 * - CPU_OPT_8086: 8086/8088 baseline (no 186+ instructions)
 * - CPU_OPT_16BIT: 186/286 (PUSHA, INS/OUTS, shift immediate)
 * - CPU_OPT_32BIT: 386+ (32-bit registers available)
 * - CPU_OPT_486_ENHANCED: 486+ (BSWAP, CMPXCHG)
 * - CPU_OPT_PENTIUM: Pentium+ (pipeline optimizations)
 */
uint8_t cpu_get_optimization_level(void) {
    switch (g_cpu_info.cpu_type) {
    case CPU_TYPE_8086:
        return CPU_OPT_8086;
    case CPU_TYPE_80186:
    case CPU_TYPE_80286:
        return CPU_OPT_16BIT;
    case CPU_TYPE_80386:
        return CPU_OPT_32BIT;
    case CPU_TYPE_80486:
        return CPU_OPT_486_ENHANCED;
    case CPU_TYPE_CPUID_CAPABLE:
        /* Pentium or higher */
        return CPU_OPT_PENTIUM;
    default:
        /* Unknown - use safest baseline */
        return CPU_OPT_8086;
    }
}

/**
 * @brief Check if running on 8086/8088 processor
 * @return 1 if 8086/8088, 0 otherwise
 *
 * Used for conditional boot path selection - 8086 systems need
 * simplified boot (no V86/VDS/XMS) and 8086-safe instruction paths.
 */
int cpu_is_8086(void) {
    return (g_cpu_info.cpu_type == CPU_TYPE_8086) ? 1 : 0;
}

/* Restore default code segment */
#pragma code_seg()