/**
 * @file dma_descriptors.h
 * @brief Advanced DMA descriptor management for 3C515-TX
 * 
 * Phase 3: Advanced DMA Features Implementation
 * Sub-Agent 1: DMA Specialist
 * 
 * This header provides advanced DMA descriptor structures and management
 * functions for the 3C515-TX Fast Ethernet controller, implementing:
 * - Ring buffer management (16 TX, 16 RX descriptors)
 * - Scatter-gather DMA support
 * - DMA completion handling
 * - Cache coherency management
 * - Performance monitoring
 * - Timeout handling and error recovery
 */

#ifndef _DMA_DESCRIPTORS_H_
#define _DMA_DESCRIPTORS_H_

#include "common.h"
#include "3c515.h"
#include "cachecoh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Advanced DMA ring buffer constants */
#define DMA_TX_RING_SIZE        16      // 16 transmit descriptors
#define DMA_RX_RING_SIZE        16      // 16 receive descriptors
#define DMA_MAX_FRAGMENT_SIZE   1536    // Maximum fragment size
#define DMA_MAX_FRAGMENTS       8       // Maximum scatter-gather fragments
#define DMA_DESCRIPTOR_ALIGN    16      // Descriptor alignment (ISA cache line)
#define DMA_BUFFER_ALIGN        4       // Buffer alignment requirement

/* DMA timeout values (in milliseconds) */
#define DMA_TIMEOUT_TX          1000    // TX DMA timeout
#define DMA_TIMEOUT_RX          1000    // RX DMA timeout
#define DMA_TIMEOUT_STALL       100     // DMA stall timeout
#define DMA_COMPLETION_WAIT     10      // DMA completion polling interval

/* DMA completion status flags */
#define DMA_COMPLETION_SUCCESS  0x0001  // DMA completed successfully
#define DMA_COMPLETION_ERROR    0x0002  // DMA completed with error
#define DMA_COMPLETION_TIMEOUT  0x0004  // DMA timed out
#define DMA_COMPLETION_STALLED  0x0008  // DMA engine stalled
#define DMA_COMPLETION_ABORTED  0x0010  // DMA aborted by software

/* Advanced descriptor status extensions */
#define DMA_DESC_OWNED_BY_NIC   0x80000000  // Descriptor owned by NIC
#define DMA_DESC_OWNED_BY_HOST  0x00000000  // Descriptor owned by host
#define DMA_DESC_ERROR_MASK     0x40000000  // Error occurred
#define DMA_DESC_COMPLETE_MASK  0x20000000  // Operation complete
#define DMA_DESC_INTERRUPT      0x10000000  // Generate interrupt
#define DMA_DESC_LAST_FRAG      0x08000000  // Last fragment
#define DMA_DESC_FIRST_FRAG     0x04000000  // First fragment

/**
 * @brief Advanced DMA fragment descriptor
 */
typedef struct dma_fragment_desc {
    uint32_t physical_addr;             // Physical address of fragment
    uint32_t length;                    // Fragment length in bytes
    uint32_t flags;                     // Fragment control flags
    struct dma_fragment_desc *next;     // Next fragment (if any)
} dma_fragment_desc_t;

/**
 * @brief Enhanced TX descriptor with scatter-gather support
 */
typedef struct enhanced_tx_desc {
    uint32_t next;                      // Physical address of next descriptor
    uint32_t status;                    // Status and control bits
    uint32_t addr;                      // Primary buffer physical address
    uint32_t length;                    // Primary buffer length
    
    /* Advanced features for scatter-gather */
    dma_fragment_desc_t *fragments;     // Additional fragments (if any)
    uint16_t fragment_count;            // Number of fragments
    uint16_t total_length;              // Total packet length
    
    /* Performance and debugging */
    uint32_t timestamp_start;           // DMA start timestamp
    uint32_t timestamp_complete;        // DMA completion timestamp
    uint32_t retry_count;               // Number of retries
    uint32_t error_flags;               // Detailed error information
    
    /* Cache coherency management */
    void *coherency_context;            // Cache coherency context
    bool coherent_memory;               // Using coherent memory
} enhanced_tx_desc_t;

/**
 * @brief Enhanced RX descriptor with advanced features
 */
typedef struct enhanced_rx_desc {
    uint32_t next;                      // Physical address of next descriptor
    uint32_t status;                    // Status and control bits
    uint32_t addr;                      // Primary buffer physical address
    uint32_t length;                    // Primary buffer length
    
    /* Advanced receive features */
    uint16_t received_length;           // Actual received length
    uint16_t checksum;                  // Hardware checksum (if available)
    uint32_t receive_timestamp;         // Packet receive timestamp
    
    /* Buffer management */
    void *buffer_virtual;               // Virtual address of buffer
    bool zero_copy_eligible;            // Can use zero-copy receive
    
    /* Error handling */
    uint32_t error_flags;               // Detailed error information
    uint32_t retry_count;               // Number of retries
    
    /* Cache coherency management */
    void *coherency_context;            // Cache coherency context
    bool coherent_memory;               // Using coherent memory
} enhanced_rx_desc_t;

/**
 * @brief DMA ring buffer management structure
 */
typedef struct dma_ring_manager {
    /* TX ring management */
    enhanced_tx_desc_t tx_ring[DMA_TX_RING_SIZE];
    uint16_t tx_head;                   // Next descriptor to use
    uint16_t tx_tail;                   // Next descriptor to clean
    uint16_t tx_count;                  // Number of active TX descriptors
    
    /* RX ring management */
    enhanced_rx_desc_t rx_ring[DMA_RX_RING_SIZE];
    uint16_t rx_head;                   // Next descriptor to use
    uint16_t rx_tail;                   // Next descriptor to clean
    uint16_t rx_count;                  // Number of active RX descriptors
    
    /* Physical addresses for hardware */
    uint32_t tx_ring_physical;          // Physical address of TX ring
    uint32_t rx_ring_physical;          // Physical address of RX ring
    
    /* Buffer pools */
    void *tx_buffers;                   // TX buffer pool
    void *rx_buffers;                   // RX buffer pool
    uint32_t buffer_size;               // Size of each buffer
    
    /* Ring state */
    bool initialized;                   // Ring properly initialized
    bool enabled;                       // DMA enabled
    uint32_t generation;                // Ring generation counter
} dma_ring_manager_t;

/**
 * @brief DMA completion tracking structure
 */
typedef struct dma_completion_tracker {
    /* Completion status */
    volatile bool tx_completion_pending;
    volatile bool rx_completion_pending;
    volatile uint16_t completed_tx_desc;
    volatile uint16_t completed_rx_desc;
    
    /* Completion handlers */
    void (*tx_completion_handler)(enhanced_tx_desc_t *desc);
    void (*rx_completion_handler)(enhanced_rx_desc_t *desc);
    
    /* Timeout management */
    uint32_t last_tx_activity;          // Last TX activity timestamp
    uint32_t last_rx_activity;          // Last RX activity timestamp
    uint32_t tx_timeout_count;          // TX timeout counter
    uint32_t rx_timeout_count;          // RX timeout counter
} dma_completion_tracker_t;

/**
 * @brief DMA performance statistics
 */
typedef struct dma_performance_stats {
    /* Transfer statistics */
    uint32_t tx_descriptors_used;       // Total TX descriptors used
    uint32_t rx_descriptors_used;       // Total RX descriptors used
    uint32_t tx_bytes_transferred;      // Total TX bytes transferred
    uint32_t rx_bytes_transferred;      // Total RX bytes transferred
    
    /* Scatter-gather statistics */
    uint32_t sg_tx_packets;             // TX packets using scatter-gather
    uint32_t sg_rx_packets;             // RX packets using scatter-gather
    uint32_t total_fragments;           // Total fragments processed
    uint32_t avg_fragments_per_packet;  // Average fragments per packet
    
    /* Performance metrics */
    uint32_t zero_copy_tx;              // Zero-copy TX operations
    uint32_t zero_copy_rx;              // Zero-copy RX operations
    uint32_t cache_hits;                // Cache coherency hits
    uint32_t cache_misses;              // Cache coherency misses
    
    /* Error statistics */
    uint32_t tx_timeouts;               // TX timeouts
    uint32_t rx_timeouts;               // RX timeouts
    uint32_t tx_retries;                // TX retries
    uint32_t rx_retries;                // RX retries
    uint32_t dma_errors;                // DMA hardware errors
    uint32_t descriptor_errors;         // Descriptor errors
    
    /* Efficiency metrics */
    uint32_t cpu_cycles_saved;          // CPU cycles saved by DMA
    uint32_t bus_utilization;           // Bus utilization percentage
    uint32_t interrupt_coalescing;      // Interrupt coalescing events
} dma_performance_stats_t;

/**
 * @brief Master DMA context for 3C515-TX
 */
typedef struct advanced_dma_context {
    /* Core components */
    dma_ring_manager_t ring_manager;
    dma_completion_tracker_t completion_tracker;
    dma_performance_stats_t performance_stats;
    
    /* Hardware interface */
    uint16_t io_base;                   // NIC I/O base address
    uint8_t irq;                        // IRQ line
    uint8_t dma_channel;                // DMA channel (if used)
    
    /* Configuration */
    bool bus_mastering_enabled;         // Bus mastering active
    bool scatter_gather_enabled;       // Scatter-gather enabled
    bool zero_copy_enabled;             // Zero-copy enabled
    bool cache_coherency_enabled;       // Cache coherency management
    
    /* State management */
    uint32_t state_flags;               // Current state flags
    uint32_t error_mask;                // Error mask
    uint32_t debug_level;               // Debug output level
    
    /* Cache coherency */
    cache_coherency_context_t *cache_context;
} advanced_dma_context_t;

/* === Core DMA Management Functions === */

/**
 * @brief Initialize advanced DMA system
 * @param ctx DMA context structure
 * @param io_base NIC I/O base address
 * @param irq IRQ line
 * @return 0 on success, negative error code on failure
 */
int advanced_dma_init(advanced_dma_context_t *ctx, uint16_t io_base, uint8_t irq);

/**
 * @brief Cleanup advanced DMA system
 * @param ctx DMA context structure
 */
void advanced_dma_cleanup(advanced_dma_context_t *ctx);

/**
 * @brief Reset DMA rings and state
 * @param ctx DMA context structure
 * @return 0 on success, negative error code on failure
 */
int advanced_dma_reset(advanced_dma_context_t *ctx);

/* === Ring Buffer Management === */

/**
 * @brief Initialize TX/RX descriptor rings
 * @param ctx DMA context structure
 * @return 0 on success, negative error code on failure
 */
int dma_init_descriptor_rings(advanced_dma_context_t *ctx);

/**
 * @brief Allocate TX descriptor from ring
 * @param ctx DMA context structure
 * @param desc_index Output for allocated descriptor index
 * @return Pointer to TX descriptor, NULL if none available
 */
enhanced_tx_desc_t *dma_alloc_tx_descriptor(advanced_dma_context_t *ctx, uint16_t *desc_index);

/**
 * @brief Free TX descriptor back to ring
 * @param ctx DMA context structure
 * @param desc_index Descriptor index to free
 * @return 0 on success, negative error code on failure
 */
int dma_free_tx_descriptor(advanced_dma_context_t *ctx, uint16_t desc_index);

/**
 * @brief Allocate RX descriptor from ring
 * @param ctx DMA context structure
 * @param desc_index Output for allocated descriptor index
 * @return Pointer to RX descriptor, NULL if none available
 */
enhanced_rx_desc_t *dma_alloc_rx_descriptor(advanced_dma_context_t *ctx, uint16_t *desc_index);

/**
 * @brief Free RX descriptor back to ring
 * @param ctx DMA context structure
 * @param desc_index Descriptor index to free
 * @return 0 on success, negative error code on failure
 */
int dma_free_rx_descriptor(advanced_dma_context_t *ctx, uint16_t desc_index);

/* === Scatter-Gather DMA Operations === */

/**
 * @brief Setup scatter-gather TX operation
 * @param ctx DMA context structure
 * @param desc TX descriptor to setup
 * @param fragments Array of fragment descriptors
 * @param fragment_count Number of fragments
 * @return 0 on success, negative error code on failure
 */
int dma_setup_sg_tx(advanced_dma_context_t *ctx, enhanced_tx_desc_t *desc,
                    dma_fragment_desc_t *fragments, uint16_t fragment_count);

/**
 * @brief Setup scatter-gather RX operation
 * @param ctx DMA context structure
 * @param desc RX descriptor to setup
 * @param fragments Array of fragment descriptors
 * @param fragment_count Number of fragments
 * @return 0 on success, negative error code on failure
 */
int dma_setup_sg_rx(advanced_dma_context_t *ctx, enhanced_rx_desc_t *desc,
                    dma_fragment_desc_t *fragments, uint16_t fragment_count);

/**
 * @brief Consolidate fragments into single buffer
 * @param fragments Array of fragment descriptors
 * @param fragment_count Number of fragments
 * @param dest_buffer Destination buffer
 * @param dest_size Size of destination buffer
 * @return Number of bytes consolidated, negative on error
 */
int dma_consolidate_fragments(dma_fragment_desc_t *fragments, uint16_t fragment_count,
                             void *dest_buffer, uint32_t dest_size);

/* === DMA Completion Handling === */

/**
 * @brief Check for TX completion
 * @param ctx DMA context structure
 * @param completed_mask Output mask of completed descriptors
 * @return Number of completed TX operations
 */
int dma_check_tx_completion(advanced_dma_context_t *ctx, uint16_t *completed_mask);

/**
 * @brief Check for RX completion
 * @param ctx DMA context structure
 * @param completed_mask Output mask of completed descriptors
 * @return Number of completed RX operations
 */
int dma_check_rx_completion(advanced_dma_context_t *ctx, uint16_t *completed_mask);

/**
 * @brief Handle TX completion
 * @param ctx DMA context structure
 * @param desc_index Index of completed descriptor
 * @return 0 on success, negative error code on failure
 */
int dma_handle_tx_completion(advanced_dma_context_t *ctx, uint16_t desc_index);

/**
 * @brief Handle RX completion
 * @param ctx DMA context structure
 * @param desc_index Index of completed descriptor
 * @return 0 on success, negative error code on failure
 */
int dma_handle_rx_completion(advanced_dma_context_t *ctx, uint16_t desc_index);

/* === Timeout and Error Recovery === */

/**
 * @brief Check for DMA timeouts
 * @param ctx DMA context structure
 * @return Bitmask of timed out operations
 */
uint32_t dma_check_timeouts(advanced_dma_context_t *ctx);

/**
 * @brief Recover from TX timeout
 * @param ctx DMA context structure
 * @param desc_index Index of timed out descriptor
 * @return 0 on success, negative error code on failure
 */
int dma_recover_tx_timeout(advanced_dma_context_t *ctx, uint16_t desc_index);

/**
 * @brief Recover from RX timeout
 * @param ctx DMA context structure
 * @param desc_index Index of timed out descriptor
 * @return 0 on success, negative error code on failure
 */
int dma_recover_rx_timeout(advanced_dma_context_t *ctx, uint16_t desc_index);

/**
 * @brief Stall DMA engine
 * @param ctx DMA context structure
 * @param tx_stall Stall TX engine
 * @param rx_stall Stall RX engine
 * @return 0 on success, negative error code on failure
 */
int dma_stall_engines(advanced_dma_context_t *ctx, bool tx_stall, bool rx_stall);

/**
 * @brief Unstall DMA engine
 * @param ctx DMA context structure
 * @param tx_unstall Unstall TX engine
 * @param rx_unstall Unstall RX engine
 * @return 0 on success, negative error code on failure
 */
int dma_unstall_engines(advanced_dma_context_t *ctx, bool tx_unstall, bool rx_unstall);

/* === Zero-Copy Operations === */

/**
 * @brief Check if packet is eligible for zero-copy TX
 * @param packet_data Packet data pointer
 * @param packet_length Packet length
 * @param alignment_requirement Required alignment
 * @return true if zero-copy eligible, false otherwise
 */
bool dma_is_zero_copy_tx_eligible(const void *packet_data, uint32_t packet_length, 
                                 uint32_t alignment_requirement);

/**
 * @brief Setup zero-copy TX operation
 * @param ctx DMA context structure
 * @param desc TX descriptor to setup
 * @param packet_data Packet data pointer
 * @param packet_length Packet length
 * @return 0 on success, negative error code on failure
 */
int dma_setup_zero_copy_tx(advanced_dma_context_t *ctx, enhanced_tx_desc_t *desc,
                          const void *packet_data, uint32_t packet_length);

/**
 * @brief Setup zero-copy RX operation
 * @param ctx DMA context structure
 * @param desc RX descriptor to setup
 * @param buffer_data Buffer data pointer
 * @param buffer_length Buffer length
 * @return 0 on success, negative error code on failure
 */
int dma_setup_zero_copy_rx(advanced_dma_context_t *ctx, enhanced_rx_desc_t *desc,
                          void *buffer_data, uint32_t buffer_length);

/* === Performance Monitoring === */

/**
 * @brief Update DMA performance statistics
 * @param ctx DMA context structure
 * @param tx_bytes TX bytes transferred
 * @param rx_bytes RX bytes transferred
 */
void dma_update_performance_stats(advanced_dma_context_t *ctx, 
                                 uint32_t tx_bytes, uint32_t rx_bytes);

/**
 * @brief Get DMA performance report
 * @param ctx DMA context structure
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @return Number of characters written, negative on error
 */
int dma_get_performance_report(advanced_dma_context_t *ctx, char *buffer, size_t buffer_size);

/**
 * @brief Reset performance statistics
 * @param ctx DMA context structure
 */
void dma_reset_performance_stats(advanced_dma_context_t *ctx);

/* === Cache Coherency Integration === */

/**
 * @brief Prepare DMA buffer for transfer (cache coherency)
 * @param ctx DMA context structure
 * @param buffer Buffer address
 * @param length Buffer length
 * @param direction Transfer direction (0=TX, 1=RX)
 * @return 0 on success, negative error code on failure
 */
int dma_prepare_coherent_buffer(advanced_dma_context_t *ctx, void *buffer, 
                               uint32_t length, int direction);

/**
 * @brief Complete DMA buffer transfer (cache coherency)
 * @param ctx DMA context structure
 * @param buffer Buffer address
 * @param length Buffer length
 * @param direction Transfer direction (0=TX, 1=RX)
 * @return 0 on success, negative error code on failure
 */
int dma_complete_coherent_buffer(advanced_dma_context_t *ctx, void *buffer,
                                uint32_t length, int direction);

/* === Hardware Interface Functions === */

/**
 * @brief Start DMA transfer
 * @param ctx DMA context structure
 * @param tx_start Start TX DMA
 * @param rx_start Start RX DMA
 * @return 0 on success, negative error code on failure
 */
int dma_start_transfer(advanced_dma_context_t *ctx, bool tx_start, bool rx_start);

/**
 * @brief Stop DMA transfer
 * @param ctx DMA context structure
 * @param tx_stop Stop TX DMA
 * @param rx_stop Stop RX DMA
 * @return 0 on success, negative error code on failure
 */
int dma_stop_transfer(advanced_dma_context_t *ctx, bool tx_stop, bool rx_stop);

/**
 * @brief Get DMA engine status
 * @param ctx DMA context structure
 * @param tx_status Output for TX status
 * @param rx_status Output for RX status
 * @return 0 on success, negative error code on failure
 */
int dma_get_engine_status(advanced_dma_context_t *ctx, uint32_t *tx_status, uint32_t *rx_status);

/* === Debugging and Diagnostics === */

/**
 * @brief Dump DMA ring state for debugging
 * @param ctx DMA context structure
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @return Number of characters written, negative on error
 */
int dma_dump_ring_state(advanced_dma_context_t *ctx, char *buffer, size_t buffer_size);

/**
 * @brief Validate DMA descriptor integrity
 * @param ctx DMA context structure
 * @return 0 if valid, negative error code if corrupted
 */
int dma_validate_descriptors(advanced_dma_context_t *ctx);

/**
 * @brief Test DMA functionality
 * @param ctx DMA context structure
 * @return 0 on success, negative error code on failure
 */
int dma_self_test(advanced_dma_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* _DMA_DESCRIPTORS_H_ */