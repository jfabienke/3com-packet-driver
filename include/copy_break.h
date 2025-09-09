/**
 * @file copy_break.h
 * @brief Copy-break optimization interface
 */

#ifndef COPY_BREAK_H
#define COPY_BREAK_H

#include <stdint.h>
#include <stdbool.h>

/* CPU types for optimization */
#define CPU_TYPE_286        1
#define CPU_TYPE_386        2
#define CPU_TYPE_486        3
#define CPU_TYPE_PENTIUM    4

/* Packet types */
typedef enum {
    PACKET_COPIED = 0,      /* Packet was copied to pool buffer */
    PACKET_ZEROCOPY = 1     /* Packet uses original buffer (zero-copy) */
} packet_type_t;

/* Copy-break statistics */
struct copybreak_statistics {
    uint32_t packets_processed;     /* Total packets processed */
    uint32_t packets_copied;        /* Packets that were copied */
    uint32_t packets_zerocopy;      /* Packets that used zero-copy */
    uint32_t copy_failures;         /* Copy attempts that failed */
    uint32_t zerocopy_failures;     /* Zero-copy attempts that failed */
    uint32_t threshold_adjustments; /* Adaptive threshold changes */
    uint16_t current_threshold;     /* Current threshold in bytes */
    uint16_t avg_packet_size;       /* Average packet size */
    uint8_t copy_percentage;        /* Percentage of packets copied */
    uint8_t zerocopy_percentage;    /* Percentage of packets zero-copy */
    uint8_t copy_success_rate;      /* Copy success rate % */
};

/* Function prototypes */

/**
 * Initialize copy-break system with CPU-appropriate settings
 */
int copybreak_init(uint8_t cpu_type);

/**
 * Process packets with copy-break optimization
 */
int copybreak_process_rx(uint8_t device_id, void *packet_data, uint16_t packet_size);
int copybreak_process_tx(uint8_t device_id, const void *packet_data, uint16_t packet_size);

/**
 * Configuration and tuning
 */
void copybreak_set_threshold(uint16_t threshold);
uint16_t copybreak_get_threshold(void);
void copybreak_set_adaptive(bool enable);

/**
 * Maintenance and monitoring
 */
void copybreak_maintenance(void);
void copybreak_get_stats(struct copybreak_statistics *stats);
void copybreak_reset_stats(void);
int copybreak_health_check(void);

/**
 * Integration points (implemented by driver)
 */
int deliver_packet(uint8_t device_id, void *buffer, uint16_t size, packet_type_t type);
void recycle_rx_buffer_immediate(uint8_t device_id, void *buffer);
void *get_tx_dma_buffer(uint8_t device_id, uint16_t size);
void free_tx_dma_buffer(uint8_t device_id, void *buffer);
int submit_tx_packet(uint8_t device_id, void *buffer, uint16_t size, packet_type_t type);
bool is_dma_safe(const void *buffer, uint16_t size);

/* Convenience macros */

/* Check if packet should use copy-break */
#define SHOULD_COPY(size) ((size) <= copybreak_get_threshold())

/* CPU-specific initialization */
#define COPYBREAK_INIT_FOR_CPU(cpu) copybreak_init(CPU_TYPE_##cpu)

/* Standard thresholds for reference */
#define COPYBREAK_THRESHOLD_286     512
#define COPYBREAK_THRESHOLD_386     256
#define COPYBREAK_THRESHOLD_486     192
#define COPYBREAK_THRESHOLD_PENTIUM 128

#endif /* COPY_BREAK_H */