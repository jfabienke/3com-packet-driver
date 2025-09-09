/* Platform Detection and DMA Policy Module
 * 
 * Simplified detection strategy based on GPT-5 recommendations:
 * - VDS presence is the primary policy gate (no V86 detection needed)
 * - Conservative policy: if virtualizers present without VDS, forbid DMA
 * - Fallback strategies for different environments
 */

#include <dos.h>
#include <string.h>
#include <stdio.h>
#include "../include/platform_probe.h"
#include "../include/vds.h"
#include "../include/logging.h"
#include "../include/xms_detect.h"  /* For XMS detection */

/* Global Platform State */
platform_probe_result_t g_platform = {0};
dma_policy_t g_dma_policy = DMA_POLICY_DIRECT;

/* Static initialization flag */
static bool platform_initialized = false;

/**
 * @brief Perform comprehensive platform detection
 * @return Platform detection results
 */
platform_probe_result_t platform_detect(void) {
    platform_probe_result_t result = {0};
    
    log_info("Starting platform detection...");
    
    /* Primary detection: VDS services */
    result.vds_available = detect_vds_services();
    log_info("VDS services: %s", result.vds_available ? "PRESENT" : "NOT PRESENT");
    
    /* Get DOS version */
    result.dos_version = get_dos_version();
    log_info("DOS version: %d.%d", 
            (result.dos_version >> 8) & 0xFF, 
            result.dos_version & 0xFF);
    
    /* If VDS present, we can safely use DMA */
    if (result.vds_available) {
        result.recommended_policy = DMA_POLICY_COMMONBUF;
        result.safe_for_busmaster = true;
        result.requires_vds = true;
        result.pio_fallback_ok = true;
        strcpy(result.environment_desc, "V86/Protected mode with VDS");
        
        log_info("VDS detected - DMA operations will use VDS services");
    } else {
        /* GPT-5 Enhanced: Refined VDS-absent detection matrix */
        result.vcpi_present = detect_vcpi_services();
        result.windows_enhanced = detect_windows_enhanced_mode();
        result.emm386_detected = detect_emm386_manager();
        result.qemm_detected = detect_qemm_manager();
        
        /* Check for HIMEM-only setup (XMS without paging/V86) */
        bool xms_present = (xms_detect_and_init() == 0);  /* Returns 0 on success */
        bool himem_only = xms_present && 
                         !result.vcpi_present && 
                         !result.windows_enhanced && 
                         !result.emm386_detected && 
                         !result.qemm_detected;
        
        log_info("Extended detection: VCPI=%s WinEnh=%s EMM386=%s QEMM=%s HIMEM-only=%s",
                result.vcpi_present ? "yes" : "no",
                result.windows_enhanced ? "yes" : "no", 
                result.emm386_detected ? "yes" : "no",
                result.qemm_detected ? "yes" : "no",
                himem_only ? "yes" : "no");
        
        /* GPT-5 Policy Matrix: More precise detection */
        bool has_paging_manager = result.emm386_detected || result.qemm_detected || 
                                 result.windows_enhanced || result.vcpi_present;
        
        if (has_paging_manager) {
            /* V86/paging mode without VDS = FORBID (unsafe for DMA) */
            result.recommended_policy = DMA_POLICY_FORBID;
            result.safe_for_busmaster = false;
            result.requires_vds = false;
            result.pio_fallback_ok = true;
            strcpy(result.environment_desc, "V86/Paging mode without VDS - DMA unsafe");
            
            log_warning("Paging manager detected without VDS - bus-master DMA FORBIDDEN");
            log_warning("Only PIO operations allowed (3C509B supported, 3C515-TX disabled)");
        } else {
            /* No paging manager detected (real mode or HIMEM-only) = DIRECT DMA OK */
            result.recommended_policy = DMA_POLICY_DIRECT;
            result.safe_for_busmaster = true;
            result.requires_vds = false;
            result.pio_fallback_ok = true;
            
            if (himem_only) {
                strcpy(result.environment_desc, "HIMEM-only (no V86) - direct DMA safe");
                log_info("HIMEM-only setup detected - direct DMA operations allowed");
            } else {
                strcpy(result.environment_desc, "Real mode - direct DMA allowed");
                log_info("Real mode detected - direct DMA operations allowed");
            }
        }
    }
    
    log_info("Platform detection complete:");
    log_info("  Environment: %s", result.environment_desc);
    log_info("  Policy: %s", platform_get_policy_desc(result.recommended_policy));
    log_info("  Bus-master safe: %s", result.safe_for_busmaster ? "YES" : "NO");
    
    return result;
}

/**
 * @brief Initialize platform detection and set global policy
 * @return 0 on success, negative on error
 */
int platform_init(void) {
    if (platform_initialized) {
        return PLATFORM_SUCCESS;
    }
    
    /* Perform detection */
    g_platform = platform_detect();
    g_dma_policy = g_platform.recommended_policy;
    
    /* Log final policy decision */
    log_info("Global DMA policy set to: %s", 
            platform_get_policy_desc(g_dma_policy));
    
    platform_initialized = true;
    return PLATFORM_SUCCESS;
}

/**
 * @brief Get current DMA policy
 * @return Current DMA policy
 */
dma_policy_t platform_get_dma_policy(void) {
    if (!platform_initialized) {
        platform_init();
    }
    return g_dma_policy;
}

/**
 * @brief Check if bus-master DMA is allowed under current policy
 * @return true if bus-master DMA allowed, false otherwise
 */
bool platform_allow_busmaster_dma(void) {
    if (!platform_initialized) {
        platform_init();
    }
    
    switch (g_dma_policy) {
        case DMA_POLICY_DIRECT:
        case DMA_POLICY_COMMONBUF:
            return true;
        case DMA_POLICY_FORBID:
        default:
            return false;
    }
}

/**
 * @brief Check if PIO fallback is available for given NIC type
 * @param nic_type NIC type identifier
 * @return true if PIO fallback available, false otherwise
 */
bool platform_has_pio_fallback(int nic_type) {
    switch (nic_type) {
        case NIC_TYPE_3C509B:
            return true;    /* 3C509B is PIO-only anyway */
        case NIC_TYPE_3C515_TX:
            return false;   /* 3C515-TX requires DMA, no PIO mode */
        default:
            return false;
    }
}

/* Specific Detection Functions */

/**
 * @brief Detect VDS (Virtual DMA Services) availability
 * @return true if VDS present, false otherwise
 */
bool detect_vds_services(void) {
    /* Use the VDS module's detection function */
    return vds_detect();
}

/**
 * @brief Detect VCPI (Virtual Control Program Interface) presence
 * @return true if VCPI present, false otherwise
 */
bool detect_vcpi_services(void) {
    union REGS regs;
    
    /* INT 67h, AX=DE00h - VCPI Installation Check */
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0xDE00;
    int86(0x67, &regs, &regs);
    
    /* VCPI present if AH=00h */
    return (regs.h.ah == 0x00);
}

/**
 * @brief Detect Windows Enhanced mode
 * @return true if Windows Enhanced mode active, false otherwise
 */
bool detect_windows_enhanced_mode(void) {
    union REGS regs;
    
    /* INT 2Fh, AX=160Ah - Windows Enhanced Mode Detection */
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0x160A;
    int86(0x2F, &regs, &regs);
    
    /* Enhanced mode if AX != 160Ah (changed) and BX != 0000h */
    if (regs.x.ax != 0x160A && regs.x.bx != 0x0000) {
        return true;
    }
    
    /* Additional check: INT 2Fh, AX=1600h - Windows Version */
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0x1600;
    int86(0x2F, &regs, &regs);
    
    /* Enhanced mode if AL >= 03h (Windows 3.x in enhanced mode) */
    return (regs.h.al >= 0x03 && regs.h.al != 0x80 && regs.h.al != 0xFF);
}

/**
 * @brief Detect EMM386 or similar memory manager
 * @return true if EMM386 detected, false otherwise
 */
bool detect_emm386_manager(void) {
    union REGS regs;
    
    /* INT 2Fh, AX=4A11h, BX=0000h - EMM386 Multiplex */
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0x4A11;
    regs.x.bx = 0x0000;
    int86(0x2F, &regs, &regs);
    
    /* EMM386 present if AL=FFh */
    if (regs.h.al == 0xFF) {
        return true;
    }
    
    /* Check for EMS driver (which might be EMM386) */
    /* INT 67h, AH=40h - EMS Status */
    memset(&regs, 0, sizeof(regs));
    regs.h.ah = 0x40;
    int86(0x67, &regs, &regs);
    
    /* EMS present if AH=00h */
    return (regs.h.ah == 0x00);
}

/**
 * @brief Detect QEMM memory manager
 * @return true if QEMM detected, false otherwise
 */
bool detect_qemm_manager(void) {
    union REGS regs;
    
    /* INT 2Fh, AX=D201h, BX=5145h ('QE'), CX=4D4Dh ('MM') - QEMM Check */
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0xD201;
    regs.x.bx = 0x5145;  /* 'QE' */
    regs.x.cx = 0x4D4D;  /* 'MM' */
    int86(0x2F, &regs, &regs);
    
    /* QEMM present if registers changed in expected way */
    return (regs.x.ax != 0xD201 || regs.x.bx != 0x5145 || regs.x.cx != 0x4D4D);
}

/**
 * @brief Get DOS version
 * @return DOS version in format (major << 8) | minor
 */
uint16_t get_dos_version(void) {
    union REGS regs;
    
    /* INT 21h, AH=30h - Get DOS Version */
    memset(&regs, 0, sizeof(regs));
    regs.h.ah = 0x30;
    int86(0x21, &regs, &regs);
    
    /* Return version as (major << 8) | minor */
    return ((uint16_t)regs.h.al << 8) | regs.h.ah;
}

/* Policy Helper Functions */

/**
 * @brief Get human-readable policy description
 * @param policy DMA policy
 * @return Policy description string
 */
const char *platform_get_policy_desc(dma_policy_t policy) {
    switch (policy) {
        case DMA_POLICY_DIRECT:
            return "DIRECT (real mode DMA)";
        case DMA_POLICY_COMMONBUF:
            return "VDS (common buffer DMA)";
        case DMA_POLICY_FORBID:
            return "FORBID (no DMA allowed)";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Get platform environment description
 * @param result Platform probe result
 * @return Environment description string
 */
const char *platform_get_environment_desc(const platform_probe_result_t *result) {
    if (!result) {
        return "Unknown environment";
    }
    return result->environment_desc;
}

/**
 * @brief Validate DMA policy for specific NIC type
 * @param nic_type NIC type identifier
 * @param policy Proposed DMA policy
 * @return true if policy is safe for NIC type, false otherwise
 */
bool platform_validate_policy_for_nic(int nic_type, dma_policy_t policy) {
    switch (nic_type) {
        case NIC_TYPE_3C509B:
            /* 3C509B uses PIO only - any policy is safe */
            return true;
            
        case NIC_TYPE_3C515_TX:
            /* 3C515-TX requires DMA - forbid policy means no support */
            return (policy != DMA_POLICY_FORBID);
            
        default:
            /* Unknown NIC type - be conservative */
            return (policy == DMA_POLICY_DIRECT || policy == DMA_POLICY_COMMONBUF);
    }
}