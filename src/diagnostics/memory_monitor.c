/**
 * @file memory_monitor.c
 * @brief Memory usage monitoring and leak detection system
 * 
 * 3Com Packet Driver - Diagnostics Agent - Week 1
 * Implements comprehensive memory monitoring, leak detection, and optimization analysis
 */

#include "../../include/diagnostics.h"
#include "../../include/common.h"
#include "../../include/memory.h"
#include "../../include/xms_detect.h"
#include "../loader/tsr_memory.h"
#include "../../docs/agents/shared/error-codes.h"
#include <string.h>
#include <stdio.h>

/* Memory monitoring configuration */
#define MAX_ALLOCATION_TRACKING     1000
#define MEMORY_SNAPSHOT_INTERVAL    10000   /* 10 seconds */
#define LEAK_DETECTION_THRESHOLD    10      /* 10 unmatched allocations */
#define FRAGMENTATION_THRESHOLD     50      /* 50% fragmentation */

/* Memory allocation tracking entry */
typedef struct allocation_entry {
    void *ptr;
    uint32_t size;
    uint32_t timestamp;
    const char *file;
    const char *function;
    uint32_t line;
    uint8_t memory_type;        /* Conventional, XMS, UMB */
    bool freed;
    struct allocation_entry *next;
} allocation_entry_t;

/* Memory pool statistics */
typedef struct memory_pool_stats {
    uint32_t pool_size;
    uint32_t allocated_bytes;
    uint32_t free_bytes;
    uint32_t largest_free_block;
    uint32_t allocation_count;
    uint32_t fragmentation_percent;
    uint32_t peak_usage;
    uint32_t allocation_failures;
} memory_pool_stats_t;

/* Memory snapshot for trend analysis */
typedef struct memory_snapshot {
    uint32_t timestamp;
    uint32_t conventional_used;
    uint32_t conventional_free;
    uint32_t xms_used;
    uint32_t xms_free;
    uint32_t umb_used;
    uint32_t umb_free;
    uint32_t active_allocations;
    uint32_t fragmentation_score;
    struct memory_snapshot *next;
} memory_snapshot_t;

/* Memory pressure levels */
typedef enum {
    MEMORY_PRESSURE_NONE = 0,
    MEMORY_PRESSURE_LOW,
    MEMORY_PRESSURE_MEDIUM,
    MEMORY_PRESSURE_HIGH,
    MEMORY_PRESSURE_CRITICAL
} memory_pressure_t;

/* Memory monitor system state */
typedef struct memory_monitor {
    bool initialized;
    bool tracking_enabled;
    bool leak_detection_enabled;
    bool fragmentation_analysis_enabled;
    
    /* Allocation tracking */
    allocation_entry_t *allocations_head;
    allocation_entry_t *allocations_tail;
    uint16_t allocation_count;
    uint32_t total_allocations;
    uint32_t total_deallocations;
    
    /* Memory pool statistics */
    memory_pool_stats_t conventional_stats;
    memory_pool_stats_t xms_stats;
    memory_pool_stats_t umb_stats;
    
    /* Memory snapshots for trend analysis */
    memory_snapshot_t *snapshots_head;
    memory_snapshot_t *snapshots_tail;
    uint16_t snapshot_count;
    uint32_t last_snapshot_time;
    uint32_t snapshot_interval;
    
    /* Leak detection */
    uint32_t potential_leaks;
    uint32_t confirmed_leaks;
    uint32_t leak_threshold;
    
    /* Memory pressure monitoring */
    memory_pressure_t current_pressure;
    uint32_t pressure_thresholds[5];
    uint32_t pressure_alerts;
    
    /* Performance impact tracking */
    uint32_t allocation_time_total_us;
    uint32_t allocation_time_max_us;
    uint32_t fragmentation_overhead;
    
} memory_monitor_t;

/* Memory type definitions */
#define MEMORY_TYPE_CONVENTIONAL    0
#define MEMORY_TYPE_XMS            1
#define MEMORY_TYPE_UMB            2
#define MEMORY_TYPE_POOL           3

static memory_monitor_t g_memory_monitor = {0};

/* Helper functions */
static const char* get_memory_type_string(uint8_t memory_type) {
    switch (memory_type) {
        case MEMORY_TYPE_CONVENTIONAL: return "CONV";
        case MEMORY_TYPE_XMS: return "XMS";
        case MEMORY_TYPE_UMB: return "UMB";
        case MEMORY_TYPE_POOL: return "POOL";
        default: return "UNK";
    }
}

static const char* get_pressure_level_string(memory_pressure_t pressure) {
    switch (pressure) {
        case MEMORY_PRESSURE_NONE: return "NONE";
        case MEMORY_PRESSURE_LOW: return "LOW";
        case MEMORY_PRESSURE_MEDIUM: return "MEDIUM";
        case MEMORY_PRESSURE_HIGH: return "HIGH";
        case MEMORY_PRESSURE_CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

static uint32_t calculate_fragmentation_score(uint32_t total_free, uint32_t largest_free_block) {
    if (total_free == 0) return 0;
    return ((total_free - largest_free_block) * 100) / total_free;
}

static memory_pressure_t assess_memory_pressure(void) {
    uint32_t total_used = g_memory_monitor.conventional_stats.allocated_bytes + 
                         g_memory_monitor.xms_stats.allocated_bytes + 
                         g_memory_monitor.umb_stats.allocated_bytes;
    
    uint32_t total_available = g_memory_monitor.conventional_stats.pool_size + 
                              g_memory_monitor.xms_stats.pool_size + 
                              g_memory_monitor.umb_stats.pool_size;
    
    if (total_available == 0) return MEMORY_PRESSURE_CRITICAL;
    
    uint32_t usage_percent = (total_used * 100) / total_available;
    
    if (usage_percent >= 95) return MEMORY_PRESSURE_CRITICAL;
    if (usage_percent >= 85) return MEMORY_PRESSURE_HIGH;
    if (usage_percent >= 70) return MEMORY_PRESSURE_MEDIUM;
    if (usage_percent >= 50) return MEMORY_PRESSURE_LOW;
    
    return MEMORY_PRESSURE_NONE;
}

/* Initialize memory monitoring system */
int memory_monitor_init(void) {
    if (g_memory_monitor.initialized) {
        return SUCCESS;
    }
    
    /* Initialize configuration */
    g_memory_monitor.tracking_enabled = true;
    g_memory_monitor.leak_detection_enabled = true;
    g_memory_monitor.fragmentation_analysis_enabled = true;
    g_memory_monitor.snapshot_interval = MEMORY_SNAPSHOT_INTERVAL;
    g_memory_monitor.leak_threshold = LEAK_DETECTION_THRESHOLD;
    
    /* Initialize tracking lists */
    g_memory_monitor.allocations_head = NULL;
    g_memory_monitor.allocations_tail = NULL;
    g_memory_monitor.allocation_count = 0;
    
    g_memory_monitor.snapshots_head = NULL;
    g_memory_monitor.snapshots_tail = NULL;
    g_memory_monitor.snapshot_count = 0;
    g_memory_monitor.last_snapshot_time = diag_get_timestamp();
    
    /* Initialize memory pool statistics */
    memset(&g_memory_monitor.conventional_stats, 0, sizeof(memory_pool_stats_t));
    memset(&g_memory_monitor.xms_stats, 0, sizeof(memory_pool_stats_t));
    memset(&g_memory_monitor.umb_stats, 0, sizeof(memory_pool_stats_t));
    
    /* Set pressure thresholds (percentage of total memory) */
    g_memory_monitor.pressure_thresholds[MEMORY_PRESSURE_NONE] = 0;
    g_memory_monitor.pressure_thresholds[MEMORY_PRESSURE_LOW] = 50;
    g_memory_monitor.pressure_thresholds[MEMORY_PRESSURE_MEDIUM] = 70;
    g_memory_monitor.pressure_thresholds[MEMORY_PRESSURE_HIGH] = 85;
    g_memory_monitor.pressure_thresholds[MEMORY_PRESSURE_CRITICAL] = 95;
    
    /* Get actual memory pool sizes from TSR memory manager and XMS detection */
    tsr_memory_stats_t tsr_stats;
    tsr_get_memory_stats(&tsr_stats);
    
    /* TSR heap size (from tsr_memory.c) */
    g_memory_monitor.conventional_stats.pool_size = tsr_stats.heap_size;
    g_memory_monitor.conventional_stats.allocated_bytes = tsr_stats.allocated_bytes;
    g_memory_monitor.conventional_stats.free_bytes = tsr_stats.free_bytes;
    g_memory_monitor.conventional_stats.peak_usage = tsr_stats.peak_allocated;
    
    /* XMS memory detection */
    xms_info_t xms_info;
    if (detect_xms_memory(&xms_info) == SUCCESS) {
        g_memory_monitor.xms_stats.pool_size = xms_info.total_kb * 1024;
        g_memory_monitor.xms_stats.free_bytes = xms_info.available_kb * 1024;
        g_memory_monitor.xms_stats.allocated_bytes = g_memory_monitor.xms_stats.pool_size - g_memory_monitor.xms_stats.free_bytes;
        debug_log_debug("XMS detected: %lu KB total, %lu KB available", xms_info.total_kb, xms_info.available_kb);
    } else {
        g_memory_monitor.xms_stats.pool_size = 0; /* No XMS available */
        debug_log_debug("No XMS memory detected");
    }
    
    /* UMB detection (Upper Memory Blocks) - basic conventional memory analysis */
    g_memory_monitor.umb_stats.pool_size = 64 * 1024; /* Estimate 64KB UMB space */
    g_memory_monitor.umb_stats.free_bytes = 32 * 1024; /* Conservative estimate */
    g_memory_monitor.umb_stats.allocated_bytes = g_memory_monitor.umb_stats.pool_size - g_memory_monitor.umb_stats.free_bytes;
    
    debug_log_debug("Memory pools initialized: TSR=%lu, XMS=%lu, UMB=%lu bytes",
                   g_memory_monitor.conventional_stats.pool_size,
                   g_memory_monitor.xms_stats.pool_size,
                   g_memory_monitor.umb_stats.pool_size);
    
    g_memory_monitor.initialized = true;
    debug_log_info("Memory monitor initialized");
    return SUCCESS;
}

/* Track memory allocation */
int memory_monitor_track_allocation(void *ptr, uint32_t size, uint8_t memory_type,
                                   const char *file, const char *function, uint32_t line) {
    if (!g_memory_monitor.initialized || !g_memory_monitor.tracking_enabled) {
        return SUCCESS; /* Don't fail the allocation */
    }
    
    if (!ptr || size == 0) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Create allocation entry */
    allocation_entry_t *entry = (allocation_entry_t*)malloc(sizeof(allocation_entry_t));
    if (!entry) {
        /* Don't fail the original allocation due to tracking failure */
        g_memory_monitor.allocation_count = MAX_ALLOCATION_TRACKING; /* Disable tracking */
        return SUCCESS;
    }
    
    /* Fill allocation entry */
    memset(entry, 0, sizeof(allocation_entry_t));
    entry->ptr = ptr;
    entry->size = size;
    entry->timestamp = diag_get_timestamp();
    entry->file = file;
    entry->function = function;
    entry->line = line;
    entry->memory_type = memory_type;
    entry->freed = false;
    
    /* Add to tracking list */
    entry->next = NULL;
    if (!g_memory_monitor.allocations_head) {
        g_memory_monitor.allocations_head = entry;
        g_memory_monitor.allocations_tail = entry;
    } else {
        g_memory_monitor.allocations_tail->next = entry;
        g_memory_monitor.allocations_tail = entry;
    }
    
    g_memory_monitor.allocation_count++;
    g_memory_monitor.total_allocations++;
    
    /* Update pool statistics */
    memory_pool_stats_t *stats = NULL;
    switch (memory_type) {
        case MEMORY_TYPE_CONVENTIONAL:
            stats = &g_memory_monitor.conventional_stats;
            break;
        case MEMORY_TYPE_XMS:
            stats = &g_memory_monitor.xms_stats;
            break;
        case MEMORY_TYPE_UMB:
            stats = &g_memory_monitor.umb_stats;
            break;
    }
    
    if (stats) {
        stats->allocated_bytes += size;
        stats->allocation_count++;
        
        if (stats->allocated_bytes > stats->peak_usage) {
            stats->peak_usage = stats->allocated_bytes;
        }
        
        /* Update free bytes calculation */
        if (stats->pool_size >= stats->allocated_bytes) {
            stats->free_bytes = stats->pool_size - stats->allocated_bytes;
        }
    }
    
    /* Remove old allocations if tracking list gets too long */
    while (g_memory_monitor.allocation_count > MAX_ALLOCATION_TRACKING) {
        allocation_entry_t *old_entry = g_memory_monitor.allocations_head;
        if (!old_entry->freed) {
            g_memory_monitor.potential_leaks++;
        }
        g_memory_monitor.allocations_head = old_entry->next;
        free(old_entry);
        g_memory_monitor.allocation_count--;
    }
    
    debug_log_trace("Memory allocation tracked: ptr=%p, size=%lu, type=%s, %s:%s:%lu",
                   ptr, size, get_memory_type_string(memory_type), 
                   file ? file : "unknown", function ? function : "unknown", line);
    
    return SUCCESS;
}

/* Track memory deallocation */
int memory_monitor_track_deallocation(void *ptr, uint8_t memory_type) {
    if (!g_memory_monitor.initialized || !g_memory_monitor.tracking_enabled || !ptr) {
        return SUCCESS; /* Don't fail the deallocation */
    }
    
    /* Find matching allocation */
    allocation_entry_t *entry = g_memory_monitor.allocations_head;
    while (entry) {
        if (entry->ptr == ptr && entry->memory_type == memory_type && !entry->freed) {
            entry->freed = true;
            g_memory_monitor.total_deallocations++;
            
            /* Update pool statistics */
            memory_pool_stats_t *stats = NULL;
            switch (memory_type) {
                case MEMORY_TYPE_CONVENTIONAL:
                    stats = &g_memory_monitor.conventional_stats;
                    break;
                case MEMORY_TYPE_XMS:
                    stats = &g_memory_monitor.xms_stats;
                    break;
                case MEMORY_TYPE_UMB:
                    stats = &g_memory_monitor.umb_stats;
                    break;
            }
            
            if (stats && stats->allocated_bytes >= entry->size) {
                stats->allocated_bytes -= entry->size;
                stats->free_bytes = stats->pool_size - stats->allocated_bytes;
            }
            
            debug_log_trace("Memory deallocation tracked: ptr=%p, size=%lu, type=%s",
                           ptr, entry->size, get_memory_type_string(memory_type));
            
            return SUCCESS;
        }
        entry = entry->next;
    }
    
    /* Allocation not found - potential double-free or untracked allocation */
    debug_log_warning("Memory deallocation without matching allocation: ptr=%p, type=%s",
                     ptr, get_memory_type_string(memory_type));
    
    return SUCCESS;
}

/* Perform leak detection analysis */
int memory_monitor_detect_leaks(void) {
    if (!g_memory_monitor.initialized || !g_memory_monitor.leak_detection_enabled) {
        return ERROR_INVALID_STATE;
    }
    
    uint32_t current_time = diag_get_timestamp();
    uint32_t leak_age_threshold = current_time - 60000; /* 1 minute */
    uint32_t active_leaks = 0;
    
    allocation_entry_t *entry = g_memory_monitor.allocations_head;
    while (entry) {
        if (!entry->freed && entry->timestamp < leak_age_threshold) {
            active_leaks++;
            
            if (active_leaks <= 10) { /* Only log first 10 leaks to avoid spam */
                debug_log_warning("Potential memory leak detected: ptr=%p, size=%lu, age=%lu ms, %s:%s:%lu",
                                 entry->ptr, entry->size, current_time - entry->timestamp,
                                 entry->file ? entry->file : "unknown",
                                 entry->function ? entry->function : "unknown",
                                 entry->line);
            }
        }
        entry = entry->next;
    }
    
    if (active_leaks > g_memory_monitor.leak_threshold) {
        g_memory_monitor.confirmed_leaks += active_leaks - g_memory_monitor.leak_threshold;
        diag_generate_alert(ALERT_TYPE_MEMORY_LOW, "Memory leaks detected");
    }
    
    g_memory_monitor.potential_leaks = active_leaks;
    
    debug_log_debug("Leak detection completed: %lu potential leaks found", active_leaks);
    return SUCCESS;
}

/* Analyze memory fragmentation */
int memory_monitor_analyze_fragmentation(void) {
    if (!g_memory_monitor.initialized || !g_memory_monitor.fragmentation_analysis_enabled) {
        return ERROR_INVALID_STATE;
    }
    
    /* Get actual fragmentation data from TSR memory manager */
    tsr_memory_stats_t tsr_stats;
    tsr_get_memory_stats(&tsr_stats);
    
    /* Update conventional memory stats with current TSR heap status */
    g_memory_monitor.conventional_stats.allocated_bytes = tsr_stats.allocated_bytes;
    g_memory_monitor.conventional_stats.free_bytes = tsr_stats.free_bytes;
    g_memory_monitor.conventional_stats.largest_free_block = tsr_stats.largest_free_block;
    g_memory_monitor.conventional_stats.peak_usage = tsr_stats.peak_allocated;
    
    /* Calculate fragmentation for TSR heap */
    if (g_memory_monitor.conventional_stats.free_bytes > 0) {
        g_memory_monitor.conventional_stats.fragmentation_percent = 
            calculate_fragmentation_score(g_memory_monitor.conventional_stats.free_bytes,
                                        g_memory_monitor.conventional_stats.largest_free_block);
    } else {
        g_memory_monitor.conventional_stats.fragmentation_percent = 0;
    }
    
    /* XMS fragmentation analysis */
    xms_info_t xms_info;
    if (detect_xms_memory(&xms_info) == SUCCESS) {
        g_memory_monitor.xms_stats.free_bytes = xms_info.available_kb * 1024;
        g_memory_monitor.xms_stats.allocated_bytes = g_memory_monitor.xms_stats.pool_size - g_memory_monitor.xms_stats.free_bytes;
        /* XMS blocks are typically large and less fragmented */
        g_memory_monitor.xms_stats.largest_free_block = g_memory_monitor.xms_stats.free_bytes;
        g_memory_monitor.xms_stats.fragmentation_percent = 
            calculate_fragmentation_score(g_memory_monitor.xms_stats.free_bytes,
                                        g_memory_monitor.xms_stats.largest_free_block);
    }
    
    /* UMB fragmentation (simplified analysis) */
    /* UMB fragmentation is harder to detect without direct DOS memory manager access */
    g_memory_monitor.umb_stats.largest_free_block = g_memory_monitor.umb_stats.free_bytes / 2; /* Conservative estimate */
    g_memory_monitor.umb_stats.fragmentation_percent = 
        calculate_fragmentation_score(g_memory_monitor.umb_stats.free_bytes,
                                    g_memory_monitor.umb_stats.largest_free_block);
    
    /* Report fragmentation issues */
    memory_pool_stats_t *pools[] = {
        &g_memory_monitor.conventional_stats,
        &g_memory_monitor.xms_stats,
        &g_memory_monitor.umb_stats
    };
    
    const char *pool_names[] = { "TSR heap", "XMS", "UMB" };
    
    for (int i = 0; i < 3; i++) {
        memory_pool_stats_t *stats = pools[i];
        
        if (stats->pool_size > 0) { /* Only analyze pools that exist */
            debug_log_debug("%s: %lu/%lu bytes used, largest free block: %lu, fragmentation: %lu%%",
                           pool_names[i], stats->allocated_bytes, stats->pool_size,
                           stats->largest_free_block, stats->fragmentation_percent);
            
            if (stats->fragmentation_percent > FRAGMENTATION_THRESHOLD) {
                debug_log_warning("High fragmentation detected in %s memory: %lu%% (threshold %d%%)",
                                 pool_names[i], stats->fragmentation_percent, FRAGMENTATION_THRESHOLD);
            }
        }
    }
    
    return SUCCESS;
}

/* Take memory snapshot for trend analysis */
int memory_monitor_take_snapshot(void) {
    if (!g_memory_monitor.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    uint32_t current_time = diag_get_timestamp();
    
    /* Check if it's time for a new snapshot */
    if (current_time - g_memory_monitor.last_snapshot_time < g_memory_monitor.snapshot_interval) {
        return SUCCESS;
    }
    
    /* Create new snapshot */
    memory_snapshot_t *snapshot = (memory_snapshot_t*)malloc(sizeof(memory_snapshot_t));
    if (!snapshot) {
        return ERROR_OUT_OF_MEMORY;
    }
    
    /* Fill snapshot data */
    memset(snapshot, 0, sizeof(memory_snapshot_t));
    snapshot->timestamp = current_time;
    snapshot->conventional_used = g_memory_monitor.conventional_stats.allocated_bytes;
    snapshot->conventional_free = g_memory_monitor.conventional_stats.free_bytes;
    snapshot->xms_used = g_memory_monitor.xms_stats.allocated_bytes;
    snapshot->xms_free = g_memory_monitor.xms_stats.free_bytes;
    snapshot->umb_used = g_memory_monitor.umb_stats.allocated_bytes;
    snapshot->umb_free = g_memory_monitor.umb_stats.free_bytes;
    
    /* Count active allocations */
    allocation_entry_t *entry = g_memory_monitor.allocations_head;
    while (entry) {
        if (!entry->freed) {
            snapshot->active_allocations++;
        }
        entry = entry->next;
    }
    
    /* Calculate overall fragmentation score */
    uint32_t total_free = snapshot->conventional_free + snapshot->xms_free + snapshot->umb_free;
    uint32_t largest_free = g_memory_monitor.conventional_stats.largest_free_block;
    if (g_memory_monitor.xms_stats.largest_free_block > largest_free) {
        largest_free = g_memory_monitor.xms_stats.largest_free_block;
    }
    if (g_memory_monitor.umb_stats.largest_free_block > largest_free) {
        largest_free = g_memory_monitor.umb_stats.largest_free_block;
    }
    snapshot->fragmentation_score = calculate_fragmentation_score(total_free, largest_free);
    
    /* Add to snapshot list */
    snapshot->next = NULL;
    if (!g_memory_monitor.snapshots_head) {
        g_memory_monitor.snapshots_head = snapshot;
        g_memory_monitor.snapshots_tail = snapshot;
    } else {
        g_memory_monitor.snapshots_tail->next = snapshot;
        g_memory_monitor.snapshots_tail = snapshot;
    }
    
    g_memory_monitor.snapshot_count++;
    g_memory_monitor.last_snapshot_time = current_time;
    
    /* Remove old snapshots (keep last 100) */
    while (g_memory_monitor.snapshot_count > 100) {
        memory_snapshot_t *old_snapshot = g_memory_monitor.snapshots_head;
        g_memory_monitor.snapshots_head = old_snapshot->next;
        free(old_snapshot);
        g_memory_monitor.snapshot_count--;
    }
    
    /* Update memory pressure assessment */
    g_memory_monitor.current_pressure = assess_memory_pressure();
    
    debug_log_debug("Memory snapshot taken: conventional=%lu/%lu, XMS=%lu/%lu, UMB=%lu/%lu",
                   snapshot->conventional_used, snapshot->conventional_free,
                   snapshot->xms_used, snapshot->xms_free,
                   snapshot->umb_used, snapshot->umb_free);
    
    return SUCCESS;
}

/* Get memory monitoring statistics */
int memory_monitor_get_statistics(uint32_t *total_allocations, uint32_t *active_allocations,
                                  uint32_t *potential_leaks, memory_pressure_t *pressure) {
    if (!g_memory_monitor.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    if (total_allocations) *total_allocations = g_memory_monitor.total_allocations;
    if (potential_leaks) *potential_leaks = g_memory_monitor.potential_leaks;
    if (pressure) *pressure = g_memory_monitor.current_pressure;
    
    if (active_allocations) {
        *active_allocations = 0;
        allocation_entry_t *entry = g_memory_monitor.allocations_head;
        while (entry) {
            if (!entry->freed) {
                (*active_allocations)++;
            }
            entry = entry->next;
        }
    }
    
    return SUCCESS;
}

/* Print memory monitoring dashboard */
int memory_monitor_print_dashboard(void) {
    if (!g_memory_monitor.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    printf("\n=== MEMORY MONITORING DASHBOARD ===\n");
    printf("Tracking: %s\n", g_memory_monitor.tracking_enabled ? "Enabled" : "Disabled");
    printf("Leak Detection: %s\n", g_memory_monitor.leak_detection_enabled ? "Enabled" : "Disabled");
    printf("Fragmentation Analysis: %s\n", g_memory_monitor.fragmentation_analysis_enabled ? "Enabled" : "Disabled");
    printf("Current Pressure: %s\n", get_pressure_level_string(g_memory_monitor.current_pressure));
    
    printf("\nOverall Statistics:\n");
    printf("  Total Allocations: %lu\n", g_memory_monitor.total_allocations);
    printf("  Total Deallocations: %lu\n", g_memory_monitor.total_deallocations);
    printf("  Potential Leaks: %lu\n", g_memory_monitor.potential_leaks);
    printf("  Confirmed Leaks: %lu\n", g_memory_monitor.confirmed_leaks);
    printf("  Snapshots Taken: %d\n", g_memory_monitor.snapshot_count);
    
    printf("\nConventional Memory:\n");
    printf("  Pool Size: %lu bytes\n", g_memory_monitor.conventional_stats.pool_size);
    printf("  Allocated: %lu bytes\n", g_memory_monitor.conventional_stats.allocated_bytes);
    printf("  Free: %lu bytes\n", g_memory_monitor.conventional_stats.free_bytes);
    printf("  Peak Usage: %lu bytes\n", g_memory_monitor.conventional_stats.peak_usage);
    printf("  Fragmentation: %lu%%\n", g_memory_monitor.conventional_stats.fragmentation_percent);
    
    if (g_memory_monitor.xms_stats.pool_size > 0) {
        printf("\nXMS Memory:\n");
        printf("  Pool Size: %lu bytes\n", g_memory_monitor.xms_stats.pool_size);
        printf("  Allocated: %lu bytes\n", g_memory_monitor.xms_stats.allocated_bytes);
        printf("  Free: %lu bytes\n", g_memory_monitor.xms_stats.free_bytes);
        printf("  Peak Usage: %lu bytes\n", g_memory_monitor.xms_stats.peak_usage);
        printf("  Fragmentation: %lu%%\n", g_memory_monitor.xms_stats.fragmentation_percent);
    }
    
    if (g_memory_monitor.umb_stats.pool_size > 0) {
        printf("\nUMB Memory:\n");
        printf("  Pool Size: %lu bytes\n", g_memory_monitor.umb_stats.pool_size);
        printf("  Allocated: %lu bytes\n", g_memory_monitor.umb_stats.allocated_bytes);
        printf("  Free: %lu bytes\n", g_memory_monitor.umb_stats.free_bytes);
        printf("  Peak Usage: %lu bytes\n", g_memory_monitor.umb_stats.peak_usage);
        printf("  Fragmentation: %lu%%\n", g_memory_monitor.umb_stats.fragmentation_percent);
    }
    
    if (g_memory_monitor.potential_leaks > 0) {
        printf("\nPotential Memory Leaks:\n");
        allocation_entry_t *entry = g_memory_monitor.allocations_head;
        int count = 0;
        while (entry && count < 10) {
            if (!entry->freed) {
                uint32_t age = diag_get_timestamp() - entry->timestamp;
                printf("  [%d] ptr=%p, size=%lu bytes, age=%lu ms, %s:%s:%lu\n",
                       count + 1, entry->ptr, entry->size, age,
                       entry->file ? entry->file : "unknown",
                       entry->function ? entry->function : "unknown",
                       entry->line);
                count++;
            }
            entry = entry->next;
        }
    }
    
    return SUCCESS;
}

/* Week 1 specific: NE2000 emulation memory monitoring */
int memory_monitor_ne2000_emulation(void *buffer, uint32_t size, bool allocated) {
    if (!g_memory_monitor.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    if (allocated) {
        return memory_monitor_track_allocation(buffer, size, MEMORY_TYPE_CONVENTIONAL,
                                             "ne2000_emulation.c", "ne2000_alloc_buffer", 0);
    } else {
        return memory_monitor_track_deallocation(buffer, MEMORY_TYPE_CONVENTIONAL);
    }
}

/* Cleanup memory monitoring system */
void memory_monitor_cleanup(void) {
    if (!g_memory_monitor.initialized) {
        return;
    }
    
    debug_log_info("Cleaning up memory monitor");
    
    /* Free allocation tracking entries */
    allocation_entry_t *entry = g_memory_monitor.allocations_head;
    while (entry) {
        allocation_entry_t *next = entry->next;
        free(entry);
        entry = next;
    }
    
    /* Free memory snapshots */
    memory_snapshot_t *snapshot = g_memory_monitor.snapshots_head;
    while (snapshot) {
        memory_snapshot_t *next = snapshot->next;
        free(snapshot);
        snapshot = next;
    }
    
    memset(&g_memory_monitor, 0, sizeof(memory_monitor_t));
}