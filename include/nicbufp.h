/**
 * @file nic_buffer_pools.h
 * @brief Per-NIC Buffer Pool Management for 3Com Packet Driver
 * 
 * Sprint 1.5: Per-NIC Buffer Pool Implementation
 * 
 * This module implements per-NIC buffer pools to provide resource isolation,
 * eliminate contention between NICs, and enable per-NIC performance tuning.
 * This addresses the architectural gap where the current implementation uses
 * global buffer pools instead of per-NIC pools as specified in the design.
 *
 * Key Features:
 * - Resource isolation: Each NIC gets dedicated buffer resources
 * - No contention: NICs don't compete for buffer resources
 * - Performance tuning: Per-NIC optimization and statistics
 * - Scalability: Better multi-NIC performance characteristics
 * - Integration: Works with existing capability system and RX_COPYBREAK
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 */

#ifndef _NIC_BUFFER_POOLS_H_
#define _NIC_BUFFER_POOLS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "bufaloc.h"
#include "nic_defs.h"

/* Forward declarations */
typedef struct nic_buffer_context nic_buffer_context_t;
typedef struct multi_nic_buffer_manager multi_nic_buffer_manager_t;

/**
 * @brief NIC identifier type for buffer pool management
 */
typedef uint8_t nic_id_t;
#define INVALID_NIC_ID          0xFF
#define NIC_ID_BROADCAST        0xFE    /* For operations affecting all NICs */

/**
 * @brief Memory tier preference for buffer allocation
 */
typedef enum {
    MEMORY_TIER_CONVENTIONAL = 0,       /* Use conventional memory */
    MEMORY_TIER_UMB,                    /* Prefer Upper Memory Blocks */
    MEMORY_TIER_XMS,                    /* Prefer Extended Memory */
    MEMORY_TIER_AUTO                    /* Auto-select based on availability */
} memory_tier_t;

/**
 * @brief Buffer pool statistics for per-NIC tracking
 */
typedef struct buffer_pool_stats {
    /* Allocation statistics */
    uint32_t total_allocations;         /* Total buffer allocations */
    uint32_t total_frees;               /* Total buffer frees */
    uint32_t allocation_failures;       /* Failed allocation attempts */
    uint32_t current_allocated;         /* Currently allocated buffers */
    uint32_t peak_allocated;            /* Peak allocation count */
    
    /* Memory usage statistics */
    uint32_t bytes_allocated;           /* Total bytes allocated */
    uint32_t bytes_freed;               /* Total bytes freed */
    uint32_t current_memory_usage;      /* Current memory usage */
    uint32_t peak_memory_usage;         /* Peak memory usage */
    
    /* Performance statistics */
    uint32_t fast_path_hits;            /* Size-specific pool hits */
    uint32_t fallback_allocations;      /* Fallback to larger pools */
    uint32_t pool_contentions;          /* Pool contention events */
    uint32_t rebalance_operations;      /* Resource rebalancing count */
    
    /* Error statistics */
    uint32_t pool_overflows;            /* Pool overflow events */
    uint32_t memory_fragmentation;      /* Fragmentation issues */
    uint32_t emergency_allocations;     /* Emergency fallback allocations */
} buffer_pool_stats_t;

/**
 * @brief Per-NIC buffer pool context for resource isolation
 * 
 * Each NIC gets its own dedicated set of buffer pools, providing complete
 * resource isolation and eliminating contention between NICs.
 */
typedef struct nic_buffer_context {
    /* NIC identification */
    nic_id_t nic_id;                    /* Unique NIC identifier */
    nic_type_t nic_type;                /* Type of NIC (3C509B, 3C515-TX) */
    char nic_name[32];                  /* Human-readable NIC name */
    bool initialized;                   /* Context initialization flag */
    
    /* === Primary Buffer Pools for Resource Isolation === */
    buffer_pool_t tx_pool;              /* Dedicated TX buffers for this NIC */
    buffer_pool_t rx_pool;              /* Dedicated RX buffers for this NIC */
    buffer_pool_t dma_pool;             /* Dedicated DMA buffers (3C515-TX only) */
    
    /* === Size-Optimized Buffer Pools for Performance === */
    buffer_pool_t small_pool;           /* < 128 bytes (control packets, ACKs) */
    buffer_pool_t medium_pool;          /* 128-512 bytes (small data packets) */
    buffer_pool_t large_pool;           /* > 512 bytes (large data transfers) */
    buffer_pool_t jumbo_pool;           /* Maximum size packets (1518 bytes) */
    
    /* === RX_COPYBREAK Integration === */
    rx_copybreak_pool_t copybreak_pool; /* Per-NIC RX_COPYBREAK optimization */
    uint32_t copybreak_threshold;       /* NIC-specific copybreak threshold */
    bool copybreak_enabled;             /* RX_COPYBREAK enabled for this NIC */
    
    /* === Resource Management === */
    uint32_t allocated_memory;          /* Total memory allocated to this NIC */
    uint32_t memory_limit;              /* Maximum memory allowed for this NIC */
    uint32_t memory_reserved;           /* Reserved memory for critical operations */
    memory_tier_t memory_preference;    /* Preferred memory tier for this NIC */
    
    /* === Performance Tuning === */
    uint32_t tx_buffer_count;           /* Number of TX buffers */
    uint32_t rx_buffer_count;           /* Number of RX buffers */
    uint32_t dma_buffer_count;          /* Number of DMA buffers */
    uint32_t small_buffer_count;        /* Number of small buffers */
    uint32_t medium_buffer_count;       /* Number of medium buffers */
    uint32_t large_buffer_count;        /* Number of large buffers */
    uint32_t jumbo_buffer_count;        /* Number of jumbo buffers */
    
    /* === Statistics and Monitoring === */
    buffer_pool_stats_t stats;          /* Comprehensive buffer statistics */
    uint32_t last_rebalance_time;       /* Last resource rebalancing timestamp */
    uint32_t activity_level;            /* NIC activity level (0-100) */
    bool needs_rebalancing;             /* Flag indicating rebalancing needed */
    
    /* === Error Handling === */
    uint32_t allocation_errors;         /* Allocation error count */
    uint32_t last_error_time;           /* Last error timestamp */
    buffer_error_t last_error;          /* Last buffer error encountered */
} nic_buffer_context_t;

/**
 * @brief Multi-NIC buffer manager for coordinating all per-NIC pools
 * 
 * Central management structure that coordinates buffer allocation across
 * multiple NICs, handles resource balancing, and provides global oversight.
 */
typedef struct multi_nic_buffer_manager {
    /* === NIC Management === */
    nic_buffer_context_t nics[MAX_NICS]; /* Per-NIC buffer contexts */
    uint8_t nic_count;                    /* Number of active NICs */
    bool initialized;                     /* Manager initialization flag */
    
    /* === Global Memory Management === */
    uint32_t total_allocated;            /* Total driver memory usage */
    uint32_t memory_limit;               /* Maximum allowed memory */
    uint32_t memory_reserved;            /* Reserved memory for system */
    memory_tier_t memory_preference;     /* Global memory preference */
    
    /* === Resource Balancing === */
    uint32_t rebalance_interval;         /* Rebalancing check interval (ms) */
    uint32_t last_global_rebalance;      /* Last global rebalancing time */
    bool auto_rebalancing;               /* Automatic rebalancing enabled */
    uint32_t rebalance_threshold;        /* Threshold for triggering rebalancing */
    
    /* === Performance Monitoring === */
    uint32_t total_allocations;          /* Total allocations across all NICs */
    uint32_t allocation_failures;        /* Total allocation failures */
    uint32_t resource_contentions;       /* Inter-NIC resource contentions */
    uint32_t emergency_situations;       /* Emergency resource situations */
    
    /* === Configuration === */
    uint32_t default_memory_per_nic;     /* Default memory allocation per NIC */
    uint32_t min_memory_per_nic;         /* Minimum memory per NIC */
    uint32_t max_memory_per_nic;         /* Maximum memory per NIC */
    bool strict_isolation;               /* Strict resource isolation mode */
} multi_nic_buffer_manager_t;

/* === Buffer Pool Configuration Constants === */

/* Default buffer counts per NIC */
#define DEFAULT_TX_BUFFERS_PER_NIC      16      /* TX buffers per NIC */
#define DEFAULT_RX_BUFFERS_PER_NIC      32      /* RX buffers per NIC */
#define DEFAULT_DMA_BUFFERS_PER_NIC     8       /* DMA buffers per NIC */

/* Size-specific pool defaults */
#define DEFAULT_SMALL_BUFFERS_PER_NIC   24      /* Small buffer count */
#define DEFAULT_MEDIUM_BUFFERS_PER_NIC  16      /* Medium buffer count */
#define DEFAULT_LARGE_BUFFERS_PER_NIC   12      /* Large buffer count */
#define DEFAULT_JUMBO_BUFFERS_PER_NIC   8       /* Jumbo buffer count */

/* Memory allocation defaults */
#define DEFAULT_MEMORY_PER_NIC_KB       128     /* Default memory per NIC (KB) */
#define MIN_MEMORY_PER_NIC_KB          64      /* Minimum memory per NIC (KB) */
#define MAX_MEMORY_PER_NIC_KB          512     /* Maximum memory per NIC (KB) */

/* Rebalancing configuration */
#define DEFAULT_REBALANCE_INTERVAL_MS   5000    /* Rebalancing check interval */
#define DEFAULT_REBALANCE_THRESHOLD     75      /* Threshold percentage for rebalancing */

/* Size thresholds for buffer pools */
#define SMALL_BUFFER_THRESHOLD          128     /* Small buffer size threshold */
#define MEDIUM_BUFFER_THRESHOLD         512     /* Medium buffer size threshold */
#define LARGE_BUFFER_THRESHOLD          1024    /* Large buffer size threshold */

/* === Function Prototypes === */

/* === Initialization and Cleanup === */

/**
 * @brief Initialize the multi-NIC buffer pool manager
 * @param memory_limit Total memory limit for all buffer pools
 * @param memory_preference Preferred memory tier
 * @return SUCCESS on success, error code on failure
 */
int nic_buffer_pool_manager_init(uint32_t memory_limit, memory_tier_t memory_preference);

/**
 * @brief Cleanup the multi-NIC buffer pool manager
 */
void nic_buffer_pool_manager_cleanup(void);

/**
 * @brief Create buffer pools for a specific NIC
 * @param nic_id Unique NIC identifier
 * @param nic_type Type of NIC
 * @param nic_name Human-readable NIC name
 * @return SUCCESS on success, error code on failure
 */
int nic_buffer_pool_create(nic_id_t nic_id, nic_type_t nic_type, const char* nic_name);

/**
 * @brief Destroy buffer pools for a specific NIC
 * @param nic_id NIC identifier
 * @return SUCCESS on success, error code on failure
 */
int nic_buffer_pool_destroy(nic_id_t nic_id);

/* === Buffer Allocation and Deallocation === */

/**
 * @brief Allocate buffer from a specific NIC's pools
 * @param nic_id NIC identifier
 * @param type Buffer type (TX, RX, DMA)
 * @param size Requested buffer size
 * @return Buffer descriptor or NULL on failure
 */
buffer_desc_t* nic_buffer_alloc(nic_id_t nic_id, buffer_type_t type, uint32_t size);

/**
 * @brief Free buffer back to the appropriate NIC pool
 * @param nic_id NIC identifier
 * @param buffer Buffer descriptor to free
 */
void nic_buffer_free(nic_id_t nic_id, buffer_desc_t* buffer);

/**
 * @brief Allocate buffer optimized for Ethernet frame size
 * @param nic_id NIC identifier
 * @param frame_size Expected frame size
 * @param type Buffer type
 * @return Buffer descriptor or NULL on failure
 */
buffer_desc_t* nic_buffer_alloc_ethernet_frame(nic_id_t nic_id, uint32_t frame_size, buffer_type_t type);

/**
 * @brief Allocate DMA-capable buffer with specific alignment
 * @param nic_id NIC identifier
 * @param size Buffer size
 * @param alignment Required alignment
 * @return Buffer descriptor or NULL on failure
 */
buffer_desc_t* nic_buffer_alloc_dma(nic_id_t nic_id, uint32_t size, uint32_t alignment);

/* === RX_COPYBREAK Integration === */

/**
 * @brief Initialize RX_COPYBREAK for a specific NIC
 * @param nic_id NIC identifier
 * @param small_count Number of small buffers
 * @param large_count Number of large buffers
 * @param threshold Copybreak threshold
 * @return SUCCESS on success, error code on failure
 */
int nic_rx_copybreak_init(nic_id_t nic_id, uint32_t small_count, uint32_t large_count, uint32_t threshold);

/**
 * @brief Allocate buffer using per-NIC RX_COPYBREAK optimization
 * @param nic_id NIC identifier
 * @param packet_size Size of packet
 * @return Buffer descriptor or NULL on failure
 */
buffer_desc_t* nic_rx_copybreak_alloc(nic_id_t nic_id, uint32_t packet_size);

/**
 * @brief Free RX_COPYBREAK buffer
 * @param nic_id NIC identifier
 * @param buffer Buffer descriptor to free
 */
void nic_rx_copybreak_free(nic_id_t nic_id, buffer_desc_t* buffer);

/* === Resource Management === */

/**
 * @brief Balance buffer resources across all NICs
 * @return SUCCESS on success, error code on failure
 */
int balance_buffer_resources(void);

/**
 * @brief Adjust buffer allocation for a specific NIC
 * @param nic_id NIC identifier
 * @param new_allocation New memory allocation in KB
 * @return SUCCESS on success, error code on failure
 */
int adjust_nic_buffer_allocation(nic_id_t nic_id, uint32_t new_allocation);

/**
 * @brief Monitor buffer usage across all NICs
 */
void monitor_nic_buffer_usage(void);

/**
 * @brief Set memory limit for a specific NIC
 * @param nic_id NIC identifier
 * @param limit_kb Memory limit in KB
 * @return SUCCESS on success, error code on failure
 */
int nic_buffer_set_memory_limit(nic_id_t nic_id, uint32_t limit_kb);

/**
 * @brief Configure buffer pool sizes for a NIC
 * @param nic_id NIC identifier
 * @param tx_count TX buffer count
 * @param rx_count RX buffer count
 * @param dma_count DMA buffer count
 * @return SUCCESS on success, error code on failure
 */
int nic_buffer_configure_pools(nic_id_t nic_id, uint32_t tx_count, uint32_t rx_count, uint32_t dma_count);

/* === Statistics and Monitoring === */

/**
 * @brief Get buffer statistics for a specific NIC
 * @param nic_id NIC identifier
 * @param stats Pointer to receive statistics
 * @return SUCCESS on success, error code on failure
 */
int nic_buffer_get_stats(nic_id_t nic_id, buffer_pool_stats_t* stats);

/**
 * @brief Get global buffer manager statistics
 * @param total_allocated Pointer to receive total allocated memory
 * @param active_nics Pointer to receive active NIC count
 * @param contentions Pointer to receive contention count
 * @return SUCCESS on success, error code on failure
 */
int nic_buffer_get_global_stats(uint32_t* total_allocated, uint32_t* active_nics, uint32_t* contentions);

/**
 * @brief Print comprehensive buffer statistics for all NICs
 */
void nic_buffer_print_all_stats(void);

/**
 * @brief Clear statistics for a specific NIC
 * @param nic_id NIC identifier
 * @return SUCCESS on success, error code on failure
 */
int nic_buffer_clear_stats(nic_id_t nic_id);

/* === Utility Functions === */

/**
 * @brief Get NIC context by ID
 * @param nic_id NIC identifier
 * @return Pointer to NIC context or NULL if not found
 */
nic_buffer_context_t* nic_buffer_get_context(nic_id_t nic_id);

/**
 * @brief Check if NIC buffer pools are initialized
 * @param nic_id NIC identifier
 * @return true if initialized, false otherwise
 */
bool nic_buffer_is_initialized(nic_id_t nic_id);

/**
 * @brief Get available memory for a NIC
 * @param nic_id NIC identifier
 * @return Available memory in bytes, 0 if NIC not found
 */
uint32_t nic_buffer_get_available_memory(nic_id_t nic_id);

/**
 * @brief Calculate optimal buffer allocation for NIC type
 * @param nic_type Type of NIC
 * @param tx_count Pointer to receive optimal TX count
 * @param rx_count Pointer to receive optimal RX count
 * @param dma_count Pointer to receive optimal DMA count
 * @return SUCCESS on success, error code on failure
 */
int nic_buffer_calculate_optimal_allocation(nic_type_t nic_type, uint32_t* tx_count, uint32_t* rx_count, uint32_t* dma_count);

/* === Backward Compatibility === */

/**
 * @brief Get global buffer pool for legacy code compatibility
 * @param type Buffer type
 * @return Pointer to first available NIC's pool of the specified type
 */
buffer_pool_t* nic_buffer_get_legacy_pool(buffer_type_t type);

/**
 * @brief Allocate buffer using legacy interface with automatic NIC selection
 * @param type Buffer type
 * @return Buffer descriptor or NULL on failure
 */
buffer_desc_t* nic_buffer_alloc_legacy(buffer_type_t type);

/**
 * @brief Free buffer using legacy interface
 * @param buffer Buffer descriptor to free
 */
void nic_buffer_free_legacy(buffer_desc_t* buffer);

/* === Error Handling === */

/**
 * @brief Get last error for NIC buffer operations
 * @param nic_id NIC identifier
 * @return Last buffer error
 */
buffer_error_t nic_buffer_get_last_error(nic_id_t nic_id);

/**
 * @brief Set error handler for NIC buffer operations
 * @param nic_id NIC identifier
 * @param handler Error handler function
 * @return SUCCESS on success, error code on failure
 */
int nic_buffer_set_error_handler(nic_id_t nic_id, void (*handler)(buffer_error_t error, const char* message));

/* === Debug and Diagnostics === */

/**
 * @brief Dump buffer pool information for a NIC
 * @param nic_id NIC identifier
 */
void nic_buffer_dump_pools(nic_id_t nic_id);

/**
 * @brief Validate buffer pool integrity for a NIC
 * @param nic_id NIC identifier
 * @return SUCCESS if valid, error code if corruption detected
 */
int nic_buffer_validate_integrity(nic_id_t nic_id);

/**
 * @brief Dump global buffer manager state
 */
void nic_buffer_dump_manager_state(void);

#ifdef __cplusplus
}
#endif

#endif /* _NIC_BUFFER_POOLS_H_ */
