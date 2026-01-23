/**
 * @file dma.h
 * @brief DMA abstraction layer for scatter-gather operations and physical memory management
 * 
 * Sprint 2.2: Scatter-Gather DMA Implementation
 * 
 * This header provides DMA abstraction layer for 3Com NICs with emphasis on:
 * - Physical address translation for DOS/XMS memory management
 * - Scatter-gather descriptor management for fragmented packets
 * - Fallback mechanisms for ISA PIO-only NICs (3C509B)
 * - Integration with enhanced ring buffer management from Sprint 0B.3
 * - Compatibility with ISA bus mastering (3C515-TX) vs no DMA (3C509B)
 * 
 * Technical Background:
 * The 3C515-TX supports basic bus mastering DMA but lacks true scatter-gather
 * capabilities found in later PCI generations. This implementation provides
 * a software scatter-gather layer that consolidates fragmented packets for
 * the 3C515-TX while providing complete fallback for the 3C509B.
 */

#ifndef _DMA_H_
#define _DMA_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "memory.h"
#include "xmsdet.h"
#include "3c515.h"
#include "enhring.h"

/* DMA capability flags matching NIC hardware capabilities */
#define DMA_CAP_NONE                0x0000   /* No DMA support (3C509B) */
#define DMA_CAP_BASIC_BUSMASTER     0x0001   /* Basic bus mastering (3C515-TX) */
#define DMA_CAP_SCATTER_GATHER      0x0002   /* True scatter-gather (not available on target NICs) */
#define DMA_CAP_64BIT_ADDRESSING    0x0004   /* 64-bit addressing (not available in DOS) */
#define DMA_CAP_COHERENT_MEMORY     0x0008   /* Coherent memory allocation */
#define DMA_CAP_STREAMING_MAPPING   0x0010   /* Streaming DMA mapping */

/* DMA direction flags */
#define DMA_DIRECTION_TO_DEVICE     0x01     /* Host to NIC */
#define DMA_DIRECTION_FROM_DEVICE   0x02     /* NIC to host */
#define DMA_DIRECTION_BIDIRECTIONAL 0x03     /* Both directions */

/* DMA memory types for DOS environment */
typedef enum {
    DMA_MEMORY_CONVENTIONAL = 0,    /* Conventional memory (below 640KB) */
    DMA_MEMORY_XMS,                 /* Extended memory (XMS) */
    DMA_MEMORY_EMS,                 /* Expanded memory (EMS) - not typically used for DMA */
    DMA_MEMORY_LOCKED_CONVENTIONAL, /* Locked conventional memory */
    DMA_MEMORY_DEVICE_COHERENT      /* Device-coherent memory */
} dma_memory_type_t;

/* Fragment descriptor for software scatter-gather implementation */
typedef struct dma_fragment {
    uint32_t physical_addr;         /* Physical address of fragment */
    uint32_t length;                /* Fragment length in bytes */
    uint32_t flags;                 /* Fragment flags */
    struct dma_fragment *next;      /* Next fragment in chain */
} dma_fragment_t;

/* Fragment flags */
#define DMA_FRAG_FIRST     0x0001   /* First fragment in packet */
#define DMA_FRAG_LAST      0x0002   /* Last fragment in packet */
#define DMA_FRAG_SINGLE    0x0003   /* Single fragment (first + last) */
#define DMA_FRAG_COHERENT  0x0004   /* Coherent memory fragment */
#define DMA_FRAG_CACHED    0x0008   /* Cached memory (needs coherency management) */

/* Scatter-gather list structure */
typedef struct dma_sg_list {
    dma_fragment_t *fragments;      /* Array of fragments */
    uint16_t fragment_count;        /* Number of fragments */
    uint16_t max_fragments;         /* Maximum fragments supported */
    uint32_t total_length;          /* Total length of all fragments */
    uint32_t flags;                 /* SG list flags */
    void *private_data;             /* NIC-specific data */
} dma_sg_list_t;

/* SG list flags */
#define DMA_SG_CONSOLIDATED    0x0001   /* Fragments consolidated into single buffer */
#define DMA_SG_MAPPED          0x0002   /* Physical addresses mapped */
#define DMA_SG_COHERENT        0x0004   /* All fragments in coherent memory */
#define DMA_SG_ZERO_COPY       0x0008   /* Zero-copy operation possible */

/* DMA mapping structure for address translation */
typedef struct dma_mapping {
    void *virtual_addr;             /* Virtual address */
    uint32_t physical_addr;         /* Physical address */
    uint32_t size;                  /* Mapping size */
    dma_memory_type_t memory_type;  /* Memory type */
    uint16_t xms_handle;            /* XMS handle if applicable */
    uint32_t xms_offset;            /* Offset within XMS block */
    uint32_t flags;                 /* Mapping flags */
    uint32_t ref_count;             /* Reference count */
} dma_mapping_t;

/* DMA mapping flags */
#define DMA_MAP_COHERENT       0x0001   /* Coherent mapping */
#define DMA_MAP_STREAMING      0x0002   /* Streaming mapping */
#define DMA_MAP_LOCKED         0x0004   /* Memory locked in physical pages */
#define DMA_MAP_XMS_LOCKED     0x0008   /* XMS memory locked */
#define DMA_MAP_CACHED         0x0010   /* Cached mapping (needs sync) */

/* DMA buffer pool for efficient allocation */
typedef struct dma_buffer_pool {
    uint8_t *base_addr;             /* Pool base address */
    uint32_t pool_size;             /* Total pool size */
    uint32_t buffer_size;           /* Individual buffer size */
    uint16_t buffer_count;          /* Number of buffers */
    uint16_t free_count;            /* Free buffers available */
    uint32_t *free_bitmap;          /* Free buffer bitmap */
    dma_mapping_t *mappings;        /* Mapping for each buffer */
    uint32_t allocation_flags;      /* Pool allocation flags */
    dma_memory_type_t memory_type;  /* Pool memory type */
} dma_buffer_pool_t;

/* DMA coherency management structure */
typedef struct dma_coherency_mgr {
    bool coherent_memory_available; /* System supports coherent memory */
    bool cache_coherent_dma;        /* DMA is cache coherent */
    uint32_t cache_line_size;       /* CPU cache line size */
    uint32_t dma_alignment;         /* Required DMA alignment */
    void (*sync_for_cpu)(void *addr, uint32_t size);    /* Sync before CPU access */
    void (*sync_for_device)(void *addr, uint32_t size); /* Sync before device access */
} dma_coherency_mgr_t;

/* NIC-specific DMA context */
typedef struct dma_nic_context {
    uint16_t nic_type;              /* NIC type (3C515 or 3C509B) */
    uint32_t dma_capabilities;      /* DMA capability flags */
    uint16_t max_dma_address;       /* Maximum DMA address (16-bit for ISA) */
    uint16_t max_sg_fragments;      /* Maximum scatter-gather fragments */
    uint32_t min_alignment;         /* Minimum buffer alignment */
    uint32_t max_transfer_size;     /* Maximum single transfer size */
    
    /* Hardware-specific function pointers */
    int (*setup_dma_transfer)(struct dma_nic_context *ctx, dma_sg_list_t *sg_list, uint8_t direction);
    int (*start_dma_transfer)(struct dma_nic_context *ctx);
    int (*stop_dma_transfer)(struct dma_nic_context *ctx);
    int (*get_dma_status)(struct dma_nic_context *ctx);
    
    /* Buffer pool management */
    dma_buffer_pool_t tx_pool;      /* TX buffer pool */
    dma_buffer_pool_t rx_pool;      /* RX buffer pool */
    
    /* Statistics */
    uint32_t sg_consolidations;     /* Number of SG consolidations performed */
    uint32_t zero_copy_transfers;   /* Number of zero-copy transfers */
    uint32_t fallback_transfers;    /* Number of fallback transfers */
    uint32_t dma_errors;            /* DMA error count */
    
    /* Hardware integration */
    uint16_t io_base;               /* NIC I/O base address */
    enhanced_ring_context_t *ring_context; /* Ring buffer context */
    void *private_data;             /* NIC-specific private data */
} dma_nic_context_t;

/* Global DMA management structure */
typedef struct dma_manager {
    bool initialized;               /* DMA manager initialized */
    dma_coherency_mgr_t coherency;  /* Coherency management */
    
    /* Physical address translation */
    uint32_t (*virt_to_phys)(void *virt_addr);
    void* (*phys_to_virt)(uint32_t phys_addr);
    
    /* XMS integration */
    bool xms_available;             /* XMS available for DMA buffers */
    uint32_t xms_dma_base;          /* Base address for XMS DMA region */
    uint16_t xms_dma_handle;        /* XMS handle for DMA region */
    
    /* Memory pools */
    dma_buffer_pool_t *coherent_pool; /* Coherent memory pool */
    dma_buffer_pool_t *streaming_pool; /* Streaming memory pool */
    
    /* NIC contexts */
    dma_nic_context_t nic_contexts[MAX_NICS]; /* Per-NIC DMA contexts */
    
    /* Statistics */
    uint32_t total_mappings;        /* Total mappings created */
    uint32_t active_mappings;       /* Currently active mappings */
    uint32_t mapping_failures;     /* Mapping failures */
    uint32_t coherency_violations;  /* Cache coherency violations detected */
} dma_manager_t;

/* Global DMA manager instance */
extern dma_manager_t g_dma_manager;

/* === Core DMA Management Functions === */

/**
 * @brief Initialize DMA subsystem
 * @return 0 on success, negative error code on failure
 */
int dma_init(void);

/**
 * @brief Cleanup DMA subsystem
 */
void dma_cleanup(void);

/**
 * @brief Initialize NIC-specific DMA context
 * @param nic_index NIC index
 * @param nic_type NIC type (3C515 or 3C509B)
 * @param io_base NIC I/O base address
 * @param ring_context Enhanced ring context
 * @return 0 on success, negative error code on failure
 */
int dma_init_nic_context(uint8_t nic_index, uint16_t nic_type, uint16_t io_base, 
                        enhanced_ring_context_t *ring_context);

/**
 * @brief Cleanup NIC-specific DMA context
 * @param nic_index NIC index
 */
void dma_cleanup_nic_context(uint8_t nic_index);

/* === Physical Address Translation === */

/**
 * @brief Convert virtual address to physical address
 * @param virt_addr Virtual address
 * @return Physical address, 0 on failure
 */
uint32_t dma_virt_to_phys(void *virt_addr);

/**
 * @brief Convert physical address to virtual address
 * @param phys_addr Physical address
 * @return Virtual address, NULL on failure
 */
void* dma_phys_to_virt(uint32_t phys_addr);

/**
 * @brief Create DMA mapping for memory region
 * @param virt_addr Virtual address
 * @param size Size in bytes
 * @param direction DMA direction
 * @param mapping Output mapping structure
 * @return 0 on success, negative error code on failure
 */
int dma_map_memory(void *virt_addr, uint32_t size, uint8_t direction, dma_mapping_t *mapping);

/**
 * @brief Unmap DMA memory region
 * @param mapping DMA mapping to unmap
 */
void dma_unmap_memory(dma_mapping_t *mapping);

/* === XMS Integration for Extended Memory === */

/**
 * @brief Initialize XMS DMA region
 * @param size_kb Size of XMS region in KB
 * @return 0 on success, negative error code on failure
 */
int dma_init_xms_region(uint32_t size_kb);

/**
 * @brief Allocate XMS memory for DMA
 * @param size Size in bytes
 * @param mapping Output mapping structure
 * @return 0 on success, negative error code on failure
 */
int dma_alloc_xms(uint32_t size, dma_mapping_t *mapping);

/**
 * @brief Free XMS DMA memory
 * @param mapping XMS mapping to free
 */
void dma_free_xms(dma_mapping_t *mapping);

/**
 * @brief Copy data to XMS memory
 * @param xms_mapping XMS mapping
 * @param offset Offset within XMS region
 * @param src_data Source data
 * @param size Size to copy
 * @return 0 on success, negative error code on failure
 */
int dma_copy_to_xms(dma_mapping_t *xms_mapping, uint32_t offset, const void *src_data, uint32_t size);

/**
 * @brief Copy data from XMS memory
 * @param dest_data Destination buffer
 * @param xms_mapping XMS mapping
 * @param offset Offset within XMS region
 * @param size Size to copy
 * @return 0 on success, negative error code on failure
 */
int dma_copy_from_xms(void *dest_data, dma_mapping_t *xms_mapping, uint32_t offset, uint32_t size);

/* === Scatter-Gather Operations === */

/**
 * @brief Create scatter-gather list
 * @param max_fragments Maximum number of fragments
 * @return Allocated SG list, NULL on failure
 */
dma_sg_list_t* dma_sg_alloc(uint16_t max_fragments);

/**
 * @brief Free scatter-gather list
 * @param sg_list SG list to free
 */
void dma_sg_free(dma_sg_list_t *sg_list);

/**
 * @brief Add fragment to scatter-gather list
 * @param sg_list SG list
 * @param virt_addr Virtual address of fragment
 * @param length Fragment length
 * @param flags Fragment flags
 * @return 0 on success, negative error code on failure
 */
int dma_sg_add_fragment(dma_sg_list_t *sg_list, void *virt_addr, uint32_t length, uint32_t flags);

/**
 * @brief Consolidate scatter-gather list into single buffer
 * @param sg_list SG list to consolidate
 * @param consolidated_buffer Output buffer for consolidated data
 * @param buffer_size Size of output buffer
 * @return Number of bytes consolidated, negative on error
 */
int dma_sg_consolidate(dma_sg_list_t *sg_list, uint8_t *consolidated_buffer, uint32_t buffer_size);

/**
 * @brief Map scatter-gather list for DMA
 * @param nic_index NIC index
 * @param sg_list SG list to map
 * @param direction DMA direction
 * @return 0 on success, negative error code on failure
 */
int dma_sg_map(uint8_t nic_index, dma_sg_list_t *sg_list, uint8_t direction);

/**
 * @brief Unmap scatter-gather list
 * @param nic_index NIC index
 * @param sg_list SG list to unmap
 */
void dma_sg_unmap(uint8_t nic_index, dma_sg_list_t *sg_list);

/* === Buffer Pool Management === */

/**
 * @brief Initialize DMA buffer pool
 * @param pool Pool structure to initialize
 * @param buffer_count Number of buffers
 * @param buffer_size Size of each buffer
 * @param memory_type Memory type for pool
 * @param alignment Buffer alignment requirement
 * @return 0 on success, negative error code on failure
 */
int dma_pool_init(dma_buffer_pool_t *pool, uint16_t buffer_count, uint32_t buffer_size,
                 dma_memory_type_t memory_type, uint32_t alignment);

/**
 * @brief Cleanup DMA buffer pool
 * @param pool Pool to cleanup
 */
void dma_pool_cleanup(dma_buffer_pool_t *pool);

/**
 * @brief Allocate buffer from DMA pool
 * @param pool Buffer pool
 * @param mapping Output mapping for allocated buffer
 * @return 0 on success, negative error code on failure
 */
int dma_pool_alloc(dma_buffer_pool_t *pool, dma_mapping_t *mapping);

/**
 * @brief Free buffer to DMA pool
 * @param pool Buffer pool
 * @param mapping Buffer mapping to free
 */
void dma_pool_free(dma_buffer_pool_t *pool, dma_mapping_t *mapping);

/* === Hardware-Specific DMA Operations === */

/**
 * @brief Setup DMA transfer for 3C515-TX
 * @param ctx NIC DMA context
 * @param sg_list Scatter-gather list
 * @param direction Transfer direction
 * @return 0 on success, negative error code on failure
 */
int dma_3c515_setup_transfer(dma_nic_context_t *ctx, dma_sg_list_t *sg_list, uint8_t direction);

/**
 * @brief Fallback transfer for 3C509B (PIO mode)
 * @param ctx NIC DMA context
 * @param sg_list Scatter-gather list (will be consolidated)
 * @param direction Transfer direction
 * @return 0 on success, negative error code on failure
 */
int dma_3c509b_fallback_transfer(dma_nic_context_t *ctx, dma_sg_list_t *sg_list, uint8_t direction);

/**
 * @brief Get DMA transfer status
 * @param nic_index NIC index
 * @param bytes_transferred Output for bytes transferred
 * @param transfer_complete Output for completion status
 * @return 0 on success, negative error code on failure
 */
int dma_get_transfer_status(uint8_t nic_index, uint32_t *bytes_transferred, bool *transfer_complete);

/* === Cache Coherency Management === */

/**
 * @brief Initialize cache coherency management
 * @return 0 on success, negative error code on failure
 */
int dma_coherency_init(void);

/**
 * @brief Synchronize memory for CPU access
 * @param addr Memory address
 * @param size Memory size
 * @param direction DMA direction
 */
void dma_sync_for_cpu(void *addr, uint32_t size, uint8_t direction);

/**
 * @brief Synchronize memory for device access
 * @param addr Memory address
 * @param size Memory size
 * @param direction DMA direction
 */
void dma_sync_for_device(void *addr, uint32_t size, uint8_t direction);

/**
 * @brief Check if memory region is cache coherent
 * @param addr Memory address
 * @param size Memory size
 * @return true if coherent, false otherwise
 */
bool dma_is_coherent(void *addr, uint32_t size);

/* === Statistics and Monitoring === */

/**
 * @brief Get DMA statistics
 * @param nic_index NIC index
 * @param sg_ops Output for scatter-gather operations count
 * @param consolidations Output for consolidation count
 * @param zero_copy Output for zero-copy count
 * @param errors Output for error count
 * @return 0 on success, negative error code on failure
 */
int dma_get_statistics(uint8_t nic_index, uint32_t *sg_ops, uint32_t *consolidations,
                      uint32_t *zero_copy, uint32_t *errors);

/**
 * @brief Reset DMA statistics
 * @param nic_index NIC index
 */
void dma_reset_statistics(uint8_t nic_index);

/**
 * @brief Print DMA status for debugging
 * @param nic_index NIC index
 */
void dma_dump_status(uint8_t nic_index);

/* === Error Handling === */

typedef enum {
    DMA_ERROR_NONE = 0,
    DMA_ERROR_INVALID_PARAM,
    DMA_ERROR_OUT_OF_MEMORY,
    DMA_ERROR_MAPPING_FAILED,
    DMA_ERROR_XMS_UNAVAILABLE,
    DMA_ERROR_ALIGNMENT_ERROR,
    DMA_ERROR_TRANSFER_TIMEOUT,
    DMA_ERROR_HARDWARE_ERROR,
    DMA_ERROR_COHERENCY_VIOLATION,
    DMA_ERROR_FRAGMENT_TOO_LARGE,
    DMA_ERROR_TOO_MANY_FRAGMENTS,
    DMA_ERROR_UNSUPPORTED_OPERATION
} dma_error_t;

/**
 * @brief Get last DMA error
 * @param nic_index NIC index
 * @return DMA error code
 */
dma_error_t dma_get_last_error(uint8_t nic_index);

/**
 * @brief Convert DMA error to string
 * @param error DMA error code
 * @return Error string
 */
const char* dma_error_to_string(dma_error_t error);

/* === Integration Functions === */

/**
 * @brief Send packet using DMA scatter-gather
 * @param nic_index NIC index
 * @param packet_fragments Array of packet fragments
 * @param fragment_count Number of fragments
 * @return 0 on success, negative error code on failure
 */
int dma_send_packet_sg(uint8_t nic_index, dma_fragment_t *packet_fragments, uint16_t fragment_count);

/**
 * @brief Receive packet using DMA into scatter-gather buffers
 * @param nic_index NIC index
 * @param sg_list Scatter-gather list for received data
 * @param max_packet_size Maximum packet size to receive
 * @return Number of bytes received, negative on error
 */
int dma_receive_packet_sg(uint8_t nic_index, dma_sg_list_t *sg_list, uint32_t max_packet_size);

/**
 * @brief Test DMA functionality
 * @param nic_index NIC index
 * @return 0 on success, negative error code on failure
 */
int dma_self_test(uint8_t nic_index);

/* === Constants === */

/* Maximum values for DOS environment */
#define DMA_MAX_FRAGMENTS_3C515    4        /* Max fragments for 3C515 pseudo-SG */
#define DMA_MAX_FRAGMENTS_3C509B   1        /* Max fragments for 3C509B (PIO only) */
#define DMA_MAX_TRANSFER_SIZE      1600     /* Maximum transfer size per fragment */
#define DMA_MIN_ALIGNMENT          4        /* Minimum buffer alignment */
#define DMA_ISA_ADDRESS_LIMIT      0xFFFF   /* ISA 16-bit address limit */

/* Pool sizes */
#define DMA_DEFAULT_TX_POOL_SIZE   16       /* Default TX buffer pool size */
#define DMA_DEFAULT_RX_POOL_SIZE   16       /* Default RX buffer pool size */
#define DMA_COHERENT_POOL_SIZE     32       /* Coherent memory pool size */

/* Performance tuning */
#define DMA_CONSOLIDATION_THRESHOLD 256     /* Threshold for SG consolidation */
#define DMA_ZERO_COPY_THRESHOLD     512     /* Threshold for zero-copy operation */

#ifdef __cplusplus
}
#endif

#endif /* _DMA_H_ */