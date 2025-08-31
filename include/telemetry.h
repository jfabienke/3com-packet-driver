/**
 * @file telemetry.h
 * @brief Production telemetry and health monitoring for packet driver
 * 
 * GPT-5 A+ Enhancement: Comprehensive telemetry for production monitoring
 */

#ifndef _TELEMETRY_H_
#define _TELEMETRY_H_

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Comprehensive driver telemetry structure
 * 
 * All counters are designed to be atomically incremented from ISR
 * without requiring locks (single writer, multiple readers)
 */
typedef struct {
    /* TX Performance Metrics */
    uint32_t tx_packets;            /* Total packets transmitted */
    uint32_t tx_bytes;              /* Total bytes transmitted */
    uint32_t tx_completions;        /* Successful TX completions */
    uint32_t tx_timeouts;           /* TX timeout events */
    uint32_t tx_stalls_detected;    /* TX stall events detected */
    uint32_t tx_stalls_recovered;   /* TX stalls successfully recovered */
    uint32_t tx_resets;             /* TX unit resets performed */
    uint32_t tx_queue_full;         /* TX queue full events */
    uint32_t tx_bounce_uses;        /* Times bounce buffer used for TX */
    
    /* RX Performance Metrics */
    uint32_t rx_packets;            /* Total packets received */
    uint32_t rx_bytes;              /* Total bytes received */
    uint32_t rx_drops_no_buffer;    /* Drops due to no buffer available */
    uint32_t rx_drops_error;        /* Drops due to receive errors */
    uint32_t rx_crc_errors;         /* CRC error count */
    uint32_t rx_alignment_errors;   /* Alignment error count */
    uint32_t rx_overruns;           /* RX FIFO overrun events */
    uint32_t rx_bounce_uses;        /* Times bounce buffer used for RX */
    
    /* IRQ Health Metrics */
    uint32_t irq_count;             /* Total interrupts handled */
    uint32_t irq_spurious;          /* Spurious interrupts detected */
    uint32_t irq_shared;            /* Shared IRQ events */
    uint32_t irq_poll_fallbacks;    /* Times switched to polled mode */
    uint32_t irq_max_duration;      /* Maximum ISR duration in ticks */
    uint32_t irq_budget_exceeded;   /* ISR budget overflow events */
    
    /* DMA Health Metrics */
    uint32_t dma_mapping_success;   /* Successful DMA mappings */
    uint32_t dma_mapping_failures;  /* DMA mapping failures */
    uint32_t dma_constraint_violations; /* Constraint check failures */
    uint32_t dma_boundary_splits;   /* 64KB boundary split operations */
    uint32_t dma_vds_lock_failures; /* VDS lock failures */
    uint32_t dma_vds_unlock_failures; /* VDS unlock failures */
    
    /* Queue Health Metrics */
    uint16_t queue_tx_high_water;   /* TX queue maximum depth seen */
    uint16_t queue_rx_high_water;   /* RX queue maximum depth seen */
    uint32_t queue_overflow_events; /* Queue overflow occurrences */
    uint32_t queue_overflow_recoveries; /* Successful overflow recoveries */
    uint32_t queue_seqlock_retries; /* Seqlock retry events */
    uint32_t queue_cli_fallbacks;   /* Times fell back to CLI */
    
    /* Link Status Metrics */
    uint32_t link_up_transitions;   /* Link up events */
    uint32_t link_down_transitions; /* Link down events */
    uint32_t link_speed_changes;    /* Speed change events */
    uint32_t link_duplex_changes;   /* Duplex change events */
    uint16_t link_current_speed;    /* Current speed (10/100) */
    bool link_current_duplex;       /* Current duplex (half/full) */
    bool link_current_status;       /* Current link status */
    
    /* System Health Indicators */
    uint32_t uptime_ticks;          /* Driver uptime in BIOS ticks */
    uint32_t last_tx_timestamp;     /* Last successful TX timestamp */
    uint32_t last_rx_timestamp;     /* Last successful RX timestamp */
    uint32_t watchdog_checks;       /* Watchdog check count */
    uint32_t watchdog_triggers;     /* Watchdog trigger count */
    uint32_t memory_alloc_failures; /* Memory allocation failures */
    
    /* Self-test Results */
    bool self_test_passed;          /* Self-test status */
    uint16_t self_test_code;        /* Self-test result code */
    
    /* VDS Integration Metrics (GPT-5 A+) */
    bool vds_available;              /* VDS services available */
    uint8_t vds_version_major;       /* VDS major version */
    uint8_t vds_version_minor;       /* VDS minor version */
    uint32_t vds_lock_successes;     /* VDS lock successes */
    uint32_t vds_lock_failures;      /* VDS lock failures */
    uint32_t vds_buffer_remaps;      /* VDS buffer remappings */
    uint16_t vds_last_error;         /* Last VDS error code */
    
} driver_telemetry_t;

/* Global telemetry instance */
extern driver_telemetry_t g_telemetry;

/* Telemetry management functions */
void telemetry_init(void);
void telemetry_update_link_status(bool up, uint16_t speed, bool full_duplex);
void telemetry_record_tx_completion(uint32_t bytes, bool bounce_used);
void telemetry_record_rx_packet(uint32_t bytes, bool bounce_used);
void telemetry_record_tx_timeout(void);
void telemetry_record_irq(uint32_t duration_ticks);
void telemetry_record_dma_mapping(bool success, bool constraint_violation);
void telemetry_update_queue_depth(uint16_t tx_depth, uint16_t rx_depth);
void telemetry_print_summary(void);
void telemetry_get_snapshot(driver_telemetry_t *snapshot);

/* Inline helpers for atomic increments from ISR */
static inline void telemetry_inc_tx_packets(void) {
    g_telemetry.tx_packets++;
}

static inline void telemetry_inc_rx_packets(void) {
    g_telemetry.rx_packets++;
}

static inline void telemetry_inc_irq_count(void) {
    g_telemetry.irq_count++;
}

#endif /* _TELEMETRY_H_ */