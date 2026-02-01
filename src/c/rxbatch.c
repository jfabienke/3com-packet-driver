/**
 * @file rx_batch_refill.c
 * @brief Batched RX buffer refill optimization
 *
 * Implements batched RX buffer replenishment to reduce doorbell writes.
 * Single UP_LIST_PTR write for multiple buffer refills.
 */

#include <string.h>
#include <dos.h>
#include "common.h"
#include "hardware.h"
#include "logging.h"
#include "bufpool.h"
#include "dmamap.h"
#include "vds.h"
#include "barrier.h"

/* Configuration parameters */
#define RX_RING_SIZE          32      /* RX descriptor ring size */
#define RX_RING_MASK          (RX_RING_SIZE - 1)
#define RX_REFILL_THRESHOLD   8       /* Refill when this many empty */
#define RX_BUDGET            16       /* Max packets per refill batch */
#define COPY_BREAK_THRESHOLD  256     /* Copy vs flip threshold (runtime configurable) */

/* Hardware registers */
#define UP_LIST_PTR          0x38     /* Upload (RX) list pointer */
#define UP_PKT_STATUS        0x30     /* Upload packet status */

/* RX descriptor bits */
#define RX_OWN_BIT           0x80000000UL  /* NIC owns descriptor */
#define RX_COMPLETE          0x00008000UL  /* RX complete */
#define RX_ERROR             0x00004000UL  /* RX error */

/* Get runtime copy-break threshold */
extern uint16_t g_copy_break_threshold;

/**
 * RX descriptor structure (3C515/Boomerang format)
 */
typedef struct {
    uint32_t next;           /* Next descriptor physical address */
    uint32_t status;         /* Status and packet length */
    uint32_t buf_addr;       /* Buffer physical address */
    uint32_t buf_len;        /* Buffer length (1536 for Ethernet) */
} rx_desc_t;

/**
 * RX batch state per NIC
 */
typedef struct {
    /* Ring management */
    rx_desc_t *ring;         /* Virtual address of ring */
    uint32_t ring_phys;      /* Physical address of ring */
    uint16_t head;           /* Producer index (driver fills) */
    uint16_t tail;           /* Consumer index (NIC fills) */
    uint16_t available;      /* Number of filled descriptors */
    uint16_t io_base;        /* I/O base address */
    uint8_t nic_index;       /* NIC index */
    bool enabled;            /* Batch refill enabled */
    
    /* Buffer tracking */
    void far *buffer_virt[RX_RING_SIZE];  /* Virtual pointers */
    uint32_t buffer_phys[RX_RING_SIZE];   /* Physical addresses */
    uint16_t buffer_size[RX_RING_SIZE];   /* Buffer sizes */
    
    /* Statistics */
    uint32_t total_packets;
    uint32_t copy_break_count;
    uint32_t bulk_refills;
    uint32_t doorbell_writes;
    uint32_t empty_events;
    uint32_t boundary_avoided;
    uint32_t boundary_retry_exhausted;
    uint16_t last_refill_count;
} rx_batch_state_t;

/* Per-NIC RX batch state */
static rx_batch_state_t g_rx_state[MAX_NICS];
static bool g_rx_batch_initialized = false;

/**
 * Check if physical address crosses 64KB boundary
 */
static int phys_crosses_64k(uint32_t phys, uint16_t len) {
    /* ISA DMA cannot cross 64KB segment boundaries */
    return ((phys & 0xFFFFU) + (uint32_t)len) > 0x10000U;
}

/**
 * Allocate DMA-safe buffer that doesn't cross 64KB boundary
 */
static void far* rx_alloc_64k_safe(uint16_t len, uint32_t *phys_out) {
    void far *buf;
    uint32_t phys;
    int attempts = 0;
    
    do {
        buf = buffer_alloc_rx(len);
        if (!buf) {
            return NULL;
        }
        
        phys = dma_get_physical_addr(buf);
        if (phys == 0) {
            buffer_free(buf);
            return NULL;
        }
        
        if (!phys_crosses_64k(phys, len)) {
            if (attempts > 0) {
                /* Track that we avoided a boundary crossing */
                g_rx_state[0].boundary_avoided++;
            }
            *phys_out = phys;
            return buf;
        }
        
        /* Buffer crosses boundary, try again */
        buffer_free(buf);
        attempts++;
        
    } while (attempts < 16);
    
    /* Failed to get safe buffer after 16 attempts */
    return NULL;
}

/**
 * Initialize RX batch refill for a NIC
 */
int rx_batch_init(uint8_t nic_index, uint16_t io_base) {
    rx_batch_state_t *state;
    nic_info_t *nic;
    int i;
    
    if (nic_index >= MAX_NICS) {
        return -1;
    }
    
    nic = hardware_get_nic(nic_index);
    if (!nic) {
        return -1;
    }
    
    /* Only for bus mastering NICs */
    if (nic->type != NIC_TYPE_3C515_TX) {
        LOG_DEBUG("RX batch refill not supported for NIC type %d", nic->type);
        return -1;
    }
    
    state = &g_rx_state[nic_index];
    memset(state, 0, sizeof(rx_batch_state_t));
    
    state->nic_index = nic_index;
    state->io_base = io_base;
    
    /* Allocate RX ring in conventional memory (DMA-safe) */
    state->ring = (rx_desc_t *)buffer_alloc_dma_safe(sizeof(rx_desc_t) * RX_RING_SIZE);
    if (!state->ring) {
        LOG_ERROR("Failed to allocate RX ring");
        return -1;
    }
    
    /* Get physical address of ring */
    state->ring_phys = dma_get_physical_addr(state->ring);
    if (state->ring_phys == 0) {
        LOG_ERROR("Failed to get physical address of RX ring");
        buffer_free(state->ring);
        return -1;
    }
    
    /* Initialize descriptors */
    memset(state->ring, 0, sizeof(rx_desc_t) * RX_RING_SIZE);
    
    /* Link descriptors in a ring */
    for (i = 0; i < RX_RING_SIZE - 1; i++) {
        state->ring[i].next = state->ring_phys + ((i + 1) * sizeof(rx_desc_t));
    }
    state->ring[RX_RING_SIZE - 1].next = state->ring_phys;  /* Wrap to start */
    
    /* Pre-allocate buffers with 64KB boundary checking */
    for (i = 0; i < RX_RING_SIZE; i++) {
        uint32_t phys;
        void far *buf = rx_alloc_64k_safe(1536, &phys);
        if (!buf) {
            LOG_ERROR("Failed to allocate 64KB-safe RX buffer %d", i);
            break;
        }
        
        state->buffer_virt[i] = buf;
        state->buffer_phys[i] = phys;
        state->buffer_size[i] = 1536;
        
        /* Initialize descriptor */
        state->ring[i].buf_addr = state->buffer_phys[i];
        state->ring[i].buf_len = 1536;
        state->ring[i].status = RX_OWN_BIT;  /* Give to NIC */
    }
    
    state->available = i;  /* Number of buffers allocated */
    state->enabled = true;
    
    /* Program UP_LIST_PTR to start of ring */
    outl(io_base + UP_LIST_PTR, state->ring_phys);
    
    LOG_INFO("RX batch refill initialized for NIC %d: %u buffers", 
             nic_index, state->available);
    
    return 0;
}

/**
 * Check if RX ring needs refill
 */
static bool rx_needs_refill(rx_batch_state_t *state) {
    uint16_t empty_slots = 0;
    uint16_t i;
    
    /* Count empty descriptors (not owned by NIC) */
    for (i = 0; i < RX_RING_SIZE; i++) {
        if (!(state->ring[i].status & RX_OWN_BIT)) {
            empty_slots++;
        }
    }
    
    return (empty_slots >= RX_REFILL_THRESHOLD);
}

/**
 * Batch refill RX descriptors
 */
int rx_batch_refill(uint8_t nic_index) {
    rx_batch_state_t *state;
    uint16_t refilled = 0;
    uint16_t i;
    uint32_t last_desc_phys = 0;
    
    if (nic_index >= MAX_NICS) {
        return -1;
    }
    
    state = &g_rx_state[nic_index];
    if (!state->enabled) {
        return 0;
    }
    
    /* Check if refill needed */
    if (!rx_needs_refill(state)) {
        return 0;
    }
    
    /* Refill empty descriptors */
    for (i = 0; i < RX_RING_SIZE && refilled < RX_BUDGET; i++) {
        uint16_t idx = (state->head + i) & RX_RING_MASK;
        
        /* Skip if NIC still owns it */
        if (state->ring[idx].status & RX_OWN_BIT) {
            continue;
        }
        
        /* Check if buffer needs replacement */
        if (!state->buffer_virt[idx]) {
            /* Allocate new buffer with 64KB boundary check */
            uint32_t phys;
            void far *buf = rx_alloc_64k_safe(1536, &phys);
            if (!buf) {
                LOG_WARNING("RX buffer allocation failed during refill");
                state->boundary_retry_exhausted++;
                break;
            }
            
            state->buffer_virt[idx] = buf;
            state->buffer_phys[idx] = phys;
            state->buffer_size[idx] = 1536;
        }
        
        /* Reset descriptor */
        state->ring[idx].buf_addr = state->buffer_phys[idx];
        state->ring[idx].buf_len = 1536;
        state->ring[idx].status = RX_OWN_BIT;  /* Give back to NIC */
        
        last_desc_phys = state->ring_phys + (idx * sizeof(rx_desc_t));
        refilled++;
    }
    
    if (refilled > 0) {
        /* Memory barrier before doorbell write */
        dma_wmb();
        
        /* Single doorbell write for all refills */
        outl(state->io_base + UP_LIST_PTR, last_desc_phys);
        
        state->bulk_refills++;
        state->doorbell_writes++;
        state->last_refill_count = refilled;
        
        LOG_DEBUG("Batch refilled %u RX buffers with single doorbell", refilled);
    } else if (refilled == 0 && rx_needs_refill(state)) {
        /* Ring exhaustion telemetry */
        state->empty_events++;
        LOG_WARNING("RX ring exhausted - packet loss likely");
    }
    
    return refilled;
}

/**
 * Process received packet
 */
/* External function - packet processing with NIC index for multi-NIC support */
extern int packet_receive_process(uint8_t *raw_data, uint16_t length, uint8_t nic_index);

int rx_batch_process(uint8_t nic_index) {
    rx_batch_state_t *state;
    uint16_t processed = 0;
    uint16_t idx;
    rx_desc_t *desc;
    uint16_t pkt_len;
    void far *pkt_buf;
    void far *small_buf;

    if (nic_index >= MAX_NICS) {
        return -1;
    }
    
    state = &g_rx_state[nic_index];
    if (!state->enabled) {
        return 0;
    }
    
    /* Process completed descriptors */
    idx = state->tail;
    
    while (processed < RX_BUDGET) {
        desc = &state->ring[idx];

        /* Check if NIC has filled this descriptor */
        if (desc->status & RX_OWN_BIT) {
            break;  /* NIC still owns it */
        }
        
        /* Check for errors */
        if (desc->status & RX_ERROR) {
            LOG_DEBUG("RX error on descriptor %u", idx);
            /* Reset descriptor and continue */
            desc->status = RX_OWN_BIT;
            idx = (idx + 1) & RX_RING_MASK;
            continue;
        }
        
        /* Extract packet length from status */
        pkt_len = (desc->status >> 16) & 0x1FFF;
        
        if (pkt_len > 0 && pkt_len <= 1536) {
            pkt_buf = state->buffer_virt[idx];

            /* Decide copy vs flip based on threshold */
            if (pkt_len <= g_copy_break_threshold) {
                /* Copy to small buffer */
                small_buf = buffer_alloc_small(pkt_len);
                if (small_buf) {
                    _fmemcpy(small_buf, pkt_buf, pkt_len);

                    /* Pass up small buffer with correct NIC index */
                    packet_receive_process((uint8_t *)small_buf, pkt_len, nic_index);

                    state->copy_break_count++;
                }
            } else {
                /* Flip - pass up large buffer with correct NIC index */
                packet_receive_process((uint8_t *)pkt_buf, pkt_len, nic_index);

                /* Mark buffer as consumed */
                state->buffer_virt[idx] = NULL;
            }
            
            state->total_packets++;
        }
        
        /* Reset descriptor for reuse */
        desc->status = RX_OWN_BIT;
        
        idx = (idx + 1) & RX_RING_MASK;
        processed++;
    }
    
    state->tail = idx;
    
    /* Trigger refill if needed */
    if (processed > 0) {
        rx_batch_refill(nic_index);
    }
    
    return processed;
}

/**
 * Get RX batch statistics
 */
int rx_batch_get_stats(uint8_t nic_index, uint32_t *total_packets,
                       uint32_t *copy_break_count, uint32_t *bulk_refills,
                       uint32_t *doorbell_writes) {
    rx_batch_state_t *state;
    
    if (nic_index >= MAX_NICS) {
        return -1;
    }
    
    state = &g_rx_state[nic_index];
    
    if (total_packets) *total_packets = state->total_packets;
    if (copy_break_count) *copy_break_count = state->copy_break_count;
    if (bulk_refills) *bulk_refills = state->bulk_refills;
    if (doorbell_writes) *doorbell_writes = state->doorbell_writes;
    
    return 0;
}

/**
 * Enable/disable RX batch refill
 */
int rx_batch_set_enabled(uint8_t nic_index, bool enable) {
    rx_batch_state_t *state;
    
    if (nic_index >= MAX_NICS) {
        return -1;
    }
    
    state = &g_rx_state[nic_index];
    state->enabled = enable;
    
    LOG_INFO("RX batch refill %s for NIC %d", 
             enable ? "enabled" : "disabled", nic_index);
    
    return 0;
}

/**
 * Cleanup RX batch resources
 */
void rx_batch_cleanup(uint8_t nic_index) {
    rx_batch_state_t *state;
    int i;
    
    if (nic_index >= MAX_NICS) {
        return;
    }
    
    state = &g_rx_state[nic_index];
    
    /* Free buffers */
    for (i = 0; i < RX_RING_SIZE; i++) {
        if (state->buffer_virt[i]) {
            buffer_free(state->buffer_virt[i]);
            state->buffer_virt[i] = NULL;
        }
    }
    
    /* Free ring */
    if (state->ring) {
        buffer_free(state->ring);
        state->ring = NULL;
    }
    
    state->enabled = false;
}