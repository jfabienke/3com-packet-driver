/**
 * @file copy_break_enhanced.h
 * @brief Enhanced copy-break interface with DMA awareness
 */

#ifndef COPY_BREAK_ENHANCED_H
#define COPY_BREAK_ENHANCED_H

#include <stdint.h>
#include <stdbool.h>
#include "copy_break.h"

/* Enhanced packet types with DMA awareness */
typedef enum {
    PACKET_COPIED = 0,          /* Packet copied to pool buffer */
    PACKET_ZEROCOPY = 1,        /* Zero-copy (may not be DMA safe) */
    PACKET_DMA_SAFE = 2,        /* Copied to DMA-safe buffer */
    PACKET_ZEROCOPY_DMA = 3     /* Zero-copy DMA-safe buffer */
} enhanced_packet_type_t;

/* Enhanced copy-break statistics with DMA tracking */
struct enhanced_copybreak_statistics {
    uint32_t packets_processed;     /* Total packets processed */
    uint32_t packets_copied;        /* Packets copied to pool buffers */
    uint32_t packets_zerocopy;      /* Packets using zero-copy */
    uint32_t packets_dma_direct;    /* Packets using DMA buffers directly */
    uint32_t umb_copies;           /* Copies to UMB buffers */
    uint32_t conventional_copies;   /* Copies to conventional buffers */
    uint32_t copy_failures;         /* Copy buffer allocation failures */
    uint32_t dma_failures;          /* DMA buffer allocation failures */
    uint32_t threshold_adjustments; /* Adaptive threshold changes */
    uint16_t current_threshold;     /* Current threshold in bytes */
    uint16_t avg_packet_size;       /* Average packet size */
    
    /* Calculated percentages */
    uint8_t copy_percentage;        /* Percentage of packets copied */
    uint8_t zerocopy_percentage;    /* Percentage zero-copy */
    uint8_t dma_direct_percentage;  /* Percentage DMA direct */
    uint8_t copy_success_rate;      /* Copy success rate % */
    uint8_t dma_success_rate;       /* DMA success rate % */
    
    /* Strategy information */
    char strategy_name[32];         /* Current strategy name */
};

/* Function prototypes */

/**
 * Initialize enhanced copy-break with memory manager awareness
 */
int enhanced_copybreak_init(uint8_t cpu_type);

/**
 * Process packets with enhanced DMA-aware copy-break
 */
int enhanced_copybreak_process_rx(uint8_t device_id, void *packet_data, 
                                 uint16_t packet_size, bool packet_is_dma_safe);
int enhanced_copybreak_process_tx(uint8_t device_id, const void *packet_data, 
                                 uint16_t packet_size);

/**
 * Maintenance and monitoring
 */
void enhanced_copybreak_maintenance(void);
void enhanced_copybreak_get_stats(struct enhanced_copybreak_statistics *stats);

/**
 * Utility functions
 */
const char *packet_type_name(enhanced_packet_type_t type);

/**
 * Integration points (implemented by driver)
 */
int deliver_packet(uint8_t device_id, void *buffer, uint16_t size, enhanced_packet_type_t type);
int submit_tx_packet(uint8_t device_id, void *buffer, uint16_t size, enhanced_packet_type_t type);
bool is_buffer_dma_safe(void *buffer);

/* Internal functions */
static void fast_packet_copy(void *dst, const void *src, uint16_t size);
static void fast_far_copy(void *dst, const void *src, uint16_t size);

/* CPU-specific initialization macros */
#define ENHANCED_COPYBREAK_INIT_286()     enhanced_copybreak_init(CPU_TYPE_286)
#define ENHANCED_COPYBREAK_INIT_386()     enhanced_copybreak_init(CPU_TYPE_386)
#define ENHANCED_COPYBREAK_INIT_486()     enhanced_copybreak_init(CPU_TYPE_486)
#define ENHANCED_COPYBREAK_INIT_PENTIUM() enhanced_copybreak_init(CPU_TYPE_PENTIUM)

/* Strategy selection based on detected environment */
#define ENHANCED_COPYBREAK_INIT_AUTO() \
    enhanced_copybreak_init(detect_cpu_type())

/**
 * Memory manager adaptation summary:
 * 
 * | Environment     | Copy Strategy | DMA Strategy | UMB Usage |
 * |-----------------|---------------|--------------|-----------|
 * | Pure DOS        | Conv memory   | Conv memory  | N/A       |
 * | HIMEM only      | UMB + Conv    | Conv memory  | Copy only |
 * | EMM386/QEMM     | UMB + Conv    | Conv memory  | Copy only |
 * | VDS enabled     | UMB + Conv    | Conv + VDS   | Copy only |
 * | Windows Enh.    | Conv memory   | Conv memory  | None      |
 * 
 * Key principles:
 * - DMA buffers NEVER use UMB (addresses GPT-5 critical feedback)
 * - Copy buffers prefer UMB to preserve conventional memory
 * - Adaptive thresholds balance pool utilization
 * - CPU-specific optimizations for copy performance
 */

#endif /* COPY_BREAK_ENHANCED_H */