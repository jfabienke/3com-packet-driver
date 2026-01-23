/**
 * @file vds_manager.c
 * @brief VDS Manager Layer Implementation
 * 
 * Provides lifecycle management and registry for VDS locks with
 * support for persistent policies and automatic cleanup.
 */

#include <dos.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "../../include/vdsmgr.h"
#include "../../include/vdssafe.h"
#include "../../include/vds_core.h"
#include "../../include/logging.h"

/* Registry and state */
static vds_registry_entry_t registry[VDS_REGISTRY_SIZE];
static vds_manager_stats_t manager_stats = {0};
static bool manager_initialized = false;
static uint16_t next_manager_id = 1;  /* Start at 1, 0 is invalid */
static uint16_t id_generation[VDS_REGISTRY_SIZE];  /* Generation counters */

/* Forward declarations */
static uint16_t allocate_registry_entry(void);
static void free_registry_entry(uint16_t index);
static uint16_t find_existing_lock(void far* addr, uint32_t size);
static bool should_persist_lock(void far* addr, uint32_t size, uint32_t access_count);

/**
 * Initialize VDS manager layer
 */
int vds_manager_init(void)
{
    if (manager_initialized) {
        return 0;
    }
    
    /* Initialize safety layer first */
    if (vds_safety_init() != 0) {
        LOG_ERROR("VDS Manager: Failed to initialize safety layer");
        return -1;
    }
    
    /* Clear registry */
    memset(registry, 0, sizeof(registry));
    memset(&manager_stats, 0, sizeof(manager_stats));
    
    /* Mark all entries as free */
    for (int i = 0; i < VDS_REGISTRY_SIZE; i++) {
        registry[i].state = VDS_ENTRY_FREE;
        registry[i].handle = VDS_INVALID_HANDLE;
        registry[i].manager_id = VDS_INVALID_HANDLE;
    }
    
    manager_initialized = true;
    next_manager_id = 1;
    
    LOG_INFO("VDS Manager: Initialized with %d-entry registry", VDS_REGISTRY_SIZE);
    return 0;
}

/**
 * Cleanup VDS manager layer
 */
void vds_manager_cleanup(void)
{
    if (!manager_initialized) {
        return;
    }
    
    /* Release all active locks */
    for (int i = 0; i < VDS_REGISTRY_SIZE; i++) {
        if (registry[i].state != VDS_ENTRY_FREE) {
            vds_safe_lock_t lock;
            lock.success = true;
            lock.lock_handle = registry[i].handle;
            
            vds_unlock_safe(&lock);
            
            LOG_DEBUG("VDS Manager: Released lock %u on cleanup", 
                     registry[i].manager_id);
        }
    }
    
    /* Cleanup safety layer */
    vds_safety_cleanup();
    
    manager_initialized = false;
    LOG_INFO("VDS Manager: Cleaned up");
}

/**
 * Acquire managed lock with policy
 */
uint16_t vds_manager_lock(void far* addr, uint32_t size,
                          const dma_constraints_t* constraints,
                          vds_lock_policy_t policy,
                          const char* description)
{
    vds_safe_lock_t lock;
    vds_safe_error_t error;
    uint16_t index;
    uint16_t existing_id;
    
    if (!manager_initialized) {
        LOG_ERROR("VDS Manager: Not initialized");
        return VDS_INVALID_HANDLE;
    }
    
    manager_stats.total_locks++;
    
    /* Check for existing lock */
    existing_id = find_existing_lock(addr, size);
    if (existing_id != VDS_INVALID_HANDLE) {
        /* Found existing lock - update access info and reference count */
        for (int i = 0; i < VDS_REGISTRY_SIZE; i++) {
            if (registry[i].manager_id == existing_id) {
                /* Increment reference count */
                registry[i].ref_count++;
                registry[i].last_access = clock();
                registry[i].access_count++;
                
                /* Auto-upgrade policy if frequently accessed */
                if (policy == VDS_POLICY_AUTO && 
                    registry[i].access_count > 10 &&
                    registry[i].policy == VDS_POLICY_TRANSIENT) {
                    registry[i].policy = VDS_POLICY_PERSISTENT;
                    manager_stats.policy_upgrades++;
                    LOG_INFO("VDS Manager: Auto-upgraded lock %u to persistent",
                            existing_id);
                }
                
                manager_stats.cache_hits++;
                LOG_DEBUG("VDS Manager: Cache hit for ID %u (refcount: %u)",
                         existing_id, registry[i].ref_count);
                return existing_id;
            }
        }
    }
    
    manager_stats.cache_misses++;
    
    /* Allocate registry entry */
    index = allocate_registry_entry();
    if (index >= VDS_REGISTRY_SIZE) {
        manager_stats.registry_full++;
        LOG_ERROR("VDS Manager: Registry full");
        return VDS_INVALID_HANDLE;
    }
    
    /* Perform the lock - default to bidirectional for safety */
    error = vds_lock_with_constraints(addr, size, constraints, 
                                     VDS_DIR_BIDIRECTIONAL, &lock);
    if (error != VDS_SAFE_OK) {
        free_registry_entry(index);
        manager_stats.lock_failures++;
        LOG_ERROR("VDS Manager: Lock failed (error: %s)",
                 vds_safe_error_string(error));
        return VDS_INVALID_HANDLE;
    }
    
    /* Fill registry entry */
    registry[index].handle = lock.lock_handle;
    registry[index].manager_id = next_manager_id++;
    registry[index].generation = ++id_generation[index];  /* Increment generation */
    registry[index].address = addr;
    registry[index].size = size;
    registry[index].physical_addr = lock.physical_addr;
    registry[index].policy = policy;
    registry[index].state = VDS_ENTRY_LOCKED;
    registry[index].ref_count = 1;  /* Initial reference */
    registry[index].busy = false;
    registry[index].lock_time = clock();
    registry[index].last_access = registry[index].lock_time;
    registry[index].access_count = 1;
    registry[index].constraints = constraints;
    registry[index].uses_bounce = lock.used_bounce;
    
    /* Track VDS bounce statistics */
    if (lock.vds_used_bounce) {
        manager_stats.vds_bounce_locks++;
    } else {
        manager_stats.vds_direct_locks++;
    }
    if (lock.used_bounce) {
        manager_stats.our_bounce_locks++;
    }
    
    if (description) {
        strncpy(registry[index].description, description, 31);
        registry[index].description[31] = '\0';
    }
    
    /* Update statistics */
    manager_stats.entries_used++;
    if (manager_stats.entries_used > manager_stats.entries_peak) {
        manager_stats.entries_peak = manager_stats.entries_used;
    }
    
    if (policy == VDS_POLICY_PERSISTENT) {
        manager_stats.persistent_locks++;
    } else {
        manager_stats.transient_locks++;
    }
    
    LOG_DEBUG("VDS Manager: Locked 0x%p + %lu as ID %u (policy: %d)",
             addr, size, registry[index].manager_id, policy);
    
    return registry[index].manager_id;
}

/**
 * Release managed lock
 */
int vds_manager_unlock(uint16_t manager_id)
{
    vds_safe_lock_t lock;
    vds_safe_error_t error;
    
    if (!manager_initialized || manager_id == VDS_INVALID_HANDLE) {
        return -1;
    }
    
    /* Find registry entry */
    for (int i = 0; i < VDS_REGISTRY_SIZE; i++) {
        if (registry[i].manager_id == manager_id &&
            registry[i].state != VDS_ENTRY_FREE) {
            
            /* Decrement reference count */
            if (registry[i].ref_count > 0) {
                registry[i].ref_count--;
                registry[i].last_access = clock();
                
                LOG_DEBUG("VDS Manager: Decremented refcount for ID %u (now: %u)",
                         manager_id, registry[i].ref_count);
                
                /* Only unlock if refcount reaches zero */
                if (registry[i].ref_count > 0) {
                    return 0;  /* Still referenced */
                }
            }
            
            /* Check policy */
            if (registry[i].policy == VDS_POLICY_PERSISTENT) {
                /* Persistent locks aren't released even at refcount 0 */
                LOG_DEBUG("VDS Manager: Persistent lock %u retained at refcount 0", 
                         manager_id);
                return 0;
            }
            
            /* Refcount is 0 and not persistent - actually unlock */
            memset(&lock, 0, sizeof(lock));
            lock.success = true;
            lock.lock_handle = registry[i].handle;
            
            /* Set busy flag to prevent race conditions */
            registry[i].busy = true;
            
            /* Unlock the region */
            error = vds_unlock_safe(&lock);
            if (error != VDS_SAFE_OK) {
                LOG_ERROR("VDS Manager: Unlock failed for ID %u", manager_id);
                registry[i].state = VDS_ENTRY_ERROR;
                registry[i].busy = false;
                return -1;
            }
            
            /* Free registry entry */
            free_registry_entry(i);
            manager_stats.total_unlocks++;
            
            LOG_DEBUG("VDS Manager: Unlocked ID %u (refcount reached 0)", manager_id);
            return 0;
        }
    }
    
    LOG_WARNING("VDS Manager: Lock ID %u not found", manager_id);
    return -1;
}

/**
 * Find existing lock for address
 */
uint16_t vds_manager_find_lock(void far* addr, uint32_t size)
{
    return find_existing_lock(addr, size);
}

/**
 * Lock packet ring buffer with persistent policy
 */
uint16_t vds_manager_lock_ring(void far* ring_addr, uint32_t ring_size,
                               uint16_t num_descriptors)
{
    uint16_t id;
    
    /* Use persistent policy for ring buffers */
    id = vds_manager_lock(ring_addr, ring_size, &PCI_DMA_CONSTRAINTS,
                         VDS_POLICY_PERSISTENT, "Packet Ring Buffer");
    
    if (id != VDS_INVALID_HANDLE) {
        /* Mark as ring buffer */
        for (int i = 0; i < VDS_REGISTRY_SIZE; i++) {
            if (registry[i].manager_id == id) {
                registry[i].is_ring_buffer = true;
                break;
            }
        }
        
        LOG_INFO("VDS Manager: Ring buffer locked as ID %u (%u descriptors)",
                id, num_descriptors);
    }
    
    return id;
}

/**
 * Get registry entry by manager ID
 */
const vds_registry_entry_t* vds_manager_get_entry(uint16_t manager_id)
{
    if (!manager_initialized || manager_id == VDS_INVALID_HANDLE) {
        return NULL;
    }
    
    for (int i = 0; i < VDS_REGISTRY_SIZE; i++) {
        if (registry[i].manager_id == manager_id) {
            return &registry[i];
        }
    }
    
    return NULL;
}

/**
 * Perform registry cleanup
 */
int vds_manager_cleanup_stale(uint32_t max_age_ticks)
{
    int cleaned = 0;
    uint32_t current_time = clock();
    vds_safe_lock_t lock;
    
    for (int i = 0; i < VDS_REGISTRY_SIZE; i++) {
        if (registry[i].state == VDS_ENTRY_LOCKED && !registry[i].busy) {
            uint32_t age = current_time - registry[i].last_access;
            
            /* Check if stale and unreferenced */
            if (age > max_age_ticks && 
                registry[i].ref_count == 0 &&
                registry[i].policy != VDS_POLICY_PERSISTENT) {
                
                /* Mark for aging */
                registry[i].state = VDS_ENTRY_AGING;
                registry[i].busy = true;  /* Prevent concurrent access */
                
                /* Unlock the region */
                memset(&lock, 0, sizeof(lock));
                lock.success = true;
                lock.lock_handle = registry[i].handle;
                vds_unlock_safe(&lock);
                
                /* Free entry */
                free_registry_entry(i);
                cleaned++;
                
                LOG_DEBUG("VDS Manager: Cleaned stale lock (age: %lu ticks)", age);
            }
        }
    }
    
    if (cleaned > 0) {
        manager_stats.stale_cleanups += cleaned;
        manager_stats.auto_cleanups += cleaned;
        LOG_INFO("VDS Manager: Cleaned %d stale locks", cleaned);
    }
    
    return cleaned;
}

/**
 * Get manager statistics
 */
void vds_manager_get_stats(vds_manager_stats_t* stats)
{
    if (stats) {
        *stats = manager_stats;
    }
}

/**
 * Dump registry for debugging
 */
void vds_manager_dump_registry(bool verbose)
{
    int active = 0;
    
    LOG_INFO("=== VDS Manager Registry Dump ===");
    LOG_INFO("Entries: %u/%d (Peak: %u)",
            manager_stats.entries_used, VDS_REGISTRY_SIZE,
            manager_stats.entries_peak);
    
    for (int i = 0; i < VDS_REGISTRY_SIZE; i++) {
        if (registry[i].state != VDS_ENTRY_FREE) {
            active++;
            
            if (verbose) {
                LOG_INFO("[%02d] ID:%u Handle:0x%04X Addr:0x%p Size:%lu "
                        "Phys:0x%08lX Policy:%d State:%d Desc:%s",
                        i, registry[i].manager_id, registry[i].handle,
                        registry[i].address, registry[i].size,
                        registry[i].physical_addr, registry[i].policy,
                        registry[i].state, registry[i].description);
            }
        }
    }
    
    LOG_INFO("Active entries: %d", active);
    LOG_INFO("Total locks: %lu, Unlocks: %lu",
            manager_stats.total_locks, manager_stats.total_unlocks);
    LOG_INFO("Cache hits: %lu, Misses: %lu",
            manager_stats.cache_hits, manager_stats.cache_misses);
}

/* Internal helper functions */

static uint16_t allocate_registry_entry(void)
{
    /* First pass: find free entry */
    for (int i = 0; i < VDS_REGISTRY_SIZE; i++) {
        if (registry[i].state == VDS_ENTRY_FREE && !registry[i].busy) {
            registry[i].state = VDS_ENTRY_LOCKED;
            registry[i].busy = true;  /* Mark as busy during allocation */
            return i;
        }
    }
    
    /* Second pass: reclaim aged entries with zero refcount */
    for (int i = 0; i < VDS_REGISTRY_SIZE; i++) {
        if (registry[i].state == VDS_ENTRY_AGING && 
            registry[i].ref_count == 0 && !registry[i].busy) {
            free_registry_entry(i);
            registry[i].state = VDS_ENTRY_LOCKED;
            registry[i].busy = true;
            return i;
        }
    }
    
    return VDS_REGISTRY_SIZE;  /* Full */
}

static void free_registry_entry(uint16_t index)
{
    if (index < VDS_REGISTRY_SIZE) {
        /* Preserve generation counter for ID reuse safety */
        uint16_t saved_generation = registry[index].generation;
        
        memset(&registry[index], 0, sizeof(vds_registry_entry_t));
        registry[index].state = VDS_ENTRY_FREE;
        registry[index].handle = VDS_INVALID_HANDLE;
        registry[index].manager_id = VDS_INVALID_HANDLE;
        registry[index].generation = saved_generation;
        registry[index].ref_count = 0;
        registry[index].busy = false;
        
        if (manager_stats.entries_used > 0) {
            manager_stats.entries_used--;
        }
    }
}

static uint16_t find_existing_lock(void far* addr, uint32_t size)
{
    for (int i = 0; i < VDS_REGISTRY_SIZE; i++) {
        if (registry[i].state == VDS_ENTRY_LOCKED &&
            registry[i].address == addr &&
            registry[i].size == size) {
            return registry[i].manager_id;
        }
    }
    
    return VDS_INVALID_HANDLE;
}