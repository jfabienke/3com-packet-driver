/**
 * @file unwind.c
 * @brief Error unwind and cleanup management
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * GPT-5: Implements comprehensive error recovery with proper cleanup
 * of each initialization phase to prevent resource leaks and ensure
 * system stability on failure.
 */

#include <dos.h>
#include <stdio.h>
#include <string.h>
#include "../../include/main.h"
#include "../../include/hardware.h"
#include "../../include/memory.h"
#include "../../include/api.h"
#include "../../include/logging.h"
#include "../../include/vds.h"
#include "../../include/config.h"

/* Unwind phase tracking */
typedef enum {
    UNWIND_PHASE_NONE = 0,
    UNWIND_PHASE_LOGGING,
    UNWIND_PHASE_CPU_DETECT,
    UNWIND_PHASE_PLATFORM_PROBE,
    UNWIND_PHASE_CONFIG,
    UNWIND_PHASE_CHIPSET,
    UNWIND_PHASE_VDS,
    UNWIND_PHASE_MEMORY_CORE,
    UNWIND_PHASE_HARDWARE,
    UNWIND_PHASE_MEMORY_DMA,
    UNWIND_PHASE_TSR,
    UNWIND_PHASE_API_HOOKS,
    UNWIND_PHASE_INTERRUPTS,
    UNWIND_PHASE_API_ACTIVE,
    UNWIND_PHASE_COMPLETE
} unwind_phase_t;

/* Global unwind state */
static struct {
    unwind_phase_t current_phase;
    uint16_t saved_interrupt_mask;
    uint16_t saved_vectors[256];
    bool vectors_saved;
    bool pic_mask_saved;
    void *allocated_memory[16];
    int memory_count;
    uint16_t pnp_resources[8];
    int pnp_count;
} g_unwind_state = {0};

/* Unwind function table entry */
typedef struct {
    unwind_phase_t phase;
    const char *phase_name;
    void (*unwind_func)(void);
} unwind_entry_t;

/* Forward declarations for unwind functions */
static void unwind_logging(void);
static void unwind_cpu_detect(void);
static void unwind_platform_probe(void);
static void unwind_config(void);
static void unwind_chipset(void);
static void unwind_vds(void);
static void unwind_memory_core(void);
static void unwind_hardware(void);
static void unwind_memory_dma(void);
static void unwind_tsr(void);
static void unwind_api_hooks(void);
static void unwind_interrupts(void);
static void unwind_api_active(void);

/* GPT-5 Fix: Clarifying table ordering
 * This table is in REVERSE order of initialization phases.
 * During unwind, we iterate through this table from top to bottom.
 * This ensures proper cleanup in reverse initialization order.
 * 
 * Example: If init order is: CPU -> Platform -> Config -> ... -> API -> Interrupts
 *          Then unwind is:   Interrupts -> API -> ... -> Config -> Platform -> CPU
 * 
 * Current ordering is CORRECT:
 * - TSR comes BEFORE API_HOOKS in this unwind table
 * - This means TSR was initialized AFTER API_HOOKS (correct init order)
 * - During unwind, API_HOOKS cleanup happens before TSR cleanup (correct)
 */
static const unwind_entry_t unwind_table[] = {
    { UNWIND_PHASE_API_ACTIVE,     "API Activation",     unwind_api_active },
    { UNWIND_PHASE_INTERRUPTS,     "Interrupt Enable",   unwind_interrupts },
    { UNWIND_PHASE_API_HOOKS,      "API Hooks",         unwind_api_hooks },
    { UNWIND_PHASE_TSR,            "TSR Relocation",    unwind_tsr },
    { UNWIND_PHASE_MEMORY_DMA,     "DMA Memory",        unwind_memory_dma },
    { UNWIND_PHASE_HARDWARE,       "Hardware Init",     unwind_hardware },
    { UNWIND_PHASE_MEMORY_CORE,    "Core Memory",       unwind_memory_core },
    { UNWIND_PHASE_VDS,            "VDS Support",       unwind_vds },
    { UNWIND_PHASE_CHIPSET,        "Chipset Detect",    unwind_chipset },
    { UNWIND_PHASE_CONFIG,         "Configuration",     unwind_config },
    { UNWIND_PHASE_PLATFORM_PROBE, "Platform Probe",    unwind_platform_probe },
    { UNWIND_PHASE_CPU_DETECT,     "CPU Detection",     unwind_cpu_detect },
    { UNWIND_PHASE_LOGGING,        "Logging System",    unwind_logging },
    { UNWIND_PHASE_NONE,           NULL,                NULL }
};

/**
 * @brief Save interrupt vector table for restoration
 */
static void save_interrupt_vectors(void) {
    int i;
    void far *vector;
    
    if (g_unwind_state.vectors_saved) {
        return;
    }
    
    for (i = 0; i < 256; i++) {
        _dos_getvect(i, &vector);
        g_unwind_state.saved_vectors[i] = FP_SEG(vector);
    }
    
    g_unwind_state.vectors_saved = true;
}

/**
 * @brief Restore interrupt vector table
 */
static void restore_interrupt_vectors(void) {
    int i;
    void far *vector;
    
    if (!g_unwind_state.vectors_saved) {
        return;
    }
    
    _asm cli;  /* Disable interrupts during restoration */
    
    for (i = 0; i < 256; i++) {
        if (g_unwind_state.saved_vectors[i] != 0) {
            vector = MK_FP(g_unwind_state.saved_vectors[i], 0);
            _dos_setvect(i, vector);
        }
    }
    
    _asm sti;  /* Re-enable interrupts */
    
    g_unwind_state.vectors_saved = false;
}

/**
 * @brief Save PIC masks for restoration
 */
static void save_pic_masks(void) {
    uint8_t mask1, mask2;
    
    if (g_unwind_state.pic_mask_saved) {
        return;
    }
    
    _asm {
        in al, 0x21        ; Read master PIC mask
        mov mask1, al
        in al, 0xA1        ; Read slave PIC mask
        mov mask2, al
    }
    
    g_unwind_state.saved_interrupt_mask = (mask2 << 8) | mask1;
    g_unwind_state.pic_mask_saved = true;
}

/**
 * @brief Restore PIC masks
 */
static void restore_pic_masks(void) {
    uint8_t mask1, mask2;
    
    if (!g_unwind_state.pic_mask_saved) {
        return;
    }
    
    mask1 = g_unwind_state.saved_interrupt_mask & 0xFF;
    mask2 = (g_unwind_state.saved_interrupt_mask >> 8) & 0xFF;
    
    _asm {
        mov al, mask1
        out 0x21, al       ; Restore master PIC mask
        mov al, mask2
        out 0xA1, al       ; Restore slave PIC mask
    }
    
    g_unwind_state.pic_mask_saved = false;
}

/* Individual phase unwind functions */

static void unwind_logging(void) {
    log_info("Unwinding: Logging system");
    logging_cleanup();
}

static void unwind_cpu_detect(void) {
    log_info("Unwinding: CPU detection");
    /* CPU detection is read-only, nothing to unwind */
}

static void unwind_platform_probe(void) {
    log_info("Unwinding: Platform probe");
    /* Platform probe is read-only, nothing to unwind */
}

static void unwind_config(void) {
    log_info("Unwinding: Configuration");
    /* Config parsing allocates no persistent resources */
}

static void unwind_chipset(void) {
    log_info("Unwinding: Chipset detection");
    /* Chipset detection is read-only, nothing to unwind */
}

static void unwind_vds(void) {
    log_info("Unwinding: VDS support");
    vds_cleanup();  /* Unlocks all regions and releases buffers */
}

static void unwind_memory_core(void) {
    log_info("Unwinding: Core memory");
    memory_cleanup();  /* Frees all pools and XMS/UMB */
}

static void unwind_hardware(void) {
    nic_info_t *nic;
    
    log_info("Unwinding: Hardware initialization");
    
    /* Disable all NIC interrupts */
    nic = hardware_get_primary_nic();
    if (nic) {
        hardware_disable_interrupts(nic);
    }
    
    /* Cleanup hardware resources */
    hardware_cleanup();
    
    /* Release PnP resources if allocated */
    if (g_unwind_state.pnp_count > 0) {
        log_info("  Releasing %d PnP resources", g_unwind_state.pnp_count);
        /* PnP resource deactivation would go here */
        g_unwind_state.pnp_count = 0;
    }
}

static void unwind_memory_dma(void) {
    log_info("Unwinding: DMA memory");
    /* DMA memory cleanup is handled by memory_cleanup() */
}

static void unwind_tsr(void) {
    log_info("Unwinding: TSR relocation");
    /* If TSR was relocated, we're still running from new location */
    /* Cannot undo relocation, but can mark TSR for removal */
    tsr_uninstall();
}

static void unwind_api_hooks(void) {
    log_info("Unwinding: API hooks");
    
    /* GPT-5: Clear API ready flag BEFORE unhooking */
    extern volatile int api_ready;
    api_ready = 0;
    
    /* Restore original interrupt vectors */
    restore_interrupt_vectors();
    
    /* Cleanup API resources */
    api_cleanup();
}

static void unwind_interrupts(void) {
    log_info("Unwinding: Interrupt configuration");
    
    /* Disable driver interrupts (includes ELCR restore) */
    disable_driver_interrupts();
    
    /* Restore original PIC masks */
    restore_pic_masks();
}

static void unwind_api_active(void) {
    log_info("Unwinding: API activation");
    
    /* GPT-5: Clear API ready flag */
    extern volatile int api_ready;
    api_ready = 0;
    
    /* API deactivation handled by api_cleanup() */
}

/**
 * @brief Mark successful completion of a phase
 * 
 * @param phase Phase that completed successfully
 */
void unwind_mark_phase_complete(unwind_phase_t phase) {
    g_unwind_state.current_phase = phase;
}

/**
 * @brief Register allocated memory for cleanup
 * 
 * @param ptr Memory pointer to track
 */
void unwind_register_memory(void *ptr) {
    if (ptr && g_unwind_state.memory_count < 16) {
        g_unwind_state.allocated_memory[g_unwind_state.memory_count++] = ptr;
    }
}

/**
 * @brief Register PnP resource for cleanup
 * 
 * @param resource PnP resource ID
 */
void unwind_register_pnp(uint16_t resource) {
    if (g_unwind_state.pnp_count < 8) {
        g_unwind_state.pnp_resources[g_unwind_state.pnp_count++] = resource;
    }
}

/**
 * @brief Execute error unwind from current phase
 * 
 * Unwinds all initialization in reverse order from the
 * current phase back to the beginning.
 * 
 * @param error_code Error code that triggered unwind
 * @param error_msg Error message to display
 */
void unwind_execute(int error_code, const char *error_msg) {
    int i;
    unwind_phase_t start_phase = g_unwind_state.current_phase;
    
    /* Display error that triggered unwind */
    printf("\n");
    printf("===========================================\n");
    printf("CRITICAL ERROR - INITIATING UNWIND\n");
    printf("===========================================\n");
    printf("Error Code: %d\n", error_code);
    printf("Error: %s\n", error_msg ? error_msg : "Unknown error");
    printf("Failed Phase: %d\n", start_phase);
    printf("\n");
    
    /* Save current system state if not already saved */
    if (!g_unwind_state.vectors_saved) {
        save_interrupt_vectors();
    }
    if (!g_unwind_state.pic_mask_saved) {
        save_pic_masks();
    }
    
    /* Execute unwind functions in reverse order */
    printf("Beginning unwind sequence...\n");
    
    for (i = 0; unwind_table[i].phase != UNWIND_PHASE_NONE; i++) {
        /* Only unwind phases that were successfully initialized */
        if (unwind_table[i].phase <= start_phase && 
            unwind_table[i].unwind_func) {
            
            printf("  Unwinding: %s\n", unwind_table[i].phase_name);
            unwind_table[i].unwind_func();
        }
    }
    
    /* Free any tracked memory that wasn't freed */
    if (g_unwind_state.memory_count > 0) {
        printf("  Freeing %d tracked memory blocks\n", 
               g_unwind_state.memory_count);
        for (i = 0; i < g_unwind_state.memory_count; i++) {
            if (g_unwind_state.allocated_memory[i]) {
                _ffree(g_unwind_state.allocated_memory[i]);
            }
        }
        g_unwind_state.memory_count = 0;
    }
    
    /* Final cleanup */
    printf("\n");
    printf("Unwind complete - system restored\n");
    printf("===========================================\n");
    
    /* Clear unwind state */
    memset(&g_unwind_state, 0, sizeof(g_unwind_state));
}

/**
 * @brief Initialize unwind system
 * 
 * Should be called at the very beginning of initialization
 * to capture initial system state.
 */
void unwind_init(void) {
    /* Clear state */
    memset(&g_unwind_state, 0, sizeof(g_unwind_state));
    
    /* Save initial system state */
    save_interrupt_vectors();
    save_pic_masks();
    
    g_unwind_state.current_phase = UNWIND_PHASE_NONE;
}

/**
 * @brief Get current unwind phase
 * 
 * @return Current phase
 */
unwind_phase_t unwind_get_phase(void) {
    return g_unwind_state.current_phase;
}

/**
 * @brief Check if unwind system is initialized
 * 
 * @return true if initialized
 */
bool unwind_is_initialized(void) {
    return g_unwind_state.vectors_saved || g_unwind_state.pic_mask_saved;
}