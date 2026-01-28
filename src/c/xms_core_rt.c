/**
 * @file xms_core_rt.c
 * @brief XMS memory management - Runtime Functions (ROOT Segment)
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * Created: 2026-01-28 08:31:40 UTC
 *
 * This file contains runtime XMS functions that are called during active
 * packet operations, including ISR and packet processing. These functions
 * must remain memory-resident (ROOT segment) for performance and reliability
 * during high-frequency network operations.
 *
 * Functions included:
 * - XMS memory access functions (xms_copy, xms_lock, xms_unlock)
 * - A20 line management (xms_enable_a20, xms_disable_a20, xms_query_a20)
 * - Query functions (xms_query_free)
 * - Feature availability checks (xms_promisc_available, xms_routing_available)
 * - XMS unavailability reason accessor
 *
 * Split from xms_core.c for memory segmentation optimization.
 * Init/cleanup functions are in xms_core_init.c (OVERLAY segment).
 */

#include <stdio.h>
#include <string.h>
#include <dos.h>
#include "common.h"
#include "xms_alloc.h"
#include "logging.h"

/* ============================================================================
 * Global XMS State (owned by this file, extern'd in xms_alloc.h)
 * ============================================================================ */

/* XMS availability and version */
int g_xms_available = 0;
uint16_t g_xms_version = 0;
uint32_t g_xms_free_kb = 0;
uint32_t g_xms_largest_block_kb = 0;

/* XMS driver entry point */
void (far *g_xms_entry)(void) = NULL;

/* Pre-allocated XMS buffer handles */
xms_block_t g_promisc_xms = {0};
xms_block_t g_routing_xms = {0};

/* ============================================================================
 * External declarations for state defined in xms_core_init.c
 * ============================================================================ */

extern int g_xms_initialized;
extern char g_xms_unavail_reason[64];

/* ============================================================================
 * Low-Level XMS Functions
 * ============================================================================ */

/**
 * @brief Call XMS function with parameters
 * @param func XMS function code (AH register)
 * @param dx DX register value (varies by function)
 * @return AX result (varies by function)
 *
 * Internal function to call XMS driver with specified parameters.
 * Uses inline assembly for proper register setup.
 */
static uint16_t xms_call(uint8_t func, uint16_t dx) {
    uint16_t result;
    void (far *entry)(void) = g_xms_entry;

    if (!entry) {
        return 0;
    }

    _asm {
        mov ah, func
        mov dx, dx
        call dword ptr [entry]
        mov result, ax
    }

    return result;
}

/* ============================================================================
 * XMS Block Lock/Unlock (Runtime Operations)
 * ============================================================================ */

/**
 * @brief Lock XMS memory block
 * @param block Block to lock
 * @return 0 on success, negative error code on failure
 */
int xms_lock(xms_block_t *block) {
    uint16_t result;
    uint16_t handle;
    uint16_t addr_high = 0;
    uint16_t addr_low = 0;
    void (far *entry)(void);

    if (!block || !block->valid || block->handle == 0) {
        return XMS_ERR_INVALID_HANDLE;
    }

    if (!g_xms_entry) {
        return XMS_ERR_NOT_AVAILABLE;
    }

    handle = block->handle;
    entry = g_xms_entry;

    /* XMS function 0Ch: Lock Extended Memory Block */
    /* Input: AH = 0Ch, DX = handle */
    /* Output: AX = 1 success, 0 failure; DX:BX = 32-bit linear address */
    _asm {
        mov ah, 0Ch
        mov dx, handle
        call dword ptr [entry]
        mov result, ax
        mov addr_high, dx
        mov addr_low, bx
    }

    if (result != 1) {
        LOG_ERROR("XMS lock failed for handle %u", handle);
        return XMS_ERR_LOCK_FAILED;
    }

    block->xms_address = ((uint32_t)addr_high << 16) | addr_low;
    block->lock_count++;
    block->locked = 1;

    LOG_DEBUG("XMS locked: handle=%u, addr=0x%08lX", handle, block->xms_address);

    return 0;
}

/**
 * @brief Unlock XMS memory block
 * @param block Block to unlock
 * @return 0 on success, negative error code on failure
 */
int xms_unlock(xms_block_t *block) {
    uint16_t result;
    uint16_t handle;
    void (far *entry)(void);

    if (!block || !block->valid || block->handle == 0) {
        return XMS_ERR_INVALID_HANDLE;
    }

    if (!block->locked) {
        return 0;  /* Already unlocked */
    }

    if (!g_xms_entry) {
        return XMS_ERR_NOT_AVAILABLE;
    }

    handle = block->handle;
    entry = g_xms_entry;

    /* XMS function 0Dh: Unlock Extended Memory Block */
    /* Input: AH = 0Dh, DX = handle */
    /* Output: AX = 1 success, 0 failure */
    _asm {
        mov ah, 0Dh
        mov dx, handle
        call dword ptr [entry]
        mov result, ax
    }

    if (result != 1) {
        LOG_WARNING("XMS unlock failed for handle %u", handle);
        return XMS_ERR_LOCK_FAILED;
    }

    block->lock_count--;
    if (block->lock_count == 0) {
        block->locked = 0;
        block->xms_address = 0;
    }

    LOG_DEBUG("XMS unlocked: handle=%u", handle);

    return 0;
}

/* ============================================================================
 * XMS Copy Operations (Runtime - Called from ISR/Packet Processing)
 * ============================================================================ */

/**
 * @brief Copy data to/from XMS block
 * @param block XMS block
 * @param offset Offset within XMS block
 * @param conv_buf Conventional memory buffer
 * @param size Bytes to copy
 * @param to_xms 1 = copy to XMS, 0 = copy from XMS
 * @return 0 on success, negative error code on failure
 */
int xms_copy(xms_block_t *block, uint32_t offset,
             void far *conv_buf, uint32_t size, int to_xms) {
    xms_move_t move;
    uint16_t result;
    void (far *entry)(void);
    xms_move_t far *move_ptr;

    if (!block || !block->valid || block->handle == 0) {
        return XMS_ERR_INVALID_HANDLE;
    }

    if (!conv_buf || size == 0) {
        return XMS_ERR_COPY_FAILED;
    }

    if (!g_xms_entry) {
        return XMS_ERR_NOT_AVAILABLE;
    }

    /* Setup move structure */
    move.length = size;

    if (to_xms) {
        /* Copy from conventional to XMS */
        move.src_handle = 0;  /* 0 = conventional memory */
        move.src_offset = ((uint32_t)FP_SEG(conv_buf) << 16) | FP_OFF(conv_buf);
        move.dst_handle = block->handle;
        move.dst_offset = offset;
    } else {
        /* Copy from XMS to conventional */
        move.src_handle = block->handle;
        move.src_offset = offset;
        move.dst_handle = 0;  /* 0 = conventional memory */
        move.dst_offset = ((uint32_t)FP_SEG(conv_buf) << 16) | FP_OFF(conv_buf);
    }

    entry = g_xms_entry;
    move_ptr = &move;

    /* XMS function 0Bh: Move Extended Memory Block */
    /* Input: AH = 0Bh, DS:SI = pointer to move structure */
    /* Output: AX = 1 success, 0 failure */
    _asm {
        push ds
        push si
        mov ah, 0Bh
        lds si, move_ptr
        call dword ptr [entry]
        mov result, ax
        pop si
        pop ds
    }

    if (result != 1) {
        LOG_ERROR("XMS copy failed: to_xms=%d, size=%lu", to_xms, size);
        return XMS_ERR_COPY_FAILED;
    }

    return 0;
}

/* ============================================================================
 * XMS Query Functions (Runtime)
 * ============================================================================ */

/**
 * @brief Query free XMS memory
 * @param free_kb Output: total free XMS in KB
 * @param largest_kb Output: largest free block in KB
 * @return 0 on success, negative error code on failure
 */
int xms_query_free(uint32_t *free_kb, uint32_t *largest_kb) {
    uint16_t ax_result;
    uint16_t dx_result;
    void (far *entry)(void);

    if (!g_xms_available || !g_xms_entry) {
        if (free_kb) *free_kb = 0;
        if (largest_kb) *largest_kb = 0;
        return XMS_ERR_NOT_AVAILABLE;
    }

    entry = g_xms_entry;

    /* XMS function 08h: Query Free Extended Memory */
    _asm {
        mov ah, 08h
        call dword ptr [entry]
        mov ax_result, ax
        mov dx_result, dx
    }

    if (largest_kb) *largest_kb = ax_result;
    if (free_kb) *free_kb = dx_result;

    return 0;
}

/* ============================================================================
 * A20 Line Management (Runtime)
 * ============================================================================ */

/**
 * @brief Enable A20 line
 * @return 0 on success, negative error code on failure
 */
int xms_enable_a20(void) {
    uint16_t result;
    void (far *entry)(void);

    if (!g_xms_entry) {
        return XMS_ERR_NOT_AVAILABLE;
    }

    entry = g_xms_entry;

    /* XMS function 05h: Local Enable A20 */
    _asm {
        mov ah, 05h
        call dword ptr [entry]
        mov result, ax
    }

    return (result == 1) ? 0 : XMS_ERR_A20_FAILED;
}

/**
 * @brief Disable A20 line
 * @return 0 on success, negative error code on failure
 */
int xms_disable_a20(void) {
    uint16_t result;
    void (far *entry)(void);

    if (!g_xms_entry) {
        return XMS_ERR_NOT_AVAILABLE;
    }

    entry = g_xms_entry;

    /* XMS function 06h: Local Disable A20 */
    _asm {
        mov ah, 06h
        call dword ptr [entry]
        mov result, ax
    }

    return (result == 1) ? 0 : XMS_ERR_A20_FAILED;
}

/**
 * @brief Check if A20 is enabled
 * @return 1 if A20 is enabled, 0 if disabled, -1 on error
 */
int xms_query_a20(void) {
    uint16_t result;
    void (far *entry)(void);

    if (!g_xms_entry) {
        return -1;
    }

    entry = g_xms_entry;

    /* XMS function 07h: Query A20 State */
    _asm {
        mov ah, 07h
        call dword ptr [entry]
        mov result, ax
    }

    return result;
}

/* ============================================================================
 * Feature Availability (Runtime Checks)
 * ============================================================================ */

/**
 * @brief Check if XMS-backed promiscuous mode is available
 * @return 1 if available, 0 if not
 */
int xms_promisc_available(void) {
    return g_promisc_xms.valid ? 1 : 0;
}

/**
 * @brief Check if XMS-backed routing is available
 * @return 1 if available, 0 if not
 */
int xms_routing_available(void) {
    return g_routing_xms.valid ? 1 : 0;
}

/**
 * @brief Get reason why XMS is not available
 * @return Human-readable string explaining unavailability
 */
const char *xms_unavailable_reason(void) {
    if (g_xms_available) {
        return NULL;
    }
    return g_xms_unavail_reason;
}
