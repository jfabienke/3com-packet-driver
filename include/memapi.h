/**
 * @file memory_api.h
 * @brief Memory Management API for 3Com Packet Driver Modules
 * 
 * Phase 3A: Dynamic Module Loading - Stream 1 Critical Path
 * 
 * This header defines the memory management interface for DOS memory
 * constraints, including XMS, UMB, and conventional memory handling.
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef MEMORY_API_H
#define MEMORY_API_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Memory management constants */
#define CONVENTIONAL_MEMORY_LIMIT (640 * 1024)  /* 640KB conventional memory */
#define PARAGRAPH_SIZE 16                       /* DOS paragraph size */
#define MAX_UMB_BLOCKS 32                      /* Maximum UMB blocks to track */
#define MAX_XMS_HANDLES 64                     /* Maximum XMS handles */

/* Memory alignment requirements */
#define MEMORY_ALIGN_BYTE     1
#define MEMORY_ALIGN_WORD     2
#define MEMORY_ALIGN_DWORD    4
#define MEMORY_ALIGN_PARA     16   /* Paragraph alignment */
#define MEMORY_ALIGN_PAGE     256  /* Page alignment for performance */

/* ============================================================================
 * Memory Type Classifications
 * ============================================================================ */

/**
 * @brief Memory type enumeration for DOS environment
 * Note: Values are bitmask flags, must use #defines for C89 compatibility
 */
typedef enum {
    MEMORY_TYPE_CONVENTIONAL,  /**< 0: Conventional memory (0-640KB) */
    MEMORY_TYPE_UMB,           /**< 1: Upper Memory Block (640KB-1MB) */
    MEMORY_TYPE_XMS,           /**< 2: Extended Memory (XMS) */
    MEMORY_TYPE_MODULE,        /**< 3: Module-specific memory */
    MEMORY_TYPE_BUFFER,        /**< 4: Packet buffer memory */
    MEMORY_TYPE_TEMP,          /**< 5: Temporary allocation */
    MEMORY_TYPE_PERSISTENT,    /**< 6: Persistent (TSR) allocation */
    MEMORY_TYPE_DMA_COHERENT   /**< 7: DMA-coherent memory */
} memory_type_t;

/* Memory type bitmask values for flag operations */
#define MEMORY_TYPE_FLAG_CONVENTIONAL  0x01
#define MEMORY_TYPE_FLAG_UMB           0x02
#define MEMORY_TYPE_FLAG_XMS           0x04
#define MEMORY_TYPE_FLAG_MODULE        0x08
#define MEMORY_TYPE_FLAG_BUFFER        0x10
#define MEMORY_TYPE_FLAG_TEMP          0x20
#define MEMORY_TYPE_FLAG_PERSISTENT    0x40
#define MEMORY_TYPE_FLAG_DMA_COHERENT  0x80

/**
 * @brief Memory allocation priority
 */
typedef enum {
    MEMORY_PRIORITY_LOW,     /**< 0: Low priority allocation */
    MEMORY_PRIORITY_NORMAL,  /**< 1: Normal priority allocation */
    MEMORY_PRIORITY_HIGH,    /**< 2: High priority allocation */
    MEMORY_PRIORITY_URGENT   /**< 3: Urgent allocation (core systems) */
} memory_priority_t;

/**
 * @brief Memory allocation flags
 * Note: Values are bitmask flags, use #defines for actual values
 */
typedef enum {
    MEMORY_FLAG_ZERO,       /**< 0: Zero-initialize memory */
    MEMORY_FLAG_ALIGN,      /**< 1: Align to specified boundary */
    MEMORY_FLAG_MOVEABLE,   /**< 2: Memory can be moved/compacted */
    MEMORY_FLAG_LOCKABLE,   /**< 3: Memory can be locked */
    MEMORY_FLAG_SHAREABLE,  /**< 4: Memory can be shared */
    MEMORY_FLAG_READABLE,   /**< 5: Memory is readable */
    MEMORY_FLAG_WRITABLE,   /**< 6: Memory is writable */
    MEMORY_FLAG_EXECUTABLE  /**< 7: Memory is executable */
} memory_flags_t;

/* Memory flag bitmask values for flag operations */
#define MEMORY_FLAG_BIT_ZERO       0x0001
#define MEMORY_FLAG_BIT_ALIGN      0x0002
#define MEMORY_FLAG_BIT_MOVEABLE   0x0004
#define MEMORY_FLAG_BIT_LOCKABLE   0x0008
#define MEMORY_FLAG_BIT_SHAREABLE  0x0010
#define MEMORY_FLAG_BIT_READABLE   0x0020
#define MEMORY_FLAG_BIT_WRITABLE   0x0040
#define MEMORY_FLAG_BIT_EXECUTABLE 0x0080

/* ============================================================================
 * Memory Block Information
 * ============================================================================ */

/**
 * @brief Memory block descriptor
 */
typedef struct {
    void*         address;        /**< Memory block address */
    size_t        size;           /**< Block size in bytes */
    memory_type_t type;           /**< Memory type */
    uint16_t      flags;          /**< Allocation flags */
    uint16_t      handle;         /**< Memory handle (XMS/UMB) */
    uint8_t       owner_id;       /**< Owner module ID */
    uint8_t       lock_count;     /**< Lock reference count */
    uint32_t      timestamp;      /**< Allocation timestamp */
} memory_block_t;

/**
 * @brief Memory statistics
 */
typedef struct {
    /* Conventional memory */
    size_t conventional_total;    /**< Total conventional memory */
    size_t conventional_free;     /**< Free conventional memory */
    size_t conventional_largest;  /**< Largest free conventional block */
    
    /* Upper memory blocks */
    size_t umb_total;            /**< Total UMB memory */
    size_t umb_free;             /**< Free UMB memory */
    uint16_t umb_blocks;         /**< Number of UMB blocks */
    
    /* Extended memory */
    size_t xms_total;            /**< Total XMS memory */
    size_t xms_free;             /**< Free XMS memory */
    uint16_t xms_handles_used;   /**< XMS handles in use */
    
    /* Allocation statistics */
    uint32_t total_allocations;  /**< Total allocations made */
    uint32_t total_deallocations;/**< Total deallocations made */
    uint32_t peak_usage;         /**< Peak memory usage */
    uint32_t current_usage;      /**< Current memory usage */
    
    /* Fragmentation */
    uint16_t fragmentation_pct;  /**< Fragmentation percentage */
    uint16_t largest_free_block; /**< Largest contiguous free block */
} memory_stats_t;

/**
 * @brief XMS memory handle information
 */
typedef struct {
    uint16_t handle;             /**< XMS handle */
    size_t   size;               /**< Handle size in KB */
    uint8_t  lock_count;         /**< Lock count */
    void*    linear_address;     /**< Linear address when locked */
    bool     in_use;             /**< Handle is allocated */
} xms_handle_info_t;

/**
 * @brief UMB block information
 */
typedef struct {
    uint16_t segment;            /**< UMB segment address */
    uint16_t paragraphs;         /**< Size in paragraphs */
    bool     in_use;             /**< Block is allocated */
    uint8_t  owner_id;           /**< Owner module ID */
} umb_block_info_t;

/* ============================================================================
 * Memory Manager Interface
 * ============================================================================ */

/**
 * @brief Memory allocation function
 * 
 * @param size Size in bytes to allocate
 * @param type Memory type preference
 * @param flags Allocation flags
 * @param alignment Required alignment (power of 2)
 * @return Pointer to allocated memory, NULL on failure
 */
typedef void* (*memory_alloc_fn)(size_t size, 
                                memory_type_t type,
                                uint16_t flags,
                                size_t alignment);

/**
 * @brief Memory deallocation function
 * 
 * @param ptr Pointer to memory to free
 * @return true on success, false on failure
 */
typedef bool (*memory_free_fn)(void* ptr);

/**
 * @brief Memory reallocation function
 * 
 * @param ptr Existing memory pointer
 * @param new_size New size in bytes
 * @return Pointer to reallocated memory, NULL on failure
 */
typedef void* (*memory_realloc_fn)(void* ptr, size_t new_size);

/**
 * @brief Memory information query function
 * 
 * @param ptr Memory pointer
 * @param block_info Output block information
 * @return true on success, false if pointer invalid
 */
typedef bool (*memory_query_fn)(const void* ptr, memory_block_t* block_info);

/**
 * @brief Memory statistics function
 * 
 * @param stats Output statistics structure
 * @return true on success, false on failure
 */
typedef bool (*memory_stats_fn)(memory_stats_t* stats);

/* ============================================================================
 * Buffer Management Interface
 * ============================================================================ */

/**
 * @brief Packet buffer descriptor
 */
typedef struct {
    uint8_t* data;               /**< Buffer data pointer */
    size_t   size;               /**< Buffer size */
    size_t   used;               /**< Used bytes in buffer */
    uint16_t buffer_id;          /**< Unique buffer identifier */
    uint8_t  ref_count;          /**< Reference count */
    uint8_t  flags;              /**< Buffer flags */
    void*    private_data;       /**< Module-specific data */
} packet_buffer_t;

/**
 * @brief Buffer pool configuration
 */
typedef struct {
    size_t   small_buffer_size;  /**< Small buffer size (typical: 256 bytes) */
    size_t   large_buffer_size;  /**< Large buffer size (typical: 1600 bytes) */
    uint16_t small_buffer_count; /**< Number of small buffers */
    uint16_t large_buffer_count; /**< Number of large buffers */
    memory_type_t memory_type;   /**< Preferred memory type */
    uint16_t alignment;          /**< Buffer alignment requirement */
} buffer_pool_config_t;

/* Forward declaration for device capabilities */
struct device_caps;

/**
 * @brief Enhanced buffer pool configuration - GPT-5 recommendation
 * 
 * Implements GPT-5's recommended buffer classes:
 * 128, 256, 512, 1536 (better than 736) - cache/descriptor friendly
 */
typedef struct {
    size_t   tiny_buffer_size;    /**< Tiny buffer size (128 bytes - control packets) */
    size_t   small_buffer_size;   /**< Small buffer size (256 bytes - ARP, ICMP, TCP ACKs) */
    size_t   medium_buffer_size;  /**< Medium buffer size (512 bytes - DNS, small HTTP) */
    size_t   large_buffer_size;   /**< Large buffer size (1536 bytes - Full MTU + headroom) */
    uint16_t tiny_buffer_count;   /**< Number of tiny buffers */
    uint16_t small_buffer_count;  /**< Number of small buffers */
    uint16_t medium_buffer_count; /**< Number of medium buffers */
    uint16_t large_buffer_count;  /**< Number of large buffers */
    memory_type_t memory_type;    /**< Preferred memory type */
    uint16_t alignment;           /**< Buffer alignment requirement */
    bool     enable_adaptive_sizing; /**< Enable adaptive threshold adjustment */
    struct device_caps* device_caps;  /**< Device-specific capabilities */
} enhanced_buffer_pool_config_t;

/**
 * @brief Get packet buffer function
 * 
 * @param size Required buffer size
 * @param timeout_ms Timeout in milliseconds (0 = no wait)
 * @return Buffer pointer, NULL if unavailable
 */
typedef packet_buffer_t* (*buffer_get_fn)(size_t size, uint16_t timeout_ms);

/**
 * @brief Return packet buffer function
 * 
 * @param buffer Buffer to return to pool
 * @return true on success, false on error
 */
typedef bool (*buffer_return_fn)(packet_buffer_t* buffer);

/**
 * @brief Buffer reference management
 * 
 * @param buffer Buffer to reference
 * @return New reference count
 */
typedef uint8_t (*buffer_addref_fn)(packet_buffer_t* buffer);

/**
 * @brief Buffer dereference management
 * 
 * @param buffer Buffer to dereference
 * @return New reference count (0 = buffer freed)
 */
typedef uint8_t (*buffer_release_fn)(packet_buffer_t* buffer);

/* ============================================================================
 * DMA and Cache-Coherent Memory
 * ============================================================================ */

/**
 * @brief DMA operation direction
 * Note: dma_direction_t values match DMA_DIRECTION_* macros in dma.h
 */
typedef enum {
    DMA_DIR_NONE,           /**< 0: No direction */
    DMA_DIR_TO_DEVICE,      /**< 1: CPU to device (TX) */
    DMA_DIR_FROM_DEVICE,    /**< 2: Device to CPU (RX) */
    DMA_DIR_BIDIRECTIONAL   /**< 3: Bidirectional */
} dma_direction_t;

/**
 * @brief DMA device type for cache management
 */
typedef enum {
    DMA_DEVICE_NONE,      /**< 0: No device */
    DMA_DEVICE_NETWORK,   /**< 1: Network interface */
    DMA_DEVICE_STORAGE,   /**< 2: Storage device */
    DMA_DEVICE_AUDIO,     /**< 3: Audio device */
    DMA_DEVICE_GENERIC    /**< 4: Generic DMA device */
} dma_device_type_t;

/* Legacy value for generic device compatibility */
#define DMA_DEVICE_GENERIC_LEGACY 0xFF

/**
 * @brief DMA operation descriptor
 */
typedef struct {
    void*             buffer;        /**< DMA buffer address */
    size_t            length;        /**< DMA length */
    dma_direction_t   direction;     /**< DMA direction */
    dma_device_type_t device_type;   /**< Device type */
    uint8_t           device_id;     /**< Device identifier */
    uint32_t          timeout_ms;    /**< Operation timeout */
    uint16_t          flags;         /**< DMA flags */
} dma_operation_t;

/**
 * @brief DMA buffer preparation function
 * 
 * Prepares buffer for DMA operation (cache flush/invalidate)
 * 
 * @param dma_op DMA operation descriptor
 * @return true on success, false on failure
 */
typedef bool (*dma_prepare_fn)(const dma_operation_t* dma_op);

/**
 * @brief DMA completion function
 * 
 * Called after DMA operation completes (cache invalidate)
 * 
 * @param dma_op DMA operation descriptor
 * @return true on success, false on failure
 */
typedef bool (*dma_complete_fn)(const dma_operation_t* dma_op);

/**
 * @brief Allocate DMA-coherent memory
 * 
 * @param size Size in bytes
 * @param device_type Type of DMA device
 * @param alignment Required alignment
 * @return Pointer to coherent memory, NULL on failure
 */
typedef void* (*dma_alloc_coherent_fn)(size_t size,
                                      dma_device_type_t device_type,
                                      size_t alignment);

/**
 * @brief Free DMA-coherent memory
 * 
 * @param ptr Pointer to coherent memory
 * @param size Size of allocation
 * @return true on success, false on failure
 */
typedef bool (*dma_free_coherent_fn)(void* ptr, size_t size);

/* ============================================================================
 * Core Memory Services Interface
 * ============================================================================ */

/**
 * @brief Complete memory management interface for modules
 * 
 * This structure contains all memory-related functions that modules
 * can use. Provided by the core loader to each module.
 */
typedef struct {
    /* Basic memory allocation */
    memory_alloc_fn   allocate;      /**< Allocate memory */
    memory_free_fn    deallocate;    /**< Free memory */
    memory_realloc_fn reallocate;    /**< Reallocate memory */
    memory_query_fn   query_block;   /**< Query memory block info */
    memory_stats_fn   get_stats;     /**< Get memory statistics */
    
    /* Buffer management */
    buffer_get_fn     get_buffer;    /**< Get packet buffer */
    buffer_return_fn  return_buffer; /**< Return packet buffer */
    buffer_addref_fn  addref_buffer; /**< Add buffer reference */
    buffer_release_fn release_buffer;/**< Release buffer reference */
    
    /* DMA and cache management */
    dma_prepare_fn    dma_prepare;   /**< Prepare for DMA */
    dma_complete_fn   dma_complete;  /**< Complete DMA operation */
    dma_alloc_coherent_fn alloc_coherent; /**< Allocate coherent memory */
    dma_free_coherent_fn  free_coherent;  /**< Free coherent memory */
    
    /* Utility functions */
    void* (*memset_fast)(void* dest, int value, size_t count);
    void* (*memcpy_fast)(void* dest, const void* src, size_t count);
    int   (*memcmp_fast)(const void* buf1, const void* buf2, size_t count);
} memory_services_t;

/* ============================================================================
 * Memory Utility Macros
 * ============================================================================ */

/**
 * @brief Align size to specified boundary
 */
#define ALIGN_SIZE(size, align) (((size) + (align) - 1) & ~((align) - 1))

/**
 * @brief Align pointer to specified boundary
 */
#define ALIGN_POINTER(ptr, align) \
    ((void*)(((uintptr_t)(ptr) + (align) - 1) & ~((align) - 1)))

/**
 * @brief Convert bytes to paragraphs (round up)
 */
#define BYTES_TO_PARAGRAPHS(bytes) (((bytes) + 15) / 16)

/**
 * @brief Convert paragraphs to bytes
 */
#define PARAGRAPHS_TO_BYTES(paras) ((paras) * 16)

/**
 * @brief Check if pointer is aligned
 */
#define IS_ALIGNED(ptr, align) (((uintptr_t)(ptr) & ((align) - 1)) == 0)

/**
 * @brief Get segment from far pointer
 */
#define GET_SEGMENT(ptr) ((uint16_t)(((uint32_t)(ptr)) >> 16))

/**
 * @brief Get offset from far pointer
 */
#define GET_OFFSET(ptr) ((uint16_t)(((uint32_t)(ptr)) & 0xFFFF))

/**
 * @brief Make far pointer from segment:offset
 */
#define MAKE_FAR_PTR(seg, off) ((void*)(((uint32_t)(seg) << 16) | (off)))

/* ============================================================================
 * Memory Allocation Convenience Functions
 * ============================================================================ */

/**
 * @brief Allocate zero-initialized memory
 */
static inline void* memory_alloc_zero(memory_services_t* mem, size_t size, memory_type_t type) {
    return mem->allocate(size, type, MEMORY_FLAG_ZERO, MEMORY_ALIGN_BYTE);
}

/**
 * @brief Allocate aligned memory
 */
static inline void* memory_alloc_aligned(memory_services_t* mem, size_t size, 
                                        memory_type_t type, size_t alignment) {
    return mem->allocate(size, type, MEMORY_FLAG_ALIGN, alignment);
}

/**
 * @brief Allocate temporary memory
 */
static inline void* memory_alloc_temp(memory_services_t* mem, size_t size) {
    return mem->allocate(size, MEMORY_TYPE_TEMP, 0, MEMORY_ALIGN_BYTE);
}

/**
 * @brief Allocate persistent memory
 */
static inline void* memory_alloc_persistent(memory_services_t* mem, size_t size) {
    return mem->allocate(size, MEMORY_TYPE_PERSISTENT, 0, MEMORY_ALIGN_BYTE);
}

/* ============================================================================
 * Error Codes and Diagnostics
 * ============================================================================ */

/**
 * @brief Memory operation result codes
 * Note: Negative values not supported in C89 enums, use defines
 */
typedef enum {
    MEMORY_SUCCESS,              /**< 0: Operation successful */
    MEMORY_ERROR_OUT_OF_MEMORY,  /**< 1: Out of memory */
    MEMORY_ERROR_INVALID_PTR,    /**< 2: Invalid pointer */
    MEMORY_ERROR_INVALID_SIZE,   /**< 3: Invalid size */
    MEMORY_ERROR_ALIGNMENT,      /**< 4: Alignment error */
    MEMORY_ERROR_FRAGMENTED,     /**< 5: Memory too fragmented */
    MEMORY_ERROR_LOCKED,         /**< 6: Memory is locked */
    MEMORY_ERROR_NOT_FOUND,      /**< 7: Memory block not found */
    MEMORY_ERROR_PERMISSION,     /**< 8: Permission denied */
    MEMORY_ERROR_DOUBLE_FREE,    /**< 9: Attempt to free already freed memory */
    MEMORY_ERROR_CORRUPTION      /**< 10: Memory corruption detected */
} memory_result_t;

/* Error code defines for functions returning int */
#define MEMORY_ERR_OUT_OF_MEMORY  (-1)
#define MEMORY_ERR_INVALID_PTR    (-2)
#define MEMORY_ERR_INVALID_SIZE   (-3)
#define MEMORY_ERR_ALIGNMENT      (-4)
#define MEMORY_ERR_FRAGMENTED     (-5)
#define MEMORY_ERR_LOCKED         (-6)
#define MEMORY_ERR_NOT_FOUND      (-7)
#define MEMORY_ERR_PERMISSION     (-8)
#define MEMORY_ERR_DOUBLE_FREE    (-9)
#define MEMORY_ERR_CORRUPTION     (-10)

/**
 * @brief Get human-readable error string
 */
static inline const char* memory_error_string(memory_result_t error) {
    switch (error) {
        case MEMORY_SUCCESS:           return "Success";
        case MEMORY_ERROR_OUT_OF_MEMORY: return "Out of memory";
        case MEMORY_ERROR_INVALID_PTR: return "Invalid pointer";
        case MEMORY_ERROR_INVALID_SIZE: return "Invalid size";
        case MEMORY_ERROR_ALIGNMENT:   return "Alignment error";
        case MEMORY_ERROR_FRAGMENTED:  return "Memory fragmented";
        case MEMORY_ERROR_LOCKED:      return "Memory locked";
        case MEMORY_ERROR_NOT_FOUND:   return "Block not found";
        case MEMORY_ERROR_PERMISSION:  return "Permission denied";
        case MEMORY_ERROR_DOUBLE_FREE: return "Double free";
        case MEMORY_ERROR_CORRUPTION:  return "Memory corruption";
        default:                       return "Unknown error";
    }
}

#endif /* MEMORY_API_H */