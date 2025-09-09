/**
 * @file vds_manager.h
 * @brief VDS Manager Layer - Lifecycle and Registry Management
 * 
 * Top layer of unified VDS architecture that provides:
 * - 64-entry lock registry for lifecycle management
 * - Persistent lock policies for packet rings
 * - Lock aging and automatic cleanup
 * - Statistics and debugging support
 * 
 * Per GPT-5 recommendations for production-quality VDS handling
 */

#ifndef VDS_MANAGER_H
#define VDS_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "vds_safety.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registry size per GPT-5 recommendation */
#define VDS_REGISTRY_SIZE       64      /* 64 active locks max */
#define VDS_INVALID_HANDLE      0xFFFF  /* Invalid handle marker */

/* Lock policies */
typedef enum {
    VDS_POLICY_TRANSIENT = 0,   /* Lock/unlock per operation */
    VDS_POLICY_PERSISTENT,       /* Keep locked (e.g., ring buffers) */
    VDS_POLICY_LAZY_RELEASE,     /* Release after timeout */
    VDS_POLICY_AUTO              /* Manager decides based on usage */
} vds_lock_policy_t;

/* Lock entry states */
typedef enum {
    VDS_ENTRY_FREE = 0,
    VDS_ENTRY_LOCKED,
    VDS_ENTRY_AGING,           /* Marked for cleanup */
    VDS_ENTRY_ERROR            /* Lock in error state */
} vds_entry_state_t;

/* Registry entry */
typedef struct {
    /* Lock identification */
    uint16_t handle;            /* VDS lock handle */
    uint16_t manager_id;        /* Manager-assigned ID */
    uint16_t generation;        /* Generation counter for ID reuse */
    
    /* Lock details */
    void far* address;          /* Locked region address */
    uint32_t size;              /* Region size */
    uint32_t physical_addr;     /* Physical address */
    
    /* Policy and state */
    vds_lock_policy_t policy;  /* Lock policy */
    vds_entry_state_t state;    /* Current state */
    
    /* Reference counting */
    uint16_t ref_count;         /* Reference counter */
    bool busy;                  /* Currently in use flag */
    
    /* Usage tracking */
    uint32_t lock_time;         /* When locked (ticks) */
    uint32_t last_access;       /* Last access time */
    uint32_t access_count;      /* Access counter */
    
    /* Metadata */
    char description[32];       /* Lock description */
    uint8_t owner_id;           /* Owner module/component */
    bool is_ring_buffer;        /* Special handling for rings */
    bool uses_bounce;           /* Using bounce buffer */
    
    /* Constraints used */
    const dma_constraints_t* constraints;
} vds_registry_entry_t;

/* Manager statistics */
typedef struct {
    /* Registry stats */
    uint16_t entries_used;      /* Current entries in use */
    uint16_t entries_peak;      /* Peak usage */
    uint32_t total_locks;       /* Total locks created */
    uint32_t total_unlocks;     /* Total unlocks */
    
    /* Policy stats */
    uint32_t persistent_locks;  /* Persistent lock count */
    uint32_t transient_locks;   /* Transient lock count */
    uint32_t auto_cleanups;     /* Automatic cleanups */
    
    /* Performance stats */
    uint32_t cache_hits;        /* Found existing lock */
    uint32_t cache_misses;      /* Had to create new lock */
    uint32_t policy_upgrades;   /* Transient -> Persistent */
    
    /* VDS bounce detection */
    uint32_t vds_bounce_locks;  /* Locks where VDS used bounce */
    uint32_t vds_direct_locks;  /* Locks where VDS didn't bounce */
    uint32_t our_bounce_locks;  /* Locks where we used bounce */
    
    /* Error stats */
    uint32_t registry_full;     /* Registry full errors */
    uint32_t lock_failures;     /* VDS lock failures */
    uint32_t stale_cleanups;    /* Stale locks cleaned */
} vds_manager_stats_t;

/* Manager initialization */

/**
 * Initialize VDS manager layer
 * @return 0 on success, negative on error
 */
int vds_manager_init(void);

/**
 * Cleanup VDS manager layer
 * Releases all locks and frees resources
 */
void vds_manager_cleanup(void);

/* Lock management with policies */

/**
 * Acquire managed lock with policy
 * @param addr Address to lock
 * @param size Size in bytes
 * @param constraints Device constraints
 * @param policy Lock policy
 * @param description Lock description for debugging
 * @return Manager ID or VDS_INVALID_HANDLE on error
 */
uint16_t vds_manager_lock(void far* addr, uint32_t size,
                          const dma_constraints_t* constraints,
                          vds_lock_policy_t policy,
                          const char* description);

/**
 * Release managed lock
 * @param manager_id Manager-assigned ID
 * @return 0 on success, negative on error
 */
int vds_manager_unlock(uint16_t manager_id);

/**
 * Find existing lock for address
 * @param addr Address to search
 * @param size Size to match
 * @return Manager ID or VDS_INVALID_HANDLE if not found
 */
uint16_t vds_manager_find_lock(void far* addr, uint32_t size);

/* Ring buffer support */

/**
 * Lock packet ring buffer with persistent policy
 * @param ring_addr Ring buffer address
 * @param ring_size Ring buffer size
 * @param num_descriptors Number of descriptors
 * @return Manager ID or VDS_INVALID_HANDLE on error
 */
uint16_t vds_manager_lock_ring(void far* ring_addr, uint32_t ring_size,
                               uint16_t num_descriptors);

/**
 * Get physical address for ring descriptor
 * @param manager_id Ring lock ID
 * @param descriptor_index Descriptor index
 * @return Physical address or 0 on error
 */
uint32_t vds_manager_get_ring_physical(uint16_t manager_id, 
                                       uint16_t descriptor_index);

/* Registry management */

/**
 * Get registry entry by manager ID
 * @param manager_id Manager ID
 * @return Entry pointer or NULL if not found
 */
const vds_registry_entry_t* vds_manager_get_entry(uint16_t manager_id);

/**
 * Perform registry cleanup
 * Removes stale and aged entries
 * @param max_age_ticks Maximum age in ticks
 * @return Number of entries cleaned
 */
int vds_manager_cleanup_stale(uint32_t max_age_ticks);

/**
 * Change lock policy
 * @param manager_id Manager ID
 * @param new_policy New policy
 * @return 0 on success, negative on error
 */
int vds_manager_change_policy(uint16_t manager_id, 
                              vds_lock_policy_t new_policy);

/* Statistics and debugging */

/**
 * Get manager statistics
 * @param stats Output statistics structure
 */
void vds_manager_get_stats(vds_manager_stats_t* stats);

/**
 * Dump registry for debugging
 * @param verbose Include detailed info
 */
void vds_manager_dump_registry(bool verbose);

/**
 * Validate registry integrity
 * @return 0 if valid, error count otherwise
 */
int vds_manager_validate_registry(void);

#ifdef __cplusplus
}
#endif

#endif /* VDS_MANAGER_H */