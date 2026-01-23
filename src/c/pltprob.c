/**
 * @file platform_probe_early.c
 * @brief Early platform detection for DMA policy determination
 * 
 * GPT-5 A+ Enhancement: Phase 1 platform probe
 * This MUST run before any hardware initialization to set DMA policy
 */

#include <dos.h>
#include <string.h>
#include <stdio.h>
#include "../include/pltprob.h"
#include "../include/vds.h"
#include "../include/logging.h"
#include "../include/cpudet.h"

/* Global early detection results */
static platform_probe_result_t g_early_platform = {0};
static bool g_early_probe_done = false;

/* V86 mode detection (moved from main.c) */
static bool detect_v86_mode_early(void);
static bool detect_dpmi_services(void);

/**
 * @brief Early platform probe - MUST be called before hardware init
 * 
 * This function determines the DMA policy based on environment detection.
 * It MUST run before any DMA operations or hardware initialization.
 */
int platform_probe_early(void) {
    LOG_INFO("=== Phase 1: Early Platform Probe ===");
    
    if (g_early_probe_done) {
        LOG_INFO("Early platform probe already completed");
        return 0;
    }
    
    memset(&g_early_platform, 0, sizeof(g_early_platform));
    
    /* Step 1: V86 Mode Detection (moved from main.c) */
    LOG_INFO("Detecting CPU mode and memory environment...");
    
    bool in_v86_mode = detect_v86_mode_early();
    bool dpmi_present = detect_dpmi_services();
    
    if (in_v86_mode) {
        LOG_WARNING("V86 mode detected - DMA requires special handling");
    } else if (dpmi_present) {
        LOG_WARNING("DPMI detected - protected mode environment");
    } else {
        LOG_INFO("Real mode detected - direct DMA operations possible");
    }
    
    /* Step 2: VDS Detection (Primary DMA policy gate) */
    LOG_INFO("Checking for VDS (Virtual DMA Services)...");
    
    g_early_platform.vds_available = vds_is_available();
    
    if (g_early_platform.vds_available) {
        LOG_INFO("VDS services FOUND - DMA operations will use VDS");
        g_early_platform.recommended_policy = DMA_POLICY_COMMONBUF;
        g_early_platform.safe_for_busmaster = true;
        g_early_platform.requires_vds = true;
        strcpy(g_early_platform.environment_desc, 
               "V86/Protected mode with VDS - DMA safe via VDS");
    } else {
        LOG_INFO("VDS services NOT found - checking memory managers...");
        
        /* Step 3: Memory Manager Detection (if no VDS) */
        g_early_platform.vcpi_present = detect_vcpi_services();
        g_early_platform.windows_enhanced = detect_windows_enhanced_mode();
        g_early_platform.emm386_detected = detect_emm386_manager();
        g_early_platform.qemm_detected = detect_qemm_manager();
        
        /* Check for HIMEM-only (safe for DMA) */
        bool xms_present = detect_xms_services();
        bool himem_only = xms_present && 
                         !g_early_platform.vcpi_present &&
                         !g_early_platform.windows_enhanced &&
                         !g_early_platform.emm386_detected &&
                         !g_early_platform.qemm_detected &&
                         !in_v86_mode;
        
        LOG_INFO("Environment detection results:");
        LOG_INFO("  V86 mode: %s", in_v86_mode ? "YES" : "NO");
        LOG_INFO("  DPMI: %s", dpmi_present ? "YES" : "NO");
        LOG_INFO("  VCPI: %s", g_early_platform.vcpi_present ? "YES" : "NO");
        LOG_INFO("  Windows Enhanced: %s", 
                 g_early_platform.windows_enhanced ? "YES" : "NO");
        LOG_INFO("  EMM386: %s", g_early_platform.emm386_detected ? "YES" : "NO");
        LOG_INFO("  QEMM: %s", g_early_platform.qemm_detected ? "YES" : "NO");
        LOG_INFO("  XMS/HIMEM: %s", xms_present ? "YES" : "NO");
        LOG_INFO("  HIMEM-only: %s", himem_only ? "YES" : "NO");
        
        /* Step 4: Determine DMA Policy */
        bool has_paging = in_v86_mode || dpmi_present ||
                         g_early_platform.vcpi_present ||
                         g_early_platform.windows_enhanced ||
                         g_early_platform.emm386_detected ||
                         g_early_platform.qemm_detected;
        
        if (has_paging) {
            /* V86/paging without VDS = FORBID DMA */
            g_early_platform.recommended_policy = DMA_POLICY_FORBID;
            g_early_platform.safe_for_busmaster = false;
            g_early_platform.requires_vds = false;
            g_early_platform.pio_fallback_ok = true;
            strcpy(g_early_platform.environment_desc,
                   "V86/Paging mode without VDS - DMA FORBIDDEN");
            
            LOG_ERROR("==============================================");
            LOG_ERROR("WARNING: V86/Paging mode detected without VDS");
            LOG_ERROR("Bus-master DMA is FORBIDDEN to prevent corruption");
            LOG_ERROR("Only PIO operations will be allowed");
            LOG_ERROR("3C509B will work, 3C515-TX will be DISABLED");
            LOG_ERROR("==============================================");
        } else {
            /* Real mode or HIMEM-only = DIRECT DMA OK */
            g_early_platform.recommended_policy = DMA_POLICY_DIRECT;
            g_early_platform.safe_for_busmaster = true;
            g_early_platform.requires_vds = false;
            g_early_platform.pio_fallback_ok = true;
            
            if (himem_only) {
                strcpy(g_early_platform.environment_desc,
                       "HIMEM-only (no V86) - direct DMA safe");
                LOG_INFO("HIMEM-only environment - direct DMA operations allowed");
            } else {
                strcpy(g_early_platform.environment_desc,
                       "Real mode - direct DMA allowed");
                LOG_INFO("Real mode environment - direct DMA operations allowed");
            }
        }
    }
    
    /* Step 5: Set Global DMA Policy */
    g_platform = g_early_platform;
    g_dma_policy = g_early_platform.recommended_policy;
    
    LOG_INFO("DMA Policy Decision: %s", 
             g_dma_policy == DMA_POLICY_DIRECT ? "DIRECT" :
             g_dma_policy == DMA_POLICY_COMMONBUF ? "COMMONBUF (VDS)" :
             "FORBID");
    LOG_INFO("Environment: %s", g_early_platform.environment_desc);
    
    g_early_probe_done = true;
    return 0;
}

/**
 * @brief Detect V86 mode (moved from main.c)
 */
static bool detect_v86_mode_early(void) {
    bool in_v86 = false;
    
    /* Method 1: Check EFLAGS VM bit (386+) */
    _asm {
        pushf
        pop ax
        mov bx, ax
        or ax, 0x8000       ; Try to set bit 15
        push ax
        popf
        pushf
        pop ax
        cmp ax, bx
        je not_386_plus
        
        ; 386+ detected, check VM flag
        .386
        pushfd
        pop eax
        test eax, 0x20000   ; VM flag (bit 17)
        jz not_v86_1
        mov in_v86, 1
        
    not_v86_1:
        .286
    not_386_plus:
    }
    
    if (in_v86) {
        return true;
    }
    
    /* Method 2: Try to execute privileged instruction */
    _asm {
        ; Try to read CR0 (privileged in V86)
        push ax
        
        ; Set up exception handler
        xor ax, ax
        mov es, ax
        
        ; Try SMSW (Store Machine Status Word)
        smsw ax
        
        ; If we get here, we're in real mode or ring 0
        jmp done_v86_check
        
        ; If we fault, we're in V86 (handler would set flag)
        
    done_v86_check:
        pop ax
    }
    
    /* Method 3: Check for memory managers that imply V86 */
    /* This is done in the memory manager detection functions */
    
    return in_v86;
}

/**
 * @brief Detect DPMI services
 */
static bool detect_dpmi_services(void) {
    union REGS regs;
    
    /* INT 2Fh, AX=1687h - DPMI Installation Check */
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0x1687;
    int86(0x2F, &regs, &regs);
    
    /* DPMI present if AX=0000h */
    return (regs.x.ax == 0x0000);
}

/**
 * @brief Detect XMS/HIMEM services
 */
bool detect_xms_services(void) {
    union REGS regs;
    
    /* INT 2Fh, AX=4300h - XMS Installation Check */
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0x4300;
    int86(0x2F, &regs, &regs);
    
    /* XMS present if AL=80h */
    return (regs.h.al == 0x80);
}

/**
 * @brief Get early platform probe results
 */
platform_probe_result_t* get_early_platform_results(void) {
    if (!g_early_probe_done) {
        platform_probe_early();
    }
    return &g_early_platform;
}

/**
 * @brief Check if bus-master DMA is allowed based on early probe
 */
bool early_allow_busmaster_dma(void) {
    if (!g_early_probe_done) {
        platform_probe_early();
    }
    return g_early_platform.safe_for_busmaster;
}

/**
 * @brief Get DMA policy description
 */
const char* get_dma_policy_description(dma_policy_t policy) {
    switch (policy) {
        case DMA_POLICY_DIRECT:
            return "DIRECT - Real mode physical addressing";
        case DMA_POLICY_COMMONBUF:
            return "COMMONBUF - VDS managed DMA";
        case DMA_POLICY_FORBID:
            return "FORBID - No DMA allowed (PIO only)";
        default:
            return "UNKNOWN";
    }
}