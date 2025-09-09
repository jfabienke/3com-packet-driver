/**
 * @file cpu_database.c
 * @brief CPU model recognition database for enhanced detection
 *
 * Contains detailed CPU model information including Intel 486 S-spec codes,
 * vendor-specific quirks, and CPUID availability mapping based on the
 * historical implementation timeline.
 */

#include <stdint.h>
#include <string.h>
#include "../include/cpu_detect.h"
#include "../include/logging.h"

/**
 * Intel 486 S-spec database
 * Maps S-spec codes to CPU models and CPUID support
 */
typedef struct {
    const char* s_spec;
    const char* model_name;
    int has_cpuid;
    int year;  /* Copyright year */
} intel_486_spec_t;

/* Intel 486 models with CPUID support (1992+ copyright) */
static const intel_486_spec_t intel_486_cpuid_models[] = {
    /* 486DX4 - All models have CPUID */
    { "SK047", "486DX4-75", 1, 1994 },
    { "SK048", "486DX4-75", 1, 1994 },
    { "SK049", "486DX4-100", 1, 1994 },
    { "SK050", "486DX4-100", 1, 1994 },
    { "SK051", "486DX4-100", 1, 1994 },
    { "SK052", "486DX4-100 OverDrive", 1, 1994 },
    { "SK096", "486DX4-100", 1, 1995 },
    
    /* SL Enhanced 486DX2 with CPUID */
    { "SX807", "486DX2-66 SL Enhanced", 1, 1992 },
    { "SX808", "486DX2-66 SL Enhanced", 1, 1992 },
    { "SX911", "486DX2-50 SL Enhanced", 1, 1993 },
    { "SX912", "486DX2-66 SL Enhanced", 1, 1993 },
    { "SX955", "486DX2-66 SL Enhanced", 1, 1993 },
    
    /* SL Enhanced 486DX with CPUID */
    { "SX679", "486DX-33 SL Enhanced", 1, 1992 },
    { "SX680", "486DX-33 SL Enhanced", 1, 1992 },
    { "SX729", "486DX-50 SL Enhanced", 1, 1992 },
    { "SX797", "486DX-33 SL Enhanced", 1, 1992 },
    { "SX798", "486DX-50 SL Enhanced", 1, 1992 },
    
    /* SL Enhanced 486SX with CPUID */
    { "SX823", "486SX-25 SL Enhanced", 1, 1992 },
    { "SX824", "486SX-33 SL Enhanced", 1, 1992 },
    { "SX903", "486SX-25 SL Enhanced", 1, 1993 },
    { "SX904", "486SX-33 SL Enhanced", 1, 1993 },
    
    { NULL, NULL, 0, 0 }  /* Terminator */
};

/* Intel 486 models WITHOUT CPUID (pre-1992 or 486SL) */
static const intel_486_spec_t intel_486_no_cpuid_models[] = {
    /* Original 486DX (1989-1991) */
    { "SX316", "486DX-25", 0, 1989 },
    { "SX328", "486DX-33", 0, 1989 },
    { "SX354", "486DX-33", 0, 1989 },
    { "SX366", "486DX-25", 0, 1989 },
    { "SX367", "486DX-33", 0, 1989 },
    { "SX368", "486DX-50", 0, 1991 },
    { "SX408", "486DX-50", 0, 1991 },
    
    /* Original 486SX (1991) */
    { "SX406", "486SX-20", 0, 1991 },
    { "SX407", "486SX-20", 0, 1991 },
    { "SX486", "486SX-25", 0, 1991 },
    { "SX487", "486SX-33", 0, 1991 },
    
    /* Original 486DX2 (1992 but early steppings) */
    { "SX626", "486DX2-50", 0, 1992 },
    { "SX627", "486DX2-66", 0, 1992 },
    { "SX628", "486DX2-50", 0, 1992 },
    { "SX629", "486DX2-66", 0, 1992 },
    
    /* 486SL - Never got CPUID despite 1992+ production */
    { "SX735", "486SL-25", 0, 1992 },
    { "SX736", "486SL-33", 0, 1992 },
    { "SX787", "486SL-25", 0, 1992 },
    { "SX788", "486SL-33", 0, 1992 },
    { "SX826", "486SL-50", 0, 1993 },
    { "SX827", "486SL-60", 0, 1993 },
    
    { NULL, NULL, 0, 0 }  /* Terminator */
};

/**
 * @brief Check if Intel 486 has CPUID based on S-spec
 * @param s_spec S-spec code to check
 * @return 1 if CPUID supported, 0 if not, -1 if unknown
 */
int intel_486_has_cpuid(const char* s_spec) {
    int i;
    
    if (!s_spec || strlen(s_spec) < 5) {
        return -1;
    }
    
    /* Check models with CPUID */
    for (i = 0; intel_486_cpuid_models[i].s_spec != NULL; i++) {
        if (strcmpi(s_spec, intel_486_cpuid_models[i].s_spec) == 0) {
            LOG_DEBUG("Intel 486 S-spec %s: %s (CPUID supported)", 
                     s_spec, intel_486_cpuid_models[i].model_name);
            return 1;
        }
    }
    
    /* Check models without CPUID */
    for (i = 0; intel_486_no_cpuid_models[i].s_spec != NULL; i++) {
        if (strcmpi(s_spec, intel_486_no_cpuid_models[i].s_spec) == 0) {
            LOG_DEBUG("Intel 486 S-spec %s: %s (no CPUID)", 
                     s_spec, intel_486_no_cpuid_models[i].model_name);
            return 0;
        }
    }
    
    /* Unknown S-spec - check prefix patterns */
    if (strncmpi(s_spec, "SK", 2) == 0) {
        /* SK prefix = 486DX4, all have CPUID */
        LOG_DEBUG("Intel 486DX4 detected (SK prefix) - CPUID supported");
        return 1;
    }
    
    if (strncmpi(s_spec, "SX3", 3) == 0 && s_spec[3] >= '0' && s_spec[3] <= '6') {
        /* SX3xx (300-369) = early 486DX without CPUID */
        LOG_DEBUG("Early Intel 486DX detected (SX3xx) - no CPUID");
        return 0;
    }
    
    if (strncmpi(s_spec, "SX4", 3) == 0 && s_spec[3] >= '0' && s_spec[3] <= '8') {
        /* SX4xx (400-489) = early 486SX/DX without CPUID */
        LOG_DEBUG("Early Intel 486 detected (SX4xx) - no CPUID");
        return 0;
    }
    
    return -1;  /* Unknown */
}

/**
 * @brief Get CPU model name from S-spec
 * @param s_spec S-spec code
 * @return Model name string or NULL if unknown
 */
const char* intel_486_get_model(const char* s_spec) {
    int i;
    
    if (!s_spec) return NULL;
    
    /* Check CPUID models */
    for (i = 0; intel_486_cpuid_models[i].s_spec != NULL; i++) {
        if (strcmpi(s_spec, intel_486_cpuid_models[i].s_spec) == 0) {
            return intel_486_cpuid_models[i].model_name;
        }
    }
    
    /* Check non-CPUID models */
    for (i = 0; intel_486_no_cpuid_models[i].s_spec != NULL; i++) {
        if (strcmpi(s_spec, intel_486_no_cpuid_models[i].s_spec) == 0) {
            return intel_486_no_cpuid_models[i].model_name;
        }
    }
    
    return NULL;
}

/**
 * AMD processor quirks and detection
 */

/**
 * @brief Check for AMD K5 PGE bug
 * @param model CPU model number
 * @return 1 if affected by PGE bug, 0 if not
 */
int amd_k5_has_pge_bug(uint8_t model) {
    /* AMD K5 Model 0 incorrectly reports PGE support in EDX bit 9 */
    /* Fixed in Model 1 and later */
    if (model == 0) {
        LOG_WARNING("AMD K5 Model 0 detected - PGE feature bit unreliable");
        return 1;
    }
    return 0;
}

/**
 * Cyrix processor quirks and detection
 */

/**
 * @brief Check if Cyrix 6x86 needs CPUID enable via CCR4
 * @param info CPU info structure
 * @return 1 if CCR4 manipulation needed, 0 if not
 */
int cyrix_needs_cpuid_enable(const cpu_info_t* info) {
    /* Cyrix 6x86 (1995) has CPUID but disabled by default */
    /* Must set bit 7 in CCR4 register after enabling extended CCRs */
    if (info->cpu_vendor == VENDOR_CYRIX && 
        info->cpu_type == CPU_TYPE_CPUID_CAPABLE) {
        
        /* Check if CPUID is already enabled */
        if (!(info->features & CPU_FEATURE_CPUID)) {
            LOG_INFO("Cyrix 6x86 detected - CPUID disabled by default");
            LOG_INFO("Enable via CCR4 bit 7 after enabling extended CCRs");
            return 1;
        }
    }
    return 0;
}

/**
 * NexGen processor quirks
 */

/**
 * @brief Check for NexGen Nx586 (CPUID without ID flag)
 * @return 1 if NexGen Nx586 detected, 0 if not
 * 
 * The NexGen Nx586 (1994) supports CPUID instruction but doesn't
 * implement the ID flag test, causing standard detection to fail.
 */
int nexgen_nx586_detected(void) {
    /* This would need to be called if:
     * 1. CPU detected as 386 (no ID flag toggle)
     * 2. But CPUID instruction doesn't cause UD fault
     * 3. Returns "NexGenDriven" vendor string
     */
    LOG_WARNING("NexGen Nx586 may be present - CPUID without ID flag");
    LOG_WARNING("Standard CPUID detection will fail on this processor");
    return 0;  /* Actual detection would be done in ASM */
}

/**
 * @brief Log CPU database information
 * @param info CPU info structure
 */
void log_cpu_database_info(const cpu_info_t* info) {
    if (info->cpu_vendor == VENDOR_INTEL && info->cpu_type == CPU_TYPE_80486) {
        LOG_INFO("Intel 486 processor database check:");
        LOG_INFO("  - Copyright dates 1989-1991: No CPUID");
        LOG_INFO("  - Copyright 1992+: Check S-spec for CPUID");
        LOG_INFO("  - All 486DX4: CPUID supported");
        LOG_INFO("  - All 486SL: No CPUID (despite 1992+ dates)");
        LOG_INFO("  - SL Enhanced suffix: Usually has CPUID");
    }
    
    if (info->cpu_vendor == VENDOR_AMD) {
        LOG_INFO("AMD processor notes:");
        LOG_INFO("  - Am486 series: No CPUID support");
        LOG_INFO("  - K5: First AMD CPU with CPUID (1995-1996)");
        LOG_INFO("  - Early K5 samples: 'AMD ISBETTER' vendor string");
        LOG_INFO("  - K5 Model 0: PGE feature bit unreliable");
    }
    
    if (info->cpu_vendor == VENDOR_CYRIX) {
        LOG_INFO("Cyrix processor notes:");
        LOG_INFO("  - 486SLC/DLC: No CPUID support");
        LOG_INFO("  - 5x86: No CPUID support");  
        LOG_INFO("  - 6x86: CPUID disabled by default (enable via CCR4)");
        LOG_INFO("  - 6x86 reports as 486 for compatibility");
        LOG_INFO("  - 6x86MX/MII: Improved CPUID implementation");
    }
}