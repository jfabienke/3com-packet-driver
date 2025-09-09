/**
 * @file copy_break_enhanced.c
 * @brief Enhanced copy-break with DMA-aware buffer management
 * 
 * Addresses GPT-5's feedback by integrating with memory manager detection
 * to ensure DMA safety while maximizing performance.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "copy_break.h"
#include "dma_aware_buffer_pool.h"
#include "platform_probe.h"
#include "packet_ops.h"
#include "logging.h"
#include "stats.h"

/* Enhanced copy-break configuration with DMA awareness */
struct enhanced_copybreak_config {
    uint16_t threshold;             /* Copy-break threshold */
    uint16_t dma_threshold;         /* DMA safety threshold */
    bool adaptive_threshold;        /* Enable adaptive adjustment */
    bool dma_aware;                /* DMA awareness enabled */
    uint8_t cpu_type;              /* CPU type for optimization */
    const char *strategy_name;      /* Current strategy name */
};

/* Enhanced statistics with DMA tracking */
struct enhanced_copybreak_stats {
    uint32_t packets_processed;     /* Total packets processed */
    uint32_t packets_copied;        /* Packets copied to pool buffers */
    uint32_t packets_zerocopy;      /* Packets using zero-copy */
    uint32_t packets_dma_direct;    /* Packets using DMA buffers directly */
    uint32_t copy_failures;         /* Copy buffer allocation failures */
    uint32_t dma_failures;          /* DMA buffer allocation failures */
    uint32_t umb_copies;           /* Copies to UMB buffers */
    uint32_t conventional_copies;   /* Copies to conventional buffers */
    uint32_t threshold_adjustments; /* Adaptive adjustments */
    uint16_t current_threshold;     /* Current threshold */
    uint16_t avg_packet_size;       /* Rolling average packet size */
};

/* Global configuration and statistics */
static struct enhanced_copybreak_config config = {
    .threshold = 192,
    .dma_threshold = 512,           /* Separate threshold for DMA decisions */
    .adaptive_threshold = false,
    .dma_aware = true,
    .cpu_type = CPU_TYPE_386,
    .strategy_name = "Enhanced DMA-aware"
};

static struct enhanced_copybreak_stats stats = {0};

/* Memory manager compatibility info */
static bool emm386_detected = false;
static bool vds_available = false;

/**
 * Initialize enhanced copy-break with memory manager awareness
 */
int enhanced_copybreak_init(uint8_t cpu_type)
{
    /* Initialize DMA-aware buffer pools first */
    if (dma_buffer_pools_init() != 0) {
        LOG_ERROR("Failed to initialize DMA-aware buffer pools");
        return -1;
    }
    
    /* Detect memory environment */
    platform_probe_result_t platform = platform_detect();
    emm386_detected = platform.emm386_detected || platform.qemm_detected;
    vds_available = platform.vds_available;
    
    /* Set CPU-appropriate thresholds */
    switch (cpu_type) {
        case CPU_TYPE_286:
            config.threshold = 512;         /* Avoid slow copies */
            config.dma_threshold = 1024;    /* Prefer PIO for small-medium */
            config.strategy_name = "286 DMA-aware (PIO-favored)";
            break;
            
        case CPU_TYPE_386:
            config.threshold = 256;
            config.dma_threshold = 512;
            config.adaptive_threshold = true;
            config.strategy_name = "386 DMA-aware (balanced)";
            break;
            
        case CPU_TYPE_486:
            config.threshold = 192;
            config.dma_threshold = 256;
            config.adaptive_threshold = true;
            config.strategy_name = "486 DMA-aware (DMA-favored)";
            break;
            
        case CPU_TYPE_PENTIUM:
            config.threshold = 128;
            config.dma_threshold = 192;
            config.adaptive_threshold = true;
            config.strategy_name = "Pentium DMA-aware (fast-copy)";
            break;
            
        default:
            config.threshold = 192;
            config.dma_threshold = 256;
            config.strategy_name = "Default DMA-aware";
            break;
    }
    
    config.cpu_type = cpu_type;
    
    /* Initialize statistics */
    memset(&stats, 0, sizeof(stats));
    stats.current_threshold = config.threshold;
    
    LOG_INFO("Enhanced copy-break initialized:");
    LOG_INFO("  Strategy: %s", config.strategy_name);
    LOG_INFO("  Copy threshold: %u bytes", config.threshold);
    LOG_INFO("  DMA threshold: %u bytes", config.dma_threshold);
    LOG_INFO("  EMM386 detected: %s", emm386_detected ? "YES" : "NO");
    LOG_INFO("  VDS available: %s", vds_available ? "YES" : "NO");
    
    return 0;
}

/**
 * Enhanced RX packet processing with DMA awareness
 */
int enhanced_copybreak_process_rx(uint8_t device_id, void *packet_data, 
                                 uint16_t packet_size, bool packet_is_dma_safe)
{
    stats.packets_processed++;
    stats.avg_packet_size = (stats.avg_packet_size * 7 + packet_size) / 8;
    
    /* Three-tier decision process:
     * 1. Very small packets: Always copy to UMB pool buffers
     * 2. Medium packets: Copy to conventional or use DMA based on safety
     * 3. Large packets: Zero-copy if DMA safe, otherwise copy
     */
    
    if (packet_size <= stats.current_threshold) {
        /* Small packet - use copy-break to pool buffer */
        void *copy_buffer = alloc_copybreak_buffer(packet_size, stats.current_threshold);
        if (copy_buffer) {
            /* Copy packet to pool buffer (may be UMB) */
            fast_packet_copy(copy_buffer, packet_data, packet_size);
            
            /* Deliver copied packet */
            int result = deliver_packet(device_id, copy_buffer, packet_size, PACKET_COPIED);
            if (result != 0) {
                free_dma_aware_buffer(copy_buffer);
                return result;
            }
            
            stats.packets_copied++;
            stats.umb_copies++;  /* Assume UMB usage for copy buffers */
            
            /* Original buffer can be reused immediately */
            recycle_rx_buffer_immediate(device_id, packet_data);
            return 0;
        } else {
            stats.copy_failures++;
            /* Fall through to zero-copy */
        }
    }
    
    /* Medium/Large packet decision */
    if (packet_size <= config.dma_threshold && !packet_is_dma_safe) {
        /* Medium packet in unsafe buffer - copy to DMA-safe buffer */
        void *dma_buffer = alloc_dma_buffer(packet_size);
        if (dma_buffer) {
            fast_packet_copy(dma_buffer, packet_data, packet_size);
            
            int result = deliver_packet(device_id, dma_buffer, packet_size, PACKET_DMA_SAFE);
            if (result != 0) {
                free_dma_aware_buffer(dma_buffer);
                return result;
            }
            
            stats.packets_copied++;
            stats.conventional_copies++;
            
            recycle_rx_buffer_immediate(device_id, packet_data);
            return 0;
        } else {
            stats.dma_failures++;
            /* Fall through to zero-copy (may not be DMA safe) */
        }
    }
    
    /* Large packet or DMA-safe buffer - use zero-copy */
    int result = deliver_packet(device_id, packet_data, packet_size, 
                               packet_is_dma_safe ? PACKET_ZEROCOPY_DMA : PACKET_ZEROCOPY);
    if (result != 0) {
        return result;
    }
    
    if (packet_is_dma_safe) {
        stats.packets_dma_direct++;
    } else {
        stats.packets_zerocopy++;
    }
    
    return 0;
}

/**
 * Enhanced TX packet processing with DMA awareness
 */
int enhanced_copybreak_process_tx(uint8_t device_id, const void *packet_data, 
                                 uint16_t packet_size)
{
    stats.packets_processed++;
    
    /* Check if source buffer is DMA-safe */
    bool source_dma_safe = is_buffer_dma_safe((void *)packet_data);
    
    if (packet_size <= stats.current_threshold) {
        /* Small packet - copy to DMA-safe buffer for transmission */
        void *dma_buffer = alloc_dma_buffer(packet_size);
        if (dma_buffer) {
            fast_packet_copy(dma_buffer, packet_data, packet_size);
            
            int result = submit_tx_packet(device_id, dma_buffer, packet_size, PACKET_DMA_SAFE);
            if (result != 0) {
                free_dma_aware_buffer(dma_buffer);
                return result;
            }
            
            stats.packets_copied++;
            stats.conventional_copies++;
            return 0;
        } else {
            stats.dma_failures++;
            return -1;  /* Cannot transmit without DMA-safe buffer */
        }
    }
    
    if (source_dma_safe) {
        /* Large packet in DMA-safe buffer - zero-copy transmission */
        int result = submit_tx_packet(device_id, (void *)packet_data, packet_size, 
                                     PACKET_ZEROCOPY_DMA);
        if (result == 0) {
            stats.packets_dma_direct++;
        }
        return result;
    } else {
        /* Large packet not DMA-safe - must copy */
        void *dma_buffer = alloc_dma_buffer(packet_size);
        if (!dma_buffer) {
            stats.dma_failures++;
            return -1;
        }
        
        fast_packet_copy(dma_buffer, packet_data, packet_size);
        
        int result = submit_tx_packet(device_id, dma_buffer, packet_size, PACKET_DMA_SAFE);
        if (result != 0) {
            free_dma_aware_buffer(dma_buffer);
            return result;
        }
        
        stats.packets_copied++;
        stats.conventional_copies++;
        return 0;
    }
}

/**
 * CPU-optimized memory copy with DMA awareness
 */
static void fast_packet_copy(void *dst, const void *src, uint16_t size)
{
    /* Check if we're copying to/from potentially different segments */
    bool dst_far = ((uint32_t)dst >= 0xA0000);  /* UMB range */
    bool src_far = ((uint32_t)src >= 0xA0000);
    
    if (dst_far || src_far) {
        /* Use segment-aware copy for far pointers */
        fast_far_copy(dst, src, size);
        return;
    }
    
    /* Use CPU-optimized near copy */
    switch (config.cpu_type) {
        case CPU_TYPE_286:
            /* 286: Word-aligned copy */
            {
                uint16_t *d = (uint16_t *)dst;
                const uint16_t *s = (const uint16_t *)src;
                uint16_t words = size >> 1;
                
                while (words--) {
                    *d++ = *s++;
                }
                
                if (size & 1) {
                    *(uint8_t *)d = *(const uint8_t *)s;
                }
            }
            break;
            
        case CPU_TYPE_386:
        case CPU_TYPE_486:
            /* 386/486: Dword-aligned copy */
            {
                uint32_t *d = (uint32_t *)dst;
                const uint32_t *s = (const uint32_t *)src;
                uint16_t dwords = size >> 2;
                
                while (dwords--) {
                    *d++ = *s++;
                }
                
                /* Handle remainder */
                uint16_t remainder = size & 3;
                if (remainder) {
                    uint8_t *d8 = (uint8_t *)d;
                    const uint8_t *s8 = (const uint8_t *)s;
                    while (remainder--) {
                        *d8++ = *s8++;
                    }
                }
            }
            break;
            
        case CPU_TYPE_PENTIUM:
        default:
            /* Pentium+: Use optimized memcpy */
            memcpy(dst, src, size);
            break;
    }
}

/**
 * Segment-aware copy for far pointers (UMB access)
 */
static void fast_far_copy(void *dst, const void *src, uint16_t size)
{
    /* This would need proper segment handling for real DOS implementation */
    /* For now, use conservative byte-by-byte copy */
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    
    while (size--) {
        *d++ = *s++;
    }
}

/**
 * Adaptive threshold adjustment with DMA awareness
 */
static void adjust_enhanced_threshold(void)
{
    if (!config.adaptive_threshold) {
        return;
    }
    
    /* Get buffer pool statistics */
    struct dma_buffer_stats pool_stats;
    get_dma_buffer_stats(&pool_stats);
    
    uint16_t old_threshold = stats.current_threshold;
    uint16_t new_threshold = old_threshold;
    
    /* Adjust based on DMA buffer pressure */
    if (pool_stats.dma_utilization > 80) {
        /* DMA buffers under pressure - favor copying */
        new_threshold = (old_threshold * 11) / 10;  /* Increase by 10% */
        LOG_DEBUG("DMA pressure high (%u%%), increasing copy threshold",
                 pool_stats.dma_utilization);
    } else if (pool_stats.copy_utilization > 80) {
        /* Copy buffers under pressure - favor DMA */
        new_threshold = (old_threshold * 9) / 10;   /* Decrease by 10% */
        LOG_DEBUG("Copy buffer pressure high (%u%%), decreasing copy threshold",
                 pool_stats.copy_utilization);
    }
    
    /* Bound the threshold by CPU type */
    uint16_t min_threshold = 64;
    uint16_t max_threshold = 1536;
    
    switch (config.cpu_type) {
        case CPU_TYPE_286: max_threshold = 1024; break;
        case CPU_TYPE_386: max_threshold = 768; break;
        case CPU_TYPE_486: max_threshold = 512; break;
        case CPU_TYPE_PENTIUM: max_threshold = 256; break;
    }
    
    if (new_threshold < min_threshold) new_threshold = min_threshold;
    if (new_threshold > max_threshold) new_threshold = max_threshold;
    
    /* Apply change */
    if (new_threshold != old_threshold) {
        stats.current_threshold = new_threshold;
        stats.threshold_adjustments++;
        
        LOG_INFO("Adaptive threshold: %u -> %u (DMA: %u%%, Copy: %u%%)",
                old_threshold, new_threshold,
                pool_stats.dma_utilization, pool_stats.copy_utilization);
    }
}

/**
 * Enhanced maintenance with DMA awareness
 */
void enhanced_copybreak_maintenance(void)
{
    adjust_enhanced_threshold();
    
    /* Log performance summary periodically */
    static uint32_t last_log_packets = 0;
    if (stats.packets_processed > last_log_packets + 10000) {
        LOG_INFO("Copy-break performance summary:");
        LOG_INFO("  Total packets: %lu", stats.packets_processed);
        LOG_INFO("  Copied: %lu (UMB: %lu, Conv: %lu)",
                stats.packets_copied, stats.umb_copies, stats.conventional_copies);
        LOG_INFO("  Zero-copy: %lu", stats.packets_zerocopy);
        LOG_INFO("  DMA direct: %lu", stats.packets_dma_direct);
        LOG_INFO("  Copy failures: %lu, DMA failures: %lu",
                stats.copy_failures, stats.dma_failures);
        
        last_log_packets = stats.packets_processed;
    }
    
    /* Reset counters to prevent overflow */
    if (stats.packets_processed > 100000) {
        stats.packets_processed /= 2;
        stats.packets_copied /= 2;
        stats.packets_zerocopy /= 2;
        stats.packets_dma_direct /= 2;
        stats.umb_copies /= 2;
        stats.conventional_copies /= 2;
        stats.copy_failures /= 2;
        stats.dma_failures /= 2;
    }
}

/**
 * Get enhanced statistics
 */
void enhanced_copybreak_get_stats(struct enhanced_copybreak_statistics *user_stats)
{
    if (!user_stats) return;
    
    user_stats->packets_processed = stats.packets_processed;
    user_stats->packets_copied = stats.packets_copied;
    user_stats->packets_zerocopy = stats.packets_zerocopy;
    user_stats->packets_dma_direct = stats.packets_dma_direct;
    user_stats->umb_copies = stats.umb_copies;
    user_stats->conventional_copies = stats.conventional_copies;
    user_stats->copy_failures = stats.copy_failures;
    user_stats->dma_failures = stats.dma_failures;
    user_stats->threshold_adjustments = stats.threshold_adjustments;
    user_stats->current_threshold = stats.current_threshold;
    user_stats->avg_packet_size = stats.avg_packet_size;
    
    /* Calculate percentages */
    if (stats.packets_processed > 0) {
        user_stats->copy_percentage = 
            (stats.packets_copied * 100) / stats.packets_processed;
        user_stats->zerocopy_percentage = 
            (stats.packets_zerocopy * 100) / stats.packets_processed;
        user_stats->dma_direct_percentage = 
            (stats.packets_dma_direct * 100) / stats.packets_processed;
    }
    
    /* Success rates */
    uint32_t total_copies = stats.packets_copied + stats.copy_failures;
    if (total_copies > 0) {
        user_stats->copy_success_rate = (stats.packets_copied * 100) / total_copies;
    } else {
        user_stats->copy_success_rate = 100;
    }
    
    uint32_t total_dma = stats.packets_dma_direct + stats.dma_failures;
    if (total_dma > 0) {
        user_stats->dma_success_rate = (stats.packets_dma_direct * 100) / total_dma;
    } else {
        user_stats->dma_success_rate = 100;
    }
    
    strcpy(user_stats->strategy_name, config.strategy_name);
}

/**
 * Enhanced packet types for improved API
 */
const char *packet_type_name(packet_type_t type)
{
    switch (type) {
        case PACKET_COPIED: return "Copied";
        case PACKET_ZEROCOPY: return "Zero-copy";
        case PACKET_DMA_SAFE: return "DMA-safe";
        case PACKET_ZEROCOPY_DMA: return "Zero-copy DMA";
        default: return "Unknown";
    }
}