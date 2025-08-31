/**
 * @file metrics_core.h
 * @brief Core Metrics System for TSR Packet Driver
 * 
 * Implements TSR-safe metrics collection with atomic counters
 * for handle tracking, memory monitoring, interrupt statistics,
 * and module-specific performance metrics.
 */

#ifndef METRICS_CORE_H
#define METRICS_CORE_H

#include <stdint.h>

/* Maximum modules supported */
#define MAX_MODULES 16

/* Completed TX ring buffer size (power of 2) */
#define TX_COMPLETE_RING_SIZE 32

/* TSR-safe atomic operations */
#ifdef __WATCOMC__
/* Watcom C specific inline assembly */
static inline void irq_off_save(uint16_t *flags) {
    *flags = 0;
    __asm {
        pushf
        pop ax
        mov bx, flags
        mov [bx], ax
        cli
    }
}

static inline void irq_restore(uint16_t flags) {
    __asm {
        mov ax, flags
        push ax
        popf
    }
}

#else
/* Generic 16-bit compiler */
void irq_off_save(uint16_t *flags);
void irq_restore(uint16_t flags);
#endif

/* Atomic 32-bit operations */
static inline uint32_t read_u32_atomic(volatile uint16_t *lo, volatile uint16_t *hi) {
    uint16_t f, l, h;
    irq_off_save(&f);
    l = *lo;
    h = *hi;
    irq_restore(f);
    return ((uint32_t)h << 16) | l;
}

static inline void isr_inc_u32(volatile uint16_t *lo, volatile uint16_t *hi) {
    __asm {
        mov bx, lo
        add word ptr [bx], 1
        mov bx, hi
        adc word ptr [bx], 0
    }
}

static inline void isr_add_u32(volatile uint16_t *lo, volatile uint16_t *hi, 
                               uint16_t add_lo, uint16_t add_hi) {
    __asm {
        mov bx, lo
        mov ax, add_lo
        add word ptr [bx], ax
        mov bx, hi
        mov ax, add_hi
        adc word ptr [bx], ax
    }
}

/* Handle tracking counters */
typedef struct {
    volatile uint16_t total_open_lo, total_open_hi;    /* Total handles opened */
    volatile uint16_t total_closed_lo, total_closed_hi; /* Total handles closed */
    volatile uint16_t live_count;                       /* Currently active handles */
    volatile uint16_t peak_count;                       /* Peak concurrent handles */
} handle_global_counters_t;

typedef struct {
    volatile uint16_t open_lo, open_hi;                /* Per-module opens */
    volatile uint16_t close_lo, close_hi;              /* Per-module closes */
    volatile uint16_t live_count;                      /* Per-module active */
    volatile uint16_t peak_count;                      /* Per-module peak */
} handle_module_counters_t;

/* Memory tracking counters */
typedef struct {
    volatile uint16_t cur_lo, cur_hi;                  /* Current allocated bytes */
    volatile uint16_t peak_lo, peak_hi;                /* Peak allocated bytes */
    volatile uint16_t total_allocs_lo, total_allocs_hi; /* Total allocations */
    volatile uint16_t total_frees_lo, total_frees_hi;  /* Total frees */
} mem_counters_t;

typedef struct {
    volatile uint16_t cur_lo, cur_hi;                  /* Current bytes per module */
    volatile uint16_t peak_lo, peak_hi;                /* Peak bytes per module */
} mem_per_module_t;

/* Interrupt and packet counters */
typedef struct {
    volatile uint16_t irq_lo, irq_hi;                  /* Total interrupts */
    volatile uint16_t rx_pkts_lo, rx_pkts_hi;          /* RX packets */
    volatile uint16_t tx_pkts_lo, tx_pkts_hi;          /* TX packets */
    volatile uint16_t rx_bytes_lo, rx_bytes_hi;        /* RX bytes */
    volatile uint16_t tx_bytes_lo, tx_bytes_hi;        /* TX bytes */
    volatile uint16_t rx_err_lo, rx_err_hi;            /* RX errors */
    volatile uint16_t tx_err_lo, tx_err_hi;            /* TX errors */
} interrupt_counters_t;

/* Module performance metrics */
typedef struct {
    volatile uint16_t rx_ok_lo, rx_ok_hi;              /* RX success count */
    volatile uint16_t tx_ok_lo, tx_ok_hi;              /* TX success count */
    volatile uint16_t err_lo, err_hi;                  /* Error count */
    
    /* Latency statistics (stored as microseconds) */
    volatile uint16_t tx_lat_ewma_lo, tx_lat_ewma_hi;  /* EWMA latency (Q16.16) */
    volatile uint16_t tx_lat_min_lo, tx_lat_min_hi;    /* Min latency */
    volatile uint16_t tx_lat_max_lo, tx_lat_max_hi;    /* Max latency */
    
    /* Throughput calculation timestamps */
    volatile uint16_t last_sample_time_lo, last_sample_time_hi;
    volatile uint16_t last_rx_bytes_lo, last_rx_bytes_hi;
    volatile uint16_t last_tx_bytes_lo, last_tx_bytes_hi;
} module_perf_stats_t;

/* TX descriptor for latency measurement */
typedef struct {
    uint32_t submit_time_1193k;                       /* Timestamp at enqueue */
    uint8_t module_id;                                 /* Module identifier */
    uint8_t flags;                                     /* Status flags */
} tx_desc_metrics_t;

/* TX completion ring for deferred latency computation */
typedef struct {
    volatile tx_desc_metrics_t *tx_ring[TX_COMPLETE_RING_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} tx_complete_ring_t;

/* Global metrics state */
typedef struct {
    uint8_t initialized;
    
    /* Global counters */
    handle_global_counters_t handle_global;
    mem_counters_t mem_global;
    interrupt_counters_t irq_global;
    
    /* Per-module counters */
    handle_module_counters_t handle_modules[MAX_MODULES];
    mem_per_module_t mem_modules[MAX_MODULES];
    module_perf_stats_t perf_modules[MAX_MODULES];
    
    /* TX completion tracking */
    tx_complete_ring_t tx_ring;
    
    /* Collection state */
    uint32_t last_collection_time;
    uint16_t collection_interval;
} metrics_system_t;

/**
 * @brief Initialize metrics system
 * 
 * @return 0 on success, negative on error
 */
int metrics_init(void);

/**
 * @brief Cleanup metrics system
 */
void metrics_cleanup(void);

/**
 * @brief Get high-resolution timestamp (1.193MHz PIT + BIOS tick)
 * 
 * @return 32-bit timestamp in ~1.193MHz units
 */
uint32_t metrics_time_1193khz(void);

/**
 * @brief Handle opened (call from handle manager)
 * 
 * @param module_id Module identifier
 */
void metrics_handle_opened(uint8_t module_id);

/**
 * @brief Handle closed (call from handle manager)
 * 
 * @param module_id Module identifier
 */
void metrics_handle_closed(uint8_t module_id);

/**
 * @brief Memory allocated (call from memory manager)
 * 
 * @param size Size in bytes
 * @param module_id Module identifier
 */
void metrics_memory_allocated(uint16_t size, uint8_t module_id);

/**
 * @brief Memory freed (call from memory manager)
 * 
 * @param size Size in bytes
 * @param module_id Module identifier
 */
void metrics_memory_freed(uint16_t size, uint8_t module_id);

/**
 * @brief Record interrupt (call from ISR - must be fast!)
 * 
 * Assembly version: METRICS_ISR_IRQ_INC
 */
void metrics_isr_interrupt(void);

/**
 * @brief Record RX packet (call from ISR - must be fast!)
 * 
 * @param bytes Packet size in bytes
 * Assembly version: METRICS_ISR_RX_PACKET
 */
void metrics_isr_rx_packet(uint16_t bytes);

/**
 * @brief Record TX packet (call from ISR - must be fast!)
 * 
 * @param bytes Packet size in bytes
 * Assembly version: METRICS_ISR_TX_PACKET
 */
void metrics_isr_tx_packet(uint16_t bytes);

/**
 * @brief Record error (call from ISR - must be fast!)
 * 
 * @param is_tx 1 for TX error, 0 for RX error
 * Assembly version: METRICS_ISR_ERROR
 */
void metrics_isr_error(uint8_t is_tx);

/**
 * @brief TX started (foreground - record timestamp)
 * 
 * @param desc TX descriptor to fill
 * @param module_id Module identifier
 */
void metrics_tx_start(tx_desc_metrics_t *desc, uint8_t module_id);

/**
 * @brief TX completed (ISR - enqueue for deferred processing)
 * 
 * @param desc TX descriptor
 */
void metrics_isr_tx_complete(tx_desc_metrics_t *desc);

/**
 * @brief Process TX completions (foreground - call periodically)
 * 
 * @return Number of completions processed
 */
int metrics_process_tx_completions(void);

/**
 * @brief Get current handle count
 * 
 * @return Current active handles
 */
uint32_t metrics_get_handle_count(void);

/**
 * @brief Get current memory usage
 * 
 * @return Current allocated bytes
 */
uint32_t metrics_get_memory_usage(void);

/**
 * @brief Get interrupt count
 * 
 * @return Total interrupts handled
 */
uint32_t metrics_get_interrupt_count(void);

/**
 * @brief Get per-module handle count
 * 
 * @param module_id Module identifier
 * @return Module active handles
 */
uint32_t metrics_get_module_handles(uint8_t module_id);

/**
 * @brief Get module performance statistics
 * 
 * @param module_id Module identifier
 * @param rx_packets Returned RX packet count
 * @param tx_packets Returned TX packet count
 * @param errors Returned error count
 * @param avg_latency_us Returned average latency in microseconds
 * @param min_latency_us Returned minimum latency in microseconds
 * @param max_latency_us Returned maximum latency in microseconds
 */
void metrics_get_module_perf(uint8_t module_id, uint32_t *rx_packets,
                           uint32_t *tx_packets, uint32_t *errors,
                           uint32_t *avg_latency_us, uint32_t *min_latency_us,
                           uint32_t *max_latency_us);

/* Assembly macros for ISR use */
#define METRICS_ISR_IRQ_INC() \
    __asm { \
        add word ptr [g_metrics.irq_global.irq_lo], 1; \
        adc word ptr [g_metrics.irq_global.irq_hi], 0; \
    }

#define METRICS_ISR_RX_PACKET(bytes) \
    __asm { \
        add word ptr [g_metrics.irq_global.rx_pkts_lo], 1; \
        adc word ptr [g_metrics.irq_global.rx_pkts_hi], 0; \
        mov ax, bytes; \
        add word ptr [g_metrics.irq_global.rx_bytes_lo], ax; \
        adc word ptr [g_metrics.irq_global.rx_bytes_hi], 0; \
    }

#define METRICS_ISR_TX_PACKET(bytes) \
    __asm { \
        add word ptr [g_metrics.irq_global.tx_pkts_lo], 1; \
        adc word ptr [g_metrics.irq_global.tx_pkts_hi], 0; \
        mov ax, bytes; \
        add word ptr [g_metrics.irq_global.tx_bytes_lo], ax; \
        adc word ptr [g_metrics.irq_global.tx_bytes_hi], 0; \
    }

#define METRICS_ISR_ERROR(is_tx) \
    if (is_tx) { \
        __asm { \
            add word ptr [g_metrics.irq_global.tx_err_lo], 1; \
            adc word ptr [g_metrics.irq_global.tx_err_hi], 0; \
        } \
    } else { \
        __asm { \
            add word ptr [g_metrics.irq_global.rx_err_lo], 1; \
            adc word ptr [g_metrics.irq_global.rx_err_hi], 0; \
        } \
    }

/* External metrics instance */
extern metrics_system_t g_metrics;

#endif /* METRICS_CORE_H */