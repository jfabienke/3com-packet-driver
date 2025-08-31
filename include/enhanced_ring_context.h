/**
 * @file enhanced_ring_context.h
 * @brief Enhanced ring buffer management with 16-descriptor rings and zero memory leaks
 * 
 * Sprint 0B.3: Enhanced Ring Buffer Management
 * 
 * This header provides enhanced ring buffer management capabilities with:
 * - 16-descriptor TX/RX rings (doubled from 8)
 * - Linux-style cur/dirty pointer tracking
 * - Sophisticated buffer recycling with zero memory leaks
 * - Buffer pool management integration
 * - Ring statistics and monitoring
 * 
 * This implementation follows the proven Linux driver design patterns
 * for maximum reliability and performance.
 */

#ifndef _ENHANCED_RING_CONTEXT_H_
#define _ENHANCED_RING_CONTEXT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "buffer_alloc.h"
#include "3c515.h"
#include "memory.h"

/* Enhanced ring buffer constants - Linux standard */
#define TX_RING_SIZE 16         /* Increase from 8 to match Linux standard */
#define RX_RING_SIZE 16         /* Increase from 8 to match Linux standard */
#define RING_ALIGNMENT 16       /* DMA alignment requirement */

/* Buffer sizes optimized for Ethernet frames */
#define RING_BUFFER_SIZE 1600   /* Buffer size per descriptor */
#define MIN_RING_SIZE 4         /* Minimum ring size */
#define MAX_RING_SIZE 256       /* Maximum ring size */

/* Ring states */
typedef enum {
    RING_STATE_UNINITIALIZED = 0,
    RING_STATE_INITIALIZING,
    RING_STATE_READY,
    RING_STATE_ACTIVE,
    RING_STATE_STOPPING,
    RING_STATE_ERROR
} ring_state_t;

/* Ring statistics structure */
typedef struct {
    /* Allocation statistics */
    uint32_t total_allocations;         /* Total buffer allocations */
    uint32_t total_deallocations;       /* Total buffer deallocations */
    uint32_t allocation_failures;       /* Failed allocations */
    uint32_t deallocation_failures;     /* Failed deallocations */
    
    /* Ring operation statistics */
    uint32_t tx_packets;                /* Transmitted packets */
    uint32_t rx_packets;                /* Received packets */
    uint32_t tx_bytes;                  /* Transmitted bytes */
    uint32_t rx_bytes;                  /* Received bytes */
    uint32_t tx_errors;                 /* Transmit errors */
    uint32_t rx_errors;                 /* Receive errors */
    
    /* Buffer management statistics */
    uint32_t buffer_recycled;           /* Buffers recycled successfully */
    uint32_t buffer_leaks_detected;     /* Memory leaks detected */
    uint32_t buffer_pool_exhausted;     /* Pool exhaustion events */
    uint32_t peak_tx_usage;             /* Peak TX ring usage */
    uint32_t peak_rx_usage;             /* Peak RX ring usage */
    
    /* Performance metrics */
    uint32_t ring_full_events;          /* Ring full events */
    uint32_t ring_empty_events;         /* Ring empty events */
    uint32_t dma_stall_events;          /* DMA stall events */
    uint32_t refill_failures;           /* RX refill failures */
    
    /* Memory leak detection */
    uint32_t current_allocated_buffers; /* Currently allocated buffers */
    uint32_t max_allocated_buffers;     /* Maximum allocated buffers */
    uint32_t leaked_buffers;            /* Buffers never freed */
    
    /* Timing statistics */
    uint32_t avg_tx_completion_time;    /* Average TX completion time */
    uint32_t avg_rx_processing_time;    /* Average RX processing time */
} ring_stats_t;

/* Buffer pool management structure */
typedef struct {
    buffer_pool_t *pool;                /* Underlying buffer pool */
    uint32_t pool_size;                 /* Pool size */
    uint32_t available_buffers;         /* Available buffers */
    uint32_t allocated_buffers;         /* Currently allocated */
    uint32_t max_allocation;            /* Maximum allocation reached */
    bool auto_expand;                   /* Auto-expand pool when full */
    uint32_t expand_increment;          /* Increment for expansion */
    uint32_t shrink_threshold;          /* Threshold for shrinking */
} buffer_pool_mgr_t;

/* Enhanced ring context structure */
typedef struct {
    /* Descriptor rings (aligned for DMA) */
    _3c515_tx_desc_t tx_ring[TX_RING_SIZE] __attribute__((aligned(RING_ALIGNMENT)));
    _3c515_rx_desc_t rx_ring[RX_RING_SIZE] __attribute__((aligned(RING_ALIGNMENT)));
    
    /* Buffer tracking arrays */
    uint8_t *tx_buffers[TX_RING_SIZE];  /* TX buffer pointers */
    uint8_t *rx_buffers[RX_RING_SIZE];  /* RX buffer pointers */
    
    /* Buffer descriptor tracking */
    buffer_desc_t *tx_buffer_descs[TX_RING_SIZE];  /* TX buffer descriptors */
    buffer_desc_t *rx_buffer_descs[RX_RING_SIZE];  /* RX buffer descriptors */
    
    /* Linux-style ring management pointers */
    uint16_t cur_tx;                    /* Current TX index (next to use) */
    uint16_t dirty_tx;                  /* Dirty TX index (next to clean) */
    uint16_t cur_rx;                    /* Current RX index (next to use) */
    uint16_t dirty_rx;                  /* Dirty RX index (next to clean) */
    
    /* Ring state and control */
    ring_state_t state;                 /* Current ring state */
    uint16_t tx_ring_size;              /* Actual TX ring size */
    uint16_t rx_ring_size;              /* Actual RX ring size */
    uint32_t flags;                     /* Ring flags */
    
    /* Buffer pool management */
    buffer_pool_mgr_t tx_pool_mgr;      /* TX buffer pool manager */
    buffer_pool_mgr_t rx_pool_mgr;      /* RX buffer pool manager */
    buffer_pool_t *shared_pool;         /* Shared buffer pool */
    
    /* Physical addresses for DMA */
    uint32_t tx_ring_phys;              /* TX ring physical address */
    uint32_t rx_ring_phys;              /* RX ring physical address */
    
    /* Statistics and monitoring */
    ring_stats_t stats;                 /* Ring statistics */
    uint32_t last_stats_update;         /* Last statistics update time */
    
    /* Memory leak detection */
    uint32_t allocated_buffer_count;    /* Currently allocated buffers */
    uint32_t allocation_sequence;       /* Allocation sequence number */
    
    /* Synchronization and locking */
    bool tx_lock;                       /* TX ring lock */
    bool rx_lock;                       /* RX ring lock */
    uint32_t lock_timeout;              /* Lock timeout in ms */
    
    /* Error handling */
    uint32_t last_error;                /* Last error code */
    char error_message[128];            /* Last error message */
    
    /* Configuration */
    uint32_t tx_threshold;              /* TX completion threshold */
    uint32_t rx_threshold;              /* RX refill threshold */
    bool auto_refill;                   /* Auto-refill RX ring */
    bool zero_copy_enabled;             /* Zero-copy optimization */
    
    /* Hardware integration */
    uint16_t io_base;                   /* Hardware I/O base address */
    uint8_t irq;                        /* IRQ number */
    bool dma_enabled;                   /* DMA enabled flag */
    
} enhanced_ring_context_t;

/* Ring flags */
#define RING_FLAG_DMA_ENABLED          BIT(0)  /* DMA enabled */
#define RING_FLAG_AUTO_REFILL          BIT(1)  /* Auto-refill RX ring */
#define RING_FLAG_ZERO_COPY            BIT(2)  /* Zero-copy enabled */
#define RING_FLAG_STATS_ENABLED        BIT(3)  /* Statistics enabled */
#define RING_FLAG_LEAK_DETECTION       BIT(4)  /* Memory leak detection */
#define RING_FLAG_POOL_AUTO_EXPAND     BIT(5)  /* Auto-expand pools */
#define RING_FLAG_PERSISTENT_BUFFERS   BIT(6)  /* Persistent buffer allocation */
#define RING_FLAG_ALIGNED_BUFFERS      BIT(7)  /* Aligned buffer allocation */

/* Enhanced ring management function declarations */

/* Ring initialization and cleanup */
int enhanced_ring_init(enhanced_ring_context_t *ring, uint16_t io_base, uint8_t irq);
void enhanced_ring_cleanup(enhanced_ring_context_t *ring);
int enhanced_ring_reset(enhanced_ring_context_t *ring);
int enhanced_ring_configure(enhanced_ring_context_t *ring, uint32_t flags);

/* Buffer pool management */
int ring_buffer_pool_init(enhanced_ring_context_t *ring);
void ring_buffer_pool_cleanup(enhanced_ring_context_t *ring);
int ring_buffer_pool_expand(enhanced_ring_context_t *ring, bool tx_pool, uint32_t additional_buffers);
int ring_buffer_pool_shrink(enhanced_ring_context_t *ring, bool tx_pool, uint32_t remove_buffers);

/* Linux-style ring operations */
int refill_rx_ring(enhanced_ring_context_t *ring);
int clean_tx_ring(enhanced_ring_context_t *ring);
uint16_t get_tx_free_slots(const enhanced_ring_context_t *ring);
uint16_t get_rx_filled_slots(const enhanced_ring_context_t *ring);

/* Buffer allocation and deallocation with leak prevention */
uint8_t *allocate_tx_buffer(enhanced_ring_context_t *ring, uint16_t entry);
uint8_t *allocate_rx_buffer(enhanced_ring_context_t *ring, uint16_t entry);
void deallocate_tx_buffer(enhanced_ring_context_t *ring, uint16_t entry);
void deallocate_rx_buffer(enhanced_ring_context_t *ring, uint16_t entry);

/* Buffer recycling and management */
int recycle_tx_buffer(enhanced_ring_context_t *ring, uint16_t entry);
int recycle_rx_buffer(enhanced_ring_context_t *ring, uint16_t entry);
int validate_buffer_integrity(enhanced_ring_context_t *ring);
int detect_buffer_leaks(enhanced_ring_context_t *ring);

/* Ring state management */
ring_state_t get_ring_state(const enhanced_ring_context_t *ring);
int set_ring_state(enhanced_ring_context_t *ring, ring_state_t state);
bool is_ring_ready(const enhanced_ring_context_t *ring);
bool is_ring_active(const enhanced_ring_context_t *ring);

/* Statistics and monitoring */
void ring_stats_init(ring_stats_t *stats);
void ring_stats_update(enhanced_ring_context_t *ring);
const ring_stats_t *get_ring_stats(const enhanced_ring_context_t *ring);
void print_ring_stats(const enhanced_ring_context_t *ring);
void reset_ring_stats(enhanced_ring_context_t *ring);

/* Memory leak detection and prevention */
int ring_leak_detection_init(enhanced_ring_context_t *ring);
int ring_leak_detection_check(enhanced_ring_context_t *ring);
void ring_leak_detection_report(const enhanced_ring_context_t *ring);
int ring_force_cleanup_leaks(enhanced_ring_context_t *ring);

/* Physical address management for DMA */
uint32_t get_physical_address(const void *virtual_addr);
int setup_dma_mapping(enhanced_ring_context_t *ring);
void cleanup_dma_mapping(enhanced_ring_context_t *ring);

/* Ring synchronization */
int ring_acquire_tx_lock(enhanced_ring_context_t *ring);
void ring_release_tx_lock(enhanced_ring_context_t *ring);
int ring_acquire_rx_lock(enhanced_ring_context_t *ring);
void ring_release_rx_lock(enhanced_ring_context_t *ring);

/* Diagnostic and debugging functions */
void ring_dump_state(const enhanced_ring_context_t *ring);
void ring_dump_descriptors(const enhanced_ring_context_t *ring, bool tx_ring);
void ring_dump_buffers(const enhanced_ring_context_t *ring, bool tx_ring);
int ring_validate_consistency(const enhanced_ring_context_t *ring);

/* Performance optimization */
void ring_prefetch_descriptors(const enhanced_ring_context_t *ring, bool tx_ring);
int ring_optimize_for_cpu(enhanced_ring_context_t *ring);
int ring_tune_thresholds(enhanced_ring_context_t *ring);

/* Error handling */
const char *ring_error_to_string(uint32_t error_code);
void ring_set_error(enhanced_ring_context_t *ring, uint32_t error_code, const char *message);
uint32_t ring_get_last_error(const enhanced_ring_context_t *ring);

/* Testing and validation support */
int ring_run_self_test(enhanced_ring_context_t *ring);
int ring_simulate_memory_pressure(enhanced_ring_context_t *ring);
int ring_validate_zero_leaks(enhanced_ring_context_t *ring);

/* Ring error codes */
#define RING_ERROR_NONE                 0x0000
#define RING_ERROR_INVALID_PARAM        0x0001
#define RING_ERROR_OUT_OF_MEMORY        0x0002
#define RING_ERROR_POOL_EXHAUSTED       0x0003
#define RING_ERROR_BUFFER_LEAK          0x0004
#define RING_ERROR_DMA_MAPPING          0x0005
#define RING_ERROR_HARDWARE_FAILURE     0x0006
#define RING_ERROR_LOCK_TIMEOUT         0x0007
#define RING_ERROR_INVALID_STATE        0x0008
#define RING_ERROR_BUFFER_CORRUPTION    0x0009
#define RING_ERROR_RING_FULL            0x000A
#define RING_ERROR_RING_EMPTY           0x000B

#ifdef __cplusplus
}
#endif

#endif /* _ENHANCED_RING_CONTEXT_H_ */