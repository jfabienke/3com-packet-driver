/**
 * @file buffer_alloc.h
 * @brief Buffer allocation functions
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef _BUFFER_ALLOC_H_
#define _BUFFER_ALLOC_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include "common.h"
#include "memory.h"
#include "nic_buffer_pools.h"

/* Buffer types */
typedef enum {
    BUFFER_TYPE_TX = 0,                     /* Transmit buffer */
    BUFFER_TYPE_RX,                         /* Receive buffer */
    BUFFER_TYPE_DMA_TX,                     /* DMA transmit buffer */
    BUFFER_TYPE_DMA_RX,                     /* DMA receive buffer */
    BUFFER_TYPE_DESCRIPTOR,                 /* Descriptor buffer */
    BUFFER_TYPE_TEMPORARY                   /* Temporary buffer */
} buffer_type_t;

/* Buffer states */
typedef enum {
    BUFFER_STATE_FREE = 0,                  /* Buffer is free */
    BUFFER_STATE_ALLOCATED,                 /* Buffer is allocated */
    BUFFER_STATE_IN_USE,                    /* Buffer is in use */
    BUFFER_STATE_PENDING,                   /* Buffer is pending */
    BUFFER_STATE_ERROR                      /* Buffer has error */
} buffer_state_t;

/* Buffer descriptor structure */
typedef struct buffer_desc {
    void *data;                             /* Buffer data pointer */
    uint32_t size;                          /* Buffer size */
    uint32_t used;                          /* Used bytes */
    buffer_type_t type;                     /* Buffer type */
    buffer_state_t state;                   /* Buffer state */
    uint32_t flags;                         /* Buffer flags */
    uint32_t timestamp;                     /* Allocation timestamp */
    uint32_t magic;                         /* Magic number for validation */
    struct buffer_desc *next;               /* Next buffer in list */
    struct buffer_desc *prev;               /* Previous buffer in list */
    void *private_data;                     /* Private data pointer */
} buffer_desc_t;

/* Buffer pool structure */
typedef struct buffer_pool {
    buffer_desc_t *free_list;               /* Free buffer list */
    buffer_desc_t *used_list;               /* Used buffer list */
    uint32_t buffer_size;                   /* Size of each buffer */
    uint32_t buffer_count;                  /* Total number of buffers */
    uint32_t free_count;                    /* Number of free buffers */
    uint32_t used_count;                    /* Number of used buffers */
    uint32_t peak_usage;                    /* Peak usage count */
    buffer_type_t type;                     /* Pool type */
    uint32_t flags;                         /* Pool flags */
    void *memory_base;                      /* Base memory pointer */
    uint32_t memory_size;                   /* Total memory size */
    bool initialized;                       /* Pool initialized flag */
} buffer_pool_t;

/* Buffer allocation statistics */
typedef struct buffer_stats {
    uint32_t total_allocations;             /* Total allocations */
    uint32_t total_frees;                   /* Total frees */
    uint32_t allocation_failures;           /* Failed allocations */
    uint32_t current_allocated;             /* Currently allocated */
    uint32_t peak_allocated;                /* Peak allocation count */
    uint32_t bytes_allocated;               /* Total bytes allocated */
    uint32_t bytes_freed;                   /* Total bytes freed */
    uint32_t pool_overflows;                /* Pool overflow count */
    uint32_t pool_underflows;               /* Pool underflow count */
} buffer_stats_t;

/* RX_COPYBREAK optimization structure */
typedef struct rx_copybreak_pool {
    buffer_pool_t small_pool;               /* Pool of small buffers */
    buffer_pool_t large_pool;               /* Pool of large buffers */
    uint32_t small_buffer_count;            /* Number of small buffers */
    uint32_t large_buffer_count;            /* Number of large buffers */
    uint32_t copybreak_threshold;           /* Size threshold for copying */
    
    /* Statistics */
    uint32_t small_allocations;             /* Small buffer allocations */
    uint32_t large_allocations;             /* Large buffer allocations */
    uint32_t copy_operations;               /* Number of copy operations */
    uint32_t memory_saved;                  /* Memory saved by optimization */
} rx_copybreak_pool_t;

/* XMS buffer pool structure for extended memory buffers */
typedef struct xms_buffer_pool {
    uint16_t xms_handle;                    /* XMS handle for this pool */
    uint32_t total_size;                    /* Total size of XMS allocation */
    uint32_t buffer_size;                   /* Size of each buffer */
    uint32_t buffer_count;                  /* Number of buffers */
    uint32_t free_map;                      /* Bitmap of free buffers (32 max) */
    uint32_t staging_offset;                /* Offset for staging area */
    
    /* Statistics */
    uint32_t xms_allocations;               /* XMS buffer allocations */
    uint32_t xms_frees;                     /* XMS buffer frees */
    uint32_t xms_copies_to;                 /* Copies to XMS */
    uint32_t xms_copies_from;               /* Copies from XMS */
    uint32_t peak_usage;                    /* Peak buffer usage */
} xms_buffer_pool_t;

/* Magic cookie for buffer validation */
#define STAGING_BUFFER_MAGIC 0xBEEF

/* Staging buffer for ISR to bottom-half transfer */
typedef struct staging_buffer {
    uint16_t magic;                         /* Magic cookie for validation */
    uint8_t *data;                          /* Conventional memory buffer */
    uint16_t size;                          /* Buffer size */
    uint16_t used;                          /* Bytes used */
    volatile uint8_t in_use;                /* Buffer in use flag (volatile!) */
    uint8_t nic_index;                      /* Source NIC index (preserved) */
    uint16_t packet_size;                   /* Size of packet in buffer */
    struct staging_buffer *next;            /* For freelist only */
} staging_buffer_t;

/* Compiler barrier for single-core x86 ordering */
#define compiler_barrier() __asm__ volatile("" : : : "memory")

/* SPSC (Single Producer Single Consumer) ring buffer for ISR safety */
#define SPSC_QUEUE_SIZE 32  /* Must be power of 2 */
#define SPSC_QUEUE_MASK (SPSC_QUEUE_SIZE - 1)

/* Compile-time checks for queue safety */
#if (SPSC_QUEUE_SIZE & (SPSC_QUEUE_SIZE - 1)) != 0
#error SPSC_QUEUE_SIZE must be a power of two
#endif

#if SPSC_QUEUE_SIZE > 256
#error SPSC_QUEUE_SIZE must be <= 256 when using 8-bit indices
#endif

typedef struct spsc_queue {
    volatile uint8_t head;                  /* Consumer index (bottom-half writes) - ATOMIC! */
    volatile uint8_t tail;                  /* Producer index (ISR writes) - ATOMIC! */
    staging_buffer_t* buffers[SPSC_QUEUE_SIZE]; /* Ring buffer */
    /* Statistics - 16-bit for atomicity on 16-bit CPU */
    uint16_t enqueue_success;
    uint16_t enqueue_full;
    uint16_t dequeue_success;
    uint16_t dequeue_empty;
} spsc_queue_t;

/* XMS packet descriptor for deferred processing */
typedef struct xms_packet_desc {
    uint16_t xms_handle;                    /* XMS handle */
    uint32_t xms_offset;                    /* Offset in XMS */
    uint16_t packet_size;                   /* Packet size */
    uint8_t nic_index;                      /* Source NIC */
} xms_packet_desc_t;

/* Buffer flags */
#define BUFFER_FLAG_DMA_CAPABLE     BIT(0)  /* Buffer is DMA-capable */
#define BUFFER_FLAG_ALIGNED         BIT(1)  /* Buffer is aligned */
#define BUFFER_FLAG_ZERO_INIT       BIT(2)  /* Zero-initialize buffer */
#define BUFFER_FLAG_PERSISTENT      BIT(3)  /* Persistent allocation */
#define BUFFER_FLAG_LOCKED          BIT(4)  /* Lock in memory */
#define BUFFER_FLAG_SHARED          BIT(5)  /* Shared buffer */
#define BUFFER_FLAG_READ_ONLY       BIT(6)  /* Read-only buffer */
#define BUFFER_FLAG_WRITE_ONLY      BIT(7)  /* Write-only buffer */

/* RX_COPYBREAK optimization constants */
#define RX_COPYBREAK_THRESHOLD      200     /* 200 bytes threshold */
#define SMALL_BUFFER_SIZE           256     /* Small buffer size */
#define LARGE_BUFFER_SIZE           1600    /* Large buffer size */

/* Global buffer pools */
extern buffer_pool_t g_tx_buffer_pool;
extern buffer_pool_t g_rx_buffer_pool;
extern buffer_pool_t g_dma_buffer_pool;
extern buffer_stats_t g_buffer_stats;

/* Buffer pool management */
int buffer_pool_init(buffer_pool_t *pool, buffer_type_t type,
                    uint32_t buffer_size, uint32_t buffer_count, uint32_t flags);
void buffer_pool_cleanup(buffer_pool_t *pool);
int buffer_pool_expand(buffer_pool_t *pool, uint32_t additional_buffers);
int buffer_pool_shrink(buffer_pool_t *pool, uint32_t remove_buffers);

/* Buffer allocation and deallocation */
buffer_desc_t* buffer_alloc(buffer_pool_t *pool);
void buffer_free(buffer_pool_t *pool, buffer_desc_t *buffer);
buffer_desc_t* buffer_alloc_type(buffer_type_t type);
void buffer_free_any(buffer_desc_t *buffer);

/* Buffer data operations */
int buffer_set_data(buffer_desc_t *buffer, const void *data, uint32_t size);
int buffer_append_data(buffer_desc_t *buffer, const void *data, uint32_t size);
int buffer_prepend_data(buffer_desc_t *buffer, const void *data, uint32_t size);
int buffer_copy_data(buffer_desc_t *dest, const buffer_desc_t *src);
int buffer_move_data(buffer_desc_t *dest, buffer_desc_t *src);
void buffer_clear_data(buffer_desc_t *buffer);

/* Buffer state management */
void buffer_set_state(buffer_desc_t *buffer, buffer_state_t state);
buffer_state_t buffer_get_state(const buffer_desc_t *buffer);
bool buffer_is_free(const buffer_desc_t *buffer);
bool buffer_is_allocated(const buffer_desc_t *buffer);
bool buffer_is_in_use(const buffer_desc_t *buffer);

/* Buffer validation */
bool buffer_is_valid(const buffer_desc_t *buffer);
bool buffer_validate_magic(const buffer_desc_t *buffer);
int buffer_validate_pool(const buffer_pool_t *pool);
int buffer_check_integrity(void);

/* Buffer utilities */
uint32_t buffer_get_size(const buffer_desc_t *buffer);
uint32_t buffer_get_used_size(const buffer_desc_t *buffer);
uint32_t buffer_get_free_size(const buffer_desc_t *buffer);
void* buffer_get_data_ptr(const buffer_desc_t *buffer);
buffer_type_t buffer_get_type(const buffer_desc_t *buffer);

/* Buffer pool information */
uint32_t buffer_pool_get_free_count(const buffer_pool_t *pool);
uint32_t buffer_pool_get_used_count(const buffer_pool_t *pool);
uint32_t buffer_pool_get_total_count(const buffer_pool_t *pool);
bool buffer_pool_is_empty(const buffer_pool_t *pool);
bool buffer_pool_is_full(const buffer_pool_t *pool);

/* DMA buffer management */
buffer_desc_t* buffer_alloc_dma(uint32_t size, uint32_t alignment);
void buffer_free_dma(buffer_desc_t *buffer);
void* buffer_get_physical_address(const buffer_desc_t *buffer);
int buffer_sync_for_device(buffer_desc_t *buffer);
int buffer_sync_for_cpu(buffer_desc_t *buffer);

/* Buffer alignment operations */
buffer_desc_t* buffer_alloc_aligned(buffer_pool_t *pool, uint32_t alignment);
bool buffer_is_aligned(const buffer_desc_t *buffer, uint32_t alignment);
int buffer_align_data(buffer_desc_t *buffer, uint32_t alignment);

/* Buffer cloning and splitting */
buffer_desc_t* buffer_clone(const buffer_desc_t *source);
int buffer_split(buffer_desc_t *buffer, uint32_t offset,
                buffer_desc_t **first, buffer_desc_t **second);
int buffer_merge(buffer_desc_t *first, buffer_desc_t *second);

/* Buffer statistics */
void buffer_stats_init(buffer_stats_t *stats);
void buffer_stats_update_alloc(buffer_stats_t *stats, uint32_t size);
void buffer_stats_update_free(buffer_stats_t *stats, uint32_t size);
const buffer_stats_t* buffer_get_stats(void);
void buffer_clear_stats(void);
void buffer_print_stats(void);
void buffer_print_pool_info(const buffer_pool_t *pool);

/* Buffer debugging */
void buffer_dump_descriptor(const buffer_desc_t *buffer);
void buffer_dump_pool(const buffer_pool_t *pool);
void buffer_dump_all_pools(void);
void buffer_dump_data(const buffer_desc_t *buffer, uint32_t max_bytes);

/* Buffer leak detection */
#ifdef DEBUG
void buffer_track_allocation(buffer_desc_t *buffer, const char *file,
                            uint32_t line, const char *function);
void buffer_track_free(buffer_desc_t *buffer);
void buffer_dump_leaks(void);
void buffer_clear_tracking(void);

#define BUFFER_ALLOC(pool) \
    buffer_track_alloc(buffer_alloc(pool), __FILE__, __LINE__, __FUNCTION__)
#define BUFFER_FREE(pool, buffer) \
    do { buffer_track_free(buffer); buffer_free(pool, buffer); } while(0)
#else
#define BUFFER_ALLOC(pool) buffer_alloc(pool)
#define BUFFER_FREE(pool, buffer) buffer_free(pool, buffer)
#endif

/* Buffer error handling */
typedef enum {
    BUFFER_ERROR_NONE = 0,
    BUFFER_ERROR_INVALID_PARAM,
    BUFFER_ERROR_OUT_OF_MEMORY,
    BUFFER_ERROR_POOL_FULL,
    BUFFER_ERROR_INVALID_BUFFER,
    BUFFER_ERROR_BUFFER_IN_USE,
    BUFFER_ERROR_SIZE_MISMATCH,
    BUFFER_ERROR_ALIGNMENT,
    BUFFER_ERROR_CORRUPTION
} buffer_error_t;

buffer_error_t buffer_get_last_error(void);
const char* buffer_error_to_string(buffer_error_t error);
void buffer_set_error_handler(void (*handler)(buffer_error_t error, const char* message));

/* Buffer initialization */
int buffer_system_init(void);
void buffer_system_cleanup(void);
int buffer_init_default_pools(void);
void buffer_cleanup_default_pools(void);

/* Optimized buffer allocation for packet processing */
buffer_desc_t* buffer_alloc_ethernet_frame(uint32_t frame_size, buffer_type_t type);
uint32_t buffer_get_optimal_size(uint32_t requested_size);
int buffer_system_init_optimized(void);
int buffer_copy_packet_data(buffer_desc_t *dest, const buffer_desc_t *src);
void buffer_prefetch_data(const buffer_desc_t *buffer);

/* RX_COPYBREAK optimization functions */
int rx_copybreak_init(uint32_t small_count, uint32_t large_count);
void rx_copybreak_cleanup(void);
buffer_desc_t* rx_copybreak_alloc(uint32_t packet_size);
void rx_copybreak_free(buffer_desc_t* buffer);
void rx_copybreak_get_stats(rx_copybreak_pool_t* stats);
int rx_copybreak_resize_pools(uint32_t new_small_count, uint32_t new_large_count);
void rx_copybreak_record_copy(void);

/* XMS buffer pool functions */
int xms_buffer_pool_init(xms_buffer_pool_t *pool, uint32_t buffer_size, uint32_t buffer_count);
void xms_buffer_pool_cleanup(xms_buffer_pool_t *pool);
int xms_buffer_alloc(xms_buffer_pool_t *pool, uint32_t *buffer_offset);
void xms_buffer_free(xms_buffer_pool_t *pool, uint32_t buffer_offset);
int xms_copy_to_buffer(xms_buffer_pool_t *pool, uint32_t offset, void *src, uint32_t size);
int xms_copy_from_buffer(xms_buffer_pool_t *pool, void *dest, uint32_t offset, uint32_t size);

/* Staging buffer functions for ISR */
int staging_buffer_init(uint32_t count, uint32_t size);
void staging_buffer_cleanup(void);
staging_buffer_t* staging_buffer_alloc(void);
void staging_buffer_free(staging_buffer_t *buffer);

/* SPSC queue functions (ISR-safe) */
int spsc_queue_init(spsc_queue_t *queue);
void spsc_queue_cleanup(spsc_queue_t *queue);

/* Hot path functions - consider inlining for performance */
int spsc_queue_enqueue(spsc_queue_t *queue, staging_buffer_t *buffer);
staging_buffer_t* spsc_queue_dequeue(spsc_queue_t *queue);

/* Helper functions */
static inline int spsc_queue_is_empty(spsc_queue_t *queue) {
    return (!queue || queue->head == queue->tail);
}

static inline int spsc_queue_is_full(spsc_queue_t *queue) {
    if (!queue) return 1;
    uint8_t next_tail = (queue->tail + 1) & SPSC_QUEUE_MASK;
    return (next_tail == queue->head);
}

/* Bottom-half processing */
void process_deferred_packets(void);
int packet_process_from_xms(xms_packet_desc_t *desc);

/* === Per-NIC Buffer Pool Functions === */

/* NIC registration and management */
int buffer_register_nic(nic_id_t nic_id, nic_type_t nic_type, const char* nic_name);
int buffer_unregister_nic(nic_id_t nic_id);

/* NIC-aware buffer allocation */
buffer_desc_t* buffer_alloc_nic_aware(nic_id_t nic_id, buffer_type_t type, uint32_t size);
void buffer_free_nic_aware(nic_id_t nic_id, buffer_desc_t* buffer);
buffer_desc_t* buffer_alloc_ethernet_frame_nic(nic_id_t nic_id, uint32_t frame_size, buffer_type_t type);

/* Per-NIC RX_COPYBREAK functions */
buffer_desc_t* buffer_rx_copybreak_alloc_nic(nic_id_t nic_id, uint32_t packet_size);
void buffer_rx_copybreak_free_nic(nic_id_t nic_id, buffer_desc_t* buffer);

/* Resource management and monitoring */
int buffer_rebalance_resources(void);
int buffer_get_nic_stats(nic_id_t nic_id, buffer_pool_stats_t* stats);
void buffer_print_comprehensive_stats(void);
void buffer_monitor_and_rebalance(void);

#ifdef __cplusplus
}
#endif

#endif /* _BUFFER_ALLOC_H_ */
