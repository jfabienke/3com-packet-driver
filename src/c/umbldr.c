/**
 * @file umb_loader.c
 * @brief UMB detection and loading for DOS packet driver
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * High-level UMB (Upper Memory Block) management and memory manager
 * compatibility layer. Provides fallback to conventional memory when
 * UMBs are not available.
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include <dos.h>
#include "dos_io.h"
#include <string.h>
#include <stdlib.h>
#include "../../include/common.h"
#include "../../include/memory.h"
#include "../../include/logging.h"

/* C89 MIN macro - not defined in headers */
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

/* UMB allocation information structure */
typedef struct {
    bool umb_available;
    uint8_t memory_manager_type;
    uint16_t allocated_segment;
    uint16_t allocated_size;
    bool using_conventional_fallback;
    uint32_t conventional_memory_saved;
    char memory_manager_name[32];
} umb_allocation_info_t;

/* TSR memory layout structure */
typedef struct {
    uint16_t resident_code_size;
    uint16_t resident_data_size;
    uint16_t resident_stack_size;
    uint16_t psp_size;
    uint32_t total_resident_bytes;
    uint16_t resident_paragraphs;
    uint16_t init_code_size;
    uint16_t init_data_size;
    uint32_t total_init_bytes;
    uint16_t init_paragraphs;
    uint16_t conventional_memory_used;
    uint16_t umb_memory_used;
    bool memory_optimization_achieved;
    uint32_t discarded_init_bytes;
} tsr_memory_layout_t;

/* UMB memory report structure */
typedef struct {
    bool umb_support_available;
    bool memory_manager_detected;
    bool allocation_attempted;
    bool allocation_successful;
    bool using_umb;
    uint16_t allocated_segment;
    uint16_t allocated_paragraphs;
    uint32_t conventional_memory_saved;
    char memory_manager_name[32];
} umb_memory_report_t;

/* UMB and memory manager constants */
#define DOS_VERSION_MIN_UMB     0x0500  /* DOS 5.0 minimum for UMB */
#define UMB_SEGMENT_MIN         0xA000  /* Minimum UMB segment */
#define UMB_SEGMENT_MAX         0xFFFF  /* Maximum UMB segment */
#define CONVENTIONAL_MAX        0x9FFF  /* End of conventional memory */

#define MEMORY_ALLOC_STRATEGY   0x5800  /* Get allocation strategy */
#define MEMORY_SET_STRATEGY     0x5801  /* Set allocation strategy */
#define MEMORY_GET_UMB_LINK     0x5802  /* Get UMB link state */
#define MEMORY_SET_UMB_LINK     0x5803  /* Set UMB link state */

#define ALLOC_FIRST_FIT_LOW     0x00    /* First fit, low memory first */
#define ALLOC_BEST_FIT_LOW      0x01    /* Best fit, low memory first */
#define ALLOC_FIRST_FIT_HIGH    0x80    /* First fit, high memory first */
#define ALLOC_BEST_FIT_HIGH     0x81    /* Best fit, high memory first */

/* UMB loader state */
typedef struct {
    bool initialized;
    bool umb_available;
    bool umb_linked;
    bool allocation_attempted;
    uint8_t memory_manager_type;
    uint8_t original_alloc_strategy;
    uint16_t allocated_segment;
    uint16_t allocated_size;
    bool using_conventional_fallback;
    uint32_t conventional_memory_saved;
} umb_state_t;

static umb_state_t g_umb_state = {0};

/* Memory manager detection strings and signatures */
static const char* memory_manager_names[] = {
    "None",
    "HIMEM.SYS",
    "EMM386.EXE", 
    "QEMM386.SYS",
    "Unknown"
};

/**
 * @brief Check DOS version for UMB support
 * @return true if DOS version supports UMBs (5.0+)
 */
static bool check_dos_version_for_umb(void) {
    union REGS regs;
    uint16_t dos_version;

    regs.h.ah = 0x30;  /* Get DOS version */
    int86(0x21, &regs, &regs);

    dos_version = (regs.h.al << 8) | regs.h.ah;

    log_debug("DOS version: %d.%d (0x%04X)", regs.h.al, regs.h.ah, dos_version);

    return (dos_version >= DOS_VERSION_MIN_UMB);
}

/**
 * @brief Detect available memory manager
 * @return Memory manager type (0=none, 1=HIMEM, 2=EMM386, 3=QEMM)
 */
static uint8_t detect_memory_manager(void) {
    union REGS regs;
    
    /* Check for XMS driver (HIMEM.SYS) */
    regs.x.ax = 0x4300;
    int86(0x2F, &regs, &regs);
    if (regs.h.al == 0x80) {
        log_debug("XMS driver detected (HIMEM.SYS)");
        return 1;
    }
    
    /* Check for EMM386 (Windows Enhanced mode or EMM386) */
    regs.x.ax = 0x1600;
    int86(0x2F, &regs, &regs);
    if (regs.h.al != 0x00 && regs.h.al != 0x80) {
        log_debug("Enhanced mode detected (EMM386 or Windows)");
        return 2;
    }
    
    /* Check for QEMM386 */
    regs.x.ax = 0x5945;  /* QEMM API signature */
    int86(0x2F, &regs, &regs);
    if (regs.x.ax != 0x5945) {
        log_debug("QEMM386 detected");
        return 3;
    }
    
    log_debug("No memory manager detected");
    return 0;
}

/**
 * @brief Get current DOS memory allocation strategy
 * @return Current allocation strategy, or 0xFF on error
 */
static uint8_t get_allocation_strategy(void) {
    union REGS regs;
    
    regs.x.ax = MEMORY_ALLOC_STRATEGY;
    int86(0x21, &regs, &regs);
    
    if (regs.x.cflag) {
        log_warning("Failed to get allocation strategy (carry flag set)");
        return 0xFF;
    }
    
    log_debug("Current allocation strategy: 0x%02X", regs.h.al);
    return regs.h.al;
}

/**
 * @brief Set DOS memory allocation strategy
 * @param strategy New allocation strategy
 * @return true on success, false on failure
 */
static bool set_allocation_strategy(uint8_t strategy) {
    union REGS regs;
    
    regs.x.ax = MEMORY_SET_STRATEGY;
    regs.x.bx = strategy;
    int86(0x21, &regs, &regs);
    
    if (regs.x.cflag) {
        log_warning("Failed to set allocation strategy to 0x%02X", strategy);
        return false;
    }
    
    log_debug("Set allocation strategy to 0x%02X", strategy);
    return true;
}

/**
 * @brief Get UMB link state
 * @return UMB link state (0=unlinked, 1=linked), or 0xFF on error
 */
static uint8_t get_umb_link_state(void) {
    union REGS regs;
    
    regs.x.ax = MEMORY_GET_UMB_LINK;
    int86(0x21, &regs, &regs);
    
    if (regs.x.cflag) {
        log_debug("UMB link state query failed (UMBs not supported)");
        return 0xFF;
    }
    
    log_debug("UMB link state: %s", regs.h.al ? "linked" : "unlinked");
    return regs.h.al;
}

/**
 * @brief Link or unlink UMBs from DOS memory chain
 * @param link_state 1 to link, 0 to unlink
 * @return true on success, false on failure
 */
static bool set_umb_link_state(uint8_t link_state) {
    union REGS regs;
    
    regs.x.ax = MEMORY_SET_UMB_LINK;
    regs.x.bx = link_state;
    int86(0x21, &regs, &regs);
    
    if (regs.x.cflag) {
        log_warning("Failed to %s UMBs", link_state ? "link" : "unlink");
        return false;
    }
    
    log_debug("UMBs %s successfully", link_state ? "linked" : "unlinked");
    return true;
}

/**
 * @brief Allocate DOS memory block
 * @param paragraphs Size in paragraphs (16-byte blocks)
 * @return Segment address, or 0 on failure
 */
static uint16_t allocate_dos_memory(uint16_t paragraphs) {
    union REGS regs;
    
    regs.h.ah = 0x48;  /* Allocate memory */
    regs.x.bx = paragraphs;
    int86(0x21, &regs, &regs);
    
    if (regs.x.cflag) {
        log_debug("Memory allocation failed for %d paragraphs (error %d, available %d)", 
                 paragraphs, regs.h.al, regs.x.bx);
        return 0;
    }
    
    log_debug("Allocated %d paragraphs at segment 0x%04X", paragraphs, regs.x.ax);
    return regs.x.ax;
}

/**
 * @brief Free DOS memory block
 * @param segment Segment to free
 * @return true on success, false on failure
 */
static bool free_dos_memory(uint16_t segment) {
    union REGS regs;
    struct SREGS sregs;
    
    if (segment == 0) {
        return true;  /* Nothing to free */
    }
    
    sregs.es = segment;
    regs.h.ah = 0x49;  /* Free memory */
    int86x(0x21, &regs, &regs, &sregs);
    
    if (regs.x.cflag) {
        log_warning("Failed to free memory segment 0x%04X", segment);
        return false;
    }
    
    log_debug("Freed memory segment 0x%04X", segment);
    return true;
}

/**
 * @brief Initialize UMB loader subsystem
 * @return 0 on success, negative on error
 */
int umb_loader_init(void) {
    uint8_t umb_link_state;

    if (g_umb_state.initialized) {
        log_debug("UMB loader already initialized");
        return 0;
    }

    log_info("Initializing UMB loader subsystem");
    
    /* Clear state structure */
    memset(&g_umb_state, 0, sizeof(g_umb_state));
    
    /* Check DOS version */
    if (!check_dos_version_for_umb()) {
        log_info("DOS version too old for UMB support (need 5.0+)");
        g_umb_state.initialized = true;
        return 0;  /* Not an error, just not available */
    }
    
    /* Detect memory manager */
    g_umb_state.memory_manager_type = detect_memory_manager();
    if (g_umb_state.memory_manager_type == 0) {
        log_info("No memory manager detected - UMBs not available");
        g_umb_state.initialized = true;
        return 0;  /* Not an error, just not available */
    }
    
    log_info("Memory manager detected: %s", 
             memory_manager_names[g_umb_state.memory_manager_type]);
    
    /* Save current allocation strategy */
    g_umb_state.original_alloc_strategy = get_allocation_strategy();
    if (g_umb_state.original_alloc_strategy == 0xFF) {
        log_warning("Could not determine current allocation strategy");
        g_umb_state.original_alloc_strategy = ALLOC_FIRST_FIT_LOW;  /* Safe default */
    }
    
    /* Check UMB availability */
    umb_link_state = get_umb_link_state();
    if (umb_link_state == 0xFF) {
        log_info("UMB functions not available");
        g_umb_state.initialized = true;
        return 0;  /* Not an error, just not available */
    }
    
    g_umb_state.umb_available = true;
    g_umb_state.umb_linked = (umb_link_state == 1);
    g_umb_state.initialized = true;
    
    log_info("UMB loader initialized successfully (UMBs %s)", 
             g_umb_state.umb_linked ? "linked" : "unlinked");
    
    return 0;
}

/**
 * @brief Attempt to allocate UMB for TSR
 * @param required_paragraphs Size needed in paragraphs
 * @return 0 on success, negative on error
 */
int umb_allocate_tsr_memory(uint16_t required_paragraphs) {
    uint16_t allocated_segment;
    bool is_umb = false;
    
    if (!g_umb_state.initialized) {
        log_error("UMB loader not initialized");
        return -1;
    }
    
    if (g_umb_state.allocation_attempted) {
        log_debug("UMB allocation already attempted, returning previous result");
        return (g_umb_state.allocated_segment != 0) ? 0 : -2;
    }
    
    g_umb_state.allocation_attempted = true;
    
    if (!g_umb_state.umb_available) {
        log_info("UMBs not available, using conventional memory");
        goto allocate_conventional;
    }
    
    /* Link UMBs if not already linked */
    if (!g_umb_state.umb_linked) {
        if (!set_umb_link_state(1)) {
            log_warning("Failed to link UMBs, trying conventional memory");
            goto allocate_conventional;
        }
        g_umb_state.umb_linked = true;
    }
    
    /* Set allocation strategy to prefer high memory (UMBs) */
    if (!set_allocation_strategy(ALLOC_BEST_FIT_HIGH)) {
        log_warning("Failed to set high memory allocation strategy");
    }
    
    /* Try to allocate memory */
    allocated_segment = allocate_dos_memory(required_paragraphs);
    if (allocated_segment == 0) {
        log_info("UMB allocation failed, trying conventional memory");
        goto allocate_conventional;
    }
    
    /* Check if we got UMB or conventional memory */
    if (allocated_segment >= UMB_SEGMENT_MIN) {
        is_umb = true;
        g_umb_state.conventional_memory_saved = required_paragraphs * 16;
        log_info("Successfully allocated %d paragraphs in UMB at segment 0x%04X", 
                 required_paragraphs, allocated_segment);
    } else {
        log_info("Allocated %d paragraphs in conventional memory at segment 0x%04X", 
                 required_paragraphs, allocated_segment);
    }
    
    g_umb_state.allocated_segment = allocated_segment;
    g_umb_state.allocated_size = required_paragraphs;
    g_umb_state.using_conventional_fallback = !is_umb;
    
    return 0;

allocate_conventional:
    /* Restore original allocation strategy */
    set_allocation_strategy(g_umb_state.original_alloc_strategy);
    
    /* Allocate conventional memory as fallback */
    allocated_segment = allocate_dos_memory(required_paragraphs);
    if (allocated_segment == 0) {
        log_error("Failed to allocate conventional memory as fallback");
        return -3;
    }
    
    g_umb_state.allocated_segment = allocated_segment;
    g_umb_state.allocated_size = required_paragraphs;
    g_umb_state.using_conventional_fallback = true;
    
    log_info("Using conventional memory fallback at segment 0x%04X", allocated_segment);
    return 0;
}

/**
 * @brief Get UMB allocation information
 * @param info Pointer to structure to fill with UMB info
 * @return 0 on success, negative on error
 */
int umb_get_allocation_info(umb_allocation_info_t *info) {
    if (!info) {
        return -1;
    }
    
    if (!g_umb_state.initialized) {
        return -2;
    }
    
    memset(info, 0, sizeof(umb_allocation_info_t));
    
    info->umb_available = g_umb_state.umb_available;
    info->memory_manager_type = g_umb_state.memory_manager_type;
    info->allocated_segment = g_umb_state.allocated_segment;
    info->allocated_size = g_umb_state.allocated_size;
    info->using_conventional_fallback = g_umb_state.using_conventional_fallback;
    info->conventional_memory_saved = g_umb_state.conventional_memory_saved;
    
    strncpy(info->memory_manager_name, 
            memory_manager_names[MIN(g_umb_state.memory_manager_type, 4)],
            sizeof(info->memory_manager_name) - 1);
    
    return 0;
}

/**
 * @brief Calculate optimal TSR memory layout
 * @param layout Pointer to structure to fill with layout info
 * @return 0 on success, negative on error
 */
int umb_calculate_tsr_layout(tsr_memory_layout_t *layout) {
    if (!layout) {
        return -1;
    }
    
    memset(layout, 0, sizeof(tsr_memory_layout_t));
    
    /* Conservative estimates for TSR components */
    layout->resident_code_size = 2048;      /* Resident ISR handlers */
    layout->resident_data_size = 1024;      /* Critical runtime data */
    layout->resident_stack_size = 512;      /* Minimal stack */
    layout->psp_size = 256;                 /* DOS PSP */
    
    /* Calculate total resident size */
    layout->total_resident_bytes = layout->resident_code_size +
                                   layout->resident_data_size +
                                   layout->resident_stack_size +
                                   layout->psp_size;
    
    /* Round up to paragraphs */
    layout->resident_paragraphs = (layout->total_resident_bytes + 15) / 16;
    
    /* Estimate init memory that can be discarded */
    layout->init_code_size = 8192;          /* Initialization code */
    layout->init_data_size = 4096;          /* Initialization data */
    layout->total_init_bytes = layout->init_code_size + layout->init_data_size;
    layout->init_paragraphs = (layout->total_init_bytes + 15) / 16;
    
    /* Calculate memory savings */
    if (g_umb_state.allocated_segment >= UMB_SEGMENT_MIN) {
        /* TSR in UMB - only PSP in conventional memory */
        layout->conventional_memory_used = 16;  /* Just PSP paragraphs */
        layout->umb_memory_used = layout->resident_paragraphs - 16;
        layout->memory_optimization_achieved = true;
    } else {
        /* TSR in conventional memory */
        layout->conventional_memory_used = layout->resident_paragraphs;
        layout->umb_memory_used = 0;
        layout->memory_optimization_achieved = false;
    }
    
    layout->discarded_init_bytes = layout->total_init_bytes;
    
    log_debug("TSR layout: %d resident paragraphs, %d init paragraphs (discarded)", 
              layout->resident_paragraphs, layout->init_paragraphs);
    
    return 0;
}

/**
 * @brief Clean up UMB resources
 * @return 0 on success, negative on error
 */
int umb_loader_cleanup(void) {
    if (!g_umb_state.initialized) {
        return 0;
    }
    
    log_info("Cleaning up UMB loader resources");
    
    /* Free allocated memory */
    if (g_umb_state.allocated_segment != 0) {
        if (!free_dos_memory(g_umb_state.allocated_segment)) {
            log_warning("Failed to free allocated memory segment 0x%04X", 
                       g_umb_state.allocated_segment);
        }
    }
    
    /* Restore original allocation strategy */
    set_allocation_strategy(g_umb_state.original_alloc_strategy);
    
    /* Clear state */
    memset(&g_umb_state, 0, sizeof(g_umb_state));
    
    log_info("UMB loader cleanup completed");
    return 0;
}

/**
 * @brief Get memory usage report
 * @param report Pointer to structure to fill with usage report
 * @return 0 on success, negative on error
 */
int umb_get_memory_usage_report(umb_memory_report_t *report) {
    if (!report) {
        return -1;
    }
    
    if (!g_umb_state.initialized) {
        return -2;
    }
    
    memset(report, 0, sizeof(umb_memory_report_t));
    
    /* Fill in basic information */
    report->umb_support_available = g_umb_state.umb_available;
    report->memory_manager_detected = (g_umb_state.memory_manager_type != 0);
    report->allocation_attempted = g_umb_state.allocation_attempted;
    report->allocation_successful = (g_umb_state.allocated_segment != 0);
    
    if (g_umb_state.allocated_segment != 0) {
        report->using_umb = (g_umb_state.allocated_segment >= UMB_SEGMENT_MIN);
        report->allocated_segment = g_umb_state.allocated_segment;
        report->allocated_paragraphs = g_umb_state.allocated_size;
        report->conventional_memory_saved = g_umb_state.conventional_memory_saved;
    }
    
    strncpy(report->memory_manager_name,
            memory_manager_names[MIN(g_umb_state.memory_manager_type, 4)],
            sizeof(report->memory_manager_name) - 1);
    
    return 0;
}

/**
 * @brief Check if UMB allocation was successful
 * @return true if UMB allocated, false otherwise
 */
bool umb_is_allocated(void) {
    return (g_umb_state.initialized && 
            g_umb_state.allocated_segment != 0 &&
            g_umb_state.allocated_segment >= UMB_SEGMENT_MIN);
}

/**
 * @brief Get allocated segment address
 * @return Segment address, or 0 if not allocated
 */
uint16_t umb_get_allocated_segment(void) {
    return g_umb_state.allocated_segment;
}

/**
 * @brief Get conventional memory savings in bytes
 * @return Bytes saved in conventional memory
 */
uint32_t umb_get_conventional_memory_saved(void) {
    return g_umb_state.conventional_memory_saved;
}
