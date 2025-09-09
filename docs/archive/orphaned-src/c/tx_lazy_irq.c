/**
 * @file tx_lazy_irq.c
 * @brief Lazy TX interrupt optimization for 3Com NICs
 * 
 * Implements TX interrupt coalescing to reduce interrupt rate by only
 * requesting interrupts every K packets or when queue becomes empty.
 * Based on DRIVER_TUNING.md specifications.
 */

#include <stdint.h>
#include <stdbool.h>
#include "3com_pci.h"
#include "logging.h"
#include "compile_checks.h"  /* Validates K_PKTS and ring sizes */

/* Configuration constants from DRIVER_TUNING.md */
#define TX_INT_BIT          0x8000  /* Request TX complete interrupt */
#define K_PKTS              8        /* Request interrupt every 8 packets */
#define TX_RING_SIZE        32       /* Typical TX ring size */
#define TX_RING_MASK        (TX_RING_SIZE - 1)

/* Lazy TX state per NIC */
typedef struct {
    uint16_t tx_since_irq;      /* Packets sent since last IRQ request */
    uint16_t tx_inflight;       /* Total packets in flight */
    uint16_t tx_head;           /* Producer index */
    uint16_t tx_tail;           /* Consumer index */
    
    /* Statistics */
    uint32_t total_packets;     /* Total packets transmitted */
    uint32_t total_interrupts;  /* Total TX interrupts requested */
    uint32_t empty_queue_irqs;  /* IRQs due to empty queue */
    uint32_t threshold_irqs;    /* IRQs due to K_PKTS threshold */
} tx_lazy_state_t;

/* Per-NIC lazy TX state */
static tx_lazy_state_t lazy_tx_state[MAX_NICS] = {0};

/**
 * @brief Initialize lazy TX IRQ for a NIC
 */
void tx_lazy_init(uint8_t nic_index) {
    tx_lazy_state_t *state;
    
    if (nic_index >= MAX_NICS) {
        return;
    }
    
    state = &lazy_tx_state[nic_index];
    
    /* Clear state */
    state->tx_since_irq = 0;
    state->tx_inflight = 0;
    state->tx_head = 0;
    state->tx_tail = 0;
    
    /* Clear statistics */
    state->total_packets = 0;
    state->total_interrupts = 0;
    state->empty_queue_irqs = 0;
    state->threshold_irqs = 0;
    
    LOG_INFO("Lazy TX-IRQ initialized for NIC %d (K=%d)", nic_index, K_PKTS);
}

/**
 * @brief Determine if TX descriptor should request interrupt
 * 
 * Called when posting a TX descriptor to determine if TX_INT_BIT
 * should be set based on lazy IRQ policy.
 * 
 * @param nic_index NIC index
 * @return true if interrupt should be requested, false otherwise
 */
bool tx_lazy_should_interrupt(uint8_t nic_index) {
    tx_lazy_state_t *state;
    bool request_irq = false;
    
    if (nic_index >= MAX_NICS) {
        return true;  /* Safe default */
    }
    
    state = &lazy_tx_state[nic_index];
    
    /* Policy: Request interrupt if:
     * 1. Queue was empty (need to ensure progress)
     * 2. Every K packets (periodic cleanup)
     * 3. Queue is becoming full (prevent stall)
     */
    
    if (state->tx_inflight == 0) {
        /* Queue was empty - need IRQ to ensure completion */
        request_irq = true;
        state->empty_queue_irqs++;
        /* LOG_DEBUG removed for hot path performance */
    } else {
        /* Increment counter first, then check */
        state->tx_since_irq++;
        
        /* Use bitmask for power-of-2 K_PKTS (must be 8) */
        if ((state->tx_since_irq & (K_PKTS - 1)) == 0) {
            /* Hit K packet threshold */
            request_irq = true;
            state->threshold_irqs++;
        } else if (state->tx_inflight >= (TX_RING_SIZE - 2)) {
            /* Queue almost full - force interrupt */
            request_irq = true;
        }
    }
    
    if (request_irq) {
        state->tx_since_irq = 0;
        state->total_interrupts++;
    }
    
    return request_irq;
}

/**
 * @brief Post TX packet with lazy IRQ logic
 * 
 * Enhanced version of tx_post from DRIVER_TUNING.md that implements
 * the lazy IRQ policy for Boomerang/Cyclone/Tornado families.
 * 
 * @param nic_index NIC index
 * @param buf_phys Physical address of packet buffer
 * @param len Packet length
 * @param desc TX descriptor to fill
 */
void tx_lazy_post_boomerang(uint8_t nic_index, uint32_t buf_phys, 
                            uint16_t len, void *desc_ptr) {
    tx_lazy_state_t *state;
    boomerang_tx_desc_t *desc = (boomerang_tx_desc_t *)desc_ptr;
    boomerang_tx_desc_t *ring = (boomerang_tx_desc_t *)desc_ptr;
    uint16_t next_idx;
    
    if (nic_index >= MAX_NICS || !desc) {
        return;
    }
    
    state = &lazy_tx_state[nic_index];
    
    /* Calculate next descriptor index */
    next_idx = (state->tx_head + 1) & TX_RING_MASK;
    
    /* PRE-TX DMA SAFETY PATCH POINT */
    __asm__ volatile ("nop\nnop\nnop" ::: "memory"); /* SMC patch site #4 */
    
    /* Fill descriptor */
    desc->next = (uint32_t)&ring[next_idx];  /* Point to next in ring */
    desc->status = 0;
    desc->buf_addr = buf_phys;
    desc->len = len | LAST_FRAG;  /* Single fragment */
    
    /* Apply lazy IRQ policy */
    if (tx_lazy_should_interrupt(nic_index)) {
        desc->status |= TX_INT_BIT;
    }
    
    /* Update state */
    state->tx_inflight++;
    state->total_packets++;
    
    /* Advance head */
    state->tx_head = next_idx;
}

/**
 * @brief Post TX packet for Vortex (PIO mode)
 * 
 * Vortex uses PIO FIFOs, not descriptors, but we still track
 * for statistics and can use lazy doorbell updates.
 * 
 * @param nic_index NIC index
 * @param len Packet length
 */
void tx_lazy_post_vortex(uint8_t nic_index, uint16_t len) {
    tx_lazy_state_t *state;
    
    if (nic_index >= MAX_NICS) {
        return;
    }
    
    state = &lazy_tx_state[nic_index];
    
    /* Vortex doesn't have per-packet IRQ control,
     * but we can still track for statistics */
    state->tx_inflight++;
    state->total_packets++;
    state->tx_since_irq++;
    
    /* Could implement lazy TX start here */
}

/**
 * @brief Batch TX completion handler
 * 
 * Called from bottom half to reclaim completed TX descriptors.
 * Processes all completed descriptors in a batch.
 * 
 * @param nic_index NIC index
 * @param ring TX descriptor ring
 * @param free_func Function to free TX buffer
 * @return Number of descriptors reclaimed
 */
uint16_t tx_lazy_reclaim_batch(uint8_t nic_index, void *ring,
                               void (*free_func)(uint32_t)) {
    tx_lazy_state_t *state;
    boomerang_tx_desc_t *tx_ring = (boomerang_tx_desc_t *)ring;
    uint16_t reclaimed = 0;
    
    if (nic_index >= MAX_NICS || !ring) {
        return 0;
    }
    
    state = &lazy_tx_state[nic_index];
    
    /* Process all completed descriptors */
    while (state->tx_inflight > 0) {
        boomerang_tx_desc_t *desc = &tx_ring[state->tx_tail];
        
        /* Check if descriptor is complete */
        if (!(desc->status & TX_COMPLETE)) {
            break;  /* Still in flight */
        }
        
        /* POST-TX DMA SAFETY PATCH POINT */
        __asm__ volatile ("nop\nnop\nnop" ::: "memory"); /* SMC patch site #5 */
        
        /* Free buffer if function provided */
        if (free_func && desc->buf_addr) {
            free_func(desc->buf_addr);
        }
        
        /* Clear descriptor and update next pointer for ring */
        desc->status = 0;
        desc->buf_addr = 0;
        desc->len = 0;
        
        /* Update next pointer to maintain ring structure */
        desc->next = (uint32_t)&tx_ring[(state->tx_tail + 1) & TX_RING_MASK];
        
        /* Advance tail and decrement inflight count */
        state->tx_tail = (state->tx_tail + 1) & TX_RING_MASK;
        state->tx_inflight--;
        reclaimed++;
    }
    
    /* Performance counter update without logging in hot path */
    
    return reclaimed;
}

/**
 * @brief Get lazy TX statistics
 * 
 * @param nic_index NIC index
 * @param stats Output statistics structure
 */
void tx_lazy_get_stats(uint8_t nic_index, tx_lazy_stats_t *stats) {
    tx_lazy_state_t *state;
    
    if (nic_index >= MAX_NICS || !stats) {
        return;
    }
    
    state = &lazy_tx_state[nic_index];
    
    stats->total_packets = state->total_packets;
    stats->total_interrupts = state->total_interrupts;
    stats->irq_reduction_percent = 0;
    
    if (state->total_packets > 0) {
        /* Calculate interrupt reduction percentage */
        uint32_t expected_irqs = state->total_packets;
        uint32_t actual_irqs = state->total_interrupts;
        
        if (expected_irqs > 0) {
            stats->irq_reduction_percent = 
                ((expected_irqs - actual_irqs) * 100) / expected_irqs;
        }
    }
    
    stats->empty_queue_irqs = state->empty_queue_irqs;
    stats->threshold_irqs = state->threshold_irqs;
    stats->packets_per_irq = 0;
    
    if (state->total_interrupts > 0) {
        stats->packets_per_irq = 
            state->total_packets / state->total_interrupts;
    }
}

/**
 * @brief Reset lazy TX statistics
 * 
 * @param nic_index NIC index
 */
void tx_lazy_reset_stats(uint8_t nic_index) {
    tx_lazy_state_t *state;
    
    if (nic_index >= MAX_NICS) {
        return;
    }
    
    state = &lazy_tx_state[nic_index];
    
    state->total_packets = 0;
    state->total_interrupts = 0;
    state->empty_queue_irqs = 0;
    state->threshold_irqs = 0;
}