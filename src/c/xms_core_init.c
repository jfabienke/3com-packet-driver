/**
 * @file xms_core_init.c
 * @brief XMS memory management - Initialization Functions (OVERLAY Segment)
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * Created: 2026-01-28 08:31:40 UTC
 *
 * This file contains XMS subsystem initialization, allocation, and cleanup
 * functions. These functions are only called during driver startup/shutdown
 * and can be placed in an overlay segment to save memory during normal
 * operation.
 *
 * Functions included:
 * - xms_init (XMS driver detection and initialization)
 * - xms_shutdown (XMS cleanup)
 * - xms_alloc / xms_free (XMS block allocation/deallocation)
 * - High-level buffer management (promisc buffers, routing tables)
 *
 * Split from xms_core.c for memory segmentation optimization.
 * Runtime functions are in xms_core_rt.c (ROOT segment).
 */

#include <stdio.h>
#include <string.h>
#include <dos.h>
#include "common.h"
#include "xms_alloc.h"
#include "init_context.h"
#include "logging.h"
#include "cpudet.h"

/* ============================================================================
 * External declarations for global state (defined in xms_core_rt.c)
 * ============================================================================ */

extern int g_xms_available;
extern uint16_t g_xms_version;
extern uint32_t g_xms_free_kb;
extern uint32_t g_xms_largest_block_kb;
extern void (far *g_xms_entry)(void);
extern xms_block_t g_promisc_xms;
extern xms_block_t g_routing_xms;

/* ============================================================================
 * Private State (owned by this file, extern'd to xms_core_rt.c)
 * ============================================================================ */

int g_xms_initialized = 0;
char g_xms_unavail_reason[64];  /* Initialized at runtime to reduce _DATA segment */

/* ============================================================================
 * Buffer Size Constants
 * ============================================================================ */

/* Promiscuous mode buffer size: 64 buffers x 1616 bytes = ~102 KB */
#define PROMISC_XMS_SIZE_KB     102

/* Routing table size: ~16 KB */
#define ROUTING_XMS_SIZE_KB     16

/* ============================================================================
 * Low-Level XMS Functions (internal to init)
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

/**
 * @brief Call XMS function with extended parameters
 * @param func XMS function code (AH register)
 * @param dx DX register value
 * @param bx Output: BX register after call
 * @param dx_out Output: DX register after call
 * @return AX result
 */
static uint16_t xms_call_ex(uint8_t func, uint16_t dx_in,
                            uint16_t *bx, uint16_t *dx_out) {
    uint16_t result;
    uint16_t bx_val;
    uint16_t dx_val;
    void (far *entry)(void) = g_xms_entry;

    if (!entry) {
        return 0;
    }

    _asm {
        mov ah, func
        mov dx, dx_in
        call dword ptr [entry]
        mov result, ax
        mov bx_val, bx
        mov dx_val, dx
    }

    if (bx) *bx = bx_val;
    if (dx_out) *dx_out = dx_val;

    return result;
}

/* ============================================================================
 * XMS Initialization
 * ============================================================================ */

/**
 * @brief Initialize XMS subsystem
 * @return 0 if XMS available and initialized, negative error code if not
 */
int xms_init(void) {
    union REGS regs;
    struct SREGS sregs;
    uint16_t bx_val;
    uint16_t dx_val;

    /* Already initialized? */
    if (g_xms_initialized) {
        return g_xms_available ? 0 : XMS_ERR_NOT_AVAILABLE;
    }

    /* Initialize reason string (moved from static init to reduce _DATA segment) */
    strcpy(g_xms_unavail_reason, "Not initialized");

    g_xms_initialized = 1;
    g_xms_available = 0;

    /* Check CPU type - XMS requires 286+ (but only useful with 386+) */
    /* We check for 386+ since that's when A20/extended memory is practical */
    if (g_init_ctx.cpu_type < CPU_TYPE_80386) {
        strncpy(g_xms_unavail_reason, "Requires 386+ CPU", sizeof(g_xms_unavail_reason));
        LOG_INFO("XMS disabled: %s", g_xms_unavail_reason);
        return XMS_ERR_CPU_NOT_SUPPORTED;
    }

    /* Check for XMS driver presence (INT 2Fh AX=4300h) */
    regs.x.ax = 0x4300;
    int86(0x2F, &regs, &regs);

    if (regs.h.al != 0x80) {
        strncpy(g_xms_unavail_reason, "XMS driver not installed", sizeof(g_xms_unavail_reason));
        LOG_INFO("XMS disabled: %s", g_xms_unavail_reason);
        return XMS_ERR_NOT_AVAILABLE;
    }

    /* Get XMS driver entry point (INT 2Fh AX=4310h) */
    regs.x.ax = 0x4310;
    segread(&sregs);
    int86x(0x2F, &regs, &regs, &sregs);

    /* Entry point is returned in ES:BX */
    g_xms_entry = (void (far *)(void))MK_FP(sregs.es, regs.x.bx);

    if (!g_xms_entry) {
        strncpy(g_xms_unavail_reason, "Failed to get XMS entry point", sizeof(g_xms_unavail_reason));
        LOG_ERROR("XMS disabled: %s", g_xms_unavail_reason);
        return XMS_ERR_NOT_AVAILABLE;
    }

    /* Get XMS version (function 00h) */
    g_xms_version = xms_call(XMS_GET_VERSION, 0);

    /* Check minimum version (2.0) */
    if ((g_xms_version >> 8) < XMS_MIN_VERSION_MAJOR) {
        snprintf(g_xms_unavail_reason, sizeof(g_xms_unavail_reason),
                "XMS version %d.%02d < 2.0 required",
                g_xms_version >> 8, g_xms_version & 0xFF);
        LOG_WARNING("XMS disabled: %s", g_xms_unavail_reason);
        g_xms_entry = NULL;
        return XMS_ERR_VERSION_TOO_OLD;
    }

    /* Query free extended memory (function 08h) */
    /* Returns: AX = largest free block (KB), DX = total free (KB) */
    xms_call_ex(XMS_QUERY_FREE_EMB, 0, &bx_val, &dx_val);

    /* Note: For function 08h, the result is in AX (largest) and DX (total) */
    /* But our call puts them in bx_val and dx_val respectively */
    /* Actually, let's call it properly */
    {
        uint16_t ax_result;
        uint16_t dx_result;
        void (far *entry)(void) = g_xms_entry;

        _asm {
            mov ah, 08h         ; XMS_QUERY_FREE_EMB
            call dword ptr [entry]
            mov ax_result, ax   ; Largest free block (KB)
            mov dx_result, dx   ; Total free (KB)
        }

        g_xms_largest_block_kb = ax_result;
        g_xms_free_kb = dx_result;
    }

    /* Mark XMS as available */
    g_xms_available = 1;
    g_xms_unavail_reason[0] = '\0';

    /* Update init context */
    g_init_ctx.xms_available = 1;
    g_init_ctx.xms_version_major = (uint8_t)(g_xms_version >> 8);
    g_init_ctx.xms_version_minor = (uint8_t)(g_xms_version & 0xFF);
    g_init_ctx.xms_free_kb = g_xms_free_kb;

    LOG_INFO("XMS initialized: version %d.%02d, %lu KB free (largest block: %lu KB)",
             g_xms_version >> 8, g_xms_version & 0xFF,
             g_xms_free_kb, g_xms_largest_block_kb);

    return 0;
}

/**
 * @brief Shutdown XMS subsystem
 */
void xms_shutdown(void) {
    /* Free any allocated blocks */
    xms_free_promisc_buffers();
    xms_free_routing_tables();

    /* Reset state */
    g_xms_available = 0;
    g_xms_entry = NULL;
    g_xms_version = 0;
    g_xms_free_kb = 0;
    g_xms_largest_block_kb = 0;
    g_xms_initialized = 0;

    strncpy(g_xms_unavail_reason, "Shutdown", sizeof(g_xms_unavail_reason));

    LOG_DEBUG("XMS shutdown complete");
}

/* ============================================================================
 * XMS Block Management (Initialization-Time Operations)
 * ============================================================================ */

/**
 * @brief Allocate XMS memory block
 * @param size_kb Size to allocate in KB (1-65535)
 * @param block Output structure with handle and info
 * @return 0 on success, negative error code on failure
 */
int xms_alloc(uint16_t size_kb, xms_block_t *block) {
    uint16_t result;
    uint16_t handle = 0;
    void (far *entry)(void);

    if (!block) {
        return XMS_ERR_INVALID_HANDLE;
    }

    /* Initialize output structure */
    memset(block, 0, sizeof(xms_block_t));

    if (!g_xms_available || !g_xms_entry) {
        return XMS_ERR_NOT_AVAILABLE;
    }

    if (size_kb == 0) {
        return XMS_ERR_ALLOC_FAILED;
    }

    entry = g_xms_entry;

    /* XMS function 09h: Allocate Extended Memory Block */
    /* Input: AH = 09h, DX = size in KB */
    /* Output: AX = 1 success, 0 failure; DX = handle */
    _asm {
        mov ah, 09h
        mov dx, size_kb
        call dword ptr [entry]
        mov result, ax
        mov handle, dx
    }

    if (result != 1) {
        LOG_ERROR("XMS alloc failed: requested %u KB", size_kb);
        return XMS_ERR_ALLOC_FAILED;
    }

    /* Fill in block structure */
    block->handle = handle;
    block->size = (uint32_t)size_kb * 1024;
    block->xms_address = 0;  /* Set by lock */
    block->lock_count = 0;
    block->valid = 1;
    block->locked = 0;

    /* Update free memory tracking */
    if (g_xms_free_kb >= size_kb) {
        g_xms_free_kb -= size_kb;
    }

    LOG_DEBUG("XMS allocated: handle=%u, size=%u KB", handle, size_kb);

    return 0;
}

/**
 * @brief Free XMS memory block
 * @param block Block to free
 */
void xms_free(xms_block_t *block) {
    uint16_t result;
    uint16_t handle;
    void (far *entry)(void);
    uint32_t size_kb;

    /* External declaration for xms_unlock from xms_core_rt.c */
    extern int xms_unlock(xms_block_t *block);

    if (!block || !block->valid || block->handle == 0) {
        return;
    }

    if (!g_xms_entry) {
        return;
    }

    /* Unlock if locked */
    if (block->locked) {
        xms_unlock(block);
    }

    handle = block->handle;
    size_kb = block->size / 1024;
    entry = g_xms_entry;

    /* XMS function 0Ah: Free Extended Memory Block */
    /* Input: AH = 0Ah, DX = handle */
    /* Output: AX = 1 success, 0 failure */
    _asm {
        mov ah, 0Ah
        mov dx, handle
        call dword ptr [entry]
        mov result, ax
    }

    if (result != 1) {
        LOG_WARNING("XMS free failed for handle %u", handle);
    } else {
        LOG_DEBUG("XMS freed: handle=%u, size=%lu KB", handle, size_kb);
        g_xms_free_kb += (uint32_t)size_kb;
    }

    /* Clear block structure */
    memset(block, 0, sizeof(xms_block_t));
}

/* ============================================================================
 * High-Level Buffer Allocation (Initialization-Time)
 * ============================================================================ */

/**
 * @brief Allocate promiscuous mode buffers from XMS
 * @return 0 on success, negative error code on failure
 */
int xms_alloc_promisc_buffers(void) {
    int result;

    if (!g_xms_available) {
        LOG_INFO("Promiscuous mode disabled (no XMS)");
        return PROMISC_NO_XMS;
    }

    /* Check if already allocated */
    if (g_promisc_xms.valid) {
        return 0;
    }

    /* Check if enough memory */
    if (g_xms_largest_block_kb < PROMISC_XMS_SIZE_KB) {
        LOG_WARNING("Not enough XMS for promiscuous buffers (%lu KB < %d KB needed)",
                   g_xms_largest_block_kb, PROMISC_XMS_SIZE_KB);
        return PROMISC_ALLOC_FAILED;
    }

    /* Allocate */
    result = xms_alloc(PROMISC_XMS_SIZE_KB, &g_promisc_xms);
    if (result != 0) {
        LOG_ERROR("Failed to allocate promiscuous XMS buffers");
        return PROMISC_ALLOC_FAILED;
    }

    LOG_INFO("Promiscuous mode buffers allocated: %d KB from XMS",
             PROMISC_XMS_SIZE_KB);

    return 0;
}

/**
 * @brief Free promiscuous mode buffers
 */
void xms_free_promisc_buffers(void) {
    if (g_promisc_xms.valid) {
        xms_free(&g_promisc_xms);
        LOG_DEBUG("Promiscuous XMS buffers freed");
    }
}

/**
 * @brief Allocate routing tables from XMS
 * @return 0 on success, negative error code on failure
 */
int xms_alloc_routing_tables(void) {
    int result;

    if (!g_xms_available) {
        LOG_INFO("Advanced routing disabled (no XMS)");
        return ROUTING_NO_XMS;
    }

    /* Check if already allocated */
    if (g_routing_xms.valid) {
        return 0;
    }

    /* Check if enough memory */
    if (g_xms_largest_block_kb < ROUTING_XMS_SIZE_KB) {
        LOG_WARNING("Not enough XMS for routing tables (%lu KB < %d KB needed)",
                   g_xms_largest_block_kb, ROUTING_XMS_SIZE_KB);
        return ROUTING_ALLOC_FAILED;
    }

    /* Allocate */
    result = xms_alloc(ROUTING_XMS_SIZE_KB, &g_routing_xms);
    if (result != 0) {
        LOG_ERROR("Failed to allocate routing XMS tables");
        return ROUTING_ALLOC_FAILED;
    }

    LOG_INFO("Routing tables allocated: %d KB from XMS", ROUTING_XMS_SIZE_KB);

    return 0;
}

/**
 * @brief Free routing tables
 */
void xms_free_routing_tables(void) {
    if (g_routing_xms.valid) {
        xms_free(&g_routing_xms);
        LOG_DEBUG("Routing XMS tables freed");
    }
}
