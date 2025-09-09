/**
 * @file rx_batch_refill.c
 * @brief Batched RX buffer refill optimization
 * 
 * Implements batched RX buffer replenishment to reduce doorbell writes.
 * Based on DRIVER_TUNING.md specifications - single UP_LIST_PTR write
 * for multiple buffer refills.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "3com_pci.h"
#include "logging.h"
#include "memory.h"

/* Configuration from DRIVER_TUNING.md */
#define RX_RING_SIZE          32      /* RX descriptor ring size */
#define RX_RING_MASK          (RX_RING_SIZE - 1)
#define RX_REFILL_THRESHOLD   8       /* Refill when this many empty */
#define RX_BUDGET            32       /* Max packets per worker run */
#define COPY_BREAK_THRESHOLD  192     /* Copy vs flip threshold */

/* Boomerang/Cyclone/Tornado UP_LIST_PTR register */
#define UP_LIST_PTR          0x38     /* Upload (RX) list pointer */

/* RX descriptor bits */
#define RX_OWN_BIT           0x80000000  /* NIC owns descriptor */
#define RX_COMPLETE          0x00008000  /* RX complete */
#define RX_ERROR             0x00004000  /* RX error */

/* RX descriptor structure (Boomerang/Cyclone/Tornado format) */
typedef struct {
    uint32_t next;           /* Next descriptor physical address (0 for end-of-list) */
    uint32_t status;         /* Status and packet length (upper 16: length, lower 16: flags) */
    uint32_t buf_addr;       /* Buffer physical address (must not cross 64KB boundary) */
    uint32_t buf_len;        /* Buffer length (typically 1536 for Ethernet) */
} rx_desc_t;

/* RX batch state per NIC */
typedef struct {
    /* Ring management */
    rx_desc_t *ring;         /* Virtual address of ring */
    uint32_t ring_phys;      /* Physical address of ring */
    uint16_t head;           /* Producer index (NIC writes) */
    uint16_t tail;           /* Consumer index (driver reads) - acts as scan pointer */
    uint16_t available;      /* Number of filled descriptors */
    uint16_t io_base;        /* I/O base address */
    
    /* Work queue */
    volatile uint8_t work_pending;  /* Work flag from ISR */
    
    /* Buffer tracking - parallel arrays to descriptors */
    void __far *buffer_virt[RX_RING_SIZE];  /* Virtual pointers for CPU access */
    
    /* Buffer pools */
    void *small_pool;        /* UMB pool for copy-break */
    void *large_pool;        /* Conventional memory pool */
    
    /* Statistics */
    uint32_t total_packets;
    uint32_t copy_break_count;
    uint32_t bulk_refills;
    uint32_t doorbell_writes;
    uint32_t last_published_tail;
} rx_batch_state_t;

/* Per-NIC RX batch state */
static rx_batch_state_t rx_state[MAX_NICS] = {0};

/**
 * @brief Tiny ISR implementation (15 instructions max)
 * 
 * This is the actual interrupt handler - just ACK and mark work.
 * Based on DRIVER_TUNING.md specification.
 */
void __interrupt __far rx_batch_isr(void) {
    uint16_t io_base = rx_state[0].io_base;  /* TODO: Multi-NIC support */
    
    /* ACK interrupt at NIC */
    outw(io_base + 0x0E, 0x6001);  /* IntStatus - ACK RX */
    
    /* Mark work pending */
    rx_state[0].work_pending = 1;
    
    /* EOI to PIC */
    outb(0x20, 0x20);  /* Master PIC EOI */
}

/**
 * @brief Initialize RX batch refill for a NIC
 */
int rx_batch_init(uint8_t nic_index, uint16_t io_base, void *ring_virt,
                   uint32_t ring_phys) {
    rx_batch_state_t *state;
    int i;
    
    if (nic_index >= MAX_NICS) {
        return -1;
    }
    
    state = &rx_state[nic_index];
    
    /* Initialize ring pointers */
    state->ring = (rx_desc_t *)ring_virt;
    state->ring_phys = ring_phys;
    state->io_base = io_base;
    state->head = 0;
    state->tail = 0;
    state->available = 0;
    state->work_pending = 0;
    
    /* Allocate buffer pools */
    /* Small pool in UMB for copy-break (4 * 256 bytes) */
    state->small_pool = alloc_umb_memory(4 * 256);
    if (!state->small_pool) {
        /* Fall back to conventional */
        state->small_pool = alloc_conventional_memory(4 * 256);
    }
    
    /* Large pool in conventional (32 * 1536 bytes) */
    state->large_pool = alloc_conventional_memory(RX_RING_SIZE * 1536);
    if (!state->large_pool) {
        LOG_ERROR("Failed to allocate RX buffer pool");
        return -1;
    }
    
    /* Initialize all descriptors with buffers */
    for (i = 0; i < RX_RING_SIZE; i++) {
        uint32_t buf_phys = (uint32_t)state->large_pool + (i * 1536);
        
        state->ring[i].next = state->ring_phys + 
                             ((i + 1) % RX_RING_SIZE) * sizeof(rx_desc_t);
        state->ring[i].status = RX_OWN_BIT;
        state->ring[i].buf_addr = buf_phys;
        state->ring[i].buf_len = 1536;
    }
    
    /* Write initial UP_LIST_PTR */
    outl(io_base + UP_LIST_PTR, ring_phys);
    state->doorbell_writes++;
    state->last_published_tail = 0;
    
    LOG_INFO("RX batch refill initialized for NIC %d", nic_index);
    return 0;
}

/**
 * @brief Allocate RX buffer with proper physical/virtual addresses
 */
static int alloc_rx_buffer_proper(rx_batch_state_t *state, uint16_t size,
                                  uint32_t *phys_addr, void __far **virt_ptr) {
    /* Use the proper buffer management with physical/virtual separation */
    extern int rx_buffer_alloc(uint8_t nic_index, uint16_t size,
                               uint32_t *phys_addr, void __far **virt_ptr);
    
    /* Determine NIC index from state pointer */
    uint8_t nic_index = (state - rx_state) / sizeof(rx_batch_state_t);
    
    return rx_buffer_alloc(nic_index, size, phys_addr, virt_ptr);
}

/**
 * @brief Bulk RX buffer refill
 * 
 * Refills multiple RX descriptors and writes UP_LIST_PTR once.
 * Based on DRIVER_TUNING.md rx_bulk_refill().
 */
static void rx_bulk_refill(rx_batch_state_t *state) {
    int refilled = 0;
    uint16_t first_refill_idx = 0xFFFF;
    uint32_t new_phys;
    void __far *new_virt;
    uint8_t nic_index = (state - rx_state) / sizeof(rx_batch_state_t);
    
    /* Use running counter instead of O(N) scan */
    uint16_t free_count = RX_RING_SIZE - state->available;
    
    if (free_count < RX_REFILL_THRESHOLD) {
        return;  /* Not enough empty slots */
    }
    
    /* Allocate buffers in batch */
    while (refilled < free_count && refilled < RX_REFILL_THRESHOLD * 2) {
        rx_desc_t *desc = &state->ring[state->tail];
        
        /* Check if we own this descriptor */
        if (desc->status & RX_OWN_BIT) {
            state->tail = (state->tail + 1) & RX_RING_MASK;
            continue;  /* Skip NIC-owned descriptors */
        }
        
        /* Allocate new buffer PER ITERATION - FIXED! */
        if (alloc_rx_buffer_proper(state, 1536, &new_phys, &new_virt) != 0) {
            break;  /* Allocation failed */
        }
        
        /* PRE-DMA SAFETY PATCH POINT */
        __asm__ volatile ("nop\nnop\nnop" ::: "memory"); /* SMC patch site #1 */
        
        /* Update descriptor with newly allocated buffer */
        desc->buf_addr = new_phys;
        desc->buf_len = 1536;
        
        /* Track virtual pointer for later CPU access/freeing */
        state->buffer_virt[state->tail] = new_virt;
        
        /* Update next pointer to maintain linked list */
        /* Cast to uint32_t to avoid 16-bit overflow in calculation */
        uint32_t next_offset = (uint32_t)(((state->tail + 1) & RX_RING_MASK) * sizeof(rx_desc_t));
        desc->next = state->ring_phys + next_offset;
        
        /* Set all required control bits before giving to NIC */
        /* Clear any previous status and set ownership */
        desc->status = RX_OWN_BIT | 0;  /* Add other control bits if NIC requires */
        
        /* Remember first refilled descriptor */
        if (first_refill_idx == 0xFFFF) {
            first_refill_idx = state->tail;
        }
        
        state->tail = (state->tail + 1) & RX_RING_MASK;
        state->available++;
        refilled++;
    }
    
    if (refilled > 0 && first_refill_idx != 0xFFFF) {
        /* POST-DMA SAFETY PATCH POINT */
        __asm__ volatile ("nop\nnop\nnop" ::: "memory"); /* SMC patch site #2 */
        
        /* Single doorbell write pointing to first new descriptor */
        /* Cast to avoid 16-bit overflow */
        uint32_t doorbell_offset = (uint32_t)(first_refill_idx * sizeof(rx_desc_t));
        uint32_t doorbell_addr = state->ring_phys + doorbell_offset;
        
        /* Use two 16-bit writes for 32-bit value */
        outw(state->io_base + UP_LIST_PTR, (uint16_t)doorbell_addr);
        outw(state->io_base + UP_LIST_PTR + 2, (uint16_t)(doorbell_addr >> 16));
        
        state->doorbell_writes++;
        state->last_published_tail = state->tail;
        state->bulk_refills++;
    }
}

/**
 * @brief RX worker - bottom half processing
 * 
 * Processes received packets with NAPI-style drain-until-empty.
 * Optimized for 80kpps throughput target.
 */
void rx_batch_worker(uint8_t nic_index) {
    rx_batch_state_t *state;
    uint16_t processed = 0;
    uint16_t batch_processed;
    uint8_t loops = 0;
    const uint8_t MAX_LOOPS = 4;  /* Prevent starvation */
    
    if (nic_index >= MAX_NICS) {
        return;
    }
    
    state = &rx_state[nic_index];
    
    /* NAPI-style: drain ring until empty or max loops */
    while (state->work_pending && loops < MAX_LOOPS) {
        rx_desc_t *desc;
        uint16_t len;
        
        state->work_pending = 0;  /* Clear flag early */
        batch_processed = 0;
        
        /* Process all available descriptors (not just budget) */
        while (1) {
            desc = &state->ring[state->head];
            
            /* Check if descriptor is ready */
            if (desc->status & RX_OWN_BIT) {
                break;  /* NIC still owns it */
            }
            
            /* Check for errors */
            if (desc->status & RX_ERROR) {
                LOG_DEBUG("RX error in descriptor %d", state->head);
                desc->status = RX_OWN_BIT;  /* Recycle */
                state->head = (state->head + 1) & RX_RING_MASK;
                continue;
            }
            
            /* Extract packet length */
            len = (desc->status >> 16) & 0x1FFF;
            
            /* POST-RX CACHE SAFETY PATCH POINT */
            __asm__ volatile ("nop\nnop\nnop" ::: "memory"); /* SMC patch site #3 */
            
            /* Use tracked virtual pointer for CPU access */
            void __far *buf_virt = state->buffer_virt[state->head];
            
            if (!buf_virt) {
                /* Buffer not tracked - fallback to lookup */
                extern void __far *rx_buffer_phys_to_virt(uint8_t nic_index, uint32_t phys_addr);
                uint8_t nic_index = (state - rx_state) / sizeof(rx_batch_state_t);
                buf_virt = rx_buffer_phys_to_virt(nic_index, desc->buf_addr);
                
                if (!buf_virt) {
                    /* Buffer not found - skip packet */
                    desc->status = RX_OWN_BIT;
                    state->head = (state->head + 1) & RX_RING_MASK;
                    continue;
                }
            }
            
            if (len <= COPY_BREAK_THRESHOLD) {
                /* Small packet - copy to small buffer and recycle immediately */
                uint32_t small_phys;
                void __far *small_virt;
                
                if (alloc_rx_buffer_proper(state, 256, &small_phys, &small_virt) == 0) {
                    /* Copy data using far pointers */
                    _fmemcpy(small_virt, buf_virt, len);
                    
                    /* Deliver small packet */
                    deliver_packet(small_virt, len);
                    
                    /* Recycle descriptor immediately with same buffer */
                    desc->status = RX_OWN_BIT;
                    state->copy_break_count++;
                }
            } else {
                /* Large packet - hand off buffer and allocate new */
                uint32_t new_phys;
                void __far *new_virt;
                
                /* Deliver current buffer */
                deliver_packet(buf_virt, len);
                
                /* Allocate replacement buffer */
                if (alloc_rx_buffer_proper(state, 1536, &new_phys, &new_virt) == 0) {
                    desc->buf_addr = new_phys;  /* Use physical for NIC */
                    desc->status = RX_OWN_BIT;
                } else {
                    /* Allocation failed - mark descriptor as unavailable */
                    desc->status = 0;
                }
            }
            
            state->head = (state->head + 1) & RX_RING_MASK;
            state->available--;  /* Decrement available count */
            state->total_packets++;
            processed++;
            batch_processed++;
            
            /* Yield after processing many packets to prevent starvation */
            if (batch_processed >= 64) {
                break;  /* Give other NICs a chance */
            }
        }
        
        /* Bulk refill after each batch */
        if (batch_processed > 0) {
            rx_bulk_refill(state);
        }
        
        /* Check if more work arrived while processing */
        if (!state->work_pending) {
            break;  /* No more work */
        }
        
        loops++;
    }
    
    /* Final refill if we processed anything */
    if (processed > 0) {
        rx_bulk_refill(state);
    }
    
    /* Performance counter update (no logging in hot path) */
}

/**
 * @brief Get RX batch statistics
 */
void rx_batch_get_stats(uint8_t nic_index, rx_batch_stats_t *stats) {
    rx_batch_state_t *state;
    
    if (nic_index >= MAX_NICS || !stats) {
        return;
    }
    
    state = &rx_state[nic_index];
    
    stats->total_packets = state->total_packets;
    stats->copy_break_count = state->copy_break_count;
    stats->bulk_refills = state->bulk_refills;
    stats->doorbell_writes = state->doorbell_writes;
    
    /* Calculate efficiency metrics */
    if (state->total_packets > 0) {
        stats->copy_break_percent = 
            (state->copy_break_count * 100) / state->total_packets;
    } else {
        stats->copy_break_percent = 0;
    }
    
    if (state->doorbell_writes > 0) {
        stats->packets_per_doorbell = 
            state->total_packets / state->doorbell_writes;
    } else {
        stats->packets_per_doorbell = 0;
    }
}

/**
 * @brief Process single RX packet (for non-batched mode)
 */
int rx_batch_process_single(uint8_t nic_index) {
    rx_batch_state_t *state;
    rx_desc_t *desc;
    
    if (nic_index >= MAX_NICS) {
        return -1;
    }
    
    state = &rx_state[nic_index];
    desc = &state->ring[state->head];
    
    /* Check if descriptor is ready */
    if (desc->status & RX_OWN_BIT) {
        return 0;  /* No packet available */
    }
    
    /* Mark work and call worker */
    state->work_pending = 1;
    rx_batch_worker(nic_index);
    
    return 1;  /* Processed */
}

/* External packet delivery function (defined elsewhere) */
extern void deliver_packet(void *buf, uint16_t len);

/* Memory allocation helpers (defined elsewhere) */
extern void* alloc_umb_memory(uint32_t size);
extern void* alloc_conventional_memory(uint32_t size);