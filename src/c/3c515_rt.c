/**
 * @file 3c515_rt.c
 * @brief 3Com 3C515-TX NIC driver - Runtime functions (ROOT segment)
 *
 * This file contains only the runtime functions needed after initialization:
 * - Packet send/receive via DMA
 * - Interrupt handling
 * - DMA cache coherency helpers
 *
 * Init-only functions are in 3c515_init.c (OVERLAY segment)
 *
 * Updated: 2026-01-28 05:15:00 UTC
 */

#include "3c515.h"
#include "hardware.h"
#include "logging.h"
#include "common.h"
#include "irqmit.h"
#include "hwchksm.h"
#include "dma.h"
#include "dmamap.h"
#include "dmadesc.h"
#include "cachemgt.h"
#include "api.h"
#include <string.h>

/* Ring size definitions */
#define TX_RING_SIZE 16
#define RX_RING_SIZE 16
#define BUFFER_SIZE  1600

/* ============================================================================
 * Private Data Structure
 * ============================================================================ */

typedef struct _3c515_private_data {
    _3c515_tx_tx_desc_t *tx_ring;       /* TX descriptor ring */
    _3c515_tx_rx_desc_t *rx_ring;       /* RX descriptor ring */
    uint8_t *buffers;                   /* Contiguous buffer memory */
    uint32_t tx_index;                  /* Current TX ring index */
    uint32_t rx_index;                  /* Current RX ring index */
} _3c515_private_data_t;

/* ============================================================================
 * DMA Cache Coherency Helpers (Runtime)
 * ============================================================================ */

/**
 * @brief Prepare DMA buffers (cache coherency)
 */
void _3c515_dma_prepare_buffers(void *buffer, size_t length, bool is_receive) {
    if (!buffer || length == 0) {
        return;
    }

    if (is_receive) {
        /* For RX: invalidate cache before DMA writes to buffer */
        cache_management_invalidate_buffer(buffer, length);
    } else {
        /* For TX: flush cache before DMA reads from buffer */
        cache_management_flush_buffer(buffer, length);
    }

    /* Memory fence to ensure cache ops complete */
    memory_fence();
}

/**
 * @brief Complete DMA buffer operation (cache coherency)
 */
void _3c515_dma_complete_buffers(void *buffer, size_t length, bool is_receive) {
    if (!buffer || length == 0) {
        return;
    }

    if (is_receive) {
        /* For RX: ensure cache sees DMA-written data */
        cache_management_dma_complete(buffer, length);
    }
    /* TX completion typically doesn't need cache ops */

    /* Memory fence to ensure visibility */
    memory_fence();
}

/* ============================================================================
 * Packet Operations - Runtime Core
 * ============================================================================ */

/**
 * @brief Send a packet using DMA
 */
int _3c515_send_packet(nic_info_t *nic, const uint8_t *packet, size_t len) {
    _3c515_private_data_t *priv;
    _3c515_tx_tx_desc_t *desc;
    dma_mapping_t *mapping;
    dma_fragment_t fragments[4];
    int frag_count;
    uint32_t phys_addr;
    void *mapped_buffer;
    int checksum_result;
    int sg_result;

    priv = (_3c515_private_data_t *)nic->private_data;
    if (!priv || !priv->tx_ring) return -1;

    desc = &priv->tx_ring[priv->tx_index];

    /* Check if descriptor is free */
    if (desc->status & _3C515_TX_TX_DESC_COMPLETE) return -1;

    /* Try scatter-gather DMA for enhanced performance */
    frag_count = dma_analyze_packet_fragmentation(packet, len, fragments, 4);

    if (frag_count > 1) {
        /* Use scatter-gather DMA for fragmented packets */
        LOG_DEBUG("Using scatter-gather DMA for %d fragments", frag_count);

        sg_result = dma_send_scatter_gather(nic->index, packet, len, fragments, frag_count);
        if (sg_result == 0) {
            priv->tx_index = (priv->tx_index + 1) % TX_RING_SIZE;
            return 0;
        } else {
            LOG_DEBUG("Scatter-gather failed (%d), falling back to consolidation", sg_result);
        }
    }

    /* Single buffer or consolidation path */
    mapping = dma_map_with_device_constraints((void *)packet, len, DMA_SYNC_TX, "3C515TX");
    if (!mapping) {
        LOG_ERROR("Failed to map TX buffer with 3C515TX constraints");
        return -1;
    }

    /* Store mapping for later cleanup */
    desc->mapping = mapping;

    /* Get physical address for hardware */
    phys_addr = dma_mapping_get_phys_addr(mapping);
    desc->addr = phys_addr;

    /* Sync mapped buffer for device access (handles cache coherency) */
    dma_mapping_sync_for_device(mapping);

    /* Calculate checksums if needed */
    if (len >= 34) {
        mapped_buffer = dma_mapping_get_address(mapping);
        checksum_result = hw_checksum_process_outbound_packet(mapped_buffer, len);
        if (checksum_result != 0) {
            LOG_DEBUG("Checksum calculation completed for outbound packet");
        }
        dma_mapping_sync_for_device(mapping);
    }

    /* Configure descriptor */
    desc->length = len;
    desc->status = _3C515_TX_TX_INTR_BIT;

    /* Start DMA transfer */
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_START_DMA_DOWN);

    /* Move to next descriptor */
    priv->tx_index = (priv->tx_index + 1) % TX_RING_SIZE;

    return 0;
}

/**
 * @brief Receive a packet using DMA
 */
int _3c515_receive_packet(nic_info_t *nic, uint8_t *buffer, size_t *len) {
    _3c515_private_data_t *priv;
    _3c515_tx_rx_desc_t *desc;
    void *rx_data_ptr;
    dma_mapping_t *dma_mapping;
    void *dma_safe_buffer;
    int checksum_result;

    priv = (_3c515_private_data_t *)nic->private_data;
    if (!priv || !priv->rx_ring) return -1;

    desc = &priv->rx_ring[priv->rx_index];

    /* Check if packet is ready */
    if (!(desc->status & _3C515_TX_RX_DESC_COMPLETE)) return -1;

    if (desc->status & _3C515_TX_RX_DESC_ERROR) {
        desc->status = 0;
        priv->rx_index = (priv->rx_index + 1) % RX_RING_SIZE;
        return -1;
    }

    /* Get packet length and data pointer */
    *len = desc->length & _3C515_TX_RX_DESC_LEN_MASK;
    rx_data_ptr = (void *)desc->addr;

    /* Map for DMA-safe access */
    dma_mapping = dma_map_rx(rx_data_ptr, *len);
    if (!dma_mapping) {
        LOG_ERROR("DMA mapping failed for RX buffer %p len=%zu", rx_data_ptr, *len);
        desc->status = 0;
        return -1;
    }

    dma_safe_buffer = dma_mapping_get_address(dma_mapping);
    if (dma_mapping_uses_bounce(dma_mapping)) {
        LOG_DEBUG("Using RX bounce buffer for packet len=%zu", *len);
    }

    /* Copy packet data to caller's buffer */
    memcpy(buffer, dma_safe_buffer, *len);

    /* Cleanup DMA mapping */
    dma_unmap_rx(dma_mapping);

    /* Verify checksums */
    if (*len >= 34) {
        checksum_result = hw_checksum_verify_inbound_packet(buffer, *len);
        if (checksum_result < 0) {
            LOG_DEBUG("Checksum verification failed for inbound packet");
        } else if (checksum_result > 0) {
            LOG_DEBUG("Checksum verification passed for inbound packet");
        }
    }

    /* Reset descriptor */
    desc->status = 0;

    /* Move to next descriptor */
    priv->rx_index = (priv->rx_index + 1) % RX_RING_SIZE;

    return 0;
}

/* ============================================================================
 * Interrupt Handling - Runtime Core
 * ============================================================================ */

/**
 * @brief Handle interrupts from the NIC
 */
void _3c515_handle_interrupt(nic_info_t *nic) {
    _3c515_private_data_t *priv;
    _3c515_tx_tx_desc_t *tx_ring;
    uint16_t status;
    int i;

    priv = (_3c515_private_data_t *)nic->private_data;
    if (!priv) return;

    tx_ring = priv->tx_ring;
    if (!tx_ring) return;

    status = inw(nic->io_base + _3C515_TX_STATUS_REG);

    if (status & _3C515_TX_STATUS_UP_COMPLETE) {
        /* Receive DMA completed; packets are ready in rx_ring */
    }

    if (status & _3C515_TX_STATUS_DOWN_COMPLETE) {
        /* Transmit DMA completed; check tx_ring for completion */
        for (i = 0; i < TX_RING_SIZE; i++) {
            if (tx_ring[i].status & _3C515_TX_TX_DESC_COMPLETE) {
                /* Queue TX completion for bottom-half processing */
                if (tx_ring[i].mapping) {
                    extern bool packet_queue_tx_completion(uint8_t nic_index, uint8_t desc_index, dma_mapping_t *mapping);
                    if (packet_queue_tx_completion(nic->index, i, tx_ring[i].mapping)) {
                        tx_ring[i].mapping = NULL;
                    }
                }
                tx_ring[i].status = 0;
            }
        }
    }

    /* Acknowledge the interrupt */
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_ACK_INTR | status);
}

/**
 * @brief Check if this NIC has pending interrupt work
 */
int _3c515_check_interrupt(nic_info_t *nic) {
    uint16_t status;

    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    status = inw(nic->io_base + _3C515_TX_STATUS_REG);

    if (status & (_3C515_TX_STATUS_UP_COMPLETE |
                  _3C515_TX_STATUS_DOWN_COMPLETE |
                  _3C515_TX_STATUS_TX_COMPLETE |
                  _3C515_TX_STATUS_RX_COMPLETE |
                  _3C515_TX_STATUS_ADAPTER_FAILURE |
                  _3C515_TX_STATUS_STATS_FULL)) {
        return 1;
    }

    return 0;
}

/**
 * @brief Process single interrupt event for batching system
 */
int _3c515_process_single_event(nic_info_t *nic, interrupt_event_type_t *event_type) {
    uint16_t status;
    int i;
    uint8_t rx_buffer[1514];
    size_t rx_length;

    if (!nic || !event_type) {
        return ERROR_INVALID_PARAM;
    }

    status = inw(nic->io_base + _3C515_TX_STATUS_REG);

    /* Handle adapter failure (highest priority) */
    if (status & _3C515_TX_STATUS_ADAPTER_FAILURE) {
        *event_type = EVENT_TYPE_RX_ERROR;
        LOG_ERROR("3C515 adapter failure detected");
        outw(nic->io_base + _3C515_TX_COMMAND_REG,
             _3C515_TX_CMD_ACK_INTR | _3C515_TX_STATUS_ADAPTER_FAILURE);
        return 1;
    }

    /* Handle RX DMA completion */
    if (status & _3C515_TX_STATUS_UP_COMPLETE) {
        *event_type = EVENT_TYPE_DMA_COMPLETE;
        outw(nic->io_base + _3C515_TX_COMMAND_REG,
             _3C515_TX_CMD_ACK_INTR | _3C515_TX_STATUS_UP_COMPLETE);
        return 1;
    }

    /* Handle TX DMA completion */
    if (status & _3C515_TX_STATUS_DOWN_COMPLETE) {
        _3c515_tx_tx_desc_t *tx_ring;
        *event_type = EVENT_TYPE_TX_COMPLETE;

        tx_ring = (_3c515_tx_tx_desc_t *)nic->tx_descriptor_ring;
        for (i = 0; i < TX_RING_SIZE; i++) {
            if (tx_ring && (tx_ring[i].status & _3C515_TX_TX_DESC_COMPLETE)) {
                tx_ring[i].status = 0;
            }
        }

        outw(nic->io_base + _3C515_TX_COMMAND_REG,
             _3C515_TX_CMD_ACK_INTR | _3C515_TX_STATUS_DOWN_COMPLETE);
        return 1;
    }

    /* Handle general RX completion */
    if (status & _3C515_TX_STATUS_RX_COMPLETE) {
        *event_type = EVENT_TYPE_RX_COMPLETE;

        if (nic->ops && nic->ops->receive_packet) {
            rx_length = sizeof(rx_buffer);
            if (nic->ops->receive_packet(nic, rx_buffer, &rx_length) == 0) {
                api_process_received_packet(rx_buffer, rx_length, nic->index);
            }
        }

        outw(nic->io_base + _3C515_TX_COMMAND_REG,
             _3C515_TX_CMD_ACK_INTR | _3C515_TX_STATUS_RX_COMPLETE);
        return 1;
    }

    /* Handle general TX completion */
    if (status & _3C515_TX_STATUS_TX_COMPLETE) {
        *event_type = EVENT_TYPE_TX_COMPLETE;
        stats_increment_tx_packets();
        stats_add_tx_bytes(1514);

        outw(nic->io_base + _3C515_TX_COMMAND_REG,
             _3C515_TX_CMD_ACK_INTR | _3C515_TX_STATUS_TX_COMPLETE);
        return 1;
    }

    /* Handle statistics counter overflow */
    if (status & _3C515_TX_STATUS_STATS_FULL) {
        *event_type = EVENT_TYPE_COUNTER_OVERFLOW;
        outw(nic->io_base + _3C515_TX_COMMAND_REG,
             _3C515_TX_CMD_ACK_INTR | _3C515_TX_STATUS_STATS_FULL);
        return 1;
    }

    return 0;
}

/**
 * @brief Enhanced interrupt handler with batching support
 */
int _3c515_handle_interrupt_batched(nic_info_t *nic) {
    interrupt_mitigation_context_t *im_ctx;

    if (!nic || !nic->private_data) {
        return ERROR_INVALID_PARAM;
    }

    im_ctx = (interrupt_mitigation_context_t *)nic->private_data;

    if (!is_interrupt_mitigation_enabled(im_ctx)) {
        _3c515_handle_interrupt(nic);
        return 1;
    }

    return process_batched_interrupts_3c515(im_ctx);
}

/**
 * @brief Enable interrupts for 3C515
 */
int _3c515_enable_interrupts(nic_info_t *nic) {
    uint16_t mask;

    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    /* Enable standard interrupt mask */
    mask = _3C515_TX_STATUS_TX_COMPLETE | _3C515_TX_STATUS_RX_COMPLETE |
           _3C515_TX_STATUS_UP_COMPLETE | _3C515_TX_STATUS_DOWN_COMPLETE |
           _3C515_TX_STATUS_ADAPTER_FAILURE;

    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_SET_INTR_ENB | mask);

    return SUCCESS;
}

/**
 * @brief Disable interrupts for 3C515
 */
int _3c515_disable_interrupts(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_SET_INTR_ENB | 0);

    return SUCCESS;
}

/**
 * @brief Get link status for 3C515
 */
int _3c515_get_link_status(nic_info_t *nic) {
    uint16_t media_status;

    if (!nic) {
        return 0;
    }

    _3C515_TX_SELECT_WINDOW(nic->io_base, _3C515_TX_WINDOW_4);
    media_status = inw(nic->io_base + _3C515_TX_W4_MEDIA);

    return (media_status & _3C515_TX_MEDIA_LNK) ? 1 : 0;
}

/**
 * @brief Get link speed for 3C515
 */
int _3c515_get_link_speed(nic_info_t *nic) {
    if (!nic) {
        return 0;
    }

    /* 3C515 can be 10 or 100 Mbps - check configuration */
    /* For now, return 10 as default - actual implementation would check hardware */
    return 10;
}
