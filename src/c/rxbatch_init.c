/**
 * @file rxbatch_init.c
 * @brief Batched RX buffer refill - initialization code (OVERLAY segment)
 *
 * Contains RX batch system initialization, buffer pool allocation,
 * configuration/threshold setup, and cleanup functions.
 *
 * Last modified: 2026-01-28
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

/* Hardware registers */
#define UP_LIST_PTR          0x38     /* Upload (RX) list pointer */

/* RX descriptor bits */
#define RX_OWN_BIT           0x80000000UL  /* NIC owns descriptor */

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

/* External state from rxbatch_rt.c */
extern rx_batch_state_t g_rx_state[MAX_NICS];
extern bool g_rx_batch_initialized;

/* External function from rxbatch_rt.c */
extern void far* rx_alloc_64k_safe(uint16_t len, uint32_t *phys_out);

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

    g_rx_batch_initialized = true;

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
