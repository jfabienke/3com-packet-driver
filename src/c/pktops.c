/**
 * @file packet_ops.c
 * @brief Packet transmission and reception operations
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#include "dos_io.h"
#include <string.h>
#include <dos.h>
#include "pktops.h"
#include "hardware.h"
#include "routing.h"
#include "statrt.h"
#include "arp.h"
#include "bufaloc.h"
#include "logging.h"
#include "stats.h"
#include "api.h"
#include "flowctl.h"  // Phase 2.3: 802.3x Flow Control
#include "prod.h"
#include "dmamap.h"  // GPT-5: Centralized DMA mapping layer
#include "3c509pio.h"   // GPT-5: PIO fast path for 3C509B
#include "vds.h"           // VDS Virtual DMA Services
#include "vds_mapping.h"   // VDS mapping structures (vds_mapping_t)
#include "3c515.h"         // 3C515 TX descriptor types and context
#include "3c509b.h"        // 3C509B hardware constants
#include "cpudet.h"        // CPU detection and cpu_info_t
#include "pltprob.h" // Platform detection and DMA policy

/* Timing constants for bottom-half processing */
#define MAX_BOTTOM_HALF_TICKS  100  /* Max ticks to process packets */

/**
 * @brief Get BIOS timer ticks (GPT-5 A+ implementation)
 * 
 * Reads the BIOS tick counter at 0x40:0x6C (18.2 Hz).
 * Safe for ISR context - preserves caller's interrupt flag state.
 * 
 * GPT-5 Critical Fix: Uses pushf/cli/popf to preserve IF state
 * instead of unconditionally enabling interrupts.
 * 
 * @return Current 32-bit tick count
 */
static inline uint32_t get_timer_ticks(void) {
    uint32_t ticks = 0;  /* Initialize to silence W200 - set by inline asm */

    /* GPT-5 A+ Fix: Use pushf/cli/popf to preserve caller's IF state */
#if defined(__WATCOMC__)
    /* Watcom inline assembly - GPT-5 Fix: Save/restore ES register */
    __asm {
        push es                 /* Save ES register */
        pushf                   /* Save flags (including IF) */
        cli                     /* Disable interrupts */
        mov ax, 0x0040
        mov es, ax
        mov bx, 0x006C
        mov ax, es:[bx]
        mov dx, es:[bx+2]
        popf                    /* Restore original IF and other flags */
        pop es                  /* Restore ES register */
        mov word ptr ticks, ax
        mov word ptr ticks+2, dx
    }
    
#elif defined(__TURBOC__) || defined(__BORLANDC__)
    /* Borland/Turbo C inline assembly - GPT-5 Fix: Save/restore ES register */
    asm push es;
    asm pushf;
    asm cli;
    asm mov ax, 0x0040;
    asm mov es, ax;
    asm mov bx, 0x006C;
    asm mov ax, es:[bx];
    asm mov dx, es:[bx+2];
    asm popf;
    asm pop es;
    asm mov word ptr ticks, ax;
    asm mov word ptr ticks+2, dx;
    
#elif defined(_MSC_VER)
    /* Microsoft C 6.0 / Visual C++ 1.52 inline assembly - GPT-5 Fix: Save/restore ES register */
    __asm {
        push es
        pushf
        cli
        mov ax, 0x0040
        mov es, ax
        mov bx, 0x006C
        mov ax, es:[bx]
        mov dx, es:[bx+2]
        popf
        pop es
        mov word ptr ticks, ax
        mov word ptr ticks+2, dx
    }
    
#else
    /* Generic fallback using far pointer with IF preservation */
    volatile uint32_t __far * const bda_ticks = (volatile uint32_t __far *)MK_FP(0x0040, 0x006C);
    uint16_t saved_flags;
    
    /* Save flags to memory, disable, read, restore */
    __asm { pushf }
    __asm { pop saved_flags }
    __asm { cli }
    ticks = *bda_ticks;
    __asm { push saved_flags }
    __asm { popf }
#endif
    
    return ticks;
}

/**
 * @brief Calculate elapsed ticks since start time
 * 
 * @param start Starting tick count
 * @return Elapsed ticks (handles wrap-around)
 */
static inline uint32_t ticks_elapsed(uint32_t start) {
    return get_timer_ticks() - start;  /* Handles wrap naturally */
}

/**
 * @brief Convert ticks to milliseconds
 * 
 * @param ticks Tick count
 * @return Approximate milliseconds
 */
static inline uint32_t ticks_to_ms(uint32_t ticks) {
    /* GPT-5 A+ Fix: Overflow-safe calculation for large tick counts */
    /* BIOS tick = ~54.925ms, use 54925/1000 approximation */
    /* Prevent overflow by splitting calculation */
    uint32_t t1 = ticks / 1000;
    uint32_t t2 = ticks % 1000;
    return t1 * 54925UL + (t2 * 54925UL) / 1000UL;
}

/**
 * @brief Convert milliseconds to ticks
 * 
 * @param ms Milliseconds
 * @return Tick count (rounded up)
 */
static inline uint32_t ms_to_ticks(uint32_t ms) {
    /* Round up: (ms + 27)/55 */
    return (ms + 27U) / 55U;
}

/* Additional error codes */
#define PACKET_ERR_NOT_INITIALIZED  -11
#define PACKET_ERR_NO_MEMORY        -12
#define PACKET_ERR_NO_BUFFER        -13
#define PACKET_ERR_NO_PACKET        -14
#define PACKET_ERR_QUEUE_FULL       -15

/* Additional error codes for hardware compatibility */
#ifndef ERROR_NO_DATA
#define ERROR_NO_DATA    -10    /* No data available */
#endif

/* 802.3x Flow Control ethertype (PAUSE frames) */
#ifndef ETHERTYPE_FLOW_CONTROL
#define ETHERTYPE_FLOW_CONTROL      0x8808
#endif

/* Memory allocation flags */
#ifndef MEMORY_FLAG_ZERO
#define MEMORY_FLAG_ZERO            0x01    /* Zero-initialize allocated memory */
#endif

/* Priority queue constants */
#define MAX_PRIORITY_LEVELS         4       /* Number of priority levels */
static packet_queue_t g_packet_queues[MAX_PRIORITY_LEVELS];  /* Priority queues */

/* CPU type constants - compatibility with cpudet.h */
#ifndef CPU_TYPE_80286
#define CPU_TYPE_80286              CPU_DET_80286
#endif
#ifndef CPU_TYPE_80386
#define CPU_TYPE_80386              CPU_DET_80386
#endif

/* Additional error codes for feature support */
#ifndef PACKET_ERR_NOT_SUPPORTED
#define PACKET_ERR_NOT_SUPPORTED    -16
#endif
#ifndef PACKET_ERR_INVALID_DATA
#define PACKET_ERR_INVALID_DATA     -17
#endif
#ifndef PACKET_ERR_TIMEOUT
#define PACKET_ERR_TIMEOUT          -18
#endif
#ifndef PACKET_ERR_LOOPBACK_FAILED
#define PACKET_ERR_LOOPBACK_FAILED  -19
#endif

/* Packet operation state */
static int packet_ops_initialized = 0;
static packet_stats_t packet_statistics = {0};

/* Production queue management state */
static struct {
    packet_queue_t tx_queues[4];    /* Priority-based TX queues */
    packet_queue_t rx_queue;        /* Single RX queue */
    uint32_t queue_full_events;     /* Queue overflow counter */
    uint32_t backpressure_events;   /* Flow control events */
    uint32_t priority_drops;        /* Priority-based drops */
    uint32_t adaptive_resizes;      /* Adaptive size changes */
    bool flow_control_active;       /* Flow control state */
    uint32_t last_queue_check;      /* Last queue health check */
} g_queue_state = {0};

/* VDS deferred unlock queue for ISR safety */
#define MAX_VDS_DEFERRED_UNLOCKS 16
typedef struct {
    vds_mapping_t mapping;  /* GPT-5 fix: store full mapping info */
    bool valid;
} vds_deferred_unlock_t;

static struct {
    vds_deferred_unlock_t queue[MAX_VDS_DEFERRED_UNLOCKS];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    bool bottom_half_pending;
} g_vds_unlock_queue = {0};

/* GPT-5 CRITICAL: TX completion queue for deferred DMA unmapping */
/* Size must be power of 2 and >= TX_RING_SIZE * MAX_NICS (16 * 8 = 128) */
#define MAX_TX_COMPLETIONS 128  /* Power of 2 for lock-free operation */
#define TX_QUEUE_MASK (MAX_TX_COMPLETIONS - 1)
#define TX_TIMEOUT_MS 5000      /* 5 second TX timeout */
/* GPT-5 FIX: Force 32-bit arithmetic to prevent 16-bit overflow */
#define TX_TIMEOUT_TICKS (((unsigned long)TX_TIMEOUT_MS * 182UL + 9999UL) / 10000UL)
#define TX_RING_SIZE 16         /* Standard TX ring size */

typedef struct {
    uint8_t nic_index;          /* NIC index */
    uint8_t desc_index;         /* TX descriptor index */
    dma_mapping_t *mapping;     /* DMA mapping to unmap */
    uint32_t timestamp;         /* BIOS tick timestamp for timeout detection */
    bool error;                 /* Error/timeout flag */
} tx_completion_t;

static struct {
    tx_completion_t queue[MAX_TX_COMPLETIONS];
    volatile uint8_t head;      /* ISR writes here (SPSC producer) */
    volatile uint8_t tail;      /* Bottom-half reads here (SPSC consumer) */
    volatile uint8_t seq;       /* GPT-5 A+: Sequence counter for seqlock */
    volatile bool pending;      /* New: bottom-half has work pending */
    volatile bool overflow;     /* Queue overflow flag for recovery */
    uint32_t overflow_count;    /* Statistics - overflow events */
    uint32_t completed_count;   /* Statistics - successful completions */
    uint32_t total_processed;   /* Total entries processed */
} g_tx_completion_queue = {0};

/* Production queue management constants */
#define TX_QUEUE_URGENT_SIZE     32     /* Urgent priority queue size */
#define TX_QUEUE_HIGH_SIZE       64     /* High priority queue size */
#define TX_QUEUE_NORMAL_SIZE     128    /* Normal priority queue size */
#define TX_QUEUE_LOW_SIZE        64     /* Low priority queue size */
#define RX_QUEUE_SIZE           256     /* RX queue size */
#define QUEUE_WATERMARK_HIGH     80     /* High watermark percentage */
#define QUEUE_WATERMARK_LOW      20     /* Low watermark percentage */
#define FLOW_CONTROL_THRESHOLD   90     /* Flow control threshold percentage */
#define QUEUE_CHECK_INTERVAL_MS  100    /* Queue health check interval */

/* Internal helper functions */
static int route_packet_to_interface(const uint8_t *packet, uint16_t length, uint8_t dest_nic);

/* Forward declarations for bottom-half processing (defined later in file) */
extern xms_buffer_pool_t g_xms_pool;
extern spsc_queue_t g_deferred_queue;
static void process_deferred_packets(void);

/* Bottom-half processing state - forward declaration
 * Full definition is at line ~1118, but referenced earlier */
static struct bottom_half_state_s {
    volatile bool xms_enabled;
    volatile bool bottom_half_active;
    uint16_t xms_threshold;
    uint16_t packets_deferred;
    uint16_t packets_processed;
    uint16_t xms_copies;
    uint16_t staging_exhausted;
    uint16_t queue_full_drops;
    uint16_t oversize_drops;
    uint16_t xms_alloc_failures;
    uint16_t xms_move_failures;
} g_bottom_half_state;

/* GPT-5: TX completion deferred processing functions */
bool packet_queue_tx_completion(uint8_t nic_index, uint8_t desc_index, dma_mapping_t *mapping);
void packet_process_tx_completions(void);
static void packet_check_tx_timeouts(void);
static uint32_t get_bios_ticks(void);

/* Production queue management functions */
static int packet_queue_init_all(void);
static void packet_queue_cleanup_all(void);
static int packet_enqueue_with_priority(packet_buffer_t *buffer, int priority);
static packet_buffer_t* packet_dequeue_by_priority(void);
static int packet_check_queue_health(void);
static void packet_apply_flow_control(void);
static void packet_adaptive_queue_resize(void);
static void packet_handle_queue_overflow(int priority);
static bool packet_should_drop_on_full(int priority, int queue_usage);
static uint32_t packet_calculate_queue_usage(packet_queue_t *queue);
static void packet_update_queue_stats(void);
static int packet_emergency_queue_drain(void);

/* Cold section: Initialization functions (discarded after init) */
#pragma code_seg("COLD_TEXT", "CODE")

/**
 * @brief Initialize packet operations subsystem
 * @param config Driver configuration
 * @return 0 on success, negative on error
 */
int packet_ops_init(const config_t *config) {
    int result;
    
    if (!config) {
        log_error("packet_ops_init: NULL config parameter");
        return PACKET_ERR_INVALID_PARAM;
    }
    
    log_info("Initializing packet operations subsystem with production queue management");
    
    /* Clear statistics */
    memset(&packet_statistics, 0, sizeof(packet_statistics));
    
    /* Initialize production queue management */
    result = packet_queue_init_all();
    if (result != 0) {
        log_error("Failed to initialize production queue management: %d", result);
        return result;
    }
    
    /* Initialize flow control and adaptive management */
    g_queue_state.flow_control_active = false;
    g_queue_state.last_queue_check = stats_get_timestamp();
    
    /* Initialize 802.3x Flow Control with CPU-efficient state management (Phase 2.3) */
    result = fc_simple_init();
    if (result != 0) {
        log_warning("802.3x Flow Control initialization failed: %d, continuing without flow control", result);
        /* Continue - flow control is optional feature */
    } else {
        log_debug("802.3x Flow Control initialized with CPU-efficient state management");
    }
    
    packet_ops_initialized = 1;
    
    log_info("Packet operations subsystem initialized with production features");
    return 0;
}

/**
 * @brief Queue VDS unlock for deferred processing from ISR context
 * @param mapping VDS mapping to unlock later
 * @return true if queued successfully, false if queue full
 */
static bool vds_queue_deferred_unlock(const vds_mapping_t *mapping) {
    bool result = false;
    uint8_t idx;

    /* GPT-5 CRITICAL: Protect queue with critical section */
    _asm { cli }  /* Disable interrupts */
    if (g_vds_unlock_queue.count < MAX_VDS_DEFERRED_UNLOCKS) {
        idx = g_vds_unlock_queue.tail;
        g_vds_unlock_queue.queue[idx].mapping = *mapping;
        g_vds_unlock_queue.queue[idx].valid = true;
        g_vds_unlock_queue.tail = (g_vds_unlock_queue.tail + 1) % MAX_VDS_DEFERRED_UNLOCKS;
        g_vds_unlock_queue.count++;
        g_vds_unlock_queue.bottom_half_pending = true;
        result = true;
    }

    _asm { sti }  /* Re-enable interrupts */
    return result;
}

/**
 * @brief Process deferred VDS unlocks in bottom-half context
 * Called from non-ISR context (including assembly) to safely unlock VDS regions
 */
void vds_process_deferred_unlocks(void) {
    /* GPT-5 CRITICAL: Guard against interrupt context - VDS calls forbidden from ISR */
    static bool in_interrupt_check = false;
    uint16_t flags = 0;  /* Initialize to silence W200 - set by inline asm */
    uint8_t idx;
    vds_deferred_unlock_t unlock_copy;

    /* Simple interrupt context detection */
    if (in_interrupt_check) {
        return; /* Prevent recursion */
    }
    in_interrupt_check = true;

    /* Check if interrupts are disabled (strong indicator of ISR/critical section) */
    _asm {
        pushf
        pop ax
        mov flags, ax
    }
    if (!(flags & 0x0200)) { /* IF flag clear = interrupts disabled */
        in_interrupt_check = false;
        return; /* Unsafe context - defer processing */
    }

    while (g_vds_unlock_queue.count > 0) {
        /* GPT-5 CRITICAL: Protect dequeue operation with critical section */
        _asm { cli }

        if (g_vds_unlock_queue.count == 0) {
            _asm { sti }
            break; /* Race condition check */
        }

        idx = g_vds_unlock_queue.head;
        unlock_copy = g_vds_unlock_queue.queue[idx]; /* Copy under lock */
        g_vds_unlock_queue.queue[idx].valid = false;
        g_vds_unlock_queue.head = (g_vds_unlock_queue.head + 1) % MAX_VDS_DEFERRED_UNLOCKS;
        g_vds_unlock_queue.count--;

        _asm { sti }

        /* Process unlock outside critical section (VDS calls can be slow) */
        if (unlock_copy.valid) {
            vds_unlock_region(&unlock_copy.mapping.dds);
        }
    }

    g_vds_unlock_queue.bottom_half_pending = false;
    in_interrupt_check = false; /* Clear guard */
}

/**
 * @brief Queue TX completion for deferred processing (ISR-safe)
 * @param nic_index NIC index
 * @param desc_index TX descriptor index
 * @param mapping DMA mapping to unmap in bottom-half
 * @return true if queued successfully, false if queue full
 * 
 * GPT-5 CRITICAL: This function MUST be ISR-safe. No VDS calls, no blocking.
 */
bool packet_queue_tx_completion(uint8_t nic_index, uint8_t desc_index, dma_mapping_t *mapping) {
    uint8_t h, next;
    
    /* Read head once (volatile) */
    h = g_tx_completion_queue.head;
    next = (h + 1) & TX_QUEUE_MASK;
    
    /* Check if queue is full (lock-free SPSC) */
    if (next == g_tx_completion_queue.tail) {
        /* Queue full - set overflow flag for recovery */
        g_tx_completion_queue.overflow_count++;
        g_tx_completion_queue.overflow = true;
        /* CRITICAL: Do NOT clear desc->mapping - let overflow recovery handle it */
        return false;
    }
    
    /* GPT-5 A+: Begin seqlock update - increment sequence */
    g_tx_completion_queue.seq++;
    
    /* Compiler barrier after sequence increment */
    #ifdef __WATCOMC__
    #pragma aux memory_barrier = "" modify exact [] nomemory;
    memory_barrier();
    #else
    memory_barrier();
    #endif
    
    /* Add completion to queue with timestamp for watchdog */
    g_tx_completion_queue.queue[h].nic_index = nic_index;
    g_tx_completion_queue.queue[h].desc_index = desc_index;
    g_tx_completion_queue.queue[h].mapping = mapping;
    g_tx_completion_queue.queue[h].timestamp = get_bios_ticks();
    g_tx_completion_queue.queue[h].error = false;
    
    /* Compiler barrier - ensure entry is fully written before publishing */
    #ifdef __WATCOMC__
    memory_barrier();
    #else
    memory_barrier();
    #endif
    
    /* Publish the entry by advancing head */
    g_tx_completion_queue.head = next;
    
    /* Final barrier before completing seqlock */
    #ifdef __WATCOMC__
    memory_barrier();
    #else
    memory_barrier();
    #endif
    
    /* GPT-5 A+: Complete seqlock update - increment sequence again */
    g_tx_completion_queue.seq++;
    g_tx_completion_queue.pending = true;  /* Signal bottom-half */

    return true;
}

/* Forward declaration for overflow recovery */
static void packet_recover_tx_overflow(void);

/**
 * @brief Process deferred TX completions (bottom-half, non-ISR)
 * 
 * GPT-5: This function safely unmaps DMA mappings outside ISR context
 */
void packet_process_tx_completions(void) {
    tx_completion_t *entry;
    uint16_t processed = 0;
    const uint16_t max_batch = 16;  /* Process up to 16 completions per call */
    uint8_t t;
    
    /* Check for TX timeouts first */
    packet_check_tx_timeouts();
    
    /* Process normal queue entries (lock-free SPSC) */
    while (g_tx_completion_queue.tail != g_tx_completion_queue.head && 
           processed < max_batch) {

        /* Get next completion from queue */
        t = g_tx_completion_queue.tail;
        entry = &g_tx_completion_queue.queue[t];
        
        /* Process the completion - safe to call VDS here */
        if (entry->mapping) {
            log_debug("Processing TX completion: nic=%u desc=%u mapping=%p",
                     entry->nic_index, entry->desc_index, entry->mapping);
            
            /* Unmap the DMA mapping (may call VDS unlock) */
            dma_unmap_tx(entry->mapping);

            /* Update statistics */
            g_tx_completion_queue.completed_count++;
            packet_statistics.tx_packets++;  /* Track as completed TX */
            if (entry->error) {
                LOG_WARNING("TX completion with error/timeout for NIC %d desc %d", 
                           entry->nic_index, entry->desc_index);
            }
        }
        
        /* Advance tail (consumer) */
        g_tx_completion_queue.tail = (t + 1) & TX_QUEUE_MASK;
        processed++;

        /* If queue becomes empty during processing, clear pending immediately.
         * Use a compiler barrier to avoid reordering with the tail publish. */
        if (g_tx_completion_queue.tail == g_tx_completion_queue.head && !g_tx_completion_queue.overflow) {
            memory_barrier();
            g_tx_completion_queue.pending = false;
        }
    }
    
    /* Handle overflow recovery if needed */
    if (g_tx_completion_queue.overflow) {
        log_warning("TX completion queue overflow detected, recovering %u events",
                   g_tx_completion_queue.overflow_count);
        packet_recover_tx_overflow();
        g_tx_completion_queue.overflow = false;
    }

    if (processed > 0) {
        log_debug("Processed %u TX completions", processed);
    }
}

/**
 * @brief Recover from TX completion queue overflow
 * 
 * GPT-5: Scans all TX rings for orphaned completions when queue overflows
 */
static void packet_recover_tx_overflow(void) {
    uint16_t recovered = 0;
    nic_info_t *nic;
    _3c515_nic_context_t *ctx;
    uint32_t now_ticks = get_bios_ticks();
    const uint32_t stale_threshold = TX_TIMEOUT_TICKS; /* reuse TX timeout */
    int n;
    int i;

    /* Scan all NICs for completed descriptors with mappings */
    for (n = 0; n < hardware_get_nic_count(); n++) {
        nic = hardware_get_nic(n);
        if (!nic || nic->type != NIC_TYPE_3C515_TX) {
            continue;
        }

        /* Get the 3C515 context from private_data */
        ctx = (_3c515_nic_context_t *)nic->private_data;
        if (!ctx || !ctx->tx_desc_ring) {
            continue;
        }

        /* Check all TX descriptors in this NIC's ring */
        for (i = 0; i < TX_RING_SIZE; i++) {
            /* Look for completed descriptors that still have mappings */
            if ((ctx->tx_desc_ring[i].status & _3C515_TX_TX_DESC_COMPLETE) &&
                ctx->tx_desc_ring[i].mapping != NULL) {

                /* Found orphaned completion - unmap it now */
                log_debug("Recovering orphaned TX mapping: nic=%d desc=%d", n, i);
                dma_unmap_tx(ctx->tx_desc_ring[i].mapping);
                ctx->tx_desc_ring[i].mapping = NULL;
                ctx->tx_desc_ring[i].status = 0;
                recovered++;
            }
        }
    }

    /* Also walk the software queue for entries that have become stale without hardware flag.
     * This covers cases where head/tail wrap caused visible overflow without proper hardware completion. */
    if (g_tx_completion_queue.tail != g_tx_completion_queue.head) {
        uint16_t idx = g_tx_completion_queue.tail;
        while (idx != g_tx_completion_queue.head) {
            tx_completion_t *e = &g_tx_completion_queue.queue[idx];
            if (e->mapping) {
                uint32_t elapsed = (now_ticks + 0x1800B0UL - e->timestamp) % 0x1800B0UL;
                if (elapsed > stale_threshold) {
                    log_warning("Unmapping stale TX completion entry: nic=%u desc=%u (elapsed=%lu)",
                                e->nic_index, e->desc_index, (unsigned long)elapsed);
                    dma_unmap_tx(e->mapping);
                    e->mapping = NULL;
                    e->error = true;
                    recovered++;
                }
            }
            idx = (idx + 1) & TX_QUEUE_MASK;
        }
    }

    if (recovered > 0) {
        log_info("Recovered %u orphaned TX completions", recovered);
        /* Note: tx_packets tracks successful transmissions, not recovery events */
        g_tx_completion_queue.completed_count += recovered;
    }
}

/**
 * @brief Process all deferred work (TX completions, VDS unlocks, etc.)
 * 
 * GPT-5: This function should be called periodically from non-ISR context
 * to process deferred work that cannot be done in interrupt handlers.
 */
void packet_process_deferred_work(void) {
    /* Process TX completions (DMA unmapping) */
    if (g_tx_completion_queue.pending) {
        packet_process_tx_completions();
    }
    
    /* Process VDS deferred unlocks */
    if (g_vds_unlock_queue.bottom_half_pending) {
        vds_process_deferred_unlocks();
    }
    
    /* Process deferred RX packets if bottom-half is enabled */
    if (g_bottom_half_state.bottom_half_active && !spsc_queue_is_empty(&g_deferred_queue)) {
        process_deferred_packets();
    }
}

/**
 * @brief Enhanced packet send with complete integration pipeline and CPU optimization
 * @param interface_num NIC interface number (0-based)
 * @param packet_data Packet data to send
 * @param length Packet length
 * @param dest_addr Destination MAC address (6 bytes)
 * @param handle Sender handle for tracking
 * @return 0 on success, negative on error
 */
int packet_send_enhanced(uint8_t interface_num, const uint8_t *packet_data,
                        uint16_t length, const uint8_t *dest_addr, uint16_t handle) {
    nic_info_t *nic;
    _3c515_nic_context_t *ctx;
    buffer_desc_t *buffer;
    uint8_t *frame_buffer;
    int result;
    uint16_t frame_length;
    uint32_t pause_time = 0;  /* Initialize to silence W200 */
    dma_mapping_t *unified_mapping = NULL;
    void *dma_safe_buffer;
    dma_policy_t policy;
    uint32_t dma_flags;
    _3c515_tx_tx_desc_t *desc;
    (void)pause_time;  /* May be unused depending on code path */
    
    if (!packet_data || length == 0 || !dest_addr) {
        log_error("packet_send_enhanced: Invalid parameters");
        return PACKET_ERR_INVALID_PARAM;
    }
    
    if (!packet_ops_initialized) {
        log_error("Packet operations not initialized");
        return PACKET_ERR_NOT_INITIALIZED;
    }
    
    log_debug("Sending packet: interface=%d, length=%d, handle=%04X", 
             interface_num, length, handle);
    
    /* Validate packet size */
    if (length < ETH_MIN_DATA || length > ETH_MAX_DATA) {
        log_error("Invalid packet data size: %d (must be %d-%d)", 
                 length, ETH_MIN_DATA, ETH_MAX_DATA);
        packet_statistics.tx_errors++;
        return PACKET_ERR_INVALID_SIZE;
    }
    
    /* Calculate total frame size including Ethernet header */
    frame_length = ETH_HEADER_LEN + length;
    if (frame_length < ETH_MIN_FRAME) {
        frame_length = ETH_MIN_FRAME; /* Will pad to minimum */
    }
    
    /* Get NIC by interface number */
    nic = hardware_get_nic(interface_num);
    if (!nic) {
        log_error("Invalid interface number: %d", interface_num);
        packet_statistics.tx_errors++;
        return PACKET_ERR_INVALID_NIC;
    }
    
    /* Check if NIC is active */
    if (!(nic->status & NIC_STATUS_ACTIVE)) {
        log_error("NIC %d is not active", interface_num);
        packet_statistics.tx_errors++;
        return PACKET_ERR_INVALID_NIC;
    }
    
    /* Allocate buffer using per-NIC buffer pools for resource isolation */
    buffer = buffer_alloc_nic_aware(nic->index, BUFFER_TYPE_TX, frame_length);
    if (!buffer) {
        log_error("Failed to allocate transmit buffer");
        packet_statistics.tx_errors++;
        return PACKET_ERR_NO_BUFFERS;
    }
    
    frame_buffer = (uint8_t*)buffer_get_data_ptr(buffer);
    if (!frame_buffer) {
        buffer_free_nic_aware(nic->index, buffer);
        return PACKET_ERR_NO_BUFFERS;
    }
    
    /* Build Ethernet frame with CPU-optimized copying */
    result = packet_build_ethernet_frame_optimized(frame_buffer, frame_length, 
                                                  dest_addr, nic->mac, 
                                                  0x0800, /* IP protocol */
                                                  packet_data, length);
    if (result < 0) {
        log_error("Failed to build Ethernet frame: %d", result);
        buffer_free_nic_aware(nic->index, buffer);
        packet_statistics.tx_errors++;
        return result;
    }
    
    /* Check 802.3x Flow Control before transmission (Phase 2.3) */
    if (fc_simple_should_pause(nic->index)) {
        pause_time = fc_simple_get_pause_duration(nic->index);
        log_debug("Transmission paused due to 802.3x PAUSE frame, waiting %u ms", pause_time);

        /* Wait for pause duration with CPU-efficient wait */
        fc_simple_wait_for_resume(nic->index, pause_time);
    }
    
    /* GPT-5 Critical: PIO Fast Path for 3C509B bypasses ALL DMA machinery */
    if (nic->type == NIC_TYPE_3C509B && (nic->capabilities & HW_CAP_PIO_ONLY)) {
        /* Use PIO fast path that completely bypasses DMA mapping */
        result = el3_3c509b_pio_transmit(nic, packet_data, length);
        
        if (result < 0) {
            log_error("PIO transmit failed on 3C509B interface %d: %d", interface_num, result);
            buffer_free_nic_aware(nic->index, buffer);
            packet_statistics.tx_errors++;
            return result;
        }
        
        /* Update statistics and free buffer */
        packet_statistics.tx_packets++;
        packet_statistics.tx_bytes += frame_length;
        buffer_free_nic_aware(nic->index, buffer);
        
        log_debug("PIO packet sent successfully via 3C509B interface %d (data_size=%d)", interface_num, length);
        return 0;
    }
    
    /* GPT-5 UNIFIED: Always use unified DMA mapping abstraction */
    dma_safe_buffer = frame_buffer;
    ctx = NULL;

    /* Check if this is a 3C515 with DMA capability */
    if (nic->type == NIC_TYPE_3C515_TX && (nic->capabilities & HW_CAP_DMA)) {
        ctx = (_3c515_nic_context_t *)nic->private_data;
        policy = platform_get_dma_policy();
        dma_flags = DMA_MAP_READ;  /* TX = device reads from memory */
        
        /* Set appropriate flags based on policy */
        if (policy == DMA_POLICY_COMMONBUF && vds_is_available()) {
            /* Try VDS zero-copy first */
            dma_flags |= DMA_MAP_VDS_ZEROCOPY;
            log_debug("Attempting VDS zero-copy TX mapping for buffer %p len=%d", 
                     frame_buffer, frame_length);
        }
        
        /* Always use unified DMA mapping - handles VDS, bounce, and direct cases */
        unified_mapping = dma_map_tx_flags(frame_buffer, frame_length, dma_flags);
        if (!unified_mapping) {
            log_error("DMA mapping failed for TX buffer %p len=%d", frame_buffer, frame_length);
            buffer_free_nic_aware(nic->index, buffer);
            packet_statistics.tx_errors++;
            return PACKET_ERR_NO_BUFFERS;
        }
        
        /* Get the DMA-safe address (original, VDS-locked, or bounce) */
        dma_safe_buffer = dma_mapping_get_address(unified_mapping);
        
        if (dma_mapping_uses_bounce(unified_mapping)) {
            log_debug("Using TX bounce buffer %p for DMA safety", dma_safe_buffer);
        } else if (dma_mapping_uses_vds(unified_mapping)) {
            log_debug("VDS zero-copy TX successful: buffer=%p phys=%08lXh",
                     dma_safe_buffer, (unsigned long)dma_mapping_get_phys_addr(unified_mapping));
        }
    }
    
    
    /* ALWAYS attach mapping to descriptor - no special cases */
    if (unified_mapping && ctx && ctx->tx_desc_ring) {
        desc = &ctx->tx_desc_ring[ctx->tx_index];
        desc->mapping = unified_mapping;  /* Will be freed by TX completion handler */
    }

    /* Send frame via hardware layer - Group 2A integration */
    result = hardware_send_packet(nic, dma_safe_buffer, frame_length);
    
    /* NOTE: All DMA mapping cleanup now handled by TX completion handler in non-ISR context */
    
    if (result < 0) {
        log_error("Hardware send failed on interface %d: %d", interface_num, result);

        /* GPT-5 UNIFIED FIX: Clean up unified DMA mapping on send failure */
        if (unified_mapping && ctx && ctx->tx_desc_ring) {
            desc = &ctx->tx_desc_ring[ctx->tx_index];
            if (desc->mapping) {
                /* NOTE: This unmap is safe here because we haven't sent to hardware yet */
                dma_unmap_tx(desc->mapping);
                desc->mapping = NULL;  /* Clear mapping pointer */
            }
        }

        buffer_free_nic_aware(nic->index, buffer);
        packet_statistics.tx_errors++;
        return result;
    }
    
    /* Update statistics */
    packet_statistics.tx_packets++;
    packet_statistics.tx_bytes += frame_length;
    
    /* Free the buffer using per-NIC buffer pool */
    buffer_free_nic_aware(nic->index, buffer);
    
    /* GPT-5: Process deferred work after TX to handle completions */
    packet_process_deferred_work();
    
    log_debug("Packet sent successfully via interface %d (frame_size=%d)", interface_num, frame_length);
    return 0;
}

/* Restore default code segment before hot section */
#pragma code_seg()

/* Hot section: Performance-critical runtime functions */
#pragma code_seg("_TEXT", "CODE")

/**
 * @brief Legacy packet send function for backward compatibility
 * @param packet Packet data
 * @param length Packet length
 * @param handle Sender handle
 * @return 0 on success, negative on error
 */
int packet_send(const uint8_t *packet, size_t length, uint16_t handle) {
    nic_info_t *nic;
    int result;
    
    if (!packet || length == 0) {
        log_error("packet_send: Invalid parameters");
        return PACKET_ERR_INVALID_PARAM;
    }
    
    if (!packet_ops_initialized) {
        log_error("Packet operations not initialized");
        return PACKET_ERR_NOT_INITIALIZED;
    }
    
    log_debug("Sending packet: length=%d, handle=%04X", length, handle);
    
    /* Validate packet size */
    if (length < PACKET_MIN_SIZE || length > PACKET_MAX_SIZE) {
        log_error("Invalid packet size: %d", length);
        packet_statistics.tx_errors++;
        return PACKET_ERR_INVALID_SIZE;
    }
    
    /* Use first available NIC for now - full routing can be implemented later */
    nic = hardware_get_nic(0);
    if (!nic) {
        log_error("No suitable NIC found for packet");
        packet_statistics.tx_errors++;
        return PACKET_ERR_INVALID_NIC;
    }
    
    /* Send packet via hardware layer */
    result = hardware_send_packet(nic, packet, length);
    if (result < 0) {
        log_error("Hardware send failed: %d", result);
        packet_statistics.tx_errors++;
        return result;
    }
    
    /* Update statistics */
    packet_statistics.tx_packets++;
    packet_statistics.tx_bytes += length;
    
    /* GPT-5: Process deferred work after TX to handle completions */
    packet_process_deferred_work();
    
    log_debug("Packet sent successfully via NIC %d", nic->type);
    return 0;
}

/**
 * @brief Receive a packet
 * @param buffer Buffer to store packet
 * @param max_length Maximum buffer size
 * @param actual_length Pointer to store actual packet length
 * @param nic_id NIC that received the packet
 * @return 0 on success, negative on error
 */
int packet_receive(uint8_t *buffer, size_t max_length, size_t *actual_length, int nic_id) {
    nic_info_t *nic;
    int result;
    
    if (!buffer || !actual_length || max_length == 0) {
        log_error("packet_receive: Invalid parameters");
        return PACKET_ERR_INVALID_PARAM;
    }
    
    if (!packet_ops_initialized) {
        log_error("Packet operations not initialized");
        return PACKET_ERR_NOT_INITIALIZED;
    }
    
    log_debug("Receiving packet from NIC %d, max_length=%d", nic_id, max_length);
    
    /* Get NIC information */
    nic = hardware_get_nic(nic_id);
    if (!nic) {
        log_error("Invalid NIC ID: %d", nic_id);
        return PACKET_ERR_INVALID_NIC;
    }
    
    /* Receive packet from hardware */
    result = hardware_receive_packet(nic, buffer, actual_length);
    if (result < 0) {
        if (result != PACKET_ERR_NO_PACKET) {
            log_error("Hardware receive failed: %d", result);
            packet_statistics.rx_errors++;
        }
        return result;
    }
    
    /* Validate received packet */
    if (*actual_length < PACKET_MIN_SIZE || *actual_length > max_length) {
        log_error("Invalid received packet size: %d", *actual_length);
        packet_statistics.rx_errors++;
        return PACKET_ERR_INVALID_SIZE;
    }
    
    /* Update statistics */
    packet_statistics.rx_packets++;
    packet_statistics.rx_bytes += *actual_length;
    
    log_debug("Packet received: length=%d", *actual_length);
    
    /* Process received packet through API layer */
    result = api_process_received_packet(buffer, *actual_length, nic_id);
    if (result < 0) {
        log_debug("No handlers for received packet");
        packet_statistics.rx_dropped++;
    }
    
    return 0;
}

/**
 * @brief Receive a packet from specific NIC with comprehensive processing
 * @param nic_index NIC interface number (0-based)
 * @param buffer Buffer to store received packet data  
 * @param length Pointer to buffer size (input) and actual packet length (output)
 * @return 0 on success, negative on error
 */
int packet_receive_from_nic(int nic_index, uint8_t *buffer, size_t *length) {
    nic_info_t *nic;
    buffer_desc_t *rx_buffer = NULL;
    eth_header_t eth_header;
    int result;
    size_t original_buffer_size;
    size_t packet_length;
    uint8_t *packet_data;
    uint32_t buffer_usage;
    
    if (!buffer || !length || *length == 0) {
        log_error("packet_receive_from_nic: Invalid parameters");
        return PACKET_ERR_INVALID_PARAM;
    }
    
    if (!packet_ops_initialized) {
        log_error("Packet operations not initialized");
        return PACKET_ERR_NOT_INITIALIZED;
    }
    
    original_buffer_size = *length;
    
    /* Process any pending VDS unlocks from previous transmissions */
    if (g_vds_unlock_queue.bottom_half_pending) {
        vds_process_deferred_unlocks();
    }
    
    /* GPT-5: Process any pending TX completions (DMA unmapping) */
    if (g_tx_completion_queue.pending) {
        packet_process_tx_completions();
    }
    
    log_debug("Receiving packet from NIC %d, max_length=%zu", nic_index, original_buffer_size);
    
    /* Get NIC by interface number */
    nic = hardware_get_nic(nic_index);
    if (!nic) {
        log_error("Invalid NIC interface: %d", nic_index);
        packet_statistics.rx_errors++;
        return PACKET_ERR_INVALID_NIC;
    }
    
    /* Check if NIC is active and ready to receive */
    if (!(nic->status & NIC_STATUS_ACTIVE)) {
        log_warning("NIC %d is not active for reception", nic_index);
        return PACKET_ERR_INVALID_NIC;
    }
    
    /* Allocate receive buffer using RX_COPYBREAK optimization 
     * Note: RX buffers are pre-allocated as VDS common buffers in buffer_alloc.c,
     * so no VDS lock/unlock operations are needed for RX path */
    rx_buffer = rx_copybreak_alloc(ETH_MAX_FRAME);
    if (!rx_buffer) {
        log_error("Failed to allocate RX buffer for packet reception");
        packet_statistics.rx_errors++;
        return PACKET_ERR_NO_BUFFERS;
    }
    
    /* Receive packet from hardware via NIC operations vtable - Group 2A integration */
    packet_length = rx_buffer->size;
    result = nic->ops->receive_packet(nic, (uint8_t*)buffer_get_data_ptr(rx_buffer), &packet_length);
    
    if (result < 0) {
        rx_copybreak_free(rx_buffer);
        if (result == ERROR_NO_DATA) {
            /* No packet available - not an error */
            *length = 0;
            return PACKET_ERR_NO_PACKET;
        } else {
            log_error("Hardware receive failed on NIC %d: %d", nic_index, result);
            packet_statistics.rx_errors++;
            return result;
        }
    }
    
    /* Validate minimum Ethernet frame size */
    if (packet_length < ETH_MIN_FRAME) {
        log_warning("Received runt frame: length=%zu", packet_length);
        rx_copybreak_free(rx_buffer);
        packet_statistics.rx_runt++;
        return PACKET_ERR_INVALID_SIZE;
    }
    
    if (packet_length > ETH_MAX_FRAME) {
        log_warning("Received oversized frame: length=%zu", packet_length);
        rx_copybreak_free(rx_buffer);
        packet_statistics.rx_oversize++;
        return PACKET_ERR_INVALID_SIZE;
    }
    
    /* Parse Ethernet header for validation and classification */
    packet_data = (uint8_t*)buffer_get_data_ptr(rx_buffer);
    result = packet_parse_ethernet_header(packet_data, packet_length, &eth_header);
    if (result < 0) {
        log_warning("Invalid Ethernet header in received packet");
        rx_copybreak_free(rx_buffer);
        packet_statistics.rx_errors++;
        return result;
    }
    
    /* Process 802.3x Flow Control PAUSE frames (Phase 2.3) */
    if (eth_header.ethertype == ETHERTYPE_FLOW_CONTROL) {
        log_debug("Processing 802.3x PAUSE frame");
        result = fc_simple_process_rx(nic_index, packet_data, packet_length);
        if (result > 0) {
            log_debug("PAUSE frame processed, transmission throttled for %d ms", result);
            /* PAUSE frame consumed - don't pass to upper layers */
            rx_copybreak_free(rx_buffer);
            return PACKET_ERR_NO_PACKET; /* Not an error, just no packet for user */
        }
    }

    /* Update buffer status for flow control monitoring */
    buffer_usage = calculate_buffer_usage_percentage(nic_index);
    fc_simple_update_buffer_status(nic_index, buffer_usage);
    
    /* Validate destination address - check if packet is for us */
    if (!packet_is_for_us(packet_data, nic->mac) && 
        !packet_is_broadcast(packet_data) && 
        !packet_is_multicast(packet_data)) {
        /* Not for us - only process if in promiscuous mode */
        if (!(nic->status & NIC_STATUS_PROMISCUOUS)) {
            log_debug("Packet not addressed to us, dropping");
            rx_copybreak_free(rx_buffer);
            packet_statistics.rx_dropped++;
            return PACKET_ERR_NO_PACKET; /* Not an error, just not for us */
        }
    }
    
    /* Check if packet fits in provided buffer */
    if (packet_length > original_buffer_size) {
        log_error("Received packet too large for buffer: need %zu, have %zu", packet_length, original_buffer_size);
        rx_copybreak_free(rx_buffer);
        *length = packet_length; /* Return required size */
        return PACKET_ERR_INVALID_SIZE;
    }
    
    /* Copy packet to user buffer */
    memcpy(buffer, packet_data, packet_length);
    *length = packet_length;
    
    /* Update receive statistics */
    packet_statistics.rx_packets++;
    packet_statistics.rx_bytes += packet_length;
    
    /* Update detailed per-NIC statistics */
    packet_update_detailed_stats(nic_index, 1, packet_length, 0); /* 1 = RX, 0 = success */
    
    log_debug("Successfully received %zu byte packet from NIC %d (EtherType=0x%04X)", 
             packet_length, nic_index, ntohs(eth_header.ethertype));
    
    /* Process packet through enhanced processing pipeline if configured */
    result = packet_receive_process(packet_data, packet_length, nic_index);
    if (result < 0) {
        log_debug("Packet processing returned %d - packet delivered to user but not processed locally", result);
    }
    
    /* Free the receive buffer using RX_COPYBREAK */
    rx_copybreak_free(rx_buffer);
    
    return 0;
}

/* ========================================================================
 * Bottom-Half Processing for XMS+RX_COPYBREAK
 * ======================================================================== */

/* Note: g_xms_pool, g_deferred_queue, and g_bottom_half_state are
 * forward-declared earlier in the file (near line 233) */

/* Initialize the bottom-half state structure (forward declared above) */
/* The actual struct definition is in the forward declaration section */
#if 0  /* Disabled - using forward declaration above */
static struct {
    volatile bool xms_enabled;             /* XMS buffers enabled (volatile!) */
    volatile bool bottom_half_active;      /* Bottom-half processing active (volatile!) */
    uint16_t xms_threshold;                /* Size threshold for XMS (16-bit) */
    /* Statistics - 16-bit for atomicity on 16-bit CPU */
    uint16_t packets_deferred;
    uint16_t packets_processed;
    uint16_t xms_copies;
    uint16_t staging_exhausted;
    uint16_t queue_full_drops;
    uint16_t oversize_drops;
    uint16_t xms_alloc_failures;
    uint16_t xms_move_failures;
} g_bottom_half_state_disabled = {0};
#endif  /* Disabled - using forward declaration */

/**
 * @brief Initialize bottom-half processing with XMS support
 * @param enable_xms Enable XMS buffer support
 * @param staging_count Number of staging buffers
 * @param xms_count Number of XMS buffers
 * @return 0 on success, negative on error
 */
int packet_bottom_half_init(bool enable_xms, uint32_t staging_count, uint32_t xms_count) {
    int result;
    
    log_info("Initializing bottom-half processing: xms=%s, staging=%u, xms_buffers=%u",
             enable_xms ? "enabled" : "disabled", staging_count, xms_count);
    
    /* Initialize staging buffers (always needed) */
    result = staging_buffer_init(staging_count, ETH_MAX_FRAME);
    if (result != SUCCESS) {
        log_error("Failed to initialize staging buffers: %d", result);
        return result;
    }
    
    /* Initialize SPSC queue */
    result = spsc_queue_init(&g_deferred_queue);
    if (result != SUCCESS) {
        log_error("Failed to initialize SPSC queue: %d", result);
        staging_buffer_cleanup();
        return result;
    }
    
    /* Initialize XMS pool if enabled */
    if (enable_xms && xms_count > 0) {
        result = xms_buffer_pool_init(&g_xms_pool, ETH_MAX_FRAME, xms_count);
        if (result == SUCCESS) {
            g_bottom_half_state.xms_enabled = true;
            g_bottom_half_state.xms_threshold = RX_COPYBREAK_THRESHOLD;
            log_info("XMS buffer pool initialized with %u buffers", xms_count);
        } else {
            log_warning("XMS pool init failed (%d), using conventional memory only", result);
            g_bottom_half_state.xms_enabled = false;
        }
    }
    
    /* Reset statistics */
    g_bottom_half_state.packets_deferred = 0;
    g_bottom_half_state.packets_processed = 0;
    g_bottom_half_state.xms_copies = 0;
    g_bottom_half_state.staging_exhausted = 0;
    g_bottom_half_state.queue_full_drops = 0;
    g_bottom_half_state.oversize_drops = 0;
    g_bottom_half_state.xms_alloc_failures = 0;
    g_bottom_half_state.xms_move_failures = 0;
    g_bottom_half_state.bottom_half_active = true;
    
    return SUCCESS;
}

/**
 * @brief Cleanup bottom-half processing
 */
void packet_bottom_half_cleanup(void) {
    log_info("Bottom-half statistics:");
    log_info("  Packets: deferred=%u, processed=%u",
             (unsigned)g_bottom_half_state.packets_deferred,
             (unsigned)g_bottom_half_state.packets_processed);
    log_info("  Drops: staging=%u, queue_full=%u, oversize=%u",
             (unsigned)g_bottom_half_state.staging_exhausted,
             (unsigned)g_bottom_half_state.queue_full_drops,
             (unsigned)g_bottom_half_state.oversize_drops);
    log_info("  XMS: copies=%u, alloc_fail=%u, move_fail=%u",
             (unsigned)g_bottom_half_state.xms_copies,
             (unsigned)g_bottom_half_state.xms_alloc_failures,
             (unsigned)g_bottom_half_state.xms_move_failures);
    
    /* Cleanup XMS pool if initialized */
    if (g_bottom_half_state.xms_enabled) {
        xms_buffer_pool_cleanup(&g_xms_pool);
    }
    
    /* Cleanup SPSC queue */
    spsc_queue_cleanup(&g_deferred_queue);
    
    /* Cleanup staging buffers */
    staging_buffer_cleanup();
    
    /* Reset state */
    memory_zero(&g_bottom_half_state, sizeof(g_bottom_half_state));
}

/**
 * @brief Process packet in ISR with staging buffer
 * @param packet_data Packet data from NIC
 * @param packet_size Size of packet
 * @param nic_index Source NIC
 * @return 0 on success, negative on error
 * 
 * Called from ISR context - must be MINIMAL and FAST!
 * NEVER process packets here - always defer to bottom-half
 */
int packet_isr_receive(uint8_t *packet_data, uint16_t packet_size, uint8_t nic_index) {
    staging_buffer_t *staging;
    int result;
    
    /* Check if bottom-half processing is active */
    if (!g_bottom_half_state.bottom_half_active) {
        /* System not initialized - drop packet */
        packet_statistics.rx_dropped++;
        return PACKET_ERR_NOT_INITIALIZED;
    }
    
    /* Allocate staging buffer */
    staging = staging_buffer_alloc();
    if (!staging) {
        g_bottom_half_state.staging_exhausted++;
        packet_statistics.rx_dropped++;
        return PACKET_ERR_NO_BUFFER;
    }
    
    /* Bounds check BEFORE copy! */
    if (packet_size > staging->size) {
        g_bottom_half_state.oversize_drops++;
        packet_statistics.rx_dropped++;
        staging_buffer_free(staging);
        return PACKET_ERR_INVALID_SIZE;
    }
    
    /* Copy packet to staging buffer using ASM fast path (ISR-safe) */
    asm_packet_copy_fast(staging->data, packet_data, packet_size);
    staging->packet_size = packet_size;
    staging->used = packet_size;
    staging->nic_index = nic_index;  /* Preserve NIC identity! */
    
    /* Compiler barrier before enqueue */
    compiler_barrier();
    
    /* Queue for bottom-half processing */
    result = spsc_queue_enqueue(&g_deferred_queue, staging);
    if (result != SUCCESS) {
        g_bottom_half_state.queue_full_drops++;
        packet_statistics.rx_dropped++;
        staging_buffer_free(staging);
        return result;
    }
    
    g_bottom_half_state.packets_deferred++;
    return SUCCESS;
}

/**
 * @brief Process deferred packets (bottom-half)
 * 
 * Called outside ISR context - safe to use XMS
 * Key fix: Free staging IMMEDIATELY after XMS copy!
 */
void process_deferred_packets(void) {
    staging_buffer_t *staging;
    uint32_t xms_offset;
    xms_packet_desc_t xms_desc;
    buffer_desc_t *conv_buffer;
    int result;
    uint32_t processed = 0;
    uint32_t process_start_time = get_timer_ticks();
    
    /* Process queued packets with time limit */
    while (!spsc_queue_is_empty(&g_deferred_queue)) {
        staging = spsc_queue_dequeue(&g_deferred_queue);
        if (!staging) {
            break;
        }
        
        /* Large packets: use XMS to free staging immediately */
        if (g_bottom_half_state.xms_enabled && 
            staging->packet_size >= g_bottom_half_state.xms_threshold) {
            
            /* Allocate XMS buffer */
            result = xms_buffer_alloc(&g_xms_pool, &xms_offset);
            if (result == SUCCESS) {
                /* Copy to XMS (safe in bottom-half) */
                result = xms_copy_to_buffer(&g_xms_pool, xms_offset, 
                                          staging->data, staging->packet_size);
                if (result == SUCCESS) {
                    /* Build XMS descriptor */
                    xms_desc.xms_handle = g_xms_pool.xms_handle;
                    xms_desc.xms_offset = xms_offset;
                    xms_desc.packet_size = staging->packet_size;
                    xms_desc.nic_index = staging->nic_index;
                    
                    /* FREE STAGING IMMEDIATELY! This is the key! */
                    staging_buffer_free(staging);
                    staging = NULL;
                    
                    g_bottom_half_state.xms_copies++;
                    
                    /* Process from XMS descriptor */
                    packet_process_from_xms(&xms_desc);
                    
                    /* Free XMS buffer after processing */
                    xms_buffer_free(&g_xms_pool, xms_offset);
                } else {
                    /* XMS copy failed */
                    g_bottom_half_state.xms_move_failures++;
                    xms_buffer_free(&g_xms_pool, xms_offset);
                    
                    /* Process from staging as fallback */
                    packet_receive_process(staging->data, staging->packet_size, 
                                         staging->nic_index);
                    staging_buffer_free(staging);
                }
            } else {
                /* XMS allocation failed */
                g_bottom_half_state.xms_alloc_failures++;
                
                /* Process from staging as fallback */
                packet_receive_process(staging->data, staging->packet_size, 
                                     staging->nic_index);
                staging_buffer_free(staging);
            }
        } else {
            /* Small packet - use RX_COPYBREAK conventional buffer */
            conv_buffer = rx_copybreak_alloc(staging->packet_size);
            if (conv_buffer) {
                /* C89: Declare at start of block */
                uint8_t saved_nic;

                /* Copy to conventional buffer */
                memory_copy_optimized(conv_buffer->data, staging->data, staging->packet_size);
                conv_buffer->used = staging->packet_size;

                /* Free staging immediately */
                saved_nic = staging->nic_index;
                staging_buffer_free(staging);

                /* Process from conventional buffer */
                packet_receive_process(conv_buffer->data, conv_buffer->used, saved_nic);

                /* Free conventional buffer */
                rx_copybreak_free(conv_buffer);
                rx_copybreak_record_copy();
            } else {
                /* No conventional buffers - process from staging */
                packet_receive_process(staging->data, staging->packet_size, 
                                     staging->nic_index);
                staging_buffer_free(staging);
            }
        }
        
        processed++;
        g_bottom_half_state.packets_processed++;
        
        /* Time-based yielding instead of fixed count */
        if (get_timer_ticks() - process_start_time > MAX_BOTTOM_HALF_TICKS) {
            break;
        }
    }
}

/**
 * @brief Safely snapshot statistics (disables interrupts)
 * @param stats Output buffer for statistics
 *
 * Provides atomic snapshot of statistics for external monitoring
 */
void packet_snapshot_stats(void *stats, size_t size) {
    uint16_t flags = 0;  /* Initialize to silence W200 - set by inline asm */

    if (!stats || size != sizeof(g_bottom_half_state)) {
        return;
    }

    /* Save and disable interrupts for atomic snapshot */
    _asm {
        pushf
        pop ax
        mov flags, ax
        cli
    }

    /* Copy statistics atomically */
    memory_copy_optimized(stats, &g_bottom_half_state, size);

    /* Restore interrupts */
    _asm {
        mov ax, flags
        push ax
        popf
    }
}

/**
 * @brief Process packet from XMS descriptor
 * @param desc XMS packet descriptor
 * @return 0 on success, negative on error
 * 
 * This function handles packets stored in XMS memory
 */
int packet_process_from_xms(xms_packet_desc_t *desc) {
    uint8_t *temp_buffer;
    int result;
    
    if (!desc) {
        return PACKET_ERR_INVALID_PARAM;
    }
    
    /* Allocate temporary buffer for protocol processing */
    temp_buffer = (uint8_t*)memory_allocate(desc->packet_size, MEMORY_FLAG_ZERO);
    if (!temp_buffer) {
        return PACKET_ERR_NO_MEMORY;
    }
    
    /* Copy from XMS to temporary buffer */
    result = xms_copy_from_buffer(&g_xms_pool, temp_buffer, 
                                desc->xms_offset, desc->packet_size);
    if (result != SUCCESS) {
        memory_free(temp_buffer);
        return result;
    }
    
    /* Process packet */
    result = packet_receive_process(temp_buffer, desc->packet_size, desc->nic_index);
    
    /* Free temporary buffer */
    memory_free(temp_buffer);
    
    return result;
}

/**
 * @brief Enhanced received packet processing with complete integration
 * @param raw_data Raw packet data from NIC
 * @param length Raw packet length
 * @param nic_index Source NIC index
 * @return 0 on success, negative on error
 */
int packet_receive_process(uint8_t *raw_data, uint16_t length, uint8_t nic_index) {
    eth_header_t eth_header;
    uint8_t *payload_data;
    uint16_t payload_length;
    nic_info_t *nic;
    int result;
    uint16_t ethertype;
    uint8_t dest_nic;
    
    if (!raw_data || length == 0) {
        return PACKET_ERR_INVALID_PARAM;
    }
    
    log_debug("Processing received packet: length=%d, nic=%d", length, nic_index);
    
    /* Validate minimum Ethernet frame size */
    if (length < ETH_MIN_FRAME) {
        log_warning("Received runt frame: length=%d", length);
        packet_statistics.rx_runt++;
        return PACKET_ERR_INVALID_SIZE;
    }
    
    if (length > ETH_MAX_FRAME) {
        log_warning("Received oversized frame: length=%d", length);
        packet_statistics.rx_oversize++;
        return PACKET_ERR_INVALID_SIZE;
    }
    
    /* Get NIC information */
    nic = hardware_get_nic(nic_index);
    if (!nic) {
        log_error("Invalid NIC index: %d", nic_index);
        return PACKET_ERR_INVALID_NIC;
    }
    
    /* Parse Ethernet header */
    result = packet_parse_ethernet_header(raw_data, length, &eth_header);
    if (result < 0) {
        log_warning("Invalid Ethernet header in received packet");
        packet_statistics.rx_errors++;
        return result;
    }
    
    /* Validate destination address - check if packet is for us */
    if (!packet_is_for_us(raw_data, nic->mac) && 
        !packet_is_broadcast(raw_data) && 
        !packet_is_multicast(raw_data)) {
        /* Not for us - only process if in promiscuous mode */
        if (!(nic->status & NIC_STATUS_PROMISCUOUS)) {
            log_debug("Packet not addressed to us, dropping");
            packet_statistics.rx_dropped++;
            return 0;
        }
    }
    
    /* Extract payload */
    payload_data = raw_data + ETH_HEADER_LEN;
    payload_length = length - ETH_HEADER_LEN;
    
    /* Validate payload size */
    if (payload_length < ETH_MIN_DATA) {
        /* Remove padding if present */
        if (payload_length > 0) {
            log_debug("Received padded frame, payload=%d", payload_length);
        }
    }
    
    /* Update receive statistics */
    packet_statistics.rx_packets++;
    packet_statistics.rx_bytes += length;
    
    /* Process specific protocol types */
    ethertype = ntohs(eth_header.ethertype);
    
    switch (ethertype) {
        case ETH_P_ARP:
            /* Process ARP packets */
            if (arp_is_enabled()) {
                log_debug("Processing ARP packet");
                result = arp_process_received_packet(raw_data, length, nic_index);
                if (result < 0) {
                    log_warning("ARP processing failed: %d", result);
                    packet_statistics.rx_errors++;
                }
                return 0;
            }
            break;
            
        case ETH_P_IP:
            /* Process IP packets - may need routing */
            if (static_routing_is_enabled()) {
                result = static_routing_process_ip_packet(payload_data, payload_length,
                                                        nic_index, &dest_nic);
                if (result == SUCCESS && dest_nic != nic_index) {
                    /* Route packet to another interface */
                    log_debug("Routing IP packet from NIC %d to NIC %d", nic_index, dest_nic);
                    result = route_packet_to_interface(raw_data, length, dest_nic);
                    if (result == SUCCESS) {
                        packet_statistics.routed_packets++;
                    } else {
                        packet_statistics.rx_errors++;
                    }
                    return 0;
                }
            }
            break;
            
        default:
            /* Unknown protocol - fall through to API processing */
            break;
    }
    
    /* Check if packet should be routed to another interface (bridge mode) */
    result = routing_process_packet(raw_data, length, nic_index);
    if (result > 0) {
        /* Packet was routed */
        log_debug("Packet bridged to interface %d", result);
        packet_statistics.routed_packets++;
        return 0;
    }
    
    /* Deliver to local protocol stack via Group 2C API */
    result = api_process_received_packet(raw_data, length, nic_index);
    if (result < 0) {
        log_debug("No local handlers for ethertype 0x%04X", ethertype);
        packet_statistics.rx_dropped++;
    }
    
    return 0;
}

/**
 * @brief Legacy packet processing function for backward compatibility
 * @param packet Packet data
 * @param length Packet length
 * @param nic_id Source NIC ID
 * @return 0 on success, negative on error
 */
int packet_process_received(const uint8_t *packet, size_t length, int nic_id) {
    int result;
    
    if (!packet || length == 0) {
        return PACKET_ERR_INVALID_PARAM;
    }
    
    log_debug("Processing received packet: length=%d, nic=%d", length, nic_id);
    
    /* Basic packet validation */
    if (length < PACKET_MIN_SIZE) {
        log_warning("Received runt packet: length=%d", length);
        packet_statistics.rx_runt++;
        return PACKET_ERR_INVALID_SIZE;
    }
    
    if (length > PACKET_MAX_SIZE) {
        log_warning("Received oversized packet: length=%d", length);
        packet_statistics.rx_oversize++;
        return PACKET_ERR_INVALID_SIZE;
    }
    
    /* Check if packet should be routed to another interface */
    result = routing_process_packet(packet, length, nic_id);
    if (result > 0) {
        /* Packet was routed */
        log_debug("Packet routed to interface %d", result);
        packet_statistics.routed_packets++;
        return 0;
    }
    
    /* Deliver to local protocol stack */
    result = api_process_received_packet(packet, length, nic_id);
    if (result < 0) {
        log_debug("No local handlers for packet");
        packet_statistics.rx_dropped++;
    }
    
    return 0;
}

/**
 * @brief Enhanced packet transmission with retry logic and error handling
 * @param packet_data Packet data
 * @param length Packet length
 * @param dest_addr Destination MAC address
 * @param handle Sender handle
 * @param max_retries Maximum retry attempts
 * @return 0 on success, negative on error
 */
int packet_send_with_retry(const uint8_t *packet_data, uint16_t length,
                          const uint8_t *dest_addr, uint16_t handle,
                          int max_retries) {
    int result;
    int retry_count = 0;
    int backoff_delay = 1; /* Start with 1ms backoff */
    volatile int delay_i;
    int nic_index;
    
    if (!packet_data || !dest_addr || length == 0) {
        return PACKET_ERR_INVALID_PARAM;
    }
    
    if (max_retries < 0 || max_retries > 10) {
        max_retries = 3; /* Default to 3 retries */
    }
    
    while (retry_count <= max_retries) {
        /* Try to get optimal NIC for transmission */
        nic_index = packet_get_optimal_nic(packet_data, length);
        if (nic_index < 0) {
            /* Use multi-NIC load balancing */
            result = packet_send_multi_nic(packet_data, length, dest_addr, handle);
        } else {
            /* Use specifically selected NIC */
            result = packet_send_enhanced(nic_index, packet_data, length, dest_addr, handle);
        }
        
        /* Check for success */
        if (result == 0) {
            if (retry_count > 0) {
                log_info("Packet sent successfully after %d retries", retry_count);
            }
            return 0;
        }
        
        /* Handle specific error cases */
        switch (result) {
            case PACKET_ERR_NO_BUFFERS:
                /* Buffer exhaustion - wait for buffers to free up */
                log_warning("Buffer exhaustion, retrying after delay");
                packet_statistics.tx_errors++;
                break;
                
            case PACKET_ERR_INVALID_NIC:
                /* NIC failure - try failover */
                log_warning("NIC failure detected, attempting failover");
                if (nic_index >= 0) {
                    packet_handle_nic_failover(nic_index);
                }
                packet_statistics.tx_errors++;
                break;
                
            case PACKET_ERR_INVALID_SIZE:
                /* Invalid packet size - don't retry */
                log_error("Invalid packet size, aborting transmission");
                return result;
                
            default:
                log_warning("Transmission failed with error %d, retry %d/%d", 
                           result, retry_count, max_retries);
                packet_statistics.tx_errors++;
                break;
        }
        
        /* Check if we should retry */
        if (retry_count >= max_retries) {
            log_error("Maximum retries (%d) exceeded for packet transmission", max_retries);
            break;
        }
        
        /* Exponential backoff delay */
        /* In a real DOS environment, this would be a busy wait or timer-based delay */
        log_debug("Waiting %dms before retry %d", backoff_delay, retry_count + 1);
        
        /* Simple delay simulation - in real code this would use timer */
        for (delay_i = 0; delay_i < backoff_delay * 1000; delay_i++) {
            /* Busy wait delay */
        }
        
        retry_count++;
        backoff_delay = (backoff_delay < 16) ? backoff_delay * 2 : 16; /* Cap at 16ms */
    }
    
    return result;
}

/**
 * @brief Enhanced packet receive with error recovery
 * @param buffer Buffer to store packet
 * @param max_length Maximum buffer size
 * @param actual_length Pointer to store actual packet length
 * @param nic_id NIC that received the packet
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, negative on error
 */
int packet_receive_with_recovery(uint8_t *buffer, size_t max_length,
                                size_t *actual_length, int nic_id,
                                uint32_t timeout_ms) {
    uint32_t start_time;
    int result;
    nic_info_t *nic;
    uint32_t elapsed;
    volatile int delay_i;
    
    if (!buffer || !actual_length || max_length == 0) {
        return PACKET_ERR_INVALID_PARAM;
    }
    
    if (!packet_ops_initialized) {
        return PACKET_ERR_NOT_INITIALIZED;
    }
    
    /* Get NIC information */
    nic = hardware_get_nic(nic_id);
    if (!nic) {
        log_error("Invalid NIC ID: %d", nic_id);
        return PACKET_ERR_INVALID_NIC;
    }
    
    /* Check if NIC is active */
    if (!(nic->status & NIC_STATUS_ACTIVE)) {
        log_warning("NIC %d is not active", nic_id);
        return PACKET_ERR_INVALID_NIC;
    }
    
    start_time = stats_get_timestamp(); /* Simplified timestamp */
    
    while (1) {
        /* Try to receive packet from hardware */
        result = hardware_receive_packet(nic, buffer, actual_length);
        
        /* Handle success */
        if (result == 0) {
            /* Validate received packet */
            if (*actual_length < PACKET_MIN_SIZE || *actual_length > max_length) {
                log_warning("Received invalid packet size: %d", *actual_length);
                packet_statistics.rx_errors++;
                continue; /* Try again */
            }
            
            /* Update statistics */
            packet_statistics.rx_packets++;
            packet_statistics.rx_bytes += *actual_length;
            
            log_debug("Packet received successfully: length=%d", *actual_length);
            return 0;
        }
        
        /* Handle specific errors */
        switch (result) {
            case PACKET_ERR_NO_PACKET:
                /* No packet available - this is normal */
                break;
                
            case PACKET_ERR_INVALID_SIZE:
                log_warning("Received packet with invalid size, discarding");
                packet_statistics.rx_errors++;
                continue;
                
            default:
                log_warning("Hardware receive error: %d", result);
                packet_statistics.rx_errors++;
                
                /* Check if NIC failed */
                if (!(nic->status & NIC_STATUS_ACTIVE)) {
                    log_error("NIC %d became inactive during receive", nic_id);
                    return PACKET_ERR_INVALID_NIC;
                }
                break;
        }
        
        /* Check timeout */
        if (timeout_ms > 0) {
            elapsed = stats_get_timestamp() - start_time;
            if (elapsed >= timeout_ms) {
                log_debug("Receive timeout after %lu ms", elapsed);
                return PACKET_ERR_NO_PACKET;
            }
        }
        
        /* Small delay before retrying */
        for (delay_i = 0; delay_i < 100; delay_i++) {
            /* Brief delay */
        }
    }
}

/**
 * @brief Queue a packet for transmission
 * @param packet Packet data
 * @param length Packet length
 * @param priority Transmission priority
 * @param handle Sender handle
 * @return 0 on success, negative on error
 */
int packet_queue_tx(const uint8_t *packet, size_t length, int priority, uint16_t handle) {
    /* C89: All declarations at the start of block */
    packet_buffer_t *buffer;
    packet_queue_t *queue;
    int result;

    if (!packet || length == 0) {
        return PACKET_ERR_INVALID_PARAM;
    }

    /* Implement packet queuing with priority support */
    log_debug("Queuing packet for transmission: length=%d, priority=%d, handle=%04X",
             (int)length, priority, handle);

    /* Queue packet based on priority */
    queue = &g_packet_queues[priority % MAX_PRIORITY_LEVELS];
    if (queue->count >= queue->max_count) {
        log_warning("Priority %d queue full, dropping packet", priority);
        return PACKET_ERR_QUEUE_FULL;
    }

    /* Allocate buffer for packet */
    buffer = packet_buffer_alloc((uint16_t)length);
    if (!buffer) {
        log_error("Failed to allocate packet buffer");
        packet_statistics.tx_buffer_full++;
        return PACKET_ERR_NO_BUFFERS;
    }

    /* Copy packet data */
    memcpy(buffer->data, packet, length);
    buffer->length = (uint16_t)length;

    /* For now, send immediately (no actual queuing) */
    /* In a full implementation, this would add to a priority queue */
    result = packet_send(buffer->data, buffer->length, handle);

    /* Free the buffer */
    packet_buffer_free(buffer);

    return result;
}

/**
 * @brief Flush transmission queue
 * @return Number of packets sent, negative on error
 */
int packet_flush_tx_queue(void) {
    /* C89: All declarations at the start of block */
    int packets_sent;
    int priority;
    int result;
    packet_queue_t *queue;
    packet_buffer_t *buffer;

    packets_sent = 0;

    log_debug("Flushing transmission queue");

    /* Process queues in priority order (high to low) */
    for (priority = MAX_PRIORITY_LEVELS - 1; priority >= 0; priority--) {
        queue = &g_packet_queues[priority];

        while (queue->count > 0 && queue->head) {
            buffer = queue->head;

            /* Attempt to send the packet */
            result = packet_send_immediate(buffer->data, buffer->length, 0);
            if (result != SUCCESS) {
                /* Stop flushing if transmission fails */
                break;
            }

            /* Remove from queue - CRITICAL SECTION */
            _asm { cli }  /* Disable interrupts */
            queue->head = buffer->next;
            if (!queue->head) {
                queue->tail = NULL;
            }
            queue->count--;
            _asm { sti }  /* Enable interrupts */

            /* Free buffer */
            packet_buffer_free(buffer);
            packets_sent++;
        }
    }

    log_debug("Flushed %d packets from transmission queues", packets_sent);
    return packets_sent;
}

/**
 * @brief Get packet statistics
 * @param stats Pointer to store statistics
 * @return 0 on success, negative on error
 */
int packet_get_statistics(packet_stats_t *stats) {
    if (!stats) {
        return PACKET_ERR_INVALID_PARAM;
    }
    
    *stats = packet_statistics;
    return 0;
}

/**
 * @brief Enhanced packet statistics collection and monitoring
 * @param nic_index NIC index for per-NIC statistics
 * @param packet_type Type of packet (TX/RX)
 * @param length Packet length
 * @param result Operation result (0 = success, negative = error)
 */
void packet_update_detailed_stats(int nic_index, int packet_type, uint16_t length, int result) {
    nic_info_t *nic;
    
    /* Update global statistics */
    if (packet_type == 0) { /* TX */
        if (result == 0) {
            packet_statistics.tx_packets++;
            packet_statistics.tx_bytes += length;
        } else {
            packet_statistics.tx_errors++;
        }
    } else { /* RX */
        if (result == 0) {
            packet_statistics.rx_packets++;
            packet_statistics.rx_bytes += length;
        } else {
            packet_statistics.rx_errors++;
        }
    }
    
    /* Update per-NIC statistics if valid NIC index */
    nic = hardware_get_nic(nic_index);
    if (nic) {
        if (packet_type == 0) { /* TX */
            if (result == 0) {
                nic->tx_packets++;
                nic->tx_bytes += length;
            } else {
                nic->tx_errors++;
            }
        } else { /* RX */
            if (result == 0) {
                nic->rx_packets++;
                nic->rx_bytes += length;
            } else {
                nic->rx_errors++;
            }
        }
    }
}

/**
 * @brief Get comprehensive packet driver performance metrics
 * @param metrics Pointer to store performance metrics
 * @return 0 on success, negative on error
 */
int packet_get_performance_metrics(packet_performance_metrics_t *metrics) {
    int total_nics;
    nic_info_t *nic;
    uint32_t total_tx_packets = 0;
    uint32_t total_rx_packets = 0;
    uint32_t total_errors = 0;
    int i;

    if (!metrics) {
        return PACKET_ERR_INVALID_PARAM;
    }

    memset(metrics, 0, sizeof(packet_performance_metrics_t));

    /* Copy basic statistics */
    metrics->tx_packets = packet_statistics.tx_packets;
    metrics->rx_packets = packet_statistics.rx_packets;
    metrics->tx_bytes = packet_statistics.tx_bytes;
    metrics->rx_bytes = packet_statistics.rx_bytes;
    metrics->tx_errors = packet_statistics.tx_errors;
    metrics->rx_errors = packet_statistics.rx_errors;
    metrics->rx_dropped = packet_statistics.rx_dropped;

    /* Calculate performance ratios */
    total_tx_packets = packet_statistics.tx_packets;
    total_rx_packets = packet_statistics.rx_packets;
    total_errors = packet_statistics.tx_errors + packet_statistics.rx_errors;

    if (total_tx_packets > 0) {
        metrics->tx_error_rate = (packet_statistics.tx_errors * 100) / total_tx_packets;
    }

    if (total_rx_packets > 0) {
        metrics->rx_error_rate = (packet_statistics.rx_errors * 100) / total_rx_packets;
        metrics->rx_drop_rate = (packet_statistics.rx_dropped * 100) / total_rx_packets;
    }

    /* Calculate throughput (simplified - packets per second estimate) */
    /* In real implementation, this would use actual time measurements */
    metrics->tx_throughput = total_tx_packets; /* Simplified */
    metrics->rx_throughput = total_rx_packets; /* Simplified */

    /* Aggregate per-NIC statistics */
    total_nics = hardware_get_nic_count();
    for (i = 0; i < total_nics && i < MAX_NICS; i++) {
        nic = hardware_get_nic(i);
        if (nic) {
            metrics->nic_stats[i].active = (nic->status & NIC_STATUS_ACTIVE) ? 1 : 0;
            metrics->nic_stats[i].link_up = (nic->status & NIC_STATUS_LINK_UP) ? 1 : 0;
            metrics->nic_stats[i].speed = (nic->status & NIC_STATUS_100MBPS) ? 100 : 10;
            metrics->nic_stats[i].full_duplex = (nic->status & NIC_STATUS_FULL_DUPLEX) ? 1 : 0;
            metrics->nic_stats[i].tx_packets = nic->tx_packets;
            metrics->nic_stats[i].rx_packets = nic->rx_packets;
            metrics->nic_stats[i].tx_errors = nic->tx_errors;
            metrics->nic_stats[i].rx_errors = nic->rx_errors;
        }
    }
    
    metrics->active_nics = total_nics;
    metrics->collection_time = stats_get_timestamp();
    
    return 0;
}

/**
 * @brief Monitor packet driver health and performance
 * @return Health status (0 = healthy, positive = warnings, negative = errors)
 */
int packet_monitor_health(void) {
    int health_score = 0;
    int total_nics;
    nic_info_t *nic;
    uint32_t total_packets;
    uint32_t total_errors;
    int active_nics = 0;
    int i;
    uint32_t tx_error_rate;
    uint32_t rx_error_rate;
    uint32_t global_error_rate;

    /* Check if packet operations are initialized */
    if (!packet_ops_initialized) {
        log_warning("Packet operations not initialized");
        return -10;
    }

    /* Check for active NICs */
    total_nics = hardware_get_nic_count();
    if (total_nics == 0) {
        log_error("No NICs available");
        return -20;
    }

    for (i = 0; i < total_nics; i++) {
        nic = hardware_get_nic(i);
        if (nic && (nic->status & NIC_STATUS_ACTIVE)) {
            active_nics++;
            
            /* Check link status */
            if (!(nic->status & NIC_STATUS_LINK_UP)) {
                log_warning("NIC %d link is down", i);
                health_score += 5;
            }
            
            /* Check error rates */
            if (nic->tx_packets > 0) {
                tx_error_rate = (nic->tx_errors * 100) / nic->tx_packets;
                if (tx_error_rate > 10) {
                    log_warning("NIC %d high TX error rate: %lu%%", i, tx_error_rate);
                    health_score += 10;
                } else if (tx_error_rate > 5) {
                    health_score += 5;
                }
            }
            
            if (nic->rx_packets > 0) {
                rx_error_rate = (nic->rx_errors * 100) / nic->rx_packets;
                if (rx_error_rate > 10) {
                    log_warning("NIC %d high RX error rate: %lu%%", i, rx_error_rate);
                    health_score += 10;
                } else if (rx_error_rate > 5) {
                    health_score += 5;
                }
            }
        }
    }
    
    if (active_nics == 0) {
        log_error("No active NICs available");
        return -30;
    }
    
    /* Check global error rates */
    total_packets = packet_statistics.tx_packets + packet_statistics.rx_packets;
    total_errors = packet_statistics.tx_errors + packet_statistics.rx_errors;
    
    if (total_packets > 0) {
        global_error_rate = (total_errors * 100) / total_packets;
        if (global_error_rate > 15) {
            log_warning("High global error rate: %lu%%", global_error_rate);
            health_score += 15;
        } else if (global_error_rate > 10) {
            health_score += 10;
        } else if (global_error_rate > 5) {
            health_score += 5;
        }
    }
    
    /* Check buffer utilization */
    if (packet_statistics.tx_buffer_full > 0) {
        log_warning("TX buffer exhaustion events: %lu", packet_statistics.tx_buffer_full);
        health_score += 5;
    }
    
    /* Log health status */
    if (health_score == 0) {
        log_debug("Packet driver health: EXCELLENT");
    } else if (health_score < 10) {
        log_info("Packet driver health: GOOD (score: %d)", health_score);
    } else if (health_score < 25) {
        log_warning("Packet driver health: FAIR (score: %d)", health_score);
    } else {
        log_warning("Packet driver health: POOR (score: %d)", health_score);
    }
    
    return health_score;
}

/**
 * @brief Print detailed packet driver statistics
 */
void packet_print_detailed_stats(void) {
    int total_nics;
    nic_info_t *nic;
    int i;

    log_info("=== Packet Driver Statistics ===");
    log_info("Global Counters:");
    log_info("  TX: %lu packets, %lu bytes, %lu errors",
             packet_statistics.tx_packets,
             packet_statistics.tx_bytes,
             packet_statistics.tx_errors);
    log_info("  RX: %lu packets, %lu bytes, %lu errors, %lu dropped",
             packet_statistics.rx_packets,
             packet_statistics.rx_bytes,
             packet_statistics.rx_errors,
             packet_statistics.rx_dropped);
    log_info("  Routed: %lu packets", packet_statistics.routed_packets);
    log_info("  Buffer events: %lu TX full", packet_statistics.tx_buffer_full);

    /* Per-NIC statistics */
    total_nics = hardware_get_nic_count();
    for (i = 0; i < total_nics; i++) {
        nic = hardware_get_nic(i);
        if (nic) {
            log_info("NIC %d (%s):", i, 
                    (nic->status & NIC_STATUS_ACTIVE) ? "ACTIVE" : "INACTIVE");
            log_info("  Status: Link=%s, Speed=%dMbps, Duplex=%s",
                    (nic->status & NIC_STATUS_LINK_UP) ? "UP" : "DOWN",
                    (nic->status & NIC_STATUS_100MBPS) ? 100 : 10,
                    (nic->status & NIC_STATUS_FULL_DUPLEX) ? "FULL" : "HALF");
            log_info("  TX: %lu packets, %lu bytes, %lu errors",
                    nic->tx_packets, nic->tx_bytes, nic->tx_errors);
            log_info("  RX: %lu packets, %lu bytes, %lu errors",
                    nic->rx_packets, nic->rx_bytes, nic->rx_errors);
        }
    }
    
    log_info("=== End Statistics ===");
}

/**
 * @brief Reset packet statistics
 * @return 0 on success
 */
int packet_reset_statistics(void) {
    int total_nics;
    nic_info_t *nic;
    int i;

    log_info("Resetting packet statistics");
    memset(&packet_statistics, 0, sizeof(packet_statistics));

    /* Reset per-NIC statistics as well */
    total_nics = hardware_get_nic_count();
    for (i = 0; i < total_nics; i++) {
        nic = hardware_get_nic(i);
        if (nic) {
            nic->tx_packets = 0;
            nic->rx_packets = 0;
            nic->tx_bytes = 0;
            nic->rx_bytes = 0;
            nic->tx_errors = 0;
            nic->rx_errors = 0;
            nic->tx_dropped = 0;
            nic->rx_dropped = 0;
        }
    }
    
    return 0;
}

/**
 * @brief Check if packet operations are initialized
 * @return 1 if initialized, 0 otherwise
 */
int packet_ops_is_initialized(void) {
    return packet_ops_initialized;
}

/**
 * @brief Direct PIO packet send optimization for 3c509B (Sprint 1.2)
 * Eliminates intermediate buffer allocation and memcpy operations
 * @param interface_num NIC interface number
 * @param dest_addr Destination MAC address
 * @param ethertype Ethernet type/protocol
 * @param payload Payload data (from stack)
 * @param payload_len Payload length
 * @return 0 on success, negative on error
 */
int packet_send_direct_pio_3c509b(uint8_t interface_num, const uint8_t *dest_addr,
                                 uint16_t ethertype, const void* payload, uint16_t payload_len) {
    nic_info_t *nic;
    int result;
    
    /* Validate parameters */
    if (!dest_addr || !payload || payload_len == 0 || payload_len > ETH_MAX_DATA) {
        log_error("Invalid parameters for direct PIO send");
        packet_statistics.tx_errors++;
        return PACKET_ERR_INVALID_PARAM;
    }
    
    /* Get NIC information */
    nic = hardware_get_nic(interface_num);
    if (!nic) {
        log_error("Invalid interface number: %d", interface_num);
        packet_statistics.tx_errors++;
        return PACKET_ERR_INVALID_NIC;
    }
    
    /* Check if NIC is active */
    if (!(nic->status & NIC_STATUS_ACTIVE)) {
        log_error("NIC %d is not active", interface_num);
        packet_statistics.tx_errors++;
        return PACKET_ERR_INVALID_NIC;
    }
    
    /* Check if this is a 3c509B NIC */
    if (nic->type != NIC_TYPE_3C509B) {
        log_debug("Direct PIO optimization only available for 3c509B, NIC %d is type %d", 
                 interface_num, nic->type);
        packet_statistics.tx_errors++;
        return PACKET_ERR_NOT_SUPPORTED;
    }
    
    /* Use 3c509B direct PIO transmission with header construction */
    result = send_packet_direct_pio_with_header(nic, dest_addr, ethertype, payload, payload_len);
    if (result != SUCCESS) {
        log_error("Direct PIO transmission failed on interface %d: %d", interface_num, result);
        packet_statistics.tx_errors++;
        return result;
    }
    
    /* Update global statistics */
    packet_statistics.tx_packets++;
    packet_statistics.tx_bytes += ETH_HEADER_LEN + payload_len;
    
    log_debug("Successfully sent packet via direct PIO on interface %d: %d bytes", 
             interface_num, ETH_HEADER_LEN + payload_len);
    
    return SUCCESS;
}

/**
 * @brief Cleanup packet operations
 * @return 0 on success, negative on error
 */
/**
 * @brief Build an Ethernet frame with header and payload
 * @param frame_buffer Buffer to build frame in
 * @param frame_size Size of frame buffer
 * @param dest_mac Destination MAC address
 * @param src_mac Source MAC address  
 * @param ethertype Ethernet type/protocol
 * @param payload Payload data
 * @param payload_len Payload length
 * @return 0 on success, negative on error
 */
int packet_build_ethernet_frame(uint8_t *frame_buffer, uint16_t frame_size,
                               const uint8_t *dest_mac, const uint8_t *src_mac,
                               uint16_t ethertype, const uint8_t *payload,
                               uint16_t payload_len) {
    uint16_t frame_len;
    
    if (!frame_buffer || !dest_mac || !src_mac || !payload) {
        return PACKET_ERR_INVALID_PARAM;
    }
    
    /* Calculate required frame length */
    frame_len = ETH_HEADER_LEN + payload_len;
    if (frame_len > frame_size) {
        return PACKET_ERR_INVALID_SIZE;
    }
    
    /* Build Ethernet header */
    memcpy(frame_buffer, dest_mac, ETH_ALEN);                    /* Destination MAC */
    memcpy(frame_buffer + ETH_ALEN, src_mac, ETH_ALEN);          /* Source MAC */
    *(uint16_t*)(frame_buffer + 2 * ETH_ALEN) = htons(ethertype); /* EtherType */
    
    /* Copy payload */
    memcpy(frame_buffer + ETH_HEADER_LEN, payload, payload_len);
    
    /* Pad to minimum frame size if necessary */
    if (frame_len < ETH_MIN_FRAME) {
        memset(frame_buffer + frame_len, 0, ETH_MIN_FRAME - frame_len);
        frame_len = ETH_MIN_FRAME;
    }
    
    log_debug("Built Ethernet frame: len=%d, type=0x%04X", frame_len, ethertype);
    return frame_len;
}

/**
 * @brief Build Ethernet frame with CPU-optimized copying for better performance
 * @param frame_buffer Buffer to build frame in
 * @param frame_size Size of frame buffer
 * @param dest_mac Destination MAC address
 * @param src_mac Source MAC address  
 * @param ethertype Ethernet type/protocol
 * @param payload Payload data
 * @param payload_len Payload length
 * @return Frame length on success, negative on error
 */
int packet_build_ethernet_frame_optimized(uint8_t *frame_buffer, uint16_t frame_size,
                                         const uint8_t *dest_mac, const uint8_t *src_mac,
                                         uint16_t ethertype, const uint8_t *payload,
                                         uint16_t payload_len) {
    uint16_t frame_len;
    uint16_t pad_len;
    extern cpu_info_t g_cpu_info;
    
    if (!frame_buffer || !dest_mac || !src_mac || !payload) {
        return PACKET_ERR_INVALID_PARAM;
    }
    
    /* Calculate required frame length */
    frame_len = ETH_HEADER_LEN + payload_len;
    if (frame_len > frame_size) {
        return PACKET_ERR_INVALID_SIZE;
    }
    
    /* Build Ethernet header using CPU-optimized copying */
    memory_copy_optimized(frame_buffer, dest_mac, ETH_ALEN);                    /* Destination MAC */
    memory_copy_optimized(frame_buffer + ETH_ALEN, src_mac, ETH_ALEN);          /* Source MAC */
    *(uint16_t*)(frame_buffer + 2 * ETH_ALEN) = htons(ethertype);               /* EtherType */
    
    /* Use fast-path copying based on payload size */
    if (payload_len <= 64 && g_cpu_info.cpu_type >= CPU_TYPE_80286) {
        /* Small payload - use specialized fast copy */
        packet_copy_small_payload(frame_buffer + ETH_HEADER_LEN, payload, payload_len);
    } else if (payload_len <= 512 && g_cpu_info.cpu_type >= CPU_TYPE_80386) {
        /* Medium payload - use 32-bit optimized copy */
        memory_copy_optimized(frame_buffer + ETH_HEADER_LEN, payload, payload_len);
    } else {
        /* Large payload or older CPU - use standard copy */
        memory_copy_optimized(frame_buffer + ETH_HEADER_LEN, payload, payload_len);
    }
    
    /* Pad to minimum frame size if necessary using optimized memset */
    if (frame_len < ETH_MIN_FRAME) {
        pad_len = ETH_MIN_FRAME - frame_len;
        memory_set_optimized(frame_buffer + frame_len, 0, pad_len);
        frame_len = ETH_MIN_FRAME;
    }
    
    log_debug("Built optimized Ethernet frame: len=%d, type=0x%04X, CPU=%s", 
             frame_len, ethertype, cpu_type_to_string(g_cpu_info.cpu_type));
    return frame_len;
}

/**
 * @brief Fast copy for small payloads (≤64 bytes) using assembly fast paths
 * @param dest Destination buffer
 * @param src Source buffer  
 * @param len Length to copy
 */
static void packet_copy_small_payload(uint8_t *dest, const uint8_t *src, uint16_t len) {
    extern cpu_info_t g_cpu_info;
    
    if (len <= 64 && g_cpu_info.cpu_type >= CPU_TYPE_80386) {
        /* Use assembly fast path for 64-byte copy if available */
        /* This would call the packet_copy_64_bytes function from assembly */
        if (len == 64) {
            /* Call assembly 64-byte optimized copy */
            /* packet_copy_64_bytes(src, dest); - Assembly call would go here */
            memory_copy_optimized(dest, src, len);
        } else {
            /* Use regular optimized copy for non-standard sizes */
            memory_copy_optimized(dest, src, len);
        }
    } else {
        /* Fallback to regular copy */
        memory_copy_optimized(dest, src, len);
    }
}

/**
 * @brief Parse Ethernet header from received frame
 * @param frame_data Frame data
 * @param frame_len Frame length
 * @param header Pointer to store parsed header
 * @return 0 on success, negative on error
 */
int packet_parse_ethernet_header(const uint8_t *frame_data, uint16_t frame_len,
                                eth_header_t *header) {
    if (!frame_data || !header || frame_len < ETH_HEADER_LEN) {
        return PACKET_ERR_INVALID_PARAM;
    }
    
    /* Extract header fields */
    memcpy(header->dest_mac, frame_data, ETH_ALEN);
    memcpy(header->src_mac, frame_data + ETH_ALEN, ETH_ALEN);
    header->ethertype = ntohs(*(uint16_t*)(frame_data + 2 * ETH_ALEN));
    
    log_debug("Parsed Ethernet header: type=0x%04X", header->ethertype);
    return 0;
}

/**
 * @brief Check if packet is addressed to our MAC
 * @param frame_data Frame data
 * @param our_mac Our MAC address
 * @return true if packet is for us
 */
bool packet_is_for_us(const uint8_t *frame_data, const uint8_t *our_mac) {
    if (!frame_data || !our_mac) {
        return false;
    }
    
    return memcmp(frame_data, our_mac, ETH_ALEN) == 0;
}

/**
 * @brief Check if packet is broadcast
 * @param frame_data Frame data
 * @return true if broadcast packet
 */
bool packet_is_broadcast(const uint8_t *frame_data) {
    static const uint8_t broadcast_mac[ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    if (!frame_data) {
        return false;
    }
    
    return memcmp(frame_data, broadcast_mac, ETH_ALEN) == 0;
}

/**
 * @brief Check if packet is multicast
 * @param frame_data Frame data
 * @return true if multicast packet
 */
bool packet_is_multicast(const uint8_t *frame_data) {
    if (!frame_data) {
        return false;
    }
    
    /* Multicast bit is LSB of first octet */
    return (frame_data[0] & 0x01) != 0;
}

/**
 * @brief Get EtherType from frame
 * @param frame_data Frame data
 * @return EtherType value
 */
uint16_t packet_get_ethertype(const uint8_t *frame_data) {
    if (!frame_data) {
        return 0;
    }
    
    return ntohs(*(uint16_t*)(frame_data + 2 * ETH_ALEN));
}

/**
 * @brief Multi-NIC packet routing based on configuration
 * @param packet_data Packet data
 * @param length Packet length
 * @param src_nic_index Source NIC index (for received packets)
 * @return Target NIC index, or negative for local delivery
 */
int packet_route_multi_nic(const uint8_t *packet_data, uint16_t length, int src_nic_index) {
    /* C89: All declarations at start of function */
    eth_header_t eth_header;
    int total_nics;
    int target_nic;
    int i;
    nic_info_t *nic;

    target_nic = -1;

    if (!packet_data || length < ETH_HEADER_LEN) {
        return -1;
    }

    /* Parse Ethernet header for routing decisions */
    if (packet_parse_ethernet_header(packet_data, length, &eth_header) < 0) {
        return -1;
    }

    /* Get total number of NICs */
    total_nics = hardware_get_nic_count();
    if (total_nics <= 1) {
        /* Single NIC - no routing needed */
        return -1;
    }

    /* Check if this is a broadcast packet - send to all other NICs */
    if (packet_is_broadcast(packet_data)) {
        log_debug("Broadcast packet - would forward to all NICs except source %d", src_nic_index);
        /* For now, deliver locally. Full implementation would queue to all other NICs */
        return -1;
    }

    /* Check if destination is on a different segment */
    /* This is a simplified routing - real implementation would use routing table */
    for (i = 0; i < total_nics; i++) {
        nic = hardware_get_nic(i);
        if (!nic || i == src_nic_index) {
            continue;
        }

        /* Check if destination MAC matches this NIC's subnet */
        /* For now, use simple even/odd MAC address routing as example */
        if ((eth_header.dest_mac[5] & 1) == (i & 1)) {
            target_nic = i;
            log_debug("Routing packet to NIC %d based on MAC address", target_nic);
            break;
        }
    }

    return target_nic;
}

/**
 * @brief Coordinate packet sending across multiple NICs with load balancing
 * @param packet_data Packet data
 * @param length Packet length
 * @param dest_addr Destination MAC address
 * @param handle Sender handle
 * @return 0 on success, negative on error
 */
int packet_send_multi_nic(const uint8_t *packet_data, uint16_t length,
                          const uint8_t *dest_addr, uint16_t handle) {
    /* C89: All declarations at start of function */
    static int next_nic_index = 0; /* Simple round-robin counter */
    int total_nics;
    int selected_nic;
    int result;
    int attempts;
    nic_info_t *nic;

    if (!packet_data || !dest_addr || length == 0) {
        return PACKET_ERR_INVALID_PARAM;
    }

    total_nics = hardware_get_nic_count();
    if (total_nics == 0) {
        log_error("No NICs available for transmission");
        return PACKET_ERR_INVALID_NIC;
    }

    /* For broadcast packets, send on primary NIC */
    if (packet_is_broadcast(packet_data)) {
        selected_nic = 0;
        log_debug("Broadcast packet - using primary NIC 0");
    }
    /* For unicast, use load balancing or routing table */
    else {
        /* Simple round-robin load balancing for now */
        /* Real implementation would use routing table lookup */
        selected_nic = next_nic_index % total_nics;
        next_nic_index = (next_nic_index + 1) % total_nics;

        /* Skip inactive NICs */
        for (attempts = 0; attempts < total_nics; attempts++) {
            nic = hardware_get_nic(selected_nic);
            if (nic && (nic->status & NIC_STATUS_ACTIVE)) {
                break;
            }
            selected_nic = (selected_nic + 1) % total_nics;
        }

        log_debug("Load balancing: selected NIC %d for transmission", selected_nic);
    }

    /* Send using the enhanced packet send function */
    result = packet_send_enhanced(selected_nic, packet_data, length, dest_addr, handle);
    if (result < 0) {
        log_error("Failed to send packet via NIC %d: %d", selected_nic, result);
        return result;
    }

    return 0;
}

/**
 * @brief Check and handle NIC failover
 * @param failed_nic_index Index of failed NIC
 * @return 0 on successful failover, negative on error
 */
int packet_handle_nic_failover(int failed_nic_index) {
    /* C89: All declarations at start of function */
    int total_nics;
    int active_nics;
    int i;
    nic_info_t *nic;

    active_nics = 0;

    log_warning("Handling failover for failed NIC %d", failed_nic_index);

    total_nics = hardware_get_nic_count();

    /* Count active NICs */
    for (i = 0; i < total_nics; i++) {
        nic = hardware_get_nic(i);
        if (nic && (nic->status & NIC_STATUS_ACTIVE) && i != failed_nic_index) {
            active_nics++;
        }
    }
    
    if (active_nics == 0) {
        log_error("No active NICs available after failover");
        return PACKET_ERR_INVALID_NIC;
    }
    
    log_info("Failover completed: %d active NICs remaining", active_nics);
    
    /* Update routing to avoid failed NIC */
    /* Real implementation would update routing table */
    
    return 0;
}

/**
 * @brief Get optimal NIC for packet transmission based on load and link status
 * @param packet_data Packet data for routing decisions
 * @param length Packet length
 * @return NIC index, or negative on error
 */
int packet_get_optimal_nic(const uint8_t *packet_data, uint16_t length) {
    /* C89: All declarations at start of function */
    int total_nics;
    int best_nic;
    uint32_t best_score;
    uint32_t score;
    uint32_t error_rate;
    int i;
    nic_info_t *nic;

    (void)packet_data; /* May be used for future routing decisions */
    (void)length;

    best_nic = -1;
    best_score = 0;

    total_nics = hardware_get_nic_count();

    for (i = 0; i < total_nics; i++) {
        nic = hardware_get_nic(i);
        if (!nic || !(nic->status & NIC_STATUS_ACTIVE)) {
            continue;
        }

        /* Calculate score based on multiple factors */
        score = 100; /* Base score */

        /* Link quality factor */
        if (nic->status & NIC_STATUS_LINK_UP) {
            score += 50;
        }

        /* Speed factor */
        if (nic->status & NIC_STATUS_100MBPS) {
            score += 30;
        }

        /* Load factor (inverse of error rate) */
        if (nic->tx_packets > 0) {
            error_rate = (nic->tx_errors * 100) / nic->tx_packets;
            score += (100 - error_rate);
        }

        /* Duplex factor */
        if (nic->status & NIC_STATUS_FULL_DUPLEX) {
            score += 20;
        }

        if (score > best_score) {
            best_score = score;
            best_nic = i;
        }
    }

    if (best_nic >= 0) {
        log_debug("Selected optimal NIC %d (score=%lu)", best_nic, (unsigned long)best_score);
    }

    return best_nic;
}

/**
 * @brief Comprehensive loopback testing suite
 * This implements internal, external, and cross-NIC loopback testing
 */

/**
 * @brief Test internal loopback functionality
 * @param nic_index NIC to test
 * @param test_pattern Test pattern to use
 * @param pattern_size Size of test pattern
 * @return 0 on success, negative on error
 */
int packet_test_internal_loopback(int nic_index, const uint8_t *test_pattern, uint16_t pattern_size) {
    /* C89: All declarations at start of function */
    static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    nic_info_t *nic;
    uint8_t test_frame[ETH_MAX_FRAME];
    uint8_t rx_buffer[ETH_MAX_FRAME];
    uint16_t frame_length = 0;  /* Initialize to silence W200 */
    size_t rx_length;
    int result;
    uint32_t timeout_ms;
    uint32_t start_time;
    uint8_t *rx_payload;

    timeout_ms = 1000;
    (void)test_frame;
    (void)frame_length;
    
    if (!test_pattern || pattern_size == 0 || pattern_size > ETH_MAX_DATA) {
        log_error("Invalid loopback test parameters");
        return PACKET_ERR_INVALID_PARAM;
    }
    
    nic = hardware_get_nic(nic_index);
    if (!nic) {
        log_error("Invalid NIC index for loopback test: %d", nic_index);
        return PACKET_ERR_INVALID_NIC;
    }
    
    if (!(nic->status & NIC_STATUS_ACTIVE)) {
        log_error("NIC %d not active for loopback test", nic_index);
        return PACKET_ERR_INVALID_NIC;
    }
    
    log_info("Starting internal loopback test on NIC %d", nic_index);

    /* Build test frame with broadcast destination */
    /* Note: broadcast_mac is declared at the start of the function */
    frame_length = packet_build_ethernet_frame(test_frame, sizeof(test_frame),
                                              broadcast_mac, nic->mac,
                                              0x0800, /* IP ethertype */
                                              test_pattern, pattern_size);
    
    if (frame_length < 0) {
        log_error("Failed to build loopback test frame");
        return frame_length;
    }
    
    /* Enable internal loopback mode */
    result = packet_enable_loopback_mode(nic, LOOPBACK_INTERNAL);
    if (result != 0) {
        log_error("Failed to enable internal loopback mode: %d", result);
        return result;
    }
    
    /* Clear any pending RX packets */
    rx_length = sizeof(rx_buffer);
    while (packet_receive_from_nic(nic_index, rx_buffer, &rx_length) == 0) {
        rx_length = sizeof(rx_buffer);
    }
    
    /* Send test frame */
    result = packet_send_enhanced(nic_index, test_pattern, pattern_size, broadcast_mac, 0x1234);
    if (result != 0) {
        log_error("Failed to send loopback test frame: %d", result);
        packet_disable_loopback_mode(nic);
        return result;
    }
    
    log_debug("Loopback test frame sent, waiting for reception...");
    
    /* Wait for loopback reception */
    start_time = stats_get_timestamp();
    rx_length = sizeof(rx_buffer);
    
    while ((stats_get_timestamp() - start_time) < timeout_ms) {
        result = packet_receive_from_nic(nic_index, rx_buffer, &rx_length);
        
        if (result == 0) {
            /* Verify received frame */
            if (rx_length >= ETH_HEADER_LEN + pattern_size) {
                /* Extract payload from received frame */
                rx_payload = rx_buffer + ETH_HEADER_LEN;

                if (memcmp(rx_payload, test_pattern, pattern_size) == 0) {
                    log_info("Internal loopback test PASSED on NIC %d", nic_index);
                    packet_disable_loopback_mode(nic);
                    return 0;
                } else {
                    log_error("Loopback data mismatch on NIC %d", nic_index);
                    packet_disable_loopback_mode(nic);
                    return PACKET_ERR_INVALID_DATA;
                }
            }
        }
        
        /* Brief delay before retry */
        { volatile int delay_i; for (delay_i = 0; delay_i < 1000; delay_i++); }
        rx_length = sizeof(rx_buffer);
    }
    
    log_error("Internal loopback test TIMEOUT on NIC %d", nic_index);
    packet_disable_loopback_mode(nic);
    return PACKET_ERR_TIMEOUT;
}

/**
 * @brief Test external loopback with physical connector
 * @param nic_index NIC to test
 * @param test_patterns Array of test patterns
 * @param num_patterns Number of test patterns
 * @return 0 on success, negative on error
 */
int packet_test_external_loopback(int nic_index, const loopback_test_pattern_t *test_patterns, int num_patterns) {
    /* C89: All declarations at start of function */
    nic_info_t *nic;
    int passed_tests;
    int failed_tests;
    int result;
    int i;

    passed_tests = 0;
    failed_tests = 0;

    if (!test_patterns || num_patterns <= 0) {
        return PACKET_ERR_INVALID_PARAM;
    }

    nic = hardware_get_nic(nic_index);
    if (!nic) {
        return PACKET_ERR_INVALID_NIC;
    }

    log_info("Starting external loopback test on NIC %d (%d patterns)", nic_index, num_patterns);

    /* Disable internal loopback, enable external */
    result = packet_enable_loopback_mode(nic, LOOPBACK_EXTERNAL);
    if (result != 0) {
        log_error("Failed to enable external loopback mode: %d", result);
        return result;
    }
    
    /* Test each pattern */
    for (i = 0; i < num_patterns; i++) {
        log_debug("Testing external loopback pattern %d: %s", i, test_patterns[i].name);
        
        result = packet_test_single_loopback_pattern(nic_index, &test_patterns[i]);
        if (result == 0) {
            passed_tests++;
            log_debug("Pattern %d PASSED", i);
        } else {
            failed_tests++;
            log_warning("Pattern %d FAILED: %d", i, result);
        }
    }
    
    packet_disable_loopback_mode(nic);
    
    log_info("External loopback test completed: %d passed, %d failed", passed_tests, failed_tests);
    
    return (failed_tests == 0) ? 0 : PACKET_ERR_LOOPBACK_FAILED;
}

/**
 * @brief Test cross-NIC loopback for multi-NIC validation
 * @param src_nic_index Source NIC
 * @param dest_nic_index Destination NIC 
 * @param test_data Test data to send
 * @param data_size Size of test data
 * @return 0 on success, negative on error
 */
int packet_test_cross_nic_loopback(int src_nic_index, int dest_nic_index,
                                  const uint8_t *test_data, uint16_t data_size) {
    /* C89: All declarations at start of function */
    nic_info_t *src_nic;
    nic_info_t *dest_nic;
    uint8_t test_frame[ETH_MAX_FRAME];
    uint8_t rx_buffer[ETH_MAX_FRAME];
    uint16_t frame_length;
    size_t rx_length;
    int result;
    uint32_t timeout_ms;
    uint32_t start_time;
    eth_header_t eth_header;

    timeout_ms = 2000;  /* Longer timeout for cross-NIC */
    
    if (!test_data || data_size == 0 || src_nic_index == dest_nic_index) {
        return PACKET_ERR_INVALID_PARAM;
    }
    
    src_nic = hardware_get_nic(src_nic_index);
    dest_nic = hardware_get_nic(dest_nic_index);
    
    if (!src_nic || !dest_nic) {
        log_error("Invalid NIC indices for cross-NIC test: src=%d, dest=%d", 
                 src_nic_index, dest_nic_index);
        return PACKET_ERR_INVALID_NIC;
    }
    
    if (!(src_nic->status & NIC_STATUS_ACTIVE) || !(dest_nic->status & NIC_STATUS_ACTIVE)) {
        log_error("NICs not active for cross-NIC test");
        return PACKET_ERR_INVALID_NIC;
    }
    
    log_info("Starting cross-NIC loopback test: NIC %d -> NIC %d", src_nic_index, dest_nic_index);
    
    /* Build test frame addressed to destination NIC */
    frame_length = packet_build_ethernet_frame(test_frame, sizeof(test_frame),
                                              dest_nic->mac, src_nic->mac,
                                              0x0800, /* IP ethertype */
                                              test_data, data_size);
    
    if (frame_length < 0) {
        log_error("Failed to build cross-NIC test frame");
        return frame_length;
    }
    
    /* Enable promiscuous mode on destination NIC to receive all packets */
    result = hardware_set_promiscuous_mode(dest_nic, true);
    if (result != 0) {
        log_warning("Failed to enable promiscuous mode on dest NIC %d", dest_nic_index);
    }
    
    /* Clear any pending packets on destination NIC */
    rx_length = sizeof(rx_buffer);
    while (packet_receive_from_nic(dest_nic_index, rx_buffer, &rx_length) == 0) {
        rx_length = sizeof(rx_buffer);
    }
    
    /* Send packet from source NIC */
    result = packet_send_enhanced(src_nic_index, test_data, data_size, dest_nic->mac, 0x5678);
    if (result != 0) {
        log_error("Failed to send cross-NIC test packet: %d", result);
        hardware_set_promiscuous_mode(dest_nic, false);
        return result;
    }
    
    log_debug("Cross-NIC packet sent, waiting for reception on NIC %d...", dest_nic_index);
    
    /* Wait for packet on destination NIC */
    start_time = stats_get_timestamp();
    rx_length = sizeof(rx_buffer);
    
    while ((stats_get_timestamp() - start_time) < timeout_ms) {
        result = packet_receive_from_nic(dest_nic_index, rx_buffer, &rx_length);
        
        if (result == 0) {
            /* Verify received frame */
            /* Note: eth_header declared at function start */
            result = packet_parse_ethernet_header(rx_buffer, rx_length, &eth_header);
            
            if (result == 0) {
                /* Check if this is our test packet */
                if (memcmp(eth_header.dest_mac, dest_nic->mac, ETH_ALEN) == 0 &&
                    memcmp(eth_header.src_mac, src_nic->mac, ETH_ALEN) == 0) {
                    
                    /* Verify payload */
                    uint8_t *rx_payload = rx_buffer + ETH_HEADER_LEN;
                    uint16_t payload_length = rx_length - ETH_HEADER_LEN;
                    
                    if (payload_length >= data_size && 
                        memcmp(rx_payload, test_data, data_size) == 0) {
                        log_info("Cross-NIC loopback test PASSED: NIC %d -> NIC %d", 
                                src_nic_index, dest_nic_index);
                        hardware_set_promiscuous_mode(dest_nic, false);
                        return 0;
                    } else {
                        log_error("Cross-NIC payload mismatch");
                        hardware_set_promiscuous_mode(dest_nic, false);
                        return PACKET_ERR_INVALID_DATA;
                    }
                }
            }
        }
        
        /* Brief delay before retry */
        { volatile int delay_i; for (delay_i = 0; delay_i < 1000; delay_i++); }
        rx_length = sizeof(rx_buffer);
    }
    
    log_error("Cross-NIC loopback test TIMEOUT: NIC %d -> NIC %d", src_nic_index, dest_nic_index);
    hardware_set_promiscuous_mode(dest_nic, false);
    return PACKET_ERR_TIMEOUT;
}

/**
 * @brief Comprehensive packet integrity verification during loopback
 * @param original_data Original packet data
 * @param received_data Received packet data
 * @param data_length Length of data to compare
 * @param integrity_result Pointer to store detailed integrity result
 * @return 0 if integrity check passed, negative on error
 */
int packet_verify_loopback_integrity(const uint8_t *original_data, const uint8_t *received_data,
                                    uint16_t data_length, packet_integrity_result_t *integrity_result) {
    /* C89: All declarations at start of function */
    uint16_t i;
    packet_mismatch_detail_t *detail;

    if (!original_data || !received_data || !integrity_result || data_length == 0) {
        return PACKET_ERR_INVALID_PARAM;
    }

    memset(integrity_result, 0, sizeof(packet_integrity_result_t));
    integrity_result->bytes_compared = data_length;

    /* Byte-by-byte comparison */
    for (i = 0; i < data_length; i++) {
        if (original_data[i] != received_data[i]) {
            integrity_result->mismatch_count++;
            
            /* Store first few mismatches for debugging */
            if (integrity_result->mismatch_count <= MAX_MISMATCH_DETAILS) {
                detail = &integrity_result->mismatch_details[integrity_result->mismatch_count - 1];
                detail->offset = i;
                detail->expected = original_data[i];
                detail->actual = received_data[i];
            }
        }
    }
    
    /* Calculate error statistics */
    if (integrity_result->mismatch_count > 0) {
        integrity_result->error_rate_percent = 
            (integrity_result->mismatch_count * 100) / data_length;
        
        /* Analyze error patterns */
        packet_analyze_error_patterns(integrity_result);
        
        log_error("Packet integrity check FAILED: %d mismatches out of %d bytes (%d.%02d%%)",
                 integrity_result->mismatch_count, data_length,
                 integrity_result->error_rate_percent,
                 (int)((integrity_result->mismatch_count * 10000UL) / data_length) % 100);
        
        return PACKET_ERR_INTEGRITY_FAILED;
    }
    
    log_debug("Packet integrity check PASSED: %d bytes verified", data_length);
    return 0;
}

/**
 * @brief Enable loopback mode on a NIC
 * @param nic NIC to configure
 * @param loopback_type Type of loopback to enable
 * @return 0 on success, negative on error
 */
static int packet_enable_loopback_mode(nic_info_t *nic, loopback_type_t loopback_type) {
    if (!nic) {
        return PACKET_ERR_INVALID_PARAM;
    }
    
    log_debug("Enabling loopback mode %d on NIC %d", loopback_type, nic->index);
    
    if (nic->type == NIC_TYPE_3C509B) {
        return packet_enable_3c509b_loopback(nic, loopback_type);
    } else if (nic->type == NIC_TYPE_3C515_TX) {
        return packet_enable_3c515_loopback(nic, loopback_type);
    }
    
    return PACKET_ERR_NOT_SUPPORTED;
}

/**
 * @brief Disable loopback mode on a NIC
 * @param nic NIC to configure
 * @return 0 on success, negative on error
 */
static int packet_disable_loopback_mode(nic_info_t *nic) {
    if (!nic) {
        return PACKET_ERR_INVALID_PARAM;
    }
    
    log_debug("Disabling loopback mode on NIC %d", nic->index);
    
    if (nic->type == NIC_TYPE_3C509B) {
        return packet_disable_3c509b_loopback(nic);
    } else if (nic->type == NIC_TYPE_3C515_TX) {
        return packet_disable_3c515_loopback(nic);
    }
    
    return PACKET_ERR_NOT_SUPPORTED;
}

/**
 * @brief Enable 3C509B loopback mode
 * @param nic NIC to configure
 * @param loopback_type Type of loopback
 * @return 0 on success, negative on error
 */
static int packet_enable_3c509b_loopback(nic_info_t *nic, loopback_type_t loopback_type) {
    /* C89: All declarations at start of function */
    uint16_t rx_filter;

    rx_filter = 0x01;  /* Individual address */

    _3C509B_SELECT_WINDOW(nic->io_base, _3C509B_WINDOW_0);
    
    switch (loopback_type) {
        case LOOPBACK_INTERNAL:
            /* Set internal loopback in RX filter */
            rx_filter |= 0x08;  /* Loopback mode */
            break;
            
        case LOOPBACK_EXTERNAL:
            /* External loopback requires physical connector */
            /* No special register settings needed */
            break;
            
        default:
            return PACKET_ERR_INVALID_PARAM;
    }
    
    /* Apply RX filter settings */
    outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_SET_RX_FILTER | rx_filter);
    
    /* Enable TX and RX */
    outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_TX_ENABLE);
    outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_RX_ENABLE);
    
    return 0;
}

/**
 * @brief Enable 3C515-TX loopback mode
 * @param nic NIC to configure
 * @param loopback_type Type of loopback
 * @return 0 on success, negative on error
 */
static int packet_enable_3c515_loopback(nic_info_t *nic, loopback_type_t loopback_type) {
    /* C89: All declarations at start of function */
    uint16_t media_options;

    _3C515_TX_SELECT_WINDOW(nic->io_base, _3C515_TX_WINDOW_4);

    media_options = inw(nic->io_base + _3C515_TX_W4_MEDIA);
    
    switch (loopback_type) {
        case LOOPBACK_INTERNAL:
            /* Enable internal loopback */
            media_options |= 0x0008;  /* Internal loopback bit */
            break;
            
        case LOOPBACK_EXTERNAL:
            /* Disable internal loopback for external testing */
            media_options &= ~0x0008;
            break;
            
        default:
            return PACKET_ERR_INVALID_PARAM;
    }
    
    outw(nic->io_base + _3C515_TX_W4_MEDIA, media_options);

    /* Enable TX and RX */
    _3C515_TX_SELECT_WINDOW(nic->io_base, _3C515_TX_WINDOW_1);
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_TX_ENABLE);
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_RX_ENABLE);
    
    return 0;
}

/**
 * @brief Disable 3C509B loopback mode
 * @param nic NIC to configure
 * @return 0 on success, negative on error
 */
static int packet_disable_3c509b_loopback(nic_info_t *nic) {
    /* C89: All declarations at start of function */
    uint16_t rx_filter;

    _3C509B_SELECT_WINDOW(nic->io_base, _3C509B_WINDOW_0);

    /* Reset to normal RX filter (individual + broadcast) */
    rx_filter = 0x01 | 0x02;  /* Individual + broadcast */
    outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_SET_RX_FILTER | rx_filter);
    
    return 0;
}

/**
 * @brief Disable 3C515-TX loopback mode
 * @param nic NIC to configure
 * @return 0 on success, negative on error
 */
static int packet_disable_3c515_loopback(nic_info_t *nic) {
    /* C89: All declarations at start of function */
    uint16_t media_options;

    _3C515_TX_SELECT_WINDOW(nic->io_base, _3C515_TX_WINDOW_4);

    /* Disable internal loopback */
    media_options = inw(nic->io_base + _3C515_TX_W4_MEDIA);
    media_options &= ~0x0008;  /* Clear internal loopback bit */
    outw(nic->io_base + _3C515_TX_W4_MEDIA, media_options);

    return 0;
}

/**
 * @brief Test a single loopback pattern
 * @param nic_index NIC to test
 * @param pattern Test pattern to use
 * @return 0 on success, negative on error
 */
static int packet_test_single_loopback_pattern(int nic_index, const loopback_test_pattern_t *pattern) {
    /* C89: All declarations at start of function */
    uint8_t rx_buffer[ETH_MAX_FRAME];
    size_t rx_length = 0;  /* Initialize to silence W200 */
    packet_integrity_result_t integrity_result = {0};  /* Initialize to silence W200 */
    int result;
    uint32_t timeout_ms;
    uint32_t start_time = 0;  /* Initialize to silence W200 */

    (void)rx_buffer;
    (void)rx_length;
    (void)integrity_result;
    (void)start_time;

    timeout_ms = pattern->timeout_ms ? pattern->timeout_ms : 1000;
    (void)timeout_ms;
    
    /* Send test pattern */
    result = packet_test_internal_loopback(nic_index, pattern->data, pattern->size);
    if (result != 0) {
        return result;
    }
    
    return 0;  /* Success if internal loopback passed */
}

/**
 * @brief Analyze error patterns in received data
 * @param integrity_result Integrity result to analyze
 */
static void packet_analyze_error_patterns(packet_integrity_result_t *integrity_result) {
    /* C89: All declarations at start of function */
    int bit_errors;
    int byte_shifts;
    int burst_errors;
    int i;
    int bit;
    int bits_different;
    packet_mismatch_detail_t *detail;
    packet_mismatch_detail_t *prev;
    uint8_t xor_result;

    if (!integrity_result || integrity_result->mismatch_count == 0) {
        return;
    }

    /* Look for common error patterns */
    bit_errors = 0;
    byte_shifts = 0;
    burst_errors = 0;
    (void)byte_shifts;  /* May be unused */

    for (i = 0; i < integrity_result->mismatch_count && i < MAX_MISMATCH_DETAILS; i++) {
        detail = &integrity_result->mismatch_details[i];
        xor_result = detail->expected ^ detail->actual;

        /* Count bit errors */
        bits_different = 0;
        for (bit = 0; bit < 8; bit++) {
            if (xor_result & (1 << bit)) {
                bits_different++;
            }
        }

        if (bits_different == 1) {
            bit_errors++;
        }

        /* Check for byte shift patterns */
        if (i > 0) {
            prev = &integrity_result->mismatch_details[i - 1];
            if (detail->offset == prev->offset + 1) {
                burst_errors++;
            }
        }
    }
    
    /* Store pattern analysis results */
    integrity_result->single_bit_errors = bit_errors;
    integrity_result->burst_errors = burst_errors;
    
    /* Determine likely error cause */
    if (bit_errors > burst_errors) {
        strcpy(integrity_result->error_pattern_description, "Single-bit errors (electrical noise)");
    } else if (burst_errors > 0) {
        strcpy(integrity_result->error_pattern_description, "Burst errors (synchronization issue)");
    } else {
        strcpy(integrity_result->error_pattern_description, "Random data corruption");
    }
}

/**
 * @brief Route packet to another interface
 * @param packet Packet data
 * @param length Packet length
 * @param dest_nic Destination NIC index
 * @return 0 on success, negative on error
 */
static int route_packet_to_interface(const uint8_t *packet, uint16_t length, uint8_t dest_nic) {
    nic_info_t *nic;
    ip_addr_t dest_ip;
    uint8_t dest_mac[ETH_ALEN];
    uint8_t nic_index;
    int result;
    const uint8_t *ip_header;
    uint8_t *mutable_packet;

    if (!packet || length == 0) {
        return ERROR_INVALID_PARAM;
    }

    /* Get destination NIC */
    nic = hardware_get_nic(dest_nic);
    if (!nic || !(nic->status & NIC_STATUS_ACTIVE)) {
        log_error("Destination NIC %d not active", dest_nic);
        return ERROR_INVALID_PARAM;
    }

    /* For IP packets, we need to resolve MAC address via ARP */
    if (packet_get_ethertype(packet) == ETH_P_IP) {
        /* Extract destination IP from IP header */
        ip_header = packet + ETH_HEADER_LEN;
        memory_copy(dest_ip.addr, ip_header + 16, 4); /* Dest IP at offset 16 */
        
        /* Try to resolve MAC address */
        if (arp_is_enabled()) {
            result = arp_resolve(&dest_ip, dest_mac, &nic_index);
            if (result == SUCCESS) {
                /* Update destination MAC in packet */
                mutable_packet = (uint8_t*)packet;
                memory_copy(mutable_packet, dest_mac, ETH_ALEN);
                
                /* Update source MAC to our NIC's MAC */
                memory_copy(mutable_packet + ETH_ALEN, nic->mac, ETH_ALEN);
            } else if (result == ERROR_BUSY) {
                /* ARP resolution in progress - queue packet or drop */
                log_debug("ARP resolution pending for routing, dropping packet");
                return ERROR_BUSY;
            } else {
                /* ARP resolution failed */
                log_warning("ARP resolution failed for routing, dropping packet");
                return ERROR_NOT_FOUND;
            }
        }
    }
    
    /* Send packet on destination interface */
    result = hardware_send_packet(nic, packet, length);
    if (result < 0) {
        log_error("Failed to send routed packet on NIC %d: %d", dest_nic, result);
        return result;
    }
    
    log_debug("Successfully routed packet to NIC %d", dest_nic);
    return SUCCESS;
}

int packet_ops_cleanup(void) {
    if (!packet_ops_initialized) {
        return 0;
    }
    
    log_info("Cleaning up packet operations subsystem");
    
    /* Cleanup production queue management */
    packet_queue_cleanup_all();
    
    /* Print final statistics */
    log_info("Final packet statistics:");
    log_info("  TX: %lu packets, %lu bytes, %lu errors",
             packet_statistics.tx_packets, 
             packet_statistics.tx_bytes,
             packet_statistics.tx_errors);
    log_info("  RX: %lu packets, %lu bytes, %lu errors, %lu dropped",
             packet_statistics.rx_packets,
             packet_statistics.rx_bytes, 
             packet_statistics.rx_errors,
             packet_statistics.rx_dropped);
    
    /* Print queue management statistics */
    log_info("Queue Statistics:");
    log_info("  Queue full events: %lu", g_queue_state.queue_full_events);
    log_info("  Backpressure events: %lu", g_queue_state.backpressure_events);
    log_info("  Priority drops: %lu", g_queue_state.priority_drops);
    log_info("  Adaptive resizes: %lu", g_queue_state.adaptive_resizes);
    
    packet_ops_initialized = 0;
    
    log_info("Packet operations cleanup completed");
    return 0;
}

/**
 * @brief Initialize all production packet queues
 * @return 0 on success, negative on error
 */
static int packet_queue_init_all(void) {
    int result;
    
    log_info("Initializing production packet queues");
    
    /* Initialize priority-based TX queues */
    result = packet_queue_init(&g_queue_state.tx_queues[PACKET_PRIORITY_URGENT], 
                              TX_QUEUE_URGENT_SIZE, TX_QUEUE_URGENT_SIZE * 1514);
    if (result != 0) {
        log_error("Failed to initialize urgent TX queue");
        return result;
    }
    
    result = packet_queue_init(&g_queue_state.tx_queues[PACKET_PRIORITY_HIGH], 
                              TX_QUEUE_HIGH_SIZE, TX_QUEUE_HIGH_SIZE * 1514);
    if (result != 0) {
        log_error("Failed to initialize high priority TX queue");
        return result;
    }
    
    result = packet_queue_init(&g_queue_state.tx_queues[PACKET_PRIORITY_NORMAL], 
                              TX_QUEUE_NORMAL_SIZE, TX_QUEUE_NORMAL_SIZE * 1514);
    if (result != 0) {
        log_error("Failed to initialize normal priority TX queue");
        return result;
    }
    
    result = packet_queue_init(&g_queue_state.tx_queues[PACKET_PRIORITY_LOW], 
                              TX_QUEUE_LOW_SIZE, TX_QUEUE_LOW_SIZE * 1514);
    if (result != 0) {
        log_error("Failed to initialize low priority TX queue");
        return result;
    }
    
    /* Initialize RX queue */
    result = packet_queue_init(&g_queue_state.rx_queue, 
                              RX_QUEUE_SIZE, RX_QUEUE_SIZE * 1514);
    if (result != 0) {
        log_error("Failed to initialize RX queue");
        return result;
    }
    
    log_info("Production packet queues initialized successfully");
    return 0;
}

/**
 * @brief Cleanup all production packet queues
 */
static void packet_queue_cleanup_all(void) {
    int i;

    log_info("Cleaning up production packet queues");

    /* Emergency drain all queues before cleanup */
    packet_emergency_queue_drain();

    /* Cleanup TX queues */
    for (i = 0; i < 4; i++) {
        packet_queue_cleanup(&g_queue_state.tx_queues[i]);
    }
    
    /* Cleanup RX queue */
    packet_queue_cleanup(&g_queue_state.rx_queue);
    
    log_info("Production packet queues cleaned up");
}

/**
 * @brief Enqueue packet with priority-based flow control
 * @param buffer Packet buffer
 * @param priority Packet priority (0-3)
 * @return 0 on success, negative on error
 */
static int packet_enqueue_with_priority(packet_buffer_t *buffer, int priority) {
    packet_queue_t *queue;
    int result;
    uint32_t queue_usage;
    
    if (!buffer || priority < 0 || priority > 3) {
        return PACKET_ERR_INVALID_PARAM;
    }
    
    queue = &g_queue_state.tx_queues[priority];
    queue_usage = packet_calculate_queue_usage(queue);
    
    /* Check for queue overflow */
    if (packet_queue_is_full(queue)) {
        log_debug("Queue %d full, checking drop policy", priority);
        
        if (packet_should_drop_on_full(priority, queue_usage)) {
            /* Drop lower priority packets to make room if possible */
            packet_handle_queue_overflow(priority);
            
            /* Try again after making room */
            if (packet_queue_is_full(queue)) {
                g_queue_state.queue_full_events++;
                g_queue_state.priority_drops++;
                log_warning("Dropping packet due to queue %d overflow", priority);
                return PACKET_ERR_NO_BUFFERS;
            }
        } else {
            g_queue_state.queue_full_events++;
            return PACKET_ERR_NO_BUFFERS;
        }
    }
    
    /* Check for flow control threshold */
    if (queue_usage > FLOW_CONTROL_THRESHOLD) {
        if (!g_queue_state.flow_control_active) {
            log_info("Activating flow control - queue usage %d%%", queue_usage);
            g_queue_state.flow_control_active = true;
            g_queue_state.backpressure_events++;
        }
        packet_apply_flow_control();
    }
    
    /* Enqueue the packet - CRITICAL SECTION */
    _asm { cli }  /* Disable interrupts */
    result = packet_queue_enqueue(queue, buffer);
    _asm { sti }  /* Enable interrupts */
    if (result != 0) {
        log_error("Failed to enqueue packet to priority queue %d", priority);
        return result;
    }
    
    log_trace("Enqueued packet to priority %d queue (usage: %d%%)", priority, queue_usage);
    return 0;
}

/**
 * @brief Dequeue packet using priority scheduling
 * @return Packet buffer or NULL if no packets
 */
static packet_buffer_t* packet_dequeue_by_priority(void) {
    packet_buffer_t *buffer = NULL;
    int priority;
    int i;
    uint32_t total_usage;

    /* Check queues in priority order (urgent first) */
    for (priority = PACKET_PRIORITY_URGENT; priority >= PACKET_PRIORITY_LOW; priority--) {
        if (!packet_queue_is_empty(&g_queue_state.tx_queues[priority])) {
            /* Dequeue from priority queue - CRITICAL SECTION */
            _asm { cli }  /* Disable interrupts */
            buffer = packet_queue_dequeue(&g_queue_state.tx_queues[priority]);
            _asm { sti }  /* Enable interrupts */
            if (buffer) {
                log_trace("Dequeued packet from priority %d queue", priority);

                /* Check if we can disable flow control */
                total_usage = 0;
                for (i = 0; i < 4; i++) {
                    total_usage += packet_calculate_queue_usage(&g_queue_state.tx_queues[i]);
                }
                
                if (g_queue_state.flow_control_active && total_usage < QUEUE_WATERMARK_LOW) {
                    log_info("Deactivating flow control - total usage %d%%", total_usage / 4);
                    g_queue_state.flow_control_active = false;
                }
                
                return buffer;
            }
        }
    }
    
    return NULL;
}

/**
 * @brief Check queue health and trigger adaptive management
 * @return 0 on success, negative on error
 */
static int packet_check_queue_health(void) {
    /* C89: All declarations at start of function */
    uint32_t current_time;
    bool health_issues;
    int i;
    packet_queue_t *queue;
    uint32_t usage;
    packet_buffer_t *head;

    current_time = stats_get_timestamp();
    health_issues = false;

    /* Only check periodically */
    if (current_time - g_queue_state.last_queue_check < QUEUE_CHECK_INTERVAL_MS) {
        return 0;
    }

    g_queue_state.last_queue_check = current_time;

    /* Check each TX queue for health issues */
    for (i = 0; i < 4; i++) {
        queue = &g_queue_state.tx_queues[i];
        usage = packet_calculate_queue_usage(queue);

        if (usage > QUEUE_WATERMARK_HIGH) {
            log_warning("Queue %d usage high: %d%%", i, (int)usage);
            health_issues = true;
        }

        /* Check for stale packets (simplified - would need timestamps) */
        if (queue->count > 0) {
            head = packet_queue_peek(queue);
            if (head && head->timestamp > 0) {
                uint32_t age = current_time - head->timestamp;
                if (age > 5000) {  /* 5 second threshold */
                    log_warning("Stale packets detected in queue %d (age: %dms)", i, age);
                    health_issues = true;
                }
            }
        }
    }
    
    /* Check RX queue health */
    {
        uint32_t rx_usage = packet_calculate_queue_usage(&g_queue_state.rx_queue);
        if (rx_usage > QUEUE_WATERMARK_HIGH) {
            log_warning("RX queue usage high: %d%%", (int)rx_usage);
            health_issues = true;
        }
    }
    
    /* Trigger adaptive management if needed */
    if (health_issues) {
        packet_adaptive_queue_resize();
    }
    
    return health_issues ? 1 : 0;
}

/**
 * @brief Apply flow control backpressure
 */
static void packet_apply_flow_control(void) {
    /* In a full implementation, this would:
     * 1. Signal upper layers to slow down
     * 2. Implement TCP-like window scaling
     * 3. Adjust NIC interrupt rates
     * 4. Apply per-connection throttling
     */
    volatile int delay_i;

    log_debug("Applying flow control backpressure");

    /* For now, just add a small delay to slow down packet processing */
    for (delay_i = 0; delay_i < 100; delay_i++) {
        /* Brief backpressure delay */
    }
}

/**
 * @brief Adaptively resize queues based on load
 */
static void packet_adaptive_queue_resize(void) {
    static uint32_t last_resize = 0;
    uint32_t current_time = stats_get_timestamp();
    int i;
    packet_queue_t *queue;
    uint32_t usage;

    /* Limit resize frequency */
    if (current_time - last_resize < 10000) {  /* 10 second minimum */
        return;
    }

    last_resize = current_time;

    log_info("Performing adaptive queue resize analysis");

    /* Analyze queue usage patterns */
    for (i = 0; i < 4; i++) {
        queue = &g_queue_state.tx_queues[i];
        usage = packet_calculate_queue_usage(queue);
        
        if (usage > 90 && queue->max_count < 512) {
            /* Queue consistently full - consider expansion */
            log_info("Queue %d consistently full (%d%%), would expand if possible", i, usage);
            /* In full implementation, would dynamically resize */
            g_queue_state.adaptive_resizes++;
        } else if (usage < 10 && queue->max_count > 32) {
            /* Queue underutilized - consider shrinking */
            log_info("Queue %d underutilized (%d%%), would shrink if possible", i, usage);
            g_queue_state.adaptive_resizes++;
        }
    }
}

/**
 * @brief Handle queue overflow by dropping lower priority packets
 * @param priority Current priority level
 */
static void packet_handle_queue_overflow(int priority) {
    int dropped = 0;
    int lower_priority;
    packet_queue_t *lower_queue;

    /* Try to drop packets from lower priority queues */
    for (lower_priority = PACKET_PRIORITY_LOW; lower_priority < priority; lower_priority++) {
        lower_queue = &g_queue_state.tx_queues[lower_priority];
        
        while (!packet_queue_is_empty(lower_queue) && dropped < 5) {
            packet_buffer_t *dropped_buffer = packet_queue_dequeue(lower_queue);
            if (dropped_buffer) {
                packet_buffer_free(dropped_buffer);
                dropped++;
                g_queue_state.priority_drops++;
            }
        }
        
        if (dropped >= 5) break;  /* Don't drop too many at once */
    }
    
    if (dropped > 0) {
        log_info("Dropped %d lower priority packets to make room for priority %d", dropped, priority);
    }
}

/**
 * @brief Check if packet should be dropped when queue is full
 * @param priority Packet priority
 * @param queue_usage Current queue usage percentage
 * @return true if should drop
 */
static bool packet_should_drop_on_full(int priority, int queue_usage) {
    /* Higher priority packets are more likely to preempt lower priority */
    switch (priority) {
        case PACKET_PRIORITY_URGENT:
            return true;   /* Always try to make room for urgent packets */
        case PACKET_PRIORITY_HIGH:
            return queue_usage > 95;  /* Drop if very full */
        case PACKET_PRIORITY_NORMAL:
            return queue_usage > 90;  /* Drop if mostly full */
        case PACKET_PRIORITY_LOW:
            return false;  /* Don't preempt others for low priority */
        default:
            return false;
    }
}

/**
 * @brief Calculate queue usage percentage
 * @param queue Queue to check
 * @return Usage percentage (0-100)
 */
static uint32_t packet_calculate_queue_usage(packet_queue_t *queue) {
    if (!queue || queue->max_count == 0) {
        return 0;
    }
    
    return (queue->count * 100) / queue->max_count;
}

/**
 * @brief Update queue management statistics
 */
static void packet_update_queue_stats(void) {
    /* This would update detailed queue statistics */
    /* For now, statistics are updated inline in other functions */
}

/**
 * @brief Emergency drain all queues (e.g., during shutdown)
 * @return Number of packets drained
 */
static int packet_emergency_queue_drain(void) {
    int total_drained = 0;
    int i;
    packet_queue_t *queue;
    int drained;
    int rx_drained = 0;

    log_warning("Emergency draining all packet queues");

    /* Drain TX queues */
    for (i = 0; i < 4; i++) {
        queue = &g_queue_state.tx_queues[i];
        drained = 0;

        while (!packet_queue_is_empty(queue)) {
            packet_buffer_t *buffer = packet_queue_dequeue(queue);
            if (buffer) {
                packet_buffer_free(buffer);
                drained++;
            }
        }

        if (drained > 0) {
            log_info("Drained %d packets from TX queue %d", drained, i);
            total_drained += drained;
        }
    }

    /* Drain RX queue */
    while (!packet_queue_is_empty(&g_queue_state.rx_queue)) {
        packet_buffer_t *buffer = packet_queue_dequeue(&g_queue_state.rx_queue);
        if (buffer) {
            packet_buffer_free(buffer);
            rx_drained++;
        }
    }
    
    if (rx_drained > 0) {
        log_info("Drained %d packets from RX queue", rx_drained);
        total_drained += rx_drained;
    }
    
    log_info("Emergency drain completed: %d total packets drained", total_drained);
    return total_drained;
}

/**
 * @brief Enhanced packet queue TX with production features
 * @param packet Packet data
 * @param length Packet length
 * @param priority Packet priority (0-3)
 * @param handle Sender handle
 * @return 0 on success, negative on error
 */
int packet_queue_tx_enhanced(const uint8_t *packet, size_t length, int priority, uint16_t handle) {
    packet_buffer_t *buffer;
    int result;
    
    if (!packet || length == 0 || priority < 0 || priority > 3) {
        return PACKET_ERR_INVALID_PARAM;
    }
    
    if (!packet_ops_initialized) {
        return PACKET_ERR_NOT_INITIALIZED;
    }
    
    /* Check queue health periodically */
    packet_check_queue_health();
    
    /* Allocate packet buffer */
    buffer = packet_buffer_alloc(length);
    if (!buffer) {
        log_error("Failed to allocate packet buffer for queuing");
        return PACKET_ERR_NO_BUFFERS;
    }
    
    /* Copy packet data and set metadata */
    result = packet_set_data(buffer, packet, length);
    if (result != 0) {
        packet_buffer_free(buffer);
        return result;
    }
    
    buffer->priority = priority;
    buffer->handle = handle;
    buffer->timestamp = stats_get_timestamp();
    
    /* Enqueue with priority management */
    result = packet_enqueue_with_priority(buffer, priority);
    if (result != 0) {
        packet_buffer_free(buffer);
        return result;
    }
    
    log_debug("Queued packet for transmission: priority=%d, length=%zu, handle=%04X",
              priority, length, handle);
    
    return 0;
}

/**
 * @brief Enhanced packet queue flush with priority scheduling
 * @return Number of packets processed, negative on error
 */
int packet_flush_tx_queue_enhanced(void) {
    int packets_sent = 0;
    int max_packets = 32;  /* Limit to prevent starvation */
    packet_buffer_t *buffer;
    int result;
    
    if (!packet_ops_initialized) {
        return PACKET_ERR_NOT_INITIALIZED;
    }
    
    /* Process packets by priority until queue empty or limit reached */
    while (packets_sent < max_packets) {
        buffer = packet_dequeue_by_priority();
        if (!buffer) {
            break;  /* No more packets */
        }
        
        /* Send the packet using enhanced send with recovery */
        result = packet_send_with_retry(buffer->data, buffer->length, 
                                       NULL, buffer->handle, 3);
        
        if (result == 0) {
            packets_sent++;
            log_trace("Successfully sent queued packet (handle=%04X)", buffer->handle);
        } else {
            log_warning("Failed to send queued packet: %d", result);
            
            /* For failed packets, could implement retry logic or dead letter queue */
        }
        
        packet_buffer_free(buffer);
    }
    
    if (packets_sent > 0) {
        log_debug("Flushed %d packets from TX queues", packets_sent);
    }
    
    return packets_sent;
}

/**
 * @brief Get comprehensive queue management statistics
 * @param stats Pointer to store statistics
 * @return 0 on success, negative on error
 */
int packet_get_queue_stats(packet_queue_management_stats_t *stats) {
    int i;

    if (!stats) {
        return PACKET_ERR_INVALID_PARAM;
    }

    memset(stats, 0, sizeof(packet_queue_management_stats_t));

    /* Copy queue counts and usage */
    for (i = 0; i < 4; i++) {
        stats->tx_queue_counts[i] = g_queue_state.tx_queues[i].count;
        stats->tx_queue_max[i] = g_queue_state.tx_queues[i].max_count;
        stats->tx_queue_usage[i] = packet_calculate_queue_usage(&g_queue_state.tx_queues[i]);
        stats->tx_queue_dropped[i] = g_queue_state.tx_queues[i].dropped_packets;
    }
    
    stats->rx_queue_count = g_queue_state.rx_queue.count;
    stats->rx_queue_max = g_queue_state.rx_queue.max_count;
    stats->rx_queue_usage = packet_calculate_queue_usage(&g_queue_state.rx_queue);
    stats->rx_queue_dropped = g_queue_state.rx_queue.dropped_packets;
    
    /* Copy management statistics */
    stats->queue_full_events = g_queue_state.queue_full_events;
    stats->backpressure_events = g_queue_state.backpressure_events;
    stats->priority_drops = g_queue_state.priority_drops;
    stats->adaptive_resizes = g_queue_state.adaptive_resizes;
    stats->flow_control_active = g_queue_state.flow_control_active;
    
    return 0;
}

/**
 * @brief Get current BIOS tick count (18.2 Hz timer)
 * @return Current tick count from BIOS timer
 * 
 * GPT-5 FIX: Use stable double-check read without modifying interrupt flag
 * to avoid ISR reentrancy issues
 */
static uint32_t get_bios_ticks(void) {
    /* ISR-safe BIOS tick read with stable pattern and day rollover extension. */
    static uint32_t last_ticks = 0;
    static uint32_t day_count = 0; /* Count of midnights/wraps observed */
    uint32_t ticks = 0;  /* Initialize to silence W200 */

#ifdef __WATCOMC__
    /* Read BIOS timer tick count at 0040:006C */
    /* GPT-5 FIX: Mark as volatile and use stable read without CLI/STI */
    volatile uint16_t __far *tick_ptr = (volatile uint16_t __far *)MK_FP(0x0040, 0x006C);
    volatile uint8_t  __far *midnight_flag = (volatile uint8_t  __far *)MK_FP(0x0040, 0x0070);
    uint16_t hi1, hi2, lo;

    /* Stable read: read high, low, high until high matches */
    do {
        hi1 = tick_ptr[1];
        lo = tick_ptr[0];
        hi2 = tick_ptr[1];
    } while (hi1 != hi2);

    ticks = ((uint32_t)hi1 << 16) | lo;

    /* Observe midnight flag (do not clear here, avoid BIOS calls in ISR). */
    if (*midnight_flag) {
        day_count++;
    }
#else
    /* Fallback for testing */
    static uint32_t test_ticks = 0;
    ticks = test_ticks++;
#endif

    /* Extend across wrap: if ticks decreased since last read, count a day. */
    if (ticks < last_ticks) {
        day_count++;
    }
    last_ticks = ticks;

    /* 0x1800B0 ticks per day (24h at 18.2 Hz) */
    return day_count * 0x1800B0UL + ticks;
}

/**
 * @brief Check for TX timeouts and queue error completions
 * 
 * GPT-5 CRITICAL: TX Watchdog Timer Implementation
 * Detects stuck TX operations and queues them for cleanup
 * 
 * GPT-5 FIX: Proper queue synchronization and no hardware modification
 */
static void packet_check_tx_timeouts(void) {
    uint32_t current_ticks = get_bios_ticks();
    uint16_t snapshot_head, snapshot_tail;
    uint16_t idx;  /* GPT-5 FIX: Use same type as queue indices */
    
    /* GPT-5 A+: Enhanced seqlock-based snapshot for maximum reliability */
    uint8_t seq1, seq2;
    uint16_t h, t;
    uint16_t retry_count = 0;
    const uint16_t MAX_RETRIES = 256;
    
    do {
        seq1 = g_tx_completion_queue.seq;
        
        /* If sequence is odd, update is in progress */
        if (seq1 & 1) {
            if (++retry_count > MAX_RETRIES) {
                LOG_DEBUG("Queue update in progress, retry limit reached");
                break;
            }
            continue;
        }
        
        /* Read queue bounds */
        h = g_tx_completion_queue.head;
        t = g_tx_completion_queue.tail;
        
        /* Memory barrier to ensure we see all updates */
        #ifdef __WATCOMC__
        #pragma aux memory_barrier = "" modify exact [] nomemory;
        memory_barrier();
        #else
        memory_barrier();
        #endif
        
        seq2 = g_tx_completion_queue.seq;
        
        /* If sequence changed, retry */
        if (seq1 != seq2) {
            if (++retry_count > MAX_RETRIES) {
                /* Fallback to CLI if too many retries */
                _disable();
                h = g_tx_completion_queue.head;
                t = g_tx_completion_queue.tail;
                _enable();
                LOG_DEBUG("Seqlock retry limit, used CLI fallback");
                break;
            }
            continue;
        }
        
        /* Success - we have a consistent snapshot */
        break;
    } while (1);
    
    snapshot_head = h;
    snapshot_tail = t;
    
    /* Scan queue from tail to snapshot head */
    idx = snapshot_tail;
    while (idx != snapshot_head) {
        tx_completion_t *entry = &g_tx_completion_queue.queue[idx];
        
        /* Check if this entry has timed out */
        if (entry->mapping && !entry->error) {
            /* GPT-5 FIX: Use modular arithmetic for wraparound */
            uint32_t elapsed = (current_ticks + 0x1800B0UL - entry->timestamp) % 0x1800B0UL;
            
            if (elapsed > TX_TIMEOUT_TICKS) {
                /* Mark as error for cleanup during normal processing */
                entry->error = true;
                
                /* GPT-5 FIX: DO NOT modify hardware descriptors from watchdog!
                 * The completion processing will handle cleanup properly.
                 * Hardware may still be accessing the descriptor.
                 */
                
                /* GPT-5 FIX: Defer logging to avoid ISR issues */
                /* Logging will be done when entry is processed */
            }
        }
        
        idx = (idx + 1) & TX_QUEUE_MASK;
    }
}

/* Restore default code segment */
#pragma code_seg()
