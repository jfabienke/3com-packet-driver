/**
 * @file smc_enhanced.h
 * @brief Enhanced SMC interface with all optimization techniques
 */

#ifndef SMC_ENHANCED_H
#define SMC_ENHANCED_H

#include <stdint.h>

/* SMC configuration structure */
struct smc_config {
    uint16_t copy_break_threshold;  /* Copy-break threshold in bytes */
    uint8_t k_pkts;                 /* Interrupt coalescing threshold */
    uint8_t doorbell_batch;         /* Doorbell batching threshold */
    uint8_t rx_batch_size;          /* RX batch processing size */
    uint8_t window_optimize;        /* Enable window optimization */
    uint16_t reserved;              /* Padding for alignment */
};

/* SMC performance statistics */
struct smc_stats {
    uint32_t isr_calls;             /* Total ISR invocations */
    uint32_t work_generated;        /* Work items generated */
    uint32_t copy_break_small;      /* Small packets (copy-break) */
    uint32_t copy_break_large;      /* Large packets (zero-copy) */
    uint32_t interrupts_coalesced;  /* Interrupts coalesced */
    uint32_t doorbells_batched;     /* Doorbells batched */
};

/* Device structure for SMC (simplified) */
struct el3_device {
    uint32_t magic;                 /* Magic number */
    uint32_t flags;                 /* Device flags */
    uint16_t iobase;                /* I/O base address */
    uint8_t irq;                    /* IRQ number */
    uint8_t generation;             /* NIC generation (0=Vortex, 1=Boomerang, etc) */
    uint8_t device_id;              /* Device ID for work queues */
    uint8_t reserved[3];            /* Padding */
};

/* Function prototypes */

/**
 * Install enhanced SMC hooks for a device
 * Combines all optimization techniques
 */
void el3_install_enhanced_smc_hooks(struct el3_device *dev, struct smc_config *config);

/**
 * Enhanced entry points (called via SMC patches)
 */
void el3_enhanced_isr_entry(void);
void el3_enhanced_tx_entry(void *buffer, uint16_t length, uint8_t device_id);
void el3_enhanced_rx_entry(void *buffer, uint16_t max_length, uint8_t device_id);

/**
 * Configuration and statistics
 */
void el3_set_smc_config(struct smc_config *config);
void el3_get_smc_stats(struct smc_stats *stats);

/**
 * CPU-specific configuration presets
 */

/* Configuration for 286 systems */
#define SMC_CONFIG_286 { \
    .copy_break_threshold = 512,    /* Larger threshold - avoid slow copies */ \
    .k_pkts = 16,                   /* Aggressive coalescing */ \
    .doorbell_batch = 8,            /* More batching */ \
    .rx_batch_size = 64,            /* Large RX batches */ \
    .window_optimize = 0,           /* N/A for 3C515 */ \
    .reserved = 0 \
}

/* Configuration for 386/486 systems */
#define SMC_CONFIG_386 { \
    .copy_break_threshold = 256,    /* Moderate threshold */ \
    .k_pkts = 8,                    /* Standard coalescing */ \
    .doorbell_batch = 4,            /* Standard batching */ \
    .rx_batch_size = 32,            /* Standard RX batches */ \
    .window_optimize = 1,           /* Enable for Vortex */ \
    .reserved = 0 \
}

/* Configuration for Pentium+ systems */
#define SMC_CONFIG_PENTIUM { \
    .copy_break_threshold = 192,    /* Standard threshold */ \
    .k_pkts = 4,                    /* Less coalescing - CPU can handle */ \
    .doorbell_batch = 2,            /* Less batching needed */ \
    .rx_batch_size = 16,            /* Smaller batches for lower latency */ \
    .window_optimize = 1,           /* Enable for Vortex */ \
    .reserved = 0 \
}

/**
 * Helper macros for SMC integration
 */

/* Initialize SMC for a device with CPU-appropriate config */
#define SMC_INIT_FOR_CPU(dev, cpu_type) do { \
    struct smc_config config = SMC_CONFIG_##cpu_type; \
    el3_install_enhanced_smc_hooks(dev, &config); \
} while(0)

/* Update SMC configuration at runtime */
#define SMC_UPDATE_THRESHOLD(threshold) do { \
    struct smc_config config; \
    config.copy_break_threshold = threshold; \
    el3_set_smc_config(&config); \
} while(0)

#endif /* SMC_ENHANCED_H */