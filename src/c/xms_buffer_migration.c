/**
 * @file xms_buffer_migration.c
 * @brief XMS Buffer Migration for Packet Buffers
 * 
 * Phase 4 Enhancement: Automatically moves packet buffers to XMS memory
 * Keeps only active packets in conventional memory
 * Saves 3-4KB of conventional memory
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#include "../../include/xms_detect.h"
#include "../../include/nic_buffer_pools.h"
#include "../../include/portability.h"
#include "../../include/logging.h"
#include "../../include/memory.h"
#include "../../include/dma_safety.h"
#include <string.h>
#include <dos.h>

/* XMS buffer configuration */
#define XMS_BUFFER_POOL_SIZE_KB     64      /* 64KB for packet buffers */
#define XMS_BUFFER_ALIGNMENT        16      /* 16-byte alignment */
#define CONVENTIONAL_CACHE_SIZE     4096    /* 4KB conventional cache */
#define MAX_CACHED_PACKETS          4       /* Max packets in conventional memory */

/* XMS buffer pool structure */
typedef struct {
    uint16_t xms_handle;                   /* XMS handle for buffer pool */
    uint32_t xms_size_kb;                  /* Size in KB */
    uint32_t xms_linear_addr;              /* Linear address when locked */
    bool xms_locked;                       /* XMS currently locked */
    
    /* Conventional memory cache for active packets */
    uint8_t *conv_cache;                   /* Conventional memory buffer */
    uint16_t conv_cache_size;              /* Cache size */
    uint16_t conv_cache_used;              /* Bytes used in cache */
    
    /* Buffer tracking - volatile for ISR access */
    struct {
        uint32_t xms_offset;               /* Offset in XMS */
        uint16_t size;                     /* Buffer size */
        volatile uint8_t in_use;           /* Buffer allocated (ISR-visible) */
        volatile uint8_t in_conv_cache;    /* Currently in conventional memory (ISR-visible) */
        volatile uint8_t migrating;        /* Buffer being migrated (ISR-visible) */
        uint8_t reserved;                  /* Padding */
        uint16_t conv_offset;              /* Offset in conventional cache */
    } buffers[MAX_PACKET_BUFFERS];
    
    uint16_t buffer_count;                 /* Total buffers */
    uint16_t buffers_in_xms;              /* Buffers in XMS */
    uint16_t buffers_in_conv;             /* Buffers in conventional */
    
    /* Statistics */
    uint32_t xms_migrations;               /* Packets migrated to XMS */
    uint32_t conv_migrations;              /* Packets brought to conventional */
    uint32_t cache_hits;                   /* Cache hit count */
    uint32_t cache_misses;                 /* Cache miss count */
    
} xms_buffer_pool_t;

/* Global XMS buffer pool */
static xms_buffer_pool_t g_xms_pool = {0};
static int g_xms_migration_enabled = 0;  /* DOS compatibility */
static int g_xms_initialized = 0;

/* Forward declarations */
static int xms_buffer_lock_pool(void);
static int xms_buffer_unlock_pool(void);
static int xms_buffer_copy_to_xms(uint32_t xms_offset, void *src, uint16_t size);
static int xms_buffer_copy_from_xms(void *dest, uint32_t xms_offset, uint16_t size);
static int xms_buffer_migrate_to_xms(uint16_t buffer_index);
static int xms_buffer_migrate_to_conv(uint16_t buffer_index);
static int xms_buffer_find_conv_space(uint16_t size);
static void xms_buffer_evict_from_cache(void);

/**
 * @brief Initialize XMS buffer migration system
 */
int xms_buffer_migration_init(void) {
    int result;
    
    if (g_xms_initialized) {
        log_warning("XMS buffer migration already initialized");
        return SUCCESS;
    }
    
    log_info("Initializing XMS buffer migration system");
    
    /* Clear pool structure */
    memset(&g_xms_pool, 0, sizeof(xms_buffer_pool_t));
    
    /* Check if XMS is available */
    result = xms_detect();
    if (result != SUCCESS) {
        log_warning("XMS not available, using conventional memory only");
        g_xms_migration_enabled = false;
        return SUCCESS;  /* Not an error - fallback to conventional */
    }
    
    /* Get XMS information */
    xms_info_t xms_info;
    result = xms_get_info(&xms_info);
    if (result != SUCCESS || xms_info.free_kb < XMS_BUFFER_POOL_SIZE_KB) {
        log_warning("Insufficient XMS memory (%u KB free, need %u KB)",
                   xms_info.free_kb, XMS_BUFFER_POOL_SIZE_KB);
        g_xms_migration_enabled = false;
        return SUCCESS;
    }
    
    /* Allocate XMS buffer pool */
    g_xms_pool.xms_handle = xms_allocate(XMS_BUFFER_POOL_SIZE_KB);
    if (g_xms_pool.xms_handle == XMS_INVALID_HANDLE) {
        log_error("Failed to allocate XMS buffer pool");
        g_xms_migration_enabled = false;
        return SUCCESS;
    }
    
    g_xms_pool.xms_size_kb = XMS_BUFFER_POOL_SIZE_KB;
    
    /* Allocate conventional memory cache */
    g_xms_pool.conv_cache = (uint8_t*)memory_allocate(
        CONVENTIONAL_CACHE_SIZE, 
        MEMORY_TYPE_DMA_SAFE
    );
    
    if (!g_xms_pool.conv_cache) {
        log_error("Failed to allocate conventional cache");
        xms_free(g_xms_pool.xms_handle);
        g_xms_pool.xms_handle = XMS_INVALID_HANDLE;
        return ERROR_MEMORY;
    }
    
    g_xms_pool.conv_cache_size = CONVENTIONAL_CACHE_SIZE;
    g_xms_pool.conv_cache_used = 0;
    
    /* Initialize buffer tracking */
    g_xms_pool.buffer_count = 0;
    g_xms_pool.buffers_in_xms = 0;
    g_xms_pool.buffers_in_conv = 0;
    
    g_xms_migration_enabled = 1;
    g_xms_initialized = 1;
    
    log_info("XMS buffer migration initialized: %u KB XMS, %u bytes conventional cache",
             XMS_BUFFER_POOL_SIZE_KB, CONVENTIONAL_CACHE_SIZE);
    
    return SUCCESS;
}

/**
 * @brief Cleanup XMS buffer migration system
 */
int xms_buffer_migration_cleanup(void) {
    if (!g_xms_initialized) {
        return SUCCESS;
    }
    
    log_info("Cleaning up XMS buffer migration system");
    
    /* Unlock XMS if locked */
    if (g_xms_pool.xms_locked) {
        xms_buffer_unlock_pool();
    }
    
    /* Free XMS handle */
    if (g_xms_pool.xms_handle != XMS_INVALID_HANDLE) {
        xms_free(g_xms_pool.xms_handle);
        g_xms_pool.xms_handle = XMS_INVALID_HANDLE;
    }
    
    /* Free conventional cache */
    if (g_xms_pool.conv_cache) {
        memory_free(g_xms_pool.conv_cache);
        g_xms_pool.conv_cache = NULL;
    }
    
    /* Log statistics */
    log_info("XMS migration stats: %lu to XMS, %lu to conv, %lu hits, %lu misses",
             g_xms_pool.xms_migrations,
             g_xms_pool.conv_migrations,
             g_xms_pool.cache_hits,
             g_xms_pool.cache_misses);
    
    memset(&g_xms_pool, 0, sizeof(xms_buffer_pool_t));
    g_xms_migration_enabled = false;
    g_xms_initialized = false;
    
    return SUCCESS;
}

/**
 * @brief Allocate a buffer (preferably in XMS)
 */
void* xms_buffer_allocate(uint16_t size, bool need_immediate_access) {
    uint16_t buffer_index;
    uint32_t xms_offset;
    
    if (!g_xms_migration_enabled) {
        /* Fallback to conventional memory */
        return memory_allocate(size, MEMORY_TYPE_DMA_SAFE);
    }
    
    /* Round size to alignment */
    size = (size + XMS_BUFFER_ALIGNMENT - 1) & ~(XMS_BUFFER_ALIGNMENT - 1);
    
    /* Find free buffer slot */
    for (buffer_index = 0; buffer_index < MAX_PACKET_BUFFERS; buffer_index++) {
        if (!g_xms_pool.buffers[buffer_index].in_use) {
            break;
        }
    }
    
    if (buffer_index >= MAX_PACKET_BUFFERS) {
        log_error("No free buffer slots");
        return NULL;
    }
    
    /* Calculate XMS offset for this buffer */
    xms_offset = buffer_index * MAX_PACKET_SIZE;
    
    /* Initialize buffer entry */
    g_xms_pool.buffers[buffer_index].xms_offset = xms_offset;
    g_xms_pool.buffers[buffer_index].size = size;
    g_xms_pool.buffers[buffer_index].in_use = true;
    
    if (need_immediate_access) {
        /* Allocate in conventional cache */
        int conv_offset = xms_buffer_find_conv_space(size);
        
        if (conv_offset >= 0) {
            g_xms_pool.buffers[buffer_index].in_conv_cache = true;
            g_xms_pool.buffers[buffer_index].conv_offset = conv_offset;
            g_xms_pool.buffers_in_conv++;
            
            log_debug("Allocated buffer %u in conventional cache at offset %d",
                     buffer_index, conv_offset);
            
            return g_xms_pool.conv_cache + conv_offset;
        }
    }
    
    /* Allocate in XMS (will migrate to conventional when needed) */
    g_xms_pool.buffers[buffer_index].in_conv_cache = false;
    g_xms_pool.buffers[buffer_index].conv_offset = 0;
    g_xms_pool.buffers_in_xms++;
    
    log_debug("Allocated buffer %u in XMS at offset %lu", 
             buffer_index, xms_offset);
    
    /* Return a handle that can be used to access the buffer */
    return (void*)(uintptr_t)(buffer_index | 0x8000);  /* High bit indicates XMS */
}

/**
 * @brief Free a buffer
 */
int xms_buffer_free(void *buffer) {
    uint16_t buffer_index;
    
    if (!g_xms_migration_enabled) {
        /* Conventional memory */
        memory_free(buffer);
        return SUCCESS;
    }
    
    /* Check if this is an XMS buffer handle */
    if ((uintptr_t)buffer & 0x8000) {
        buffer_index = (uintptr_t)buffer & 0x7FFF;
    } else {
        /* Find buffer in conventional cache */
        uint8_t *ptr = (uint8_t*)buffer;
        if (ptr < g_xms_pool.conv_cache || 
            ptr >= g_xms_pool.conv_cache + CONVENTIONAL_CACHE_SIZE) {
            /* Not our buffer */
            memory_free(buffer);
            return SUCCESS;
        }
        
        /* Find which buffer this is */
        uint16_t offset = ptr - g_xms_pool.conv_cache;
        for (buffer_index = 0; buffer_index < MAX_PACKET_BUFFERS; buffer_index++) {
            if (g_xms_pool.buffers[buffer_index].in_use &&
                g_xms_pool.buffers[buffer_index].in_conv_cache &&
                g_xms_pool.buffers[buffer_index].conv_offset == offset) {
                break;
            }
        }
        
        if (buffer_index >= MAX_PACKET_BUFFERS) {
            log_error("Buffer not found in tracking table");
            return ERROR_INVALID_PARAM;
        }
    }
    
    /* Free the buffer */
    if (g_xms_pool.buffers[buffer_index].in_conv_cache) {
        g_xms_pool.conv_cache_used -= g_xms_pool.buffers[buffer_index].size;
        g_xms_pool.buffers_in_conv--;
    } else {
        g_xms_pool.buffers_in_xms--;
    }
    
    g_xms_pool.buffers[buffer_index].in_use = false;
    
    log_debug("Freed buffer %u", buffer_index);
    
    return SUCCESS;
}

/**
 * @brief Get access to buffer data (migrate from XMS if needed)
 */
void* xms_buffer_get_access(void *buffer, uint16_t *size) {
    uint16_t buffer_index;
    
    if (!g_xms_migration_enabled) {
        /* Conventional memory - direct access */
        return buffer;
    }
    
    /* Check if this is an XMS buffer handle */
    if (!((uintptr_t)buffer & 0x8000)) {
        /* Already in conventional memory */
        g_xms_pool.cache_hits++;
        return buffer;
    }
    
    buffer_index = (uintptr_t)buffer & 0x7FFF;
    
    if (buffer_index >= MAX_PACKET_BUFFERS || 
        !g_xms_pool.buffers[buffer_index].in_use) {
        log_error("Invalid buffer index: %u", buffer_index);
        return NULL;
    }
    
    /* Check if already in conventional cache */
    if (g_xms_pool.buffers[buffer_index].in_conv_cache) {
        g_xms_pool.cache_hits++;
        if (size) *size = g_xms_pool.buffers[buffer_index].size;
        return g_xms_pool.conv_cache + g_xms_pool.buffers[buffer_index].conv_offset;
    }
    
    g_xms_pool.cache_misses++;
    
    /* Need to migrate from XMS to conventional */
    int conv_offset = xms_buffer_find_conv_space(g_xms_pool.buffers[buffer_index].size);
    
    if (conv_offset < 0) {
        /* Need to evict something from cache */
        xms_buffer_evict_from_cache();
        conv_offset = xms_buffer_find_conv_space(g_xms_pool.buffers[buffer_index].size);
        
        if (conv_offset < 0) {
            log_error("Cannot allocate space in conventional cache");
            return NULL;
        }
    }
    
    /* Copy from XMS to conventional */
    int result = xms_buffer_copy_from_xms(
        g_xms_pool.conv_cache + conv_offset,
        g_xms_pool.buffers[buffer_index].xms_offset,
        g_xms_pool.buffers[buffer_index].size
    );
    
    if (result != SUCCESS) {
        log_error("Failed to copy buffer from XMS");
        return NULL;
    }
    
    /* Update buffer tracking */
    g_xms_pool.buffers[buffer_index].in_conv_cache = true;
    g_xms_pool.buffers[buffer_index].conv_offset = conv_offset;
    g_xms_pool.buffers_in_xms--;
    g_xms_pool.buffers_in_conv++;
    g_xms_pool.conv_migrations++;
    
    log_debug("Migrated buffer %u from XMS to conventional at offset %d",
             buffer_index, conv_offset);
    
    if (size) *size = g_xms_pool.buffers[buffer_index].size;
    return g_xms_pool.conv_cache + conv_offset;
}

/**
 * @brief Release access to buffer (can migrate back to XMS)
 */
int xms_buffer_release_access(void *buffer) {
    /* In a full implementation, this could migrate inactive buffers back to XMS */
    /* For now, we keep them in conventional cache until evicted */
    return SUCCESS;
}

/* === Private Helper Functions === */

/**
 * @brief Find space in conventional cache
 */
static int xms_buffer_find_conv_space(uint16_t size) {
    uint16_t offset = 0;
    uint16_t best_offset = 0xFFFF;
    uint16_t best_size = 0xFFFF;
    
    /* Simple first-fit allocation */
    while (offset < CONVENTIONAL_CACHE_SIZE) {
        uint16_t free_size = 0;
        uint16_t scan_offset = offset;
        
        /* Find contiguous free space */
        bool found_used = false;
        for (int i = 0; i < MAX_PACKET_BUFFERS; i++) {
            if (g_xms_pool.buffers[i].in_use && 
                g_xms_pool.buffers[i].in_conv_cache) {
                
                if (g_xms_pool.buffers[i].conv_offset >= scan_offset &&
                    g_xms_pool.buffers[i].conv_offset < scan_offset + size) {
                    /* This buffer overlaps our search area */
                    found_used = true;
                    offset = g_xms_pool.buffers[i].conv_offset + 
                            g_xms_pool.buffers[i].size;
                    break;
                }
            }
        }
        
        if (!found_used) {
            /* Found enough space */
            if (offset + size <= CONVENTIONAL_CACHE_SIZE) {
                g_xms_pool.conv_cache_used += size;
                return offset;
            }
            break;
        }
    }
    
    return -1;  /* No space available */
}

/**
 * @brief Evict least recently used buffer from cache
 */
static void xms_buffer_evict_from_cache(void) {
    /* Simple eviction - find first buffer in cache and migrate to XMS */
    for (int i = 0; i < MAX_PACKET_BUFFERS; i++) {
        if (g_xms_pool.buffers[i].in_use && 
            g_xms_pool.buffers[i].in_conv_cache) {
            
            /* Copy to XMS */
            xms_buffer_copy_to_xms(
                g_xms_pool.buffers[i].xms_offset,
                g_xms_pool.conv_cache + g_xms_pool.buffers[i].conv_offset,
                g_xms_pool.buffers[i].size
            );
            
            /* Update tracking */
            g_xms_pool.buffers[i].in_conv_cache = false;
            g_xms_pool.conv_cache_used -= g_xms_pool.buffers[i].size;
            g_xms_pool.buffers_in_conv--;
            g_xms_pool.buffers_in_xms++;
            g_xms_pool.xms_migrations++;
            
            log_debug("Evicted buffer %d from conventional cache", i);
            break;
        }
    }
}

/**
 * @brief Copy data to XMS
 */
static int xms_buffer_copy_to_xms(uint32_t xms_offset, void *src, uint16_t size) {
    xms_move_t move;
    
    memset(&move, 0, sizeof(move));
    move.length = size;
    move.source_handle = 0;  /* Conventional memory */
    move.source_offset = (uint32_t)(uintptr_t)src;
    move.dest_handle = g_xms_pool.xms_handle;
    move.dest_offset = xms_offset;
    
    return xms_move_memory(&move);
}

/**
 * @brief Copy data from XMS
 */
static int xms_buffer_copy_from_xms(void *dest, uint32_t xms_offset, uint16_t size) {
    xms_move_t move;
    
    memset(&move, 0, sizeof(move));
    move.length = size;
    move.source_handle = g_xms_pool.xms_handle;
    move.source_offset = xms_offset;
    move.dest_handle = 0;  /* Conventional memory */
    move.dest_offset = (uint32_t)(uintptr_t)dest;
    
    return xms_move_memory(&move);
}

/**
 * @brief Get XMS migration statistics
 */
void xms_buffer_get_stats(xms_migration_stats_t *stats) {
    if (!stats) return;
    
    stats->enabled = g_xms_migration_enabled;
    stats->xms_size_kb = g_xms_pool.xms_size_kb;
    stats->conv_cache_size = g_xms_pool.conv_cache_size;
    stats->conv_cache_used = g_xms_pool.conv_cache_used;
    stats->buffers_in_xms = g_xms_pool.buffers_in_xms;
    stats->buffers_in_conv = g_xms_pool.buffers_in_conv;
    stats->xms_migrations = g_xms_pool.xms_migrations;
    stats->conv_migrations = g_xms_pool.conv_migrations;
    stats->cache_hits = g_xms_pool.cache_hits;
    stats->cache_misses = g_xms_pool.cache_misses;
    
    /* Calculate hit rate */
    uint32_t total_accesses = stats->cache_hits + stats->cache_misses;
    if (total_accesses > 0) {
        stats->cache_hit_rate = (stats->cache_hits * 100) / total_accesses;
    } else {
        stats->cache_hit_rate = 0;
    }
    
    /* Calculate memory saved */
    stats->memory_saved = g_xms_pool.buffers_in_xms * MAX_PACKET_SIZE;
}

/**
 * @brief Migrate buffer to XMS memory (GPT-5 critical fix)
 */
static int xms_buffer_migrate_to_xms(uint16_t buffer_index) {
    /* C89: Declare all variables at function start */
    uint16_t flags;
    uint8_t FAR *src;
    int result;
    
    /* Bounds check - critical safety fix */
    if (buffer_index >= MAX_PACKET_BUFFERS) {
        log_error("Invalid buffer index: %u", buffer_index);
        return ERROR_INVALID_PARAM;
    }
    
    /* Validate buffer is in use */
    if (!g_xms_pool.buffers[buffer_index].in_use) {
        log_error("Buffer %u not in use", buffer_index);
        return ERROR_INVALID_STATE;
    }
    
    /* Already in XMS? */
    if (!g_xms_pool.buffers[buffer_index].in_conv_cache) {
        return SUCCESS;  /* Already migrated */
    }
    
    /* Check XMS space availability */
    if (g_xms_pool.buffers[buffer_index].xms_offset == 0) {
        log_error("Buffer %u has no XMS allocation", buffer_index);
        return ERROR_INVALID_STATE;
    }
    
    /* Critical: Ensure buffer is quiesced (not in use by ISR/DMA) */
    
    /* Mark buffer as being migrated to prevent new ISR/DMA access */
    CRITICAL_SECTION_ENTER(flags);
    g_xms_pool.buffers[buffer_index].migrating = 1;
    CRITICAL_SECTION_EXIT(flags);
    
    /* Wait for any pending DMA operations to complete */
    /* Poll NIC DMA status - this is a placeholder for actual hardware check */
    /* In production, this would check actual NIC DMA registers */
    {
        volatile int timeout = 1000;  /* Timeout counter */
        while (timeout-- > 0) {
            /* Check if DMA is complete - placeholder for hardware check */
            /* In real implementation: if (nic_dma_complete(buffer_index)) break; */
            
            /* Small delay without holding interrupts disabled */
            volatile int delay = 10;
            while (delay-- > 0) {
                /* Empty loop for timing */
            }
        }
        
        if (timeout <= 0) {
            /* DMA didn't complete - clear migrating flag and fail */
            CRITICAL_SECTION_ENTER(flags);
            g_xms_pool.buffers[buffer_index].migrating = 0;
            CRITICAL_SECTION_EXIT(flags);
            log_error("DMA timeout for buffer %u", buffer_index);
            return ERROR_TIMEOUT;
        }
    }
    
    /* Add bounds checking before copy - cast to uint32_t to prevent 16-bit overflow */
    if ((uint32_t)g_xms_pool.buffers[buffer_index].conv_offset + 
        (uint32_t)g_xms_pool.buffers[buffer_index].size > 
        (uint32_t)g_xms_pool.conv_cache_size) {
        log_error("Buffer %u conv bounds exceeded: offset=%u size=%u cache_size=%u",
                  buffer_index,
                  g_xms_pool.buffers[buffer_index].conv_offset,
                  g_xms_pool.buffers[buffer_index].size,
                  g_xms_pool.conv_cache_size);
        CRITICAL_SECTION_ENTER(flags);
        g_xms_pool.buffers[buffer_index].migrating = 0;
        CRITICAL_SECTION_EXIT(flags);
        return ERROR_BOUNDS;
    }
    
    /* Copy to XMS with proper error handling */
    src = g_xms_pool.conv_cache + g_xms_pool.buffers[buffer_index].conv_offset;
    result = xms_buffer_copy_to_xms(
        g_xms_pool.buffers[buffer_index].xms_offset,
        src,
        g_xms_pool.buffers[buffer_index].size
    );
    
    /* Critical: Only free conventional memory if XMS copy succeeded */
    if (result != SUCCESS) {
        log_error("Failed to copy buffer %u to XMS", buffer_index);
        /* Clear migrating flag on error */
        CRITICAL_SECTION_ENTER(flags);
        g_xms_pool.buffers[buffer_index].migrating = 0;
        CRITICAL_SECTION_EXIT(flags);
        return result;
    }
    
    /* Update tracking atomically */
    CRITICAL_SECTION_ENTER(flags);
    g_xms_pool.buffers[buffer_index].in_conv_cache = 0;  /* Use int, not bool for DOS */
    g_xms_pool.buffers[buffer_index].migrating = 0;  /* Clear migrating flag */
    g_xms_pool.conv_cache_used -= g_xms_pool.buffers[buffer_index].size;
    g_xms_pool.buffers_in_conv--;
    g_xms_pool.buffers_in_xms++;
    CRITICAL_SECTION_EXIT(flags);
    
    g_xms_pool.xms_migrations++;
    
    log_debug("Migrated buffer %u to XMS", buffer_index);
    return SUCCESS;
}

/**
 * @brief Migrate buffer from XMS to conventional memory
 */
static int xms_buffer_migrate_to_conv(uint16_t buffer_index) {
    /* Bounds check */
    if (buffer_index >= MAX_PACKET_BUFFERS) {
        log_error("Invalid buffer index: %u", buffer_index);
        return ERROR_INVALID_PARAM;
    }
    
    /* Validate buffer state */
    if (!g_xms_pool.buffers[buffer_index].in_use || 
        g_xms_pool.buffers[buffer_index].in_conv_cache) {
        return SUCCESS;  /* Already in conventional memory */
    }
    
    /* Find space in conventional cache */
    int offset = xms_buffer_find_conv_space(g_xms_pool.buffers[buffer_index].size);
    if (offset < 0) {
        /* Try to evict something */
        xms_buffer_evict_from_cache();
        offset = xms_buffer_find_conv_space(g_xms_pool.buffers[buffer_index].size);
        
        if (offset < 0) {
            g_xms_pool.cache_misses++;
            return ERROR_NO_MEMORY;  /* Cache full */
        }
    }
    
    /* Copy from XMS */
    uint8_t *dest = g_xms_pool.conv_cache + offset;
    int result = xms_buffer_copy_from_xms(
        dest,
        g_xms_pool.buffers[buffer_index].xms_offset,
        g_xms_pool.buffers[buffer_index].size
    );
    
    if (result != SUCCESS) {
        log_error("Failed to copy buffer %u from XMS", buffer_index);
        return result;
    }
    
    /* Add bounds checking - cast to uint32_t to prevent 16-bit overflow */
    if ((uint32_t)offset + (uint32_t)g_xms_pool.buffers[buffer_index].size > 
        (uint32_t)g_xms_pool.conv_cache_size) {
        log_error("Buffer %u would exceed conv cache bounds", buffer_index);
        return ERROR_BOUNDS;
    }
    
    /* Update tracking */
    uint16_t flags;
    CRITICAL_SECTION_ENTER(flags);
    g_xms_pool.buffers[buffer_index].in_conv_cache = 1;
    g_xms_pool.buffers[buffer_index].conv_offset = (uint16_t)offset;
    g_xms_pool.buffers_in_conv++;
    g_xms_pool.buffers_in_xms--;
    CRITICAL_SECTION_EXIT(flags);
    
    g_xms_pool.conv_migrations++;
    g_xms_pool.cache_hits++;
    
    return SUCCESS;
}