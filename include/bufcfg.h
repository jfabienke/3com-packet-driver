/**
 * @file buffer_config.h
 * @brief Auto-configuration for packet buffer sizing
 *
 * Intelligent buffer configuration based on:
 * - NIC type (3C509B vs 3C515-TX)
 * - Link speed (10 vs 100 Mbps)
 * - Transfer mode (PIO vs Bus Master)
 * - Available memory
 * - CPU capabilities
 */

#ifndef BUFFER_CONFIG_H
#define BUFFER_CONFIG_H

#include <stdint.h>

/* Buffer size options (must be DMA-safe) */
#define BUFSIZE_256     256     /* Minimal, always DMA-safe */
#define BUFSIZE_512     512     /* Good balance */
#define BUFSIZE_1024    1024    /* Performance oriented */
#define BUFSIZE_1536    1536    /* Full MTU, requires alignment */

/* Ring size limits */
#define MIN_TX_RING     4       /* Minimum TX descriptors */
#define MAX_TX_RING     32      /* Maximum TX descriptors */
#define MIN_RX_RING     8       /* Minimum RX descriptors */
#define MAX_RX_RING     32      /* Maximum RX descriptors */

/* Configuration presets */
#define CONFIG_MINIMAL  0x01    /* Force 3KB (256×12) */
#define CONFIG_STANDARD 0x02    /* Auto-detect optimal */
#define CONFIG_OPTIMAL  0x03    /* Force maximum (1536×32) */

/* Transfer modes */
typedef enum {
    XFER_MODE_AUTO,             /* 0: Auto-detect best mode */
    XFER_MODE_PIO,              /* 1: Programmed I/O */
    XFER_MODE_BUS_MASTER        /* 2: DMA bus mastering */
} transfer_mode_t;

/* NIC types - only define if nic_defs.h not already included */
#ifndef _NIC_DEFS_H_
typedef enum {
    NIC_UNKNOWN,                /* 0: Unknown NIC */
    NIC_3C509B,                 /* 1: EtherLink III (10 Mbps, PIO only) */
    NIC_3C515_TX                /* 2: Fast EtherLink (10/100, BM capable) */
} nic_type_t;
#endif

/* CPU classes for optimization */
/* Note: Values match CPU generation numbers for compatibility */
typedef enum {
    CPU_8086,                   /* 0: Not supported (need 286+) */
    CPU_80186,                  /* 1: Not supported (need 286+) */
    CPU_80286,                  /* 2: Minimum supported */
    CPU_80386,                  /* 3: 32-bit capable */
    CPU_80486,                  /* 4: Cache, BSWAP */
    CPU_PENTIUM,                /* 5: Dual pipeline */
    CPU_PENTIUM4                /* 6: CLFLUSH support */
} cpu_class_t;

/* Buffer configuration structure */
typedef struct {
    /* Hardware detection */
    nic_type_t      nic_type;          /* Detected NIC */
    uint8_t         link_speed;        /* 10 or 100 Mbps */
    uint8_t         bus_master_ok;     /* DMA test passed */
    cpu_class_t     cpu_class;         /* CPU generation */
    
    /* Memory availability */
    uint16_t        conventional_free;  /* KB free in 640KB */
    uint16_t        umb_free;          /* KB free in UMB */
    uint32_t        xms_free;          /* KB free in XMS */
    
    /* Calculated configuration */
    uint16_t        buffer_size;       /* Selected buffer size */
    uint8_t         tx_ring_count;     /* Number of TX buffers */
    uint8_t         rx_ring_count;     /* Number of RX buffers */
    transfer_mode_t transfer_mode;     /* PIO or BM */
    
    /* Memory requirements */
    uint16_t        total_buffer_memory; /* Total KB needed */
    uint8_t         use_umb;           /* Allocate in UMB */
    uint8_t         use_xms;           /* Use XMS for large buffers */
    
    /* XMS configuration (when use_xms = 1) */
    uint8_t         xms_buffers;       /* Number of XMS buffers */
    uint8_t         staging_buffers;   /* Number of staging buffers */
    uint16_t        xms_threshold;     /* Size threshold for XMS */
    
    /* Performance estimates */
    uint8_t         expected_throughput; /* Percentage of line rate */
    uint8_t         cpu_utilization;   /* Expected CPU usage % */
} buffer_config_t;

/* Command-line override structure */
typedef struct {
    uint16_t        buffer_size;       /* 0 = auto */
    uint8_t         tx_ring_count;     /* 0 = auto */
    uint8_t         rx_ring_count;     /* 0 = auto */
    uint8_t         force_pio;         /* Force PIO mode */
    uint8_t         force_minimal;     /* Force 3KB config */
    uint8_t         force_optimal;     /* Force max config */
} buffer_override_t;

/* Auto-configuration functions */
buffer_config_t auto_configure_buffers(void);
void apply_buffer_overrides(buffer_config_t* config, const buffer_override_t* override);
int validate_buffer_config(const buffer_config_t* config);
void display_buffer_config(const buffer_config_t* config);

/* Configuration helpers */
uint16_t calculate_buffer_memory(uint16_t buffer_size, uint8_t tx_count, uint8_t rx_count);
int check_dma_alignment(uint16_t buffer_size, uint16_t count);
transfer_mode_t select_transfer_mode(nic_type_t nic, uint8_t link_speed, uint8_t bus_master_ok);

/* Preset configurations */
void apply_minimal_config(buffer_config_t* config);
void apply_standard_config(buffer_config_t* config);
void apply_optimal_config(buffer_config_t* config);

/* Performance estimation */
uint8_t estimate_throughput(const buffer_config_t* config);
uint8_t estimate_cpu_usage(const buffer_config_t* config);

#endif /* BUFFER_CONFIG_H */
