/**
 * @file xms_alloc.h
 * @brief XMS memory allocation for large optional buffers
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * On 386+ systems with XMS available, large buffers are allocated
 * from extended memory. On 8086/286 or without XMS, features using
 * these buffers are gracefully disabled.
 *
 * This module addresses the 183 KB data segment overflow by moving:
 * - Promiscuous mode buffers (102.4 KB) to XMS
 * - Routing/bridge tables (~16 KB) to XMS
 *
 * Last Updated: 2026-01-26 14:30:00 UTC
 */

#ifndef _XMS_ALLOC_H_
#define _XMS_ALLOC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

/* ============================================================================
 * XMS Constants
 * ============================================================================ */

/* XMS function codes */
#define XMS_GET_VERSION         0x00    /* Get XMS version */
#define XMS_REQUEST_HMA         0x01    /* Request High Memory Area */
#define XMS_RELEASE_HMA         0x02    /* Release HMA */
#define XMS_GLOBAL_A20_ENABLE   0x03    /* Enable A20 globally */
#define XMS_GLOBAL_A20_DISABLE  0x04    /* Disable A20 globally */
#define XMS_LOCAL_A20_ENABLE    0x05    /* Enable A20 locally */
#define XMS_LOCAL_A20_DISABLE   0x06    /* Disable A20 locally */
#define XMS_QUERY_A20           0x07    /* Query A20 state */
#define XMS_QUERY_FREE_EMB      0x08    /* Query free extended memory */
#define XMS_ALLOC_EMB           0x09    /* Allocate extended memory block */
#define XMS_FREE_EMB            0x0A    /* Free extended memory block */
#define XMS_MOVE_EMB            0x0B    /* Move extended memory block */
#define XMS_LOCK_EMB            0x0C    /* Lock extended memory block */
#define XMS_UNLOCK_EMB          0x0D    /* Unlock extended memory block */
#define XMS_GET_EMB_INFO        0x0E    /* Get EMB handle info */
#define XMS_REALLOC_EMB         0x0F    /* Reallocate EMB */

/* XMS error codes */
#define XMS_OK                  0x00    /* Success */
#define XMS_NOT_IMPLEMENTED     0x80    /* Function not implemented */
#define XMS_VDISK_DETECTED      0x81    /* VDISK device detected */
#define XMS_A20_ERROR           0x82    /* A20 line error */
#define XMS_DRIVER_ERROR        0x8E    /* General driver error */
#define XMS_FATAL_ERROR         0x8F    /* Fatal driver error */
#define XMS_NO_HMA              0x90    /* No HMA available */
#define XMS_HMA_IN_USE          0x91    /* HMA already in use */
#define XMS_HMA_TOO_SMALL       0x92    /* HMA request too small */
#define XMS_HMA_NOT_ALLOCATED   0x93    /* HMA not allocated */
#define XMS_A20_STILL_ENABLED   0x94    /* A20 still enabled */
#define XMS_NO_FREE_EMB         0xA0    /* No free extended memory */
#define XMS_NO_FREE_HANDLES     0xA1    /* No free EMB handles */
#define XMS_INVALID_HANDLE      0xA2    /* Invalid EMB handle */
#define XMS_INVALID_SOURCE      0xA3    /* Invalid source handle */
#define XMS_INVALID_SOURCE_OFF  0xA4    /* Invalid source offset */
#define XMS_INVALID_DEST        0xA5    /* Invalid dest handle */
#define XMS_INVALID_DEST_OFF    0xA6    /* Invalid dest offset */
#define XMS_INVALID_LENGTH      0xA7    /* Invalid length */
#define XMS_INVALID_OVERLAP     0xA8    /* Invalid overlap */
#define XMS_PARITY_ERROR        0xA9    /* Parity error */
#define XMS_NOT_LOCKED          0xAA    /* EMB not locked */
#define XMS_LOCKED              0xAB    /* EMB locked */
#define XMS_LOCK_OVERFLOW       0xAC    /* Lock count overflow */
#define XMS_LOCK_FAILED         0xAD    /* Lock failed */

/* XMS minimum version for our requirements */
#define XMS_MIN_VERSION_MAJOR   2
#define XMS_MIN_VERSION_MINOR   0

/* ============================================================================
 * XMS Block Structure
 * ============================================================================ */

/**
 * @brief XMS allocation result/handle structure
 *
 * Contains all information needed to access an XMS memory block.
 * The handle is used for all XMS operations, and the linear address
 * is used for direct memory access via A20.
 */
typedef struct {
    uint16_t handle;            /* XMS handle (0 if not allocated) */
    uint32_t xms_address;       /* 32-bit linear address in XMS (after lock) */
    uint32_t size;              /* Allocated size in bytes */
    uint16_t lock_count;        /* Current lock count */
    uint8_t  valid;             /* Block is valid and allocated */
    uint8_t  locked;            /* Block is currently locked */
} xms_block_t;

/**
 * @brief XMS move structure (for XMS function 0Bh)
 *
 * Used to copy data between conventional and extended memory.
 * Handle of 0 indicates conventional memory (segment:offset).
 */
typedef struct {
    uint32_t length;            /* Transfer length in bytes */
    uint16_t src_handle;        /* Source handle (0=conventional) */
    uint32_t src_offset;        /* Source offset (or seg:off if handle=0) */
    uint16_t dst_handle;        /* Destination handle (0=conventional) */
    uint32_t dst_offset;        /* Destination offset (or seg:off if handle=0) */
} xms_move_t;

/* ============================================================================
 * Global XMS State
 * ============================================================================ */

/**
 * @brief XMS availability flag
 *
 * Set during Stage 1 (CPU detect) after determining CPU >= 386
 * and XMS driver is present.
 */
extern int g_xms_available;

/**
 * @brief XMS driver version
 *
 * Format: high byte = major, low byte = minor (e.g., 0x0300 = 3.00)
 */
extern uint16_t g_xms_version;

/**
 * @brief XMS driver entry point
 *
 * Far call entry point obtained via INT 2Fh/4310h
 */
extern void (far *g_xms_entry)(void);

/**
 * @brief Free XMS memory in KB
 *
 * Updated during xms_init() and after allocations
 */
extern uint32_t g_xms_free_kb;

/**
 * @brief Largest free XMS block in KB
 */
extern uint32_t g_xms_largest_block_kb;

/* ============================================================================
 * Pre-allocated XMS Buffer Handles
 * ============================================================================ */

/**
 * XMS buffer handles for large optional subsystems.
 * These are allocated during initialization if XMS is available.
 */

/** Promiscuous mode packet buffers (64 buffers x 1616 bytes = ~102 KB) */
extern xms_block_t g_promisc_xms;

/** Routing/bridge learning tables (~16 KB) */
extern xms_block_t g_routing_xms;

/* ============================================================================
 * XMS Functions
 * ============================================================================ */

/**
 * @brief Initialize XMS subsystem
 * @return 0 if XMS available and initialized, -1 if not available
 *
 * Called during Stage 1 (CPU detect) after determining CPU >= 386.
 * If XMS not available, g_xms_available = 0 and all XMS-dependent
 * features will be gracefully disabled.
 *
 * Detection sequence:
 * 1. Check CPU type >= 386 (required for XMS access)
 * 2. Call INT 2Fh AX=4300h to check for XMS driver
 * 3. Call INT 2Fh AX=4310h to get XMS entry point
 * 4. Get XMS version and verify >= 2.0
 * 5. Query free extended memory
 */
int xms_init(void);

/**
 * @brief Shutdown XMS subsystem
 *
 * Frees all allocated XMS blocks and resets state.
 * Called during driver cleanup.
 */
void xms_shutdown(void);

/**
 * @brief Allocate XMS memory block
 * @param size_kb Size to allocate in KB (1-65535)
 * @param block Output structure with handle and info
 * @return 0 on success, -1 on failure
 *
 * Allocates an extended memory block from the XMS driver.
 * The block is initially unlocked.
 */
int xms_alloc(uint16_t size_kb, xms_block_t *block);

/**
 * @brief Free XMS memory block
 * @param block Block to free
 *
 * Unlocks (if locked) and frees the extended memory block.
 */
void xms_free(xms_block_t *block);

/**
 * @brief Lock XMS memory block
 * @param block Block to lock
 * @return 0 on success, -1 on failure
 *
 * Locks the block and retrieves the 32-bit linear address.
 * The block must be locked before direct memory access.
 */
int xms_lock(xms_block_t *block);

/**
 * @brief Unlock XMS memory block
 * @param block Block to unlock
 * @return 0 on success, -1 on failure
 *
 * Unlocks the block. The linear address becomes invalid.
 */
int xms_unlock(xms_block_t *block);

/**
 * @brief Copy data to/from XMS block
 * @param block XMS block
 * @param offset Offset within XMS block
 * @param conv_buf Conventional memory buffer
 * @param size Bytes to copy
 * @param to_xms 1 = copy to XMS, 0 = copy from XMS
 * @return 0 on success, -1 on failure
 *
 * Uses XMS function 0Bh to copy data between conventional
 * memory and the extended memory block.
 */
int xms_copy(xms_block_t *block, uint32_t offset,
             void far *conv_buf, uint32_t size, int to_xms);

/**
 * @brief Query free XMS memory
 * @param free_kb Output: total free XMS in KB
 * @param largest_kb Output: largest free block in KB
 * @return 0 on success, -1 on failure
 */
int xms_query_free(uint32_t *free_kb, uint32_t *largest_kb);

/**
 * @brief Enable A20 line
 * @return 0 on success, -1 on failure
 *
 * Enables the A20 address line to access memory above 1MB.
 * Uses XMS local A20 enable (function 05h).
 */
int xms_enable_a20(void);

/**
 * @brief Disable A20 line
 * @return 0 on success, -1 on failure
 *
 * Disables the A20 address line.
 * Uses XMS local A20 disable (function 06h).
 */
int xms_disable_a20(void);

/**
 * @brief Check if A20 is enabled
 * @return 1 if A20 is enabled, 0 if disabled, -1 on error
 */
int xms_query_a20(void);

/* ============================================================================
 * High-Level XMS Buffer Management
 * ============================================================================ */

/**
 * @brief Allocate promiscuous mode buffers from XMS
 * @return 0 on success, negative error code on failure
 *
 * Allocates ~102 KB for 64 packet capture buffers.
 * Called during promiscuous mode initialization.
 */
int xms_alloc_promisc_buffers(void);

/**
 * @brief Free promiscuous mode buffers
 */
void xms_free_promisc_buffers(void);

/**
 * @brief Allocate routing tables from XMS
 * @return 0 on success, negative error code on failure
 *
 * Allocates ~16 KB for routing and bridge tables.
 * Called during routing subsystem initialization.
 */
int xms_alloc_routing_tables(void);

/**
 * @brief Free routing tables
 */
void xms_free_routing_tables(void);

/* ============================================================================
 * Error Codes
 * ============================================================================ */

#define XMS_ERR_NOT_AVAILABLE       -1      /* XMS not available */
#define XMS_ERR_CPU_NOT_SUPPORTED   -2      /* CPU < 386 */
#define XMS_ERR_VERSION_TOO_OLD     -3      /* XMS version < 2.0 */
#define XMS_ERR_NO_MEMORY           -4      /* Not enough XMS memory */
#define XMS_ERR_ALLOC_FAILED        -5      /* Allocation failed */
#define XMS_ERR_INVALID_HANDLE      -6      /* Invalid handle */
#define XMS_ERR_LOCK_FAILED         -7      /* Lock failed */
#define XMS_ERR_COPY_FAILED         -8      /* Copy operation failed */
#define XMS_ERR_A20_FAILED          -9      /* A20 enable/disable failed */

/* ============================================================================
 * Promiscuous Mode XMS Error Codes
 * ============================================================================ */

#define PROMISC_NO_XMS              -100    /* XMS not available for promisc */
#define PROMISC_ALLOC_FAILED        -101    /* XMS allocation failed */
#define PROMISC_NOT_INITIALIZED     -102    /* Promisc not initialized */

/* ============================================================================
 * Routing XMS Error Codes
 * ============================================================================ */

#define ROUTING_NO_XMS              -110    /* XMS not available for routing */
#define ROUTING_ALLOC_FAILED        -111    /* XMS allocation failed */

/* ============================================================================
 * Feature Availability Check
 * ============================================================================ */

/**
 * @brief Check if XMS-backed promiscuous mode is available
 * @return 1 if available, 0 if not
 */
int xms_promisc_available(void);

/**
 * @brief Check if XMS-backed routing is available
 * @return 1 if available, 0 if not
 */
int xms_routing_available(void);

/**
 * @brief Get reason why XMS is not available
 * @return Human-readable string explaining unavailability
 */
const char *xms_unavailable_reason(void);

#ifdef __cplusplus
}
#endif

#endif /* _XMS_ALLOC_H_ */
