/**
 * @file rx_batch_refill.h
 * @brief Batched RX buffer refill interface
 * 
 * Reduces doorbell writes by refilling multiple RX buffers in batch
 * with a single UP_LIST_PTR write, as specified in DRIVER_TUNING.md.
 * 
 * Ring Management:
 * - head: Points to next descriptor NIC will fill (producer)
 * - tail: Scan pointer for next free slot to refill (consumer)
 *        Note: tail may skip over NIC-owned descriptors during scan
 * - available: Count of descriptors with valid packets
 */

#ifndef _RX_BATCH_REFILL_H_
#define _RX_BATCH_REFILL_H_

#include <stdint.h>
#include <stdbool.h>

/* RX batch statistics */
typedef struct {
    uint32_t total_packets;         /* Total packets received */
    uint32_t copy_break_count;      /* Packets handled via copy-break */
    uint32_t copy_break_percent;    /* Percentage using copy-break */
    uint32_t bulk_refills;          /* Number of bulk refill operations */
    uint32_t doorbell_writes;       /* Total UP_LIST_PTR writes */
    uint32_t packets_per_doorbell;  /* Average packets per doorbell */
} rx_batch_stats_t;

/**
 * @brief Initialize RX batch refill for a NIC
 * 
 * @param nic_index Index of the NIC (0-3)
 * @param io_base I/O base address
 * @param ring_virt Virtual address of RX descriptor ring
 * @param ring_phys Physical address of RX descriptor ring
 * @return 0 on success, -1 on error
 */
int rx_batch_init(uint8_t nic_index, uint16_t io_base, void *ring_virt,
                   uint32_t ring_phys);

/**
 * @brief RX worker - bottom half processing
 * 
 * Processes received packets with budget and copy-break optimization.
 * Should be called from main loop when work_pending is set.
 * 
 * @param nic_index NIC index
 */
void rx_batch_worker(uint8_t nic_index);

/**
 * @brief Process single RX packet (non-batched mode)
 * 
 * For compatibility with existing code that doesn't use batching.
 * 
 * @param nic_index NIC index
 * @return 1 if packet processed, 0 if no packet, -1 on error
 */
int rx_batch_process_single(uint8_t nic_index);

/**
 * @brief Get RX batch statistics
 * 
 * @param nic_index NIC index
 * @param stats Output statistics structure
 */
void rx_batch_get_stats(uint8_t nic_index, rx_batch_stats_t *stats);

/**
 * @brief Tiny ISR for RX interrupts
 * 
 * Minimal interrupt handler that just ACKs and marks work.
 * Must be installed as the actual interrupt vector.
 */
void __interrupt __far rx_batch_isr(void);

/**
 * @brief Check if RX work is pending
 * 
 * @param nic_index NIC index
 * @return true if work pending, false otherwise
 */
static inline bool rx_batch_work_pending(uint8_t nic_index) {
    extern volatile uint8_t rx_work_pending[];
    return (nic_index < 4) ? rx_work_pending[nic_index] : false;
}

#endif /* _RX_BATCH_REFILL_H_ */