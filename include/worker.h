/**
 * @file worker.h
 * @brief Bottom-half worker interface
 * 
 * Worker processes deferred interrupt work in batches
 */

#ifndef WORKER_H
#define WORKER_H

#include <stdint.h>

/* Worker statistics */
struct worker_stats {
    uint32_t total_processed;    /* Total work items processed */
    uint32_t rx_processed;       /* RX packets processed */
    uint32_t tx_processed;       /* TX completions processed */
    uint32_t errors_processed;   /* Error conditions processed */
    uint32_t budget_exceeded;    /* Times budget was exceeded */
    uint32_t empty_polls;        /* Polls with no work */
    uint16_t max_burst_size;     /* Largest work burst processed */
    uint16_t avg_burst_size;     /* Average work burst size */
    uint8_t efficiency;          /* Processing efficiency % */
    uint8_t rx_percentage;       /* % of work that was RX */
    uint8_t tx_percentage;       /* % of work that was TX */
    uint8_t error_percentage;    /* % of work that was errors */
};

/* Function prototypes */

/**
 * Worker processing functions
 */
int worker_process_all(void);           /* Process all pending work */
int worker_process_device(uint8_t device_id);  /* Process work for one device */
int worker_process_priority(void);     /* Process high priority work only */
int worker_process_adaptive(void);     /* Adaptive processing based on load */
int worker_process_batched(void);      /* Batch-optimized processing */

/**
 * Statistics and monitoring
 */
void worker_get_stats(struct worker_stats *stats);
void worker_reset_stats(void);
int worker_health_check(void);

/**
 * Integration points for packet handlers
 * These should be implemented by the actual driver
 */
int handle_rx_packet(uint8_t device_id, uint16_t length, void *buffer);
int handle_tx_complete(uint8_t device_id, uint16_t descriptor_id);
int handle_device_error(uint8_t device_id, uint16_t error_code, uint32_t error_data);
int schedule_rx_refill(uint8_t device_id);

#endif /* WORKER_H */