/**
 * @file txlazy_init.c
 * @brief Lazy TX Interrupt Optimization - Initialization Functions (OVERLAY Segment)
 *
 * Created: 2026-01-28 09:17:48 UTC
 *
 * This file contains TX lazy IRQ initialization, configuration, and
 * statistics functions. These functions are only called during driver
 * startup/shutdown and can be placed in an overlay segment to save
 * memory during normal operation.
 *
 * Functions included:
 * - tx_lazy_init - Initialize lazy TX IRQ for a NIC
 * - tx_lazy_get_stats - Retrieve TX lazy statistics
 * - tx_lazy_reset_stats - Reset TX lazy statistics
 *
 * Split from txlazy.c for memory segmentation optimization.
 * Runtime TX functions are in txlazy_rt.c (ROOT segment).
 */

#include <dos.h>
#include <string.h>
#include "common.h"
#include "hardware.h"
#include "logging.h"
#include "txlazy.h"

/* Configuration constants - must match txlazy_rt.c */
#define K_PKTS              8       /* Request interrupt every 8 packets */

/*==============================================================================
 * Lazy TX state structure - must match txlazy_rt.c exactly
 *==============================================================================*/

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

/*==============================================================================
 * External declarations for global state (defined in txlazy_rt.c)
 *==============================================================================*/

extern tx_lazy_state_t g_lazy_tx_state[MAX_NICS];
extern uint8_t g_tx_lazy_initialized;

/*==============================================================================
 * Initialization functions
 *==============================================================================*/

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

/*==============================================================================
 * Statistics functions
 *==============================================================================*/

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

/* End of txlazy_init.c */
