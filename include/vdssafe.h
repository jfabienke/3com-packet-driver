/**
 * @file vds_safety.h
 * @brief VDS Safety Layer - Production Hardening and Constraints
 * 
 * Middle layer of unified VDS architecture that provides:
 * - ISR context detection (CRITICAL per GPT-5)
 * - Device constraint validation
 * - Bounce buffer management
 * - 3-tier error recovery
 * - 64KB boundary checking
 */

#ifndef VDS_SAFETY_H
#define VDS_SAFETY_H

#include <stdint.h>
#include <stdbool.h>
#include "vds_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Enhanced error codes for safety layer */
typedef enum {
    VDS_SAFE_OK,                /* 0: Success */
    VDS_SAFE_NOT_PRESENT,       /* 1: VDS not present */
    VDS_SAFE_IN_ISR,            /* 2: CRITICAL: Called from ISR context */
    VDS_SAFE_BOUNDARY_VIOLATION,/* 3: Boundary violation */
    VDS_SAFE_ALIGNMENT_ERROR,   /* 4: Alignment error */
    VDS_SAFE_SG_TOO_LONG,       /* 5: S/G list too long */
    VDS_SAFE_NO_MEMORY,         /* 6: Out of memory */
    VDS_SAFE_BOUNCE_REQUIRED,   /* 7: Bounce buffer required */
    VDS_SAFE_INVALID_CONSTRAINTS,/* 8: Invalid constraints */
    VDS_SAFE_LOCK_FAILED,       /* 9: Lock failed */
    VDS_SAFE_RECOVERY_FAILED,   /* 10: Recovery failed */
    VDS_SAFE_UNKNOWN_ERROR      /* 11: Unknown error */
} vds_safe_error_t;

/* DMA constraints structure (per GPT-5 recommendation) */
typedef struct {
    uint8_t address_bits;       /* 24 for ISA, 32 for PCI */
    uint16_t max_sg_entries;    /* Max scatter/gather entries */
    uint32_t max_segment_len;   /* Max segment length */
    uint32_t no_cross_mask;     /* Boundary mask (0xFFFF for 64KB) */
    uint16_t alignment_mask;    /* Alignment requirement */
    bool require_contiguous;    /* Require contiguous buffer */
    bool allow_bounce;          /* Allow bounce buffer fallback */
} dma_constraints_t;

/* Safe lock result */
typedef struct {
    bool success;
    vds_safe_error_t error;
    uint16_t lock_handle;       /* VDS lock handle */
    uint32_t physical_addr;     /* Physical address */
    void far* bounce_buffer;    /* Our bounce buffer if used */
    uint32_t bounce_size;       /* Bounce buffer size */
    bool used_bounce;           /* True if we used our bounce buffer */
    bool vds_used_bounce;       /* True if VDS used ALTERNATE buffer (copy required) */
    bool is_scattered;          /* True if scatter/gather */
    uint16_t sg_count;          /* Number of S/G entries */
    vds_sg_entry_t* sg_list;    /* Scatter/gather list */
} vds_safe_lock_t;

/* Bounce buffer pool configuration */
#define BOUNCE_POOL_MIN_SIZE    (32 * 1024)    /* 32KB minimum */
#define BOUNCE_POOL_DEFAULT     (64 * 1024)    /* 64KB default pool */
#define BOUNCE_POOL_MAX_SIZE    (256 * 1024)   /* 256KB maximum */
#define BOUNCE_BLOCK_SIZE       4096            /* 4KB blocks */

/* Default constraints for common devices */
extern const dma_constraints_t ISA_DMA_CONSTRAINTS;    /* ISA bus DMA */
extern const dma_constraints_t PCI_DMA_CONSTRAINTS;    /* PCI bus DMA */
extern const dma_constraints_t NIC_3C509_CONSTRAINTS;  /* 3C509B NIC */
extern const dma_constraints_t NIC_3C515_CONSTRAINTS;  /* 3C515-TX NIC */

/* Safety layer initialization */

/**
 * Initialize VDS safety layer with default settings
 * Allocates default bounce buffer pool (64KB)
 */
int vds_safety_init(void);

/**
 * Initialize VDS safety layer with custom pool size
 * @param pool_size_kb Pool size in KB (32-256)
 * @return 0 on success, negative on error
 */
int vds_safety_init_ex(uint32_t pool_size_kb);

/**
 * Cleanup VDS safety layer
 * Frees bounce buffers and cleans up resources
 */
void vds_safety_cleanup(void);

/* ISR context detection */

/**
 * Check if currently in ISR context
 * CRITICAL: Must be called before any VDS operation
 * @return true if in ISR, false if safe to call VDS
 */
bool vds_in_isr_context(void);

/**
 * Set ISR context flag (called by ISR entry)
 */
void vds_enter_isr_context(void);

/**
 * Clear ISR context flag (called by ISR exit)
 */
void vds_exit_isr_context(void);

/**
 * Get current ISR nesting depth
 * @return Current nesting depth
 */
uint16_t vds_get_isr_nesting_depth(void);

/**
 * Set ISR nesting depth (for integration with module_bridge)
 * @param depth New nesting depth
 */
void vds_set_isr_nesting_depth(uint16_t depth);

/* Safe locking with constraints */

/**
 * Lock region with device constraints and recovery
 * @param addr Address to lock
 * @param size Size in bytes
 * @param constraints Device-specific constraints
 * @param direction Transfer direction (for copy determination)
 * @param lock Output lock structure
 * @return Error code
 */
vds_safe_error_t vds_lock_with_constraints(void far* addr, uint32_t size,
                                          const dma_constraints_t* constraints,
                                          vds_transfer_direction_t direction,
                                          vds_safe_lock_t* lock);

/**
 * Unlock region with cleanup
 * @param lock Lock to release
 * @return Error code
 */
vds_safe_error_t vds_unlock_safe(vds_safe_lock_t* lock);

/* Constraint validation */

/**
 * Check if buffer meets constraints
 * @param addr Buffer address
 * @param size Buffer size
 * @param constraints Device constraints
 * @return true if buffer is safe for DMA
 */
bool vds_check_constraints(void far* addr, uint32_t size,
                          const dma_constraints_t* constraints);

/**
 * Check 64KB boundary crossing
 * @param addr Address
 * @param size Size
 * @return true if crosses 64KB boundary
 */
bool vds_crosses_64k_boundary(void far* addr, uint32_t size);

/**
 * Find safe alignment to avoid boundary
 * @param addr Original address
 * @param size Size
 * @param boundary Boundary mask (e.g., 0xFFFF)
 * @return Aligned address
 */
uint32_t vds_find_safe_alignment(uint32_t addr, uint32_t size, uint32_t boundary);

/* Bounce buffer management */

/**
 * Allocate bounce buffer
 * @param size Required size
 * @param constraints Device constraints
 * @return Bounce buffer address or NULL
 */
void far* vds_allocate_bounce_buffer(uint32_t size, 
                                    const dma_constraints_t* constraints);

/**
 * Free bounce buffer
 * @param buffer Buffer to free
 */
void vds_free_bounce_buffer(void far* buffer);

/**
 * Copy to bounce buffer
 * @param bounce Bounce buffer
 * @param src Source data
 * @param size Size to copy
 */
void vds_copy_to_bounce(void far* bounce, void far* src, uint32_t size);

/**
 * Copy from bounce buffer
 * @param dst Destination
 * @param bounce Bounce buffer
 * @param size Size to copy
 */
void vds_copy_from_bounce(void far* dst, void far* bounce, uint32_t size);

/* Recovery mechanisms */

/**
 * Attempt 3-tier recovery for lock failure
 * @param addr Address to lock
 * @param size Size
 * @param constraints Constraints
 * @param direction Transfer direction
 * @param lock Output lock
 * @return Error code
 */
vds_safe_error_t vds_lock_with_recovery(void far* addr, uint32_t size,
                                       const dma_constraints_t* constraints,
                                       vds_transfer_direction_t direction,
                                       vds_safe_lock_t* lock);

/* Statistics and diagnostics */

typedef struct {
    uint32_t total_locks;
    uint32_t successful_locks;
    uint32_t failed_locks;
    uint32_t isr_rejections;       /* Rejected due to ISR context */
    uint32_t boundary_violations;
    uint32_t bounce_buffer_uses;    /* Our bounce buffer uses */
    uint32_t vds_bounce_uses;      /* VDS bounce buffer uses */
    uint32_t recovery_attempts;
    uint32_t recovery_successes;
    uint16_t bounce_pool_used;     /* Bytes used in bounce pool */
    uint16_t bounce_pool_size;     /* Total pool size */
} vds_safety_stats_t;

/**
 * Get safety layer statistics
 */
void vds_safety_get_stats(vds_safety_stats_t* stats);

/**
 * Reset safety layer statistics
 */
void vds_safety_reset_stats(void);

/**
 * Get error description
 */
const char* vds_safe_error_string(vds_safe_error_t error);

/* Scatter-gather coalescing */

/**
 * Coalesce scatter-gather list to minimize descriptors
 * Merges adjacent or nearby entries when possible
 * 
 * @param sg_list Input/output S/G list
 * @param sg_count Input count, updated with coalesced count
 * @param max_gap Maximum gap between entries to coalesce (bytes)
 * @return New count after coalescing
 */
uint16_t vds_coalesce_sg_list(vds_sg_entry_t* sg_list, uint16_t sg_count, 
                              uint32_t max_gap);

/**
 * Check if two S/G entries can be coalesced
 * @param entry1 First entry
 * @param entry2 Second entry
 * @param max_gap Maximum gap allowed
 * @return true if can be coalesced
 */
bool vds_can_coalesce_sg_entries(const vds_sg_entry_t* entry1,
                                 const vds_sg_entry_t* entry2,
                                 uint32_t max_gap);

#ifdef __cplusplus
}
#endif

#endif /* VDS_SAFETY_H */
