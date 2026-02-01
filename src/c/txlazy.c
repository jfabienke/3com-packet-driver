/**
 * @file txlazy.c
 * @brief Lazy TX interrupt optimization
 *
 * Implements TX interrupt coalescing to reduce interrupt rate by only
 * requesting interrupts every K packets or when queue becomes empty.
 */

#include <dos.h>
#include <string.h>
#include "common.h"
#include "hardware.h"
#include "logging.h"
#include "barrier.h"
#include "txlazy.h"
#include "bufaloc.h"

/* Configuration constants */
/* Note: TX_INT_BIT already defined in txlazy.h - use local prefix */
#define TXLAZY_INT_BIT      0x8000  /* Request TX complete interrupt */
#define K_PKTS              8        /* Request interrupt every 8 packets */
#define TX_RING_SIZE        32       /* TX ring size */
#define TX_RING_MASK        (TX_RING_SIZE - 1)
#define TX_HIGH_WATER       24       /* Force IRQ if this many in flight */
#define TXQ_RETRY           (-2001)  /* Backpressure error code */

/* Lazy TX state per NIC */
typedef struct {
    /* Ring management */
    boomerang_tx_desc_t *ring;  /* Virtual address of ring (use header's type) */
    uint32_t ring_phys;         /* Physical address of ring */
    uint16_t head;              /* Producer index (driver fills) */
    uint16_t tail;              /* Consumer index (NIC drains) */
    uint16_t io_base;           /* I/O base address */
    uint8_t nic_index;          /* NIC index */
    uint8_t enabled;            /* Lazy TX enabled (use uint8_t for C89) */

    /* Lazy IRQ tracking */
    uint16_t tx_since_irq;      /* Packets sent since last IRQ request */
    uint16_t tx_inflight;       /* Total packets in flight */
    uint16_t last_irq_desc;     /* Last descriptor that requested IRQ */
    uint8_t force_next_irq;     /* Force IRQ on next packet (use uint8_t for C89) */

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
static uint8_t g_tx_lazy_initialized = 0;

/**
 * Initialize lazy TX IRQ for a NIC
 * Note: Signature matches header declaration: void tx_lazy_init(uint8_t nic_index)
 */
void tx_lazy_init(uint8_t nic_index) {
    tx_lazy_state_t *state;
    nic_info_t *nic;

    if (nic_index >= MAX_NICS) {
        return;
    }

    nic = hardware_get_nic(nic_index);
    if (!nic) {
        return;
    }

    /* Only for bus mastering NICs */
    if (nic->type != NIC_TYPE_3C515_TX) {
        LOG_DEBUG("TX lazy IRQ not supported for NIC type %d", nic->type);
        return;
    }

    state = &g_lazy_tx_state[nic_index];
    memset(state, 0, sizeof(tx_lazy_state_t));

    state->nic_index = nic_index;
    state->io_base = nic->io_base;

    /* Note: Ring allocation deferred - use static ring or simple allocation */
    /* For now, mark as enabled and rely on per-packet descriptor setup */
    state->ring = NULL;
    state->ring_phys = 0;

    state->enabled = 1;
    g_tx_lazy_initialized = 1;

    LOG_INFO("Lazy TX-IRQ initialized for NIC %d (K=%d)", nic_index, K_PKTS);
}

/**
 * Determine if TX descriptor should request interrupt
 *
 * Called when posting a TX descriptor to determine if TX_INT_BIT
 * should be set based on lazy IRQ policy.
 */
bool tx_lazy_should_interrupt(uint8_t nic_index) {
    tx_lazy_state_t *state;
    int request_irq;

    request_irq = 0;

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
        request_irq = 1;
        state->empty_queue_irqs++;
    } else if (state->tx_since_irq >= K_PKTS - 1) {
        /* Hit K packets threshold */
        request_irq = 1;
        state->threshold_irqs++;
    } else if (state->tx_inflight >= TX_HIGH_WATER) {
        /* Queue getting full - force cleanup */
        request_irq = 1;
        state->high_water_irqs++;
    } else if (state->force_next_irq) {
        /* Forced IRQ due to previous backpressure */
        request_irq = 1;
        state->force_next_irq = 0;
    }

    if (request_irq) {
        state->tx_since_irq = 0;
        state->total_interrupts++;
    } else {
        state->tx_since_irq++;
        state->interrupts_saved++;
    }

    return (request_irq != 0);
}

/**
 * Post TX packet for Boomerang/Cyclone/Tornado NICs
 * Note: Signature matches header declaration
 */
void tx_lazy_post_boomerang(uint8_t nic_index, uint32_t buf_phys,
                            uint16_t len, void *desc) {
    tx_lazy_state_t *state;
    boomerang_tx_desc_t *tx_desc;
    int request_irq;

    if (nic_index >= MAX_NICS || desc == NULL) {
        return;
    }

    state = &g_lazy_tx_state[nic_index];
    if (!state->enabled) {
        return;
    }

    tx_desc = (boomerang_tx_desc_t *)desc;

    /* Fill descriptor */
    tx_desc->buf_addr = buf_phys;
    tx_desc->len = len | LAST_FRAG;

    /* Decide on interrupt request */
    request_irq = tx_lazy_should_interrupt(nic_index) ? 1 : 0;

    /* Set status with optional interrupt bit */
    tx_desc->status = 0x80000000UL;  /* OWN bit for NIC */
    if (request_irq) {
        tx_desc->status |= TXLAZY_INT_BIT;
        state->last_irq_desc = state->head;
    }

    /* Update counters */
    state->head = (state->head + 1) & TX_RING_MASK;
    state->tx_inflight++;
    state->total_packets++;

    /* Memory barrier before doorbell */
    dma_wmb();
}

/**
 * Post TX packet for Vortex (PIO mode)
 * Note: Signature matches header declaration
 */
void tx_lazy_post_vortex(uint8_t nic_index, uint16_t len) {
    tx_lazy_state_t *state;

    if (nic_index >= MAX_NICS) {
        return;
    }

    state = &g_lazy_tx_state[nic_index];
    if (!state->enabled) {
        return;
    }

    /* For Vortex PIO mode, just track statistics */
    state->total_packets++;

    /* Decide if we should request interrupt (for stats) */
    (void)tx_lazy_should_interrupt(nic_index);

    /* Suppress unused parameter warning */
    (void)len;
}

/**
 * Batch TX completion handler
 * Note: Signature matches header declaration
 */
uint16_t tx_lazy_reclaim_batch(uint8_t nic_index, void *ring,
                               void (*free_func)(uint32_t)) {
    tx_lazy_state_t *state;
    uint16_t completed;
    uint16_t idx;
    boomerang_tx_desc_t *tx_ring;
    boomerang_tx_desc_t *desc;

    completed = 0;

    if (nic_index >= MAX_NICS) {
        return 0;
    }

    state = &g_lazy_tx_state[nic_index];
    if (!state->enabled || ring == NULL) {
        return 0;
    }

    tx_ring = (boomerang_tx_desc_t *)ring;
    idx = state->tail;

    /* Process completed descriptors */
    while (state->tx_inflight > 0) {
        desc = &tx_ring[idx];

        /* Check if NIC has completed this descriptor */
        if (desc->status & 0x80000000UL) {
            break;  /* NIC still owns it */
        }

        /* Free the buffer if callback provided */
        if (free_func && desc->buf_addr) {
            free_func(desc->buf_addr);
        }

        /* Clear descriptor */
        desc->status = 0;
        desc->buf_addr = 0;
        desc->len = 0;

        idx = (idx + 1) & TX_RING_MASK;
        state->tx_inflight--;
        completed++;
    }

    state->tail = idx;

    return completed;
}

/**
 * Get lazy TX statistics
 * Note: Signature matches header declaration
 */
void tx_lazy_get_stats(uint8_t nic_index, tx_lazy_stats_t *stats) {
    tx_lazy_state_t *state;
    uint32_t total_potential;

    if (nic_index >= MAX_NICS || stats == NULL) {
        return;
    }

    state = &g_lazy_tx_state[nic_index];

    stats->total_packets = state->total_packets;
    stats->total_interrupts = state->total_interrupts;
    stats->empty_queue_irqs = state->empty_queue_irqs;
    stats->threshold_irqs = state->threshold_irqs;

    /* Calculate interrupt reduction percentage (integer math) */
    if (state->total_packets > 0) {
        total_potential = state->total_packets;
        stats->irq_reduction_percent = (state->interrupts_saved * 100) / total_potential;
    } else {
        stats->irq_reduction_percent = 0;
    }

    /* Calculate average packets per interrupt */
    if (state->total_interrupts > 0) {
        stats->packets_per_irq = state->total_packets / state->total_interrupts;
    } else {
        stats->packets_per_irq = 0;
    }
}

/**
 * Reset lazy TX statistics
 * Note: Signature matches header declaration
 */
void tx_lazy_reset_stats(uint8_t nic_index) {
    tx_lazy_state_t *state;

    if (nic_index >= MAX_NICS) {
        return;
    }

    state = &g_lazy_tx_state[nic_index];

    state->total_packets = 0;
    state->total_interrupts = 0;
    state->empty_queue_irqs = 0;
    state->threshold_irqs = 0;
    state->high_water_irqs = 0;
    state->interrupts_saved = 0;
    state->ring_full_events = 0;
}

/* End of txlazy.c */

