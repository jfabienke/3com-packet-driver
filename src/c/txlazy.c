/**
 * @file tx_lazy_irq.c
 * @brief Lazy TX interrupt optimization
 *
 * Implements TX interrupt coalescing to reduce interrupt rate by only
 * requesting interrupts every K packets or when queue becomes empty.
 */

#include <dos.h>
#include <string.h>
#include "../include/common.h"
#include "../include/hardware.h"
#include "../include/logging.h"
#include "../include/barrier.h"
#include "../include/txlazy.h"

/* Configuration constants */
#define TX_INT_BIT          0x8000  /* Request TX complete interrupt */
#define K_PKTS              8        /* Request interrupt every 8 packets */
#define TX_RING_SIZE        32       /* TX ring size */
#define TX_RING_MASK        (TX_RING_SIZE - 1)
#define TX_HIGH_WATER       24       /* Force IRQ if this many in flight */
#define TXQ_RETRY           (-2001)  /* Backpressure error code */

/* TX descriptor structure (3C515/Boomerang format) */
typedef struct {
    uint32_t next;           /* Next descriptor physical address */
    uint32_t status;         /* Status and control */
    uint32_t buf_addr;       /* Buffer physical address */
    uint32_t buf_len;        /* Buffer length and flags */
} tx_desc_t;

/* Lazy TX state per NIC */
typedef struct {
    /* Ring management */
    tx_desc_t *ring;         /* Virtual address of ring */
    uint32_t ring_phys;      /* Physical address of ring */
    uint16_t head;           /* Producer index (driver fills) */
    uint16_t tail;           /* Consumer index (NIC drains) */
    uint16_t io_base;        /* I/O base address */
    uint8_t nic_index;       /* NIC index */
    bool enabled;            /* Lazy TX enabled */
    
    /* Lazy IRQ tracking */
    uint16_t tx_since_irq;      /* Packets sent since last IRQ request */
    uint16_t tx_inflight;       /* Total packets in flight */
    uint16_t last_irq_desc;     /* Last descriptor that requested IRQ */
    bool force_next_irq;        /* Force IRQ on next packet */
    
    /* Statistics */
    uint32_t total_packets;     /* Total packets transmitted */
    uint32_t total_interrupts;  /* Total TX interrupts requested */
    uint32_t empty_queue_irqs;  /* IRQs due to empty queue */
    uint32_t threshold_irqs;    /* IRQs due to K_PKTS threshold */
    uint32_t high_water_irqs;   /* IRQs due to high water mark */
    uint32_t interrupts_saved;  /* Interrupts avoided */
    uint32_t ring_full_events;  /* Ring full backpressure events */
} tx_lazy_state_t;

/* Per-NIC lazy TX state */
static tx_lazy_state_t g_lazy_tx_state[MAX_NICS];
static bool g_tx_lazy_initialized = false;

/**
 * Initialize lazy TX IRQ for a NIC
 */
int tx_lazy_init(uint8_t nic_index, uint16_t io_base) {
    tx_lazy_state_t *state;
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
        LOG_DEBUG("TX lazy IRQ not supported for NIC type %d", nic->type);
        return -1;
    }
    
    state = &g_lazy_tx_state[nic_index];
    memset(state, 0, sizeof(tx_lazy_state_t));
    
    state->nic_index = nic_index;
    state->io_base = io_base;
    
    /* Allocate TX ring in conventional memory (DMA-safe) */
    state->ring = (tx_desc_t *)buffer_alloc_dma_safe(sizeof(tx_desc_t) * TX_RING_SIZE);
    if (!state->ring) {
        LOG_ERROR("Failed to allocate TX ring");
        return -1;
    }
    
    /* Get physical address of ring */
    state->ring_phys = dma_get_physical_addr(state->ring);
    if (state->ring_phys == 0) {
        LOG_ERROR("Failed to get physical address of TX ring");
        buffer_free(state->ring);
        return -1;
    }
    
    /* Initialize descriptors */
    memset(state->ring, 0, sizeof(tx_desc_t) * TX_RING_SIZE);
    
    /* Link descriptors in a ring */
    for (i = 0; i < TX_RING_SIZE - 1; i++) {
        state->ring[i].next = state->ring_phys + ((i + 1) * sizeof(tx_desc_t));
    }
    state->ring[TX_RING_SIZE - 1].next = state->ring_phys;  /* Wrap to start */
    
    state->enabled = true;
    
    LOG_INFO("Lazy TX-IRQ initialized for NIC %d (K=%d)", nic_index, K_PKTS);
    
    return 0;
}

/**
 * Determine if TX descriptor should request interrupt
 *
 * Called when posting a TX descriptor to determine if TX_INT_BIT
 * should be set based on lazy IRQ policy.
 */
bool tx_lazy_should_interrupt(uint8_t nic_index) {
    tx_lazy_state_t *state;
    bool request_irq = false;
    
    if (nic_index >= MAX_NICS) {
        return true;  /* Safe default - always request IRQ */
    }
    
    state = &g_lazy_tx_state[nic_index];
    
    if (!state->enabled) {
        return true;  /* Default behavior when disabled */
    }
    
    /* Policy: Request interrupt if:
     * 1. Queue was empty (need to ensure progress)
     * 2. Every K packets (periodic cleanup)
     * 3. Queue is becoming full (prevent stall)
     */
    
    if (state->tx_inflight == 0) {
        /* Queue was empty - need IRQ to ensure completion */
        request_irq = true;
        state->empty_queue_irqs++;
    } else if (state->tx_since_irq >= K_PKTS - 1) {
        /* Hit K packets threshold */
        request_irq = true;
        state->threshold_irqs++;
    } else if (state->tx_inflight >= TX_HIGH_WATER) {
        /* Queue getting full - force cleanup */
        request_irq = true;
        state->high_water_irqs++;
    } else if (state->force_next_irq) {
        /* Forced IRQ due to previous backpressure */
        request_irq = true;
        state->force_next_irq = false;
    }
    
    if (request_irq) {
        state->tx_since_irq = 0;
        state->total_interrupts++;
    } else {
        state->tx_since_irq++;
        state->interrupts_saved++;
    }
    
    return request_irq;
}

/**
 * Queue TX packet with lazy IRQ decision
 */
int tx_lazy_queue_packet(uint8_t nic_index, void far *buffer, 
                         uint16_t length, uint32_t phys_addr) {
    tx_lazy_state_t *state;
    tx_desc_t *desc;
    uint16_t idx;
    bool request_irq;
    
    if (nic_index >= MAX_NICS) {
        return -1;
    }
    
    state = &g_lazy_tx_state[nic_index];
    if (!state->enabled) {
        return -1;
    }
    
    /* Check for ring full with explicit backpressure */
    if (state->tx_inflight >= TX_RING_SIZE - 1) {
        /* Signal backpressure and arrange for IRQ on next packet */
        state->force_next_irq = true;
        state->ring_full_events++;
        LOG_DEBUG("TX ring full for NIC %d - signaling backpressure", nic_index);
        return TXQ_RETRY;
    }
    
    idx = state->head;
    desc = &state->ring[idx];
    
    /* Fill descriptor */
    desc->buf_addr = phys_addr;
    desc->buf_len = length;
    
    /* Decide on interrupt request */
    request_irq = tx_lazy_should_interrupt(nic_index);
    
    /* Set status with optional interrupt bit */
    desc->status = 0x80000000UL;  /* OWN bit for NIC */
    if (request_irq) {
        desc->status |= TX_INT_BIT;
        state->last_irq_desc = idx;
    }
    
    /* Update counters */
    state->head = (state->head + 1) & TX_RING_MASK;
    state->tx_inflight++;
    state->total_packets++;
    
    /* Memory barrier before doorbell */
    dma_wmb();
    
    /* Kick TX (doorbell) */
    outl(state->io_base + 0x24, state->ring_phys + (idx * sizeof(tx_desc_t)));
    
    return 0;
}

/**
 * Handle TX completion
 */
int tx_lazy_complete(uint8_t nic_index) {
    tx_lazy_state_t *state;
    uint16_t completed = 0;
    uint16_t idx;
    
    if (nic_index >= MAX_NICS) {
        return -1;
    }
    
    state = &g_lazy_tx_state[nic_index];
    if (!state->enabled) {
        return 0;
    }
    
    idx = state->tail;
    
    /* Process completed descriptors */
    while (state->tx_inflight > 0) {
        tx_desc_t *desc = &state->ring[idx];
        
        /* Check if NIC has completed this descriptor */
        if (desc->status & 0x80000000UL) {
            break;  /* NIC still owns it */
        }
        
        /* Clear descriptor */
        desc->status = 0;
        desc->buf_addr = 0;
        desc->buf_len = 0;
        
        idx = (idx + 1) & TX_RING_MASK;
        state->tx_inflight--;
        completed++;
    }
    
    state->tail = idx;
    
    return completed;
}

/**
 * Get lazy TX statistics
 */
int tx_lazy_get_stats(uint8_t nic_index, uint32_t *total_packets,
                      uint32_t *total_interrupts, uint32_t *interrupts_saved) {
    tx_lazy_state_t *state;
    
    if (nic_index >= MAX_NICS) {
        return -1;
    }
    
    state = &g_lazy_tx_state[nic_index];
    
    if (total_packets) *total_packets = state->total_packets;
    if (total_interrupts) *total_interrupts = state->total_interrupts;
    if (interrupts_saved) *interrupts_saved = state->interrupts_saved;
    
    return 0;
}

/**
 * Get ring full events count
 */
uint32_t tx_lazy_get_ring_full_events(uint8_t nic_index) {
    if (nic_index >= MAX_NICS) {
        return 0;
    }
    
    return g_lazy_tx_state[nic_index].ring_full_events;
}

/**
 * Get interrupt reduction percentage
 */
float tx_lazy_get_reduction_percent(uint8_t nic_index) {
    tx_lazy_state_t *state;
    uint32_t total_potential;
    
    if (nic_index >= MAX_NICS) {
        return 0.0f;
    }
    
    state = &g_lazy_tx_state[nic_index];
    
    if (state->total_packets == 0) {
        return 0.0f;
    }
    
    /* Without lazy IRQ, we'd have one interrupt per packet */
    total_potential = state->total_packets;
    
    /* Calculate reduction */
    return ((float)state->interrupts_saved * 100.0f) / (float)total_potential;
}

/**
 * Enable/disable lazy TX
 */
int tx_lazy_set_enabled(uint8_t nic_index, bool enable) {
    tx_lazy_state_t *state;
    
    if (nic_index >= MAX_NICS) {
        return -1;
    }
    
    state = &g_lazy_tx_state[nic_index];
    state->enabled = enable;
    
    if (!enable) {
        /* Reset counters when disabling */
        state->tx_since_irq = 0;
    }
    
    LOG_INFO("TX lazy IRQ %s for NIC %d", 
             enable ? "enabled" : "disabled", nic_index);
    
    return 0;
}

/**
 * Cleanup lazy TX resources
 */
void tx_lazy_cleanup(uint8_t nic_index) {
    tx_lazy_state_t *state;
    
    if (nic_index >= MAX_NICS) {
        return;
    }
    
    state = &g_lazy_tx_state[nic_index];
    
    /* Free ring */
    if (state->ring) {
        buffer_free(state->ring);
        state->ring = NULL;
    }
    
    state->enabled = false;
}

/**
 * Global initialization
 */
int tx_lazy_global_init(void) {
    int i;
    nic_info_t *nic;
    
    if (g_tx_lazy_initialized) {
        return 0;
    }
    
    /* Initialize for each bus mastering NIC */
    for (i = 0; i < MAX_NICS; i++) {
        nic = hardware_get_nic(i);
        if (nic && (nic->status & NIC_STATUS_PRESENT) &&
            nic->type == NIC_TYPE_3C515_TX) {
            tx_lazy_init(i, nic->io_base);
        }
    }
    
    g_tx_lazy_initialized = true;
    LOG_INFO("TX lazy IRQ system initialized");
    
    return 0;
}