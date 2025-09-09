/**
 * @file tx_lazy_irq.h
 * @brief Lazy TX interrupt optimization interface
 * 
 * Reduces TX interrupt rate by requesting interrupts only every K packets
 * or when the queue becomes empty, as specified in DRIVER_TUNING.md.
 */

#ifndef _TX_LAZY_IRQ_H_
#define _TX_LAZY_IRQ_H_

#include <stdint.h>
#include <stdbool.h>

/* Maximum NICs supported */
#define MAX_NICS 4

/* TX descriptor status bits */
#define TX_COMPLETE     0x0001      /* TX completed */
#define TX_INT_BIT      0x8000      /* Request TX interrupt */
#define LAST_FRAG       0x80000000  /* Last fragment flag */

/* Boomerang/Cyclone/Tornado TX descriptor */
typedef struct {
    uint32_t next;          /* Next descriptor pointer */
    uint32_t status;        /* Status and control */
    uint32_t buf_addr;      /* Buffer physical address */
    uint32_t len;           /* Length and flags */
} boomerang_tx_desc_t;

/* TX lazy IRQ statistics */
typedef struct {
    uint32_t total_packets;         /* Total packets transmitted */
    uint32_t total_interrupts;      /* Total TX interrupts requested */
    uint32_t irq_reduction_percent; /* Percentage reduction in IRQs */
    uint32_t empty_queue_irqs;      /* IRQs due to empty queue */
    uint32_t threshold_irqs;        /* IRQs due to K_PKTS threshold */
    uint32_t packets_per_irq;       /* Average packets per interrupt */
} tx_lazy_stats_t;

/**
 * @brief Initialize lazy TX IRQ for a NIC
 * 
 * @param nic_index Index of the NIC (0-3)
 */
void tx_lazy_init(uint8_t nic_index);

/**
 * @brief Determine if TX descriptor should request interrupt
 * 
 * @param nic_index NIC index
 * @return true if TX_INT_BIT should be set, false otherwise
 */
bool tx_lazy_should_interrupt(uint8_t nic_index);

/**
 * @brief Post TX packet with lazy IRQ for Boomerang/Cyclone/Tornado
 * 
 * @param nic_index NIC index
 * @param buf_phys Physical address of packet buffer
 * @param len Packet length
 * @param desc TX descriptor to fill
 */
void tx_lazy_post_boomerang(uint8_t nic_index, uint32_t buf_phys, 
                            uint16_t len, void *desc);

/**
 * @brief Post TX packet for Vortex (PIO mode)
 * 
 * @param nic_index NIC index
 * @param len Packet length
 */
void tx_lazy_post_vortex(uint8_t nic_index, uint16_t len);

/**
 * @brief Batch TX completion handler
 * 
 * @param nic_index NIC index
 * @param ring TX descriptor ring
 * @param free_func Function to free TX buffer
 * @return Number of descriptors reclaimed
 */
uint16_t tx_lazy_reclaim_batch(uint8_t nic_index, void *ring,
                               void (*free_func)(uint32_t));

/**
 * @brief Get lazy TX statistics
 * 
 * @param nic_index NIC index
 * @param stats Output statistics structure
 */
void tx_lazy_get_stats(uint8_t nic_index, tx_lazy_stats_t *stats);

/**
 * @brief Reset lazy TX statistics
 * 
 * @param nic_index NIC index
 */
void tx_lazy_reset_stats(uint8_t nic_index);

#endif /* _TX_LAZY_IRQ_H_ */