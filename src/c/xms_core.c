/**
 * @file xms_core.c
 * @brief XMS memory management implementation
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * Uses INT 2Fh/4300h to detect XMS driver and INT 2Fh/4310h
 * to get XMS driver entry point. All XMS calls go through
 * the entry point via far call.
 *
 * Last Updated: 2026-01-26 14:45:00 UTC
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
 * Global XMS State
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

/* Private state */
static int g_xms_initialized = 0;
static char g_xms_unavail_reason[64];  /* Initialized at runtime to reduce _DATA segment */

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
 * XMS Block Management
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
 * A20 Line Management
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
 * High-Level Buffer Allocation
 * ============================================================================ */

/* Promiscuous mode buffer size: 64 buffers x 1616 bytes = ~102 KB */
#define PROMISC_XMS_SIZE_KB     102

/* Routing table size: ~16 KB */
#define ROUTING_XMS_SIZE_KB     16

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

/* ============================================================================
 * Feature Availability
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
