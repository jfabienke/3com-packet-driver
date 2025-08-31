/**
 * @file mempool_api.h
 * @brief Stable Memory Pool API for 3Com Packet Driver Modules
 * 
 * Agent 11 - Memory Management - Day 3-4 Deliverable
 * 
 * This header defines the stable, frozen API that all modules use for
 * memory allocation. It provides a unified interface with DMA-safe
 * guarantees and optimal performance across all supported CPUs.
 * 
 * API VERSION: 1.0 (FROZEN)
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef MEMPOOL_API_H
#define MEMPOOL_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* API version for compatibility checking */
#define MEMPOOL_API_VERSION_MAJOR   1
#define MEMPOOL_API_VERSION_MINOR   0
#define MEMPOOL_API_VERSION        ((MEMPOOL_API_VERSION_MAJOR << 8) | MEMPOOL_API_VERSION_MINOR)

/* Maximum safe allocation sizes */
#define MEMPOOL_MAX_SINGLE_ALLOC    32768   /* 32KB max per allocation */
#define MEMPOOL_MAX_TOTAL_ALLOC     262144  /* 256KB total per module */
#define MEMPOOL_MAX_DMA_BUFFERS     64      /* Maximum DMA buffers */

/* Memory alignment constants */
#define MEMPOOL_ALIGN_BYTE          1
#define MEMPOOL_ALIGN_WORD          2
#define MEMPOOL_ALIGN_DWORD         4
#define MEMPOOL_ALIGN_PARAGRAPH     16      /* DOS paragraph */
#define MEMPOOL_ALIGN_CACHE         32      /* Cache line alignment */
#define MEMPOOL_ALIGN_DMA           16      /* DMA minimum */
#define MEMPOOL_ALIGN_DESCRIPTOR    32      /* DMA descriptor rings */

/* Boundary constraints */
#define MEMPOOL_64KB_BOUNDARY       0x10000    /* 64KB boundary */
#define MEMPOOL_16MB_LIMIT          0x1000000  /* ISA DMA limit */

/*============================================================================
 * API RESULT CODES
 *============================================================================*/

typedef enum {
    MEMPOOL_SUCCESS             =  0,   /**< Operation successful */
    MEMPOOL_ERROR_INVALID_PARAM = -1,   /**< Invalid parameter */
    MEMPOOL_ERROR_OUT_OF_MEMORY = -2,   /**< Insufficient memory */
    MEMPOOL_ERROR_ALIGNMENT     = -3,   /**< Alignment violation */
    MEMPOOL_ERROR_BOUNDARY      = -4,   /**< 64KB boundary violation */
    MEMPOOL_ERROR_SIZE_LIMIT    = -5,   /**< Size exceeds limits */
    MEMPOOL_ERROR_NOT_FOUND     = -6,   /**< Buffer not found */
    MEMPOOL_ERROR_ALREADY_LOCKED= -7,   /**< Buffer already locked */
    MEMPOOL_ERROR_NOT_LOCKED    = -8,   /**< Buffer not locked */
    MEMPOOL_ERROR_CORRUPTION    = -9,   /**< Buffer corruption detected */
    MEMPOOL_ERROR_NOT_INITIALIZED=-10,  /**< Memory pool not initialized */
    MEMPOOL_ERROR_QUOTA_EXCEEDED = -11, /**< Module quota exceeded */
    MEMPOOL_ERROR_ISA_LIMIT     = -12   /**< Above 16MB ISA limit */
} mempool_result_t;

/*============================================================================
 * MEMORY TYPE AND FLAG DEFINITIONS
 *============================================================================*/

/**
 * @brief Memory tier preference for allocation
 */
typedef enum {
    MEMPOOL_TIER_XMS        = 0x01,     /**< XMS extended memory */
    MEMPOOL_TIER_UMB        = 0x02,     /**< Upper memory blocks */
    MEMPOOL_TIER_CONVENTIONAL = 0x04,   /**< Conventional memory */
    MEMPOOL_TIER_AUTO       = 0x07,     /**< Automatic tier selection */
    MEMPOOL_TIER_DMA_CAPABLE = 0x10     /**< Must be DMA-capable */
} mempool_tier_t;

/**
 * @brief Memory allocation flags
 */
typedef enum {
    MEMPOOL_FLAG_ZERO       = 0x0001,   /**< Zero-initialize memory */
    MEMPOOL_FLAG_ALIGN      = 0x0002,   /**< Enforce alignment */
    MEMPOOL_FLAG_DMA_SAFE   = 0x0004,   /**< DMA-safe allocation */
    MEMPOOL_FLAG_ISR_SAFE   = 0x0008,   /**< ISR-safe try-lock only */
    MEMPOOL_FLAG_PERSISTENT = 0x0010,   /**< Long-lived allocation */
    MEMPOOL_FLAG_TEMPORARY  = 0x0020,   /**< Short-lived allocation */
    MEMPOOL_FLAG_POOLED     = 0x0040,   /**< Use pool allocation */
    MEMPOOL_FLAG_GUARD      = 0x0080    /**< Add guard patterns */
} mempool_flags_t;

/**
 * @brief DMA device types for optimization
 */
typedef enum {
    MEMPOOL_DMA_DEVICE_NETWORK  = 0x01, /**< Network interface */
    MEMPOOL_DMA_DEVICE_STORAGE  = 0x02, /**< Storage device */
    MEMPOOL_DMA_DEVICE_GENERIC  = 0xFF  /**< Generic DMA device */
} mempool_dma_device_t;

/*============================================================================
 * BUFFER DESCRIPTORS AND HANDLES
 *============================================================================*/

/**
 * @brief Memory buffer handle (opaque)
 */
typedef struct mempool_buffer* mempool_handle_t;

/**
 * @brief Buffer information structure
 */
typedef struct {
    void*               address;        /**< Buffer virtual address */
    uint32_t            physical_addr;  /**< Physical address (DOS = virtual) */
    size_t              size;           /**< Buffer size in bytes */
    size_t              alignment;      /**< Buffer alignment */
    mempool_tier_t      tier;          /**< Memory tier used */
    mempool_flags_t     flags;         /**< Allocation flags */
    uint8_t             ref_count;      /**< Reference count */
    bool                is_locked;      /**< DMA locked status */
    uint32_t            alloc_time;     /**< Allocation timestamp */
    uint8_t             owner_id;       /**< Owner module ID */
} mempool_buffer_info_t;

/**
 * @brief Memory statistics
 */
typedef struct {
    /* Allocation statistics */
    uint32_t total_allocations;     /**< Total allocations made */
    uint32_t active_allocations;    /**< Currently active allocations */
    uint32_t peak_allocations;      /**< Peak simultaneous allocations */
    
    /* Memory usage */
    size_t   bytes_allocated;       /**< Total bytes allocated */
    size_t   bytes_in_use;          /**< Bytes currently in use */
    size_t   peak_usage;            /**< Peak memory usage */
    
    /* Memory tiers */
    size_t   xms_available;         /**< XMS memory available */
    size_t   xms_used;              /**< XMS memory in use */
    size_t   umb_available;         /**< UMB memory available */
    size_t   umb_used;              /**< UMB memory in use */
    size_t   conventional_available;/**< Conventional memory available */
    size_t   conventional_used;     /**< Conventional memory in use */
    
    /* Error statistics */
    uint32_t allocation_failures;   /**< Failed allocations */
    uint32_t boundary_violations;   /**< 64KB boundary violations */
    uint32_t corruption_detected;   /**< Buffer corruption instances */
    uint32_t isa_limit_violations;  /**< ISA 16MB limit violations */
    
    /* Performance statistics */
    uint32_t pool_hits;             /**< Pool allocation hits */
    uint32_t pool_misses;           /**< Pool allocation misses */
    uint32_t dma_operations;        /**< DMA operations performed */
    uint16_t fragmentation_pct;     /**< Memory fragmentation percentage */
} mempool_statistics_t;

/*============================================================================
 * CORE MEMORY POOL API
 *============================================================================*/

/**
 * @brief Initialize memory pool system
 * 
 * Must be called before any other memory pool operations.
 * 
 * @param module_id Unique module identifier (0x01-0xFF)
 * @param quota_bytes Maximum memory this module can allocate (0 = no limit)
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_init(uint8_t module_id, size_t quota_bytes);

/**
 * @brief Shutdown memory pool for this module
 * 
 * Frees all allocations made by this module and releases resources.
 * 
 * @param module_id Module identifier used in mempool_init
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_shutdown(uint8_t module_id);

/**
 * @brief Allocate memory buffer
 * 
 * @param size Buffer size in bytes (1 - MEMPOOL_MAX_SINGLE_ALLOC)
 * @param alignment Required alignment (must be power of 2)
 * @param tier Memory tier preference
 * @param flags Allocation flags
 * @param handle_out Pointer to store buffer handle
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_alloc(size_t size, 
                              size_t alignment,
                              mempool_tier_t tier,
                              mempool_flags_t flags,
                              mempool_handle_t* handle_out);

/**
 * @brief Free memory buffer
 * 
 * @param handle Buffer handle to free
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_free(mempool_handle_t handle);

/**
 * @brief Get buffer address and information
 * 
 * @param handle Buffer handle
 * @param info_out Pointer to receive buffer information
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_get_info(mempool_handle_t handle, mempool_buffer_info_t* info_out);

/**
 * @brief Get buffer virtual address
 * 
 * @param handle Buffer handle
 * @param address_out Pointer to store buffer address
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_get_address(mempool_handle_t handle, void** address_out);

/*============================================================================
 * DMA-SAFE BUFFER ALLOCATION
 *============================================================================*/

/**
 * @brief Allocate DMA-safe buffer with 64KB boundary compliance
 * 
 * This function guarantees:
 * - Buffer does not cross 64KB boundaries
 * - Buffer is below 16MB for ISA compatibility (if requested)
 * - Proper alignment for DMA operations
 * - Physical address availability
 * 
 * @param size Buffer size in bytes
 * @param alignment Required alignment (≥ MEMPOOL_ALIGN_DMA)
 * @param device_type Type of DMA device for optimization
 * @param device_id Device identifier
 * @param flags DMA-specific flags (MEMPOOL_FLAG_DMA_SAFE implied)
 * @param handle_out Pointer to store buffer handle
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_alloc_dma(size_t size,
                                  size_t alignment,
                                  mempool_dma_device_t device_type,
                                  uint8_t device_id,
                                  mempool_flags_t flags,
                                  mempool_handle_t* handle_out);

/**
 * @brief Lock DMA buffer for hardware access
 * 
 * @param handle DMA buffer handle
 * @param physical_addr_out Pointer to store physical address
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_dma_lock(mempool_handle_t handle, uint32_t* physical_addr_out);

/**
 * @brief Unlock DMA buffer after hardware access
 * 
 * @param handle DMA buffer handle
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_dma_unlock(mempool_handle_t handle);

/**
 * @brief Prepare buffer for DMA operation (cache management)
 * 
 * @param handle Buffer handle
 * @param direction DMA direction (not used in DOS but API compatible)
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_dma_sync_before(mempool_handle_t handle, int direction);

/**
 * @brief Complete DMA operation (cache management)
 * 
 * @param handle Buffer handle
 * @param direction DMA direction
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_dma_sync_after(mempool_handle_t handle, int direction);

/*============================================================================
 * POOL-BASED ALLOCATION (HIGH PERFORMANCE)
 *============================================================================*/

/**
 * @brief Allocate from pre-sized pool for common packet sizes
 * 
 * This is the fastest allocation path for network packets.
 * 
 * @param packet_size Expected packet size (determines pool selection)
 * @param flags Pool allocation flags
 * @param handle_out Pointer to store buffer handle
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_alloc_packet(size_t packet_size,
                                     mempool_flags_t flags,
                                     mempool_handle_t* handle_out);

/**
 * @brief Allocate temporary buffer (optimized for short lifetime)
 * 
 * @param size Buffer size
 * @param handle_out Pointer to store buffer handle
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_alloc_temp(size_t size, mempool_handle_t* handle_out);

/**
 * @brief Allocate persistent buffer (optimized for long lifetime)
 * 
 * @param size Buffer size
 * @param alignment Required alignment
 * @param handle_out Pointer to store buffer handle
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_alloc_persistent(size_t size,
                                         size_t alignment,
                                         mempool_handle_t* handle_out);

/*============================================================================
 * REFERENCE COUNTING AND SHARING
 *============================================================================*/

/**
 * @brief Add reference to buffer (increment reference count)
 * 
 * @param handle Buffer handle
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_addref(mempool_handle_t handle);

/**
 * @brief Release reference to buffer (decrement reference count)
 * 
 * Buffer is automatically freed when reference count reaches zero.
 * 
 * @param handle Buffer handle
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_release(mempool_handle_t handle);

/**
 * @brief Get current reference count
 * 
 * @param handle Buffer handle
 * @param ref_count_out Pointer to store reference count
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_get_refcount(mempool_handle_t handle, uint8_t* ref_count_out);

/*============================================================================
 * MEMORY OPERATIONS (CPU-OPTIMIZED)
 *============================================================================*/

/**
 * @brief CPU-optimized memory copy with 64KB boundary safety
 * 
 * @param dest Destination buffer
 * @param src Source buffer
 * @param size Number of bytes to copy
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_copy(void* dest, const void* src, size_t size);

/**
 * @brief CPU-optimized memory move (handles overlap)
 * 
 * @param dest Destination buffer
 * @param src Source buffer  
 * @param size Number of bytes to move
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_move(void* dest, const void* src, size_t size);

/**
 * @brief CPU-optimized memory set
 * 
 * @param dest Destination buffer
 * @param value Fill value
 * @param size Number of bytes to set
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_set(void* dest, uint8_t value, size_t size);

/**
 * @brief CPU-optimized memory compare
 * 
 * @param buf1 First buffer
 * @param buf2 Second buffer
 * @param size Number of bytes to compare
 * @param result_out Pointer to store result (-1, 0, 1)
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_compare(const void* buf1, const void* buf2, size_t size, int* result_out);

/*============================================================================
 * VALIDATION AND DEBUGGING
 *============================================================================*/

/**
 * @brief Validate buffer handle and check for corruption
 * 
 * @param handle Buffer handle to validate
 * @return MEMPOOL_SUCCESS if valid, error code if invalid/corrupted
 */
mempool_result_t mempool_validate(mempool_handle_t handle);

/**
 * @brief Validate all buffers owned by this module
 * 
 * @param module_id Module identifier
 * @param corrupt_count_out Pointer to store number of corrupted buffers
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_validate_all(uint8_t module_id, uint32_t* corrupt_count_out);

/**
 * @brief Check if address range is valid and safe
 * 
 * @param address Buffer address
 * @param size Buffer size
 * @return MEMPOOL_SUCCESS if valid, error code if invalid
 */
mempool_result_t mempool_validate_range(const void* address, size_t size);

/**
 * @brief Check if buffer is DMA-safe (64KB boundary compliant)
 * 
 * @param address Buffer address
 * @param size Buffer size
 * @return MEMPOOL_SUCCESS if DMA-safe, error code if boundary violation
 */
mempool_result_t mempool_validate_dma_safe(const void* address, size_t size);

/*============================================================================
 * STATISTICS AND MONITORING
 *============================================================================*/

/**
 * @brief Get memory pool statistics for this module
 * 
 * @param module_id Module identifier
 * @param stats_out Pointer to receive statistics
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_get_stats(uint8_t module_id, mempool_statistics_t* stats_out);

/**
 * @brief Clear statistics counters for this module
 * 
 * @param module_id Module identifier
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_clear_stats(uint8_t module_id);

/**
 * @brief Get global memory pool statistics
 * 
 * @param stats_out Pointer to receive global statistics
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_get_global_stats(mempool_statistics_t* stats_out);

/**
 * @brief Print memory pool status to log
 * 
 * @param module_id Module identifier (0 for all modules)
 * @return MEMPOOL_SUCCESS on success, error code on failure
 */
mempool_result_t mempool_print_status(uint8_t module_id);

/*============================================================================
 * UTILITY MACROS
 *============================================================================*/

/**
 * @brief Get buffer address from handle (inline for performance)
 */
#define MEMPOOL_GET_ADDRESS(handle, ptr_var) \
    do { \
        mempool_result_t _result = mempool_get_address((handle), (void**)&(ptr_var)); \
        if (_result != MEMPOOL_SUCCESS) { \
            (ptr_var) = NULL; \
        } \
    } while(0)

/**
 * @brief Safe buffer free with null check
 */
#define MEMPOOL_SAFE_FREE(handle) \
    do { \
        if ((handle) != NULL) { \
            mempool_free(handle); \
            (handle) = NULL; \
        } \
    } while(0)

/**
 * @brief Check if size is valid for allocation
 */
#define MEMPOOL_SIZE_IS_VALID(size) \
    ((size) > 0 && (size) <= MEMPOOL_MAX_SINGLE_ALLOC)

/**
 * @brief Check if alignment is valid (power of 2)
 */
#define MEMPOOL_ALIGNMENT_IS_VALID(align) \
    ((align) > 0 && ((align) & ((align) - 1)) == 0)

/*============================================================================
 * CONVENIENCE FUNCTIONS
 *============================================================================*/

/**
 * @brief Allocate zero-initialized memory
 */
static inline mempool_result_t mempool_calloc(size_t size,
                                             size_t alignment,
                                             mempool_tier_t tier,
                                             mempool_handle_t* handle_out) {
    return mempool_alloc(size, alignment, tier, MEMPOOL_FLAG_ZERO, handle_out);
}

/**
 * @brief Allocate DMA descriptor ring
 */
static inline mempool_result_t mempool_alloc_descriptors(size_t count,
                                                        size_t descriptor_size,
                                                        mempool_handle_t* handle_out) {
    return mempool_alloc_dma(count * descriptor_size,
                           MEMPOOL_ALIGN_DESCRIPTOR,
                           MEMPOOL_DMA_DEVICE_NETWORK,
                           0,
                           MEMPOOL_FLAG_ZERO | MEMPOOL_FLAG_PERSISTENT,
                           handle_out);
}

/**
 * @brief Get API version for compatibility checking
 */
static inline uint16_t mempool_get_api_version(void) {
    return MEMPOOL_API_VERSION;
}

/**
 * @brief Convert result code to string
 */
const char* mempool_result_to_string(mempool_result_t result);

/*============================================================================
 * ERROR HANDLING MACROS
 *============================================================================*/

/**
 * @brief Check result and return on error
 */
#define MEMPOOL_CHECK_RESULT(call) \
    do { \
        mempool_result_t _result = (call); \
        if (_result != MEMPOOL_SUCCESS) { \
            return _result; \
        } \
    } while(0)

/**
 * @brief Check result and goto error label on failure
 */
#define MEMPOOL_CHECK_RESULT_GOTO(call, label) \
    do { \
        mempool_result_t _result = (call); \
        if (_result != MEMPOOL_SUCCESS) { \
            goto label; \
        } \
    } while(0)

#ifdef __cplusplus
}
#endif

#endif /* MEMPOOL_API_H */