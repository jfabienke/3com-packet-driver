/**
 * @file xms_service.c
 * @brief XMS Integration Service for Memory Pool Module
 * 
 * Agent 11 - Memory Management - Day 3-4 Deliverable
 * 
 * Provides XMS memory detection, handle management, and graceful fallback
 * to conventional memory when XMS is not available or exhausted.
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#include "../../../include/common.h"
#include "../../../include/xms_detect.h"
#include "../../../include/memory_api.h"
#include "../../../include/logging.h"
#include <dos.h>
#include <string.h>

/* XMS service state */
typedef struct {
    bool initialized;
    bool xms_available;
    uint16_t total_handles;
    uint16_t used_handles;
    uint32_t total_memory_kb;
    uint32_t used_memory_kb;
    uint32_t largest_block_kb;
    xms_handle_info_t handles[MAX_XMS_HANDLES];
} xms_service_state_t;

static xms_service_state_t g_xms_service = {0};

/* XMS service configuration */
#define XMS_MIN_BLOCK_SIZE_KB    4      /* Minimum XMS allocation */
#define XMS_ALIGNMENT_KB         1      /* XMS alignment requirement */
#define XMS_RETRY_COUNT          3      /* Allocation retry attempts */
#define XMS_LOCK_TIMEOUT_MS      100    /* Lock operation timeout */

/* Forward declarations */
static int xms_service_detect_and_init(void);
static uint16_t xms_service_allocate_handle(size_t size_kb);
static bool xms_service_free_handle(uint16_t handle);
static bool xms_service_lock_handle(uint16_t handle, void** linear_address);
static bool xms_service_unlock_handle(uint16_t handle);
static bool xms_service_get_handle_info(uint16_t handle, xms_handle_info_t* info);
static void xms_service_cleanup_all_handles(void);
static bool xms_service_validate_handle(uint16_t handle);
static size_t xms_service_get_available_memory(void);

/**
 * @brief Initialize XMS service with detection and fallback
 * @return 0 on success, negative on error
 */
int xms_service_init(void) {
    if (g_xms_service.initialized) {
        return 0;
    }
    
    log_info("XMS Service: Initializing XMS integration service");
    
    /* Clear service state */
    memset(&g_xms_service, 0, sizeof(xms_service_state_t));
    
    /* Initialize all handle slots as unused */
    for (int i = 0; i < MAX_XMS_HANDLES; i++) {
        g_xms_service.handles[i].in_use = false;
        g_xms_service.handles[i].handle = 0;
        g_xms_service.handles[i].size = 0;
        g_xms_service.handles[i].lock_count = 0;
        g_xms_service.handles[i].linear_address = NULL;
    }
    
    /* Detect and initialize XMS */
    int result = xms_service_detect_and_init();
    if (result < 0) {
        log_info("XMS Service: XMS not available, using conventional memory fallback");
        g_xms_service.xms_available = false;
    } else {
        log_info("XMS Service: XMS available, %u KB total", g_xms_service.total_memory_kb);
        g_xms_service.xms_available = true;
    }
    
    g_xms_service.initialized = true;
    return 0;
}

/**
 * @brief Cleanup XMS service and free all handles
 */
void xms_service_cleanup(void) {
    if (!g_xms_service.initialized) {
        return;
    }
    
    log_info("XMS Service: Cleaning up XMS integration service");
    
    /* Free all allocated handles */
    xms_service_cleanup_all_handles();
    
    /* Cleanup base XMS system */
    if (g_xms_service.xms_available) {
        xms_cleanup();
    }
    
    /* Clear state */
    memset(&g_xms_service, 0, sizeof(xms_service_state_t));
    
    log_info("XMS Service: Cleanup completed");
}

/**
 * @brief Allocate XMS memory with automatic fallback
 * @param size_kb Size in kilobytes
 * @param handle_out Pointer to store XMS handle
 * @return 0 on success, negative on error
 */
int xms_service_alloc(size_t size_kb, uint16_t* handle_out) {
    if (!handle_out) {
        return -1;
    }
    
    *handle_out = 0;
    
    if (!g_xms_service.initialized) {
        log_error("XMS Service: Not initialized");
        return -1;
    }
    
    /* Check if XMS is available */
    if (!g_xms_service.xms_available) {
        log_debug("XMS Service: XMS not available for %u KB allocation", size_kb);
        return -1;
    }
    
    /* Validate size */
    if (size_kb < XMS_MIN_BLOCK_SIZE_KB) {
        log_debug("XMS Service: Allocation size %u KB too small, minimum %u KB", 
                 size_kb, XMS_MIN_BLOCK_SIZE_KB);
        return -1;
    }
    
    /* Check available memory */
    size_t available_kb = xms_service_get_available_memory();
    if (size_kb > available_kb) {
        log_debug("XMS Service: Insufficient XMS memory: need %u KB, have %u KB", 
                 size_kb, available_kb);
        return -1;
    }
    
    /* Allocate handle with retry */
    uint16_t handle = 0;
    for (int retry = 0; retry < XMS_RETRY_COUNT; retry++) {
        handle = xms_service_allocate_handle(size_kb);
        if (handle != 0) {
            break;
        }
        
        log_debug("XMS Service: Allocation retry %d for %u KB", retry + 1, size_kb);
        
        /* Brief delay before retry */
        for (volatile int i = 0; i < 1000; i++);
    }
    
    if (handle == 0) {
        log_warning("XMS Service: Failed to allocate %u KB after %d retries", 
                   size_kb, XMS_RETRY_COUNT);
        return -1;
    }
    
    *handle_out = handle;
    g_xms_service.used_memory_kb += size_kb;
    
    log_debug("XMS Service: Allocated %u KB, handle %04X", size_kb, handle);
    return 0;
}

/**
 * @brief Free XMS memory handle
 * @param handle XMS handle to free
 * @return 0 on success, negative on error
 */
int xms_service_free(uint16_t handle) {
    if (!g_xms_service.initialized || !g_xms_service.xms_available) {
        return -1;
    }
    
    if (!xms_service_validate_handle(handle)) {
        log_error("XMS Service: Invalid handle %04X", handle);
        return -1;
    }
    
    /* Find handle in our tracking */
    int slot = -1;
    for (int i = 0; i < MAX_XMS_HANDLES; i++) {
        if (g_xms_service.handles[i].in_use && g_xms_service.handles[i].handle == handle) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        log_error("XMS Service: Handle %04X not found in tracking table", handle);
        return -1;
    }
    
    size_t size_kb = g_xms_service.handles[slot].size;
    
    /* Unlock if currently locked */
    if (g_xms_service.handles[slot].lock_count > 0) {
        log_warning("XMS Service: Unlocking handle %04X before freeing", handle);
        while (g_xms_service.handles[slot].lock_count > 0) {
            xms_service_unlock_handle(handle);
        }
    }
    
    /* Free the handle */
    if (!xms_service_free_handle(handle)) {
        log_error("XMS Service: Failed to free handle %04X", handle);
        return -1;
    }
    
    /* Update tracking */
    g_xms_service.handles[slot].in_use = false;
    g_xms_service.handles[slot].handle = 0;
    g_xms_service.handles[slot].size = 0;
    g_xms_service.handles[slot].lock_count = 0;
    g_xms_service.handles[slot].linear_address = NULL;
    
    g_xms_service.used_handles--;
    g_xms_service.used_memory_kb -= size_kb;
    
    log_debug("XMS Service: Freed %u KB, handle %04X", size_kb, handle);
    return 0;
}

/**
 * @brief Lock XMS handle and get linear address
 * @param handle XMS handle to lock
 * @param linear_address_out Pointer to store linear address
 * @return 0 on success, negative on error
 */
int xms_service_lock(uint16_t handle, void** linear_address_out) {
    if (!linear_address_out) {
        return -1;
    }
    
    *linear_address_out = NULL;
    
    if (!g_xms_service.initialized || !g_xms_service.xms_available) {
        return -1;
    }
    
    if (!xms_service_validate_handle(handle)) {
        return -1;
    }
    
    /* Find handle in tracking */
    int slot = -1;
    for (int i = 0; i < MAX_XMS_HANDLES; i++) {
        if (g_xms_service.handles[i].in_use && g_xms_service.handles[i].handle == handle) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        log_error("XMS Service: Handle %04X not found for locking", handle);
        return -1;
    }
    
    /* If already locked, increment count and return cached address */
    if (g_xms_service.handles[slot].lock_count > 0) {
        g_xms_service.handles[slot].lock_count++;
        *linear_address_out = g_xms_service.handles[slot].linear_address;
        log_debug("XMS Service: Handle %04X lock count now %u", 
                 handle, g_xms_service.handles[slot].lock_count);
        return 0;
    }
    
    /* Lock the handle */
    void* linear_address;
    if (!xms_service_lock_handle(handle, &linear_address)) {
        log_error("XMS Service: Failed to lock handle %04X", handle);
        return -1;
    }
    
    /* Update tracking */
    g_xms_service.handles[slot].lock_count = 1;
    g_xms_service.handles[slot].linear_address = linear_address;
    
    *linear_address_out = linear_address;
    
    log_debug("XMS Service: Locked handle %04X at linear address %p", handle, linear_address);
    return 0;
}

/**
 * @brief Unlock XMS handle
 * @param handle XMS handle to unlock
 * @return 0 on success, negative on error
 */
int xms_service_unlock(uint16_t handle) {
    if (!g_xms_service.initialized || !g_xms_service.xms_available) {
        return -1;
    }
    
    if (!xms_service_validate_handle(handle)) {
        return -1;
    }
    
    /* Find handle in tracking */
    int slot = -1;
    for (int i = 0; i < MAX_XMS_HANDLES; i++) {
        if (g_xms_service.handles[i].in_use && g_xms_service.handles[i].handle == handle) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        log_error("XMS Service: Handle %04X not found for unlocking", handle);
        return -1;
    }
    
    if (g_xms_service.handles[slot].lock_count == 0) {
        log_warning("XMS Service: Handle %04X not locked", handle);
        return 0;  /* Not an error */
    }
    
    /* Decrement lock count */
    g_xms_service.handles[slot].lock_count--;
    
    /* If still locked by other references, don't unlock yet */
    if (g_xms_service.handles[slot].lock_count > 0) {
        log_debug("XMS Service: Handle %04X lock count now %u", 
                 handle, g_xms_service.handles[slot].lock_count);
        return 0;
    }
    
    /* Actually unlock the handle */
    if (!xms_service_unlock_handle(handle)) {
        log_error("XMS Service: Failed to unlock handle %04X", handle);
        /* Restore lock count to maintain consistency */
        g_xms_service.handles[slot].lock_count = 1;
        return -1;
    }
    
    /* Clear linear address */
    g_xms_service.handles[slot].linear_address = NULL;
    
    log_debug("XMS Service: Unlocked handle %04X", handle);
    return 0;
}

/**
 * @brief Get XMS service statistics
 * @param stats Pointer to receive statistics
 * @return 0 on success, negative on error
 */
int xms_service_get_stats(memory_stats_t* stats) {
    if (!stats) {
        return -1;
    }
    
    if (!g_xms_service.initialized) {
        memset(stats, 0, sizeof(memory_stats_t));
        return -1;
    }
    
    /* Fill XMS-specific stats */
    stats->xms_total = g_xms_service.total_memory_kb * 1024;
    stats->xms_free = (g_xms_service.total_memory_kb - g_xms_service.used_memory_kb) * 1024;
    stats->xms_handles_used = g_xms_service.used_handles;
    
    /* Get current largest block */
    if (g_xms_service.xms_available) {
        xms_info_t xms_info;
        if (xms_get_info(&xms_info) == 0) {
            stats->largest_free_block = xms_info.largest_block_kb * 1024;
        }
    }
    
    return 0;
}

/**
 * @brief Check if XMS is available and initialized
 * @return true if available, false otherwise
 */
bool xms_service_is_available(void) {
    return g_xms_service.initialized && g_xms_service.xms_available;
}

/**
 * @brief Get available XMS memory size
 * @return Available memory in KB
 */
size_t xms_service_get_available_kb(void) {
    if (!g_xms_service.initialized || !g_xms_service.xms_available) {
        return 0;
    }
    
    return xms_service_get_available_memory();
}

/* === Internal Implementation Functions === */

/**
 * @brief Detect and initialize XMS system
 * @return 0 on success, negative on error
 */
static int xms_service_detect_and_init(void) {
    int result = xms_detect_and_init();
    if (result != 0) {
        return result;
    }
    
    /* Get XMS information */
    xms_info_t xms_info;
    if (xms_get_info(&xms_info) != 0) {
        log_error("XMS Service: Failed to get XMS information");
        return -1;
    }
    
    g_xms_service.total_memory_kb = xms_info.total_kb;
    g_xms_service.used_memory_kb = 0;
    g_xms_service.largest_block_kb = xms_info.largest_block_kb;
    g_xms_service.total_handles = MAX_XMS_HANDLES;
    g_xms_service.used_handles = 0;
    
    log_info("XMS Service: Detected %u KB total, largest block %u KB", 
             g_xms_service.total_memory_kb, g_xms_service.largest_block_kb);
    
    return 0;
}

/**
 * @brief Allocate XMS handle
 * @param size_kb Size in kilobytes
 * @return XMS handle, 0 on error
 */
static uint16_t xms_service_allocate_handle(size_t size_kb) {
    /* Find free slot in tracking table */
    int slot = -1;
    for (int i = 0; i < MAX_XMS_HANDLES; i++) {
        if (!g_xms_service.handles[i].in_use) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        log_error("XMS Service: No free handle slots");
        return 0;
    }
    
    /* Allocate XMS memory */
    uint16_t handle;
    if (xms_allocate(size_kb, &handle) != 0) {
        return 0;
    }
    
    /* Store in tracking table */
    g_xms_service.handles[slot].in_use = true;
    g_xms_service.handles[slot].handle = handle;
    g_xms_service.handles[slot].size = size_kb;
    g_xms_service.handles[slot].lock_count = 0;
    g_xms_service.handles[slot].linear_address = NULL;
    
    g_xms_service.used_handles++;
    
    return handle;
}

/**
 * @brief Free XMS handle
 * @param handle XMS handle to free
 * @return true on success, false on error
 */
static bool xms_service_free_handle(uint16_t handle) {
    return (xms_free(handle) == 0);
}

/**
 * @brief Lock XMS handle
 * @param handle XMS handle
 * @param linear_address Output linear address
 * @return true on success, false on error
 */
static bool xms_service_lock_handle(uint16_t handle, void** linear_address) {
    uint32_t linear_addr;
    if (xms_lock(handle, &linear_addr) != 0) {
        return false;
    }
    
    *linear_address = (void*)linear_addr;
    return true;
}

/**
 * @brief Unlock XMS handle
 * @param handle XMS handle
 * @return true on success, false on error
 */
static bool xms_service_unlock_handle(uint16_t handle) {
    return (xms_unlock(handle) == 0);
}

/**
 * @brief Validate XMS handle
 * @param handle XMS handle to validate
 * @return true if valid, false otherwise
 */
static bool xms_service_validate_handle(uint16_t handle) {
    if (handle == 0) {
        return false;
    }
    
    /* Check if handle is in our tracking table */
    for (int i = 0; i < MAX_XMS_HANDLES; i++) {
        if (g_xms_service.handles[i].in_use && g_xms_service.handles[i].handle == handle) {
            return true;
        }
    }
    
    return false;
}

/**
 * @brief Get available XMS memory
 * @return Available memory in KB
 */
static size_t xms_service_get_available_memory(void) {
    if (!g_xms_service.xms_available) {
        return 0;
    }
    
    xms_info_t xms_info;
    if (xms_get_info(&xms_info) != 0) {
        return 0;
    }
    
    return xms_info.free_kb;
}

/**
 * @brief Cleanup all allocated handles
 */
static void xms_service_cleanup_all_handles(void) {
    for (int i = 0; i < MAX_XMS_HANDLES; i++) {
        if (g_xms_service.handles[i].in_use) {
            uint16_t handle = g_xms_service.handles[i].handle;
            log_warning("XMS Service: Freeing unreleased handle %04X", handle);
            xms_service_free(handle);
        }
    }
}

/**
 * @brief Copy memory between XMS and conventional memory
 * @param dest_handle Destination handle (0 for conventional)
 * @param dest_offset Destination offset
 * @param src_handle Source handle (0 for conventional)
 * @param src_offset Source offset
 * @param length Length in bytes
 * @return 0 on success, negative on error
 */
int xms_service_copy_memory(uint16_t dest_handle, uint32_t dest_offset,
                           uint16_t src_handle, uint32_t src_offset, 
                           uint32_t length) {
    if (!g_xms_service.initialized || !g_xms_service.xms_available) {
        return -1;
    }
    
    /* Use existing XMS move function */
    return xms_move_memory(dest_handle, dest_offset, src_handle, src_offset, length);
}

/**
 * @brief Get handle information
 * @param handle XMS handle
 * @param info Output handle information
 * @return 0 on success, negative on error
 */
int xms_service_get_handle_info(uint16_t handle, xms_handle_info_t* info) {
    if (!info || !xms_service_validate_handle(handle)) {
        return -1;
    }
    
    /* Find handle in tracking */
    for (int i = 0; i < MAX_XMS_HANDLES; i++) {
        if (g_xms_service.handles[i].in_use && g_xms_service.handles[i].handle == handle) {
            *info = g_xms_service.handles[i];
            return 0;
        }
    }
    
    return -1;
}

/**
 * @brief Print XMS service status and statistics
 */
void xms_service_print_status(void) {
    if (!g_xms_service.initialized) {
        log_info("XMS Service: Not initialized");
        return;
    }
    
    log_info("=== XMS Service Status ===");
    log_info("XMS Available: %s", g_xms_service.xms_available ? "Yes" : "No");
    
    if (g_xms_service.xms_available) {
        log_info("Total Memory: %u KB", g_xms_service.total_memory_kb);
        log_info("Used Memory: %u KB", g_xms_service.used_memory_kb);
        log_info("Free Memory: %u KB", g_xms_service.total_memory_kb - g_xms_service.used_memory_kb);
        log_info("Largest Block: %u KB", g_xms_service.largest_block_kb);
        log_info("Handles Used: %u / %u", g_xms_service.used_handles, g_xms_service.total_handles);
        
        /* List active handles */
        log_info("Active Handles:");
        for (int i = 0; i < MAX_XMS_HANDLES; i++) {
            if (g_xms_service.handles[i].in_use) {
                log_info("  Handle %04X: %u KB, locks %u, addr %p",
                        g_xms_service.handles[i].handle,
                        g_xms_service.handles[i].size,
                        g_xms_service.handles[i].lock_count,
                        g_xms_service.handles[i].linear_address);
            }
        }
    }
}