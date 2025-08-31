/**
 * @file discardable.c
 * @brief Discardable Memory Segment Implementation
 * 
 * Implementation of discardable memory management for DOS TSR.
 * Allows initialization code to be freed after boot to save memory.
 */

#include "../include/discardable.h"
#include "../include/common.h"
#include "../include/logging.h"
#include <dos.h>
#include <i86.h>

/* Global state for discardable memory management */
static uint8_t g_init_complete = 0;
static uint8_t g_init_segment_discarded = 0;
static uint32_t g_init_segment_size = 0;
static void far *g_init_segment_start = NULL;

/* Internal functions */
static uint32_t calculate_segment_size(void far *start, void far *end);
static int release_dos_memory(void far *segment, uint32_t size);

/**
 * @brief Calculate size of discardable INIT segment
 */
uint32_t discardable_get_init_segment_size(void) {
#if HAS_DISCARDABLE_SEGMENTS
    if (g_init_segment_size == 0) {
        /* Calculate INIT segment size using linker-provided symbols */
        extern void __init_start(void);
        extern void __init_end(void);
        
        void far *start = (void far *)__init_start;
        void far *end = (void far *)__init_end;
        
        g_init_segment_size = calculate_segment_size(start, end);
        g_init_segment_start = start;
        
        LOG_DEBUG("Discardable: INIT segment size calculated: %lu bytes", g_init_segment_size);
    }
    
    return g_init_segment_size;
#else
    return 0; /* No discardable segments supported */
#endif
}

/**
 * @brief Free INIT segment memory
 */
uint32_t discardable_free_init_segment(void) {
    uint32_t freed_bytes = 0;
    
#if HAS_DISCARDABLE_SEGMENTS
    if (!g_init_complete) {
        LOG_WARNING("Discardable: Attempted to free INIT segment before init complete");
        return 0;
    }
    
    if (g_init_segment_discarded) {
        LOG_DEBUG("Discardable: INIT segment already discarded");
        return 0;
    }
    
    if (g_init_segment_size == 0) {
        discardable_get_init_segment_size();
    }
    
    if (g_init_segment_size > 0 && g_init_segment_start) {
        LOG_INFO("Discardable: Freeing INIT segment - %lu bytes", g_init_segment_size);
        
        /* Release memory back to DOS */
        if (release_dos_memory(g_init_segment_start, g_init_segment_size) == SUCCESS) {
            freed_bytes = g_init_segment_size;
            g_init_segment_discarded = 1;
            
            LOG_INFO("Discardable: Successfully freed %lu bytes of INIT segment", freed_bytes);
        } else {
            LOG_ERROR("Discardable: Failed to release INIT segment memory");
        }
    }
#endif

    return freed_bytes;
}

/**
 * @brief Check if INIT segment is still available
 */
int discardable_init_available(void) {
    return (!g_init_segment_discarded);
}

/**
 * @brief Mark initialization phase as complete
 */
void discardable_mark_init_complete(void) {
    if (g_init_complete) {
        LOG_DEBUG("Discardable: Initialization already marked complete");
        return;
    }
    
    g_init_complete = 1;
    LOG_INFO("Discardable: Initialization phase marked complete - INIT segment can be freed");
    
    /* Automatically free INIT segment */
    uint32_t freed_bytes = discardable_free_init_segment();
    if (freed_bytes > 0) {
        LOG_INFO("Discardable: Automatically freed %lu bytes after init", freed_bytes);
    }
}

/**
 * @brief Check if initialization is complete
 */
int discardable_is_init_complete(void) {
    return g_init_complete;
}

/* Internal helper functions */

/**
 * @brief Calculate size between two far pointers
 */
static uint32_t calculate_segment_size(void far *start, void far *end) {
    uint16_t start_seg, start_off;
    uint16_t end_seg, end_off;
    uint32_t start_linear, end_linear;
    
    /* Extract segment:offset from far pointers */
    start_seg = FP_SEG(start);
    start_off = FP_OFF(start);
    end_seg = FP_SEG(end);
    end_off = FP_OFF(end);
    
    /* Convert to linear addresses */
    start_linear = ((uint32_t)start_seg << 4) + start_off;
    end_linear = ((uint32_t)end_seg << 4) + end_off;
    
    if (end_linear > start_linear) {
        return end_linear - start_linear;
    } else {
        LOG_WARNING("Discardable: Invalid segment boundaries - end before start");
        return 0;
    }
}

/**
 * @brief Release memory back to DOS
 */
static int release_dos_memory(void far *segment, uint32_t size) {
    union REGS regs;
    struct SREGS sregs;
    uint16_t segment_addr;
    
    if (!segment || size == 0) {
        return ERROR_INVALID_PARAMETER;
    }
    
    segment_addr = FP_SEG(segment);
    
    LOG_DEBUG("Discardable: Attempting to release DOS memory - segment 0x%04X, size %lu", 
              segment_addr, size);
    
    /* Use DOS INT 21h, function 49h (Free Memory Block) */
    regs.h.ah = 0x49;           /* Free memory block */
    sregs.es = segment_addr;    /* Segment to free */
    
    int86x(0x21, &regs, &regs, &sregs);
    
    if (regs.x.cflag) {
        /* Error occurred */
        LOG_ERROR("Discardable: DOS free memory failed - error code %d", regs.h.al);
        return ERROR_GENERIC;
    }
    
    LOG_DEBUG("Discardable: DOS memory block freed successfully");
    return SUCCESS;
}

/* Initialization-only functions - will be discarded after init */

/**
 * @brief Initialize discardable memory system
 * 
 * This function itself is marked as discardable since it's only
 * needed during initialization.
 */
INIT_FUNCTION int discardable_init(void) {
    uint32_t init_size;
    
    LOG_DEBUG("Discardable: Initializing discardable memory system");
    
    init_size = discardable_get_init_segment_size();
    
    if (init_size > 0) {
        LOG_INFO("Discardable: INIT segment detected - %lu bytes available for reclaim", 
                 init_size);
    } else {
        LOG_INFO("Discardable: No discardable segments available");
    }
    
    return SUCCESS;
}

/**
 * @brief Test function to verify discardable segments work
 */
INIT_FUNCTION int discardable_test(void) {
    LOG_DEBUG("Discardable: Test function in INIT segment - this will be discarded");
    
    /* This function and its code will be discarded after initialization */
    static INIT_DATA_DECL char test_message[] = "This data will be discarded";
    
    LOG_DEBUG("Discardable: Test message: %s", test_message);
    
    return SUCCESS;
}