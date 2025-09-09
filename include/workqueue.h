/**
 * @file workqueue.h
 * @brief Work queue interface for ISR deferral
 * 
 * Provides lock-free SPSC queues for deferring interrupt processing
 * to bottom-half workers.
 */

#ifndef WORKQUEUE_H
#define WORKQUEUE_H

#include <stdint.h>
#include <stdbool.h>

/* Work queue statistics */
struct workqueue_stats {
    uint32_t enqueued;      /* Total items enqueued */
    uint32_t dequeued;      /* Total items dequeued */
    uint32_t overruns;      /* Queue full events */
    uint32_t spurious;      /* Empty queue polls */
    uint16_t pending;       /* Currently pending items */
    uint16_t queue_size;    /* Queue capacity */
};

/* Work item for internal use */
typedef struct {
    uint8_t type;
    uint8_t device_id;
    uint16_t data1;
    uint32_t data2;
    void *ptr;
    uint32_t timestamp;
} work_item_t;

/* Function prototypes */

/**
 * Initialize work queue system
 */
int workqueue_init(void);

/**
 * Create work queue for a device
 */
int workqueue_create(uint8_t device_id);

/**
 * Enqueue work items (called from ISR)
 */
int workqueue_enqueue_rx(uint8_t device_id, uint16_t length, void *buffer);
int workqueue_enqueue_tx_complete(uint8_t device_id, uint16_t descriptor_id);
int workqueue_enqueue_error(uint8_t device_id, uint16_t error_code, uint32_t error_data);

/**
 * Dequeue work item (called from worker)
 * Returns 1 if work item retrieved, 0 if no work
 */
int workqueue_dequeue(uint8_t device_id, work_item_t *item);

/**
 * Quick work availability checks
 */
bool workqueue_has_work(uint8_t device_id);
bool workqueue_has_any_work(void);

/**
 * Statistics and monitoring
 */
void workqueue_get_stats(uint8_t device_id, struct workqueue_stats *stats);
void workqueue_reset_stats(uint8_t device_id);

/**
 * Advanced operations
 */
int workqueue_drain(uint8_t device_id);
uint8_t workqueue_utilization(uint8_t device_id);
int workqueue_health_check(uint8_t device_id);

/**
 * ISR support
 * Get address of work pending flag for SMC patching
 */
volatile uint8_t *workqueue_get_pending_flag(uint8_t device_id);

#endif /* WORKQUEUE_H */