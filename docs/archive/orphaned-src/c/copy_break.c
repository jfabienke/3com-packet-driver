/**
 * @file copy_break.c
 * @brief Copy-break optimization implementation
 * 
 * Implements the copy-break algorithm that decides between copying small
 * packets to pool buffers vs zero-copy for large packets.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "copy_break.h"
#include "buffer_pool.h"
#include "packet_ops.h"
#include "logging.h"
#include "stats.h"

/* Copy-break configuration */
struct copybreak_config {
    uint16_t threshold;         /* Copy-break threshold in bytes */
    uint16_t alignment;         /* Required buffer alignment */
    bool adaptive_threshold;    /* Enable adaptive threshold adjustment */
    uint8_t cpu_type;          /* CPU type for optimization */
};

/* Copy-break statistics */
struct copybreak_stats {
    uint32_t packets_processed; /* Total packets processed */
    uint32_t packets_copied;    /* Packets that were copied */
    uint32_t packets_zerocopy;  /* Packets that used zero-copy */
    uint32_t copy_failures;     /* Copy attempts that failed */
    uint32_t zerocopy_failures; /* Zero-copy attempts that failed */
    uint32_t threshold_adjustments; /* Adaptive threshold changes */
    uint16_t current_threshold; /* Current adaptive threshold */
    uint16_t avg_packet_size;   /* Rolling average packet size */
};

/* Global configuration and statistics */
static struct copybreak_config config = {
    .threshold = 192,           /* Default threshold */
    .alignment = 16,            /* 16-byte alignment for cache lines */
    .adaptive_threshold = false,
    .cpu_type = CPU_TYPE_386    /* Conservative default */
};

static struct copybreak_stats stats = {0};

/* CPU-specific thresholds */
#define THRESHOLD_286       512     /* Large threshold - avoid slow copies */
#define THRESHOLD_386       256     /* Moderate threshold */
#define THRESHOLD_486       192     /* Standard threshold */
#define THRESHOLD_PENTIUM   128     /* Small threshold - fast copies */

/**
 * Initialize copy-break system
 */
int copybreak_init(uint8_t cpu_type)
{
    /* Set CPU-appropriate threshold */
    switch (cpu_type) {
        case CPU_TYPE_286:
            config.threshold = THRESHOLD_286;
            config.cpu_type = cpu_type;
            break;
            
        case CPU_TYPE_386:
            config.threshold = THRESHOLD_386;
            config.cpu_type = cpu_type;
            break;
            
        case CPU_TYPE_486:
            config.threshold = THRESHOLD_486;
            config.cpu_type = cpu_type;
            config.adaptive_threshold = true;  /* Enable on 486+ */
            break;
            
        case CPU_TYPE_PENTIUM:
            config.threshold = THRESHOLD_PENTIUM;
            config.cpu_type = cpu_type;
            config.adaptive_threshold = true;
            break;
            
        default:
            config.threshold = 192;  /* Safe default */
            config.cpu_type = CPU_TYPE_386;
            break;
    }
    
    /* Initialize statistics */
    memset(&stats, 0, sizeof(stats));
    stats.current_threshold = config.threshold;
    
    LOG_INFO("Copy-break initialized: threshold=%u, CPU type=%u",
             config.threshold, cpu_type);
    
    return 0;
}

/**
 * Fast copy-break decision (inline-friendly)
 */
static inline bool should_copy(uint16_t packet_size)
{
    return packet_size <= stats.current_threshold;
}

/**
 * Optimized memory copy for small packets
 */
static void fast_packet_copy(void *dst, const void *src, uint16_t size)
{
    /* Use CPU-optimized copy based on detected CPU type */
    switch (config.cpu_type) {
        case CPU_TYPE_286:
            /* 286: Use simple word copy to avoid slow string ops */
            {
                uint16_t *d = (uint16_t *)dst;
                const uint16_t *s = (const uint16_t *)src;
                uint16_t words = size >> 1;
                
                while (words--) {
                    *d++ = *s++;
                }
                
                /* Handle odd byte */
                if (size & 1) {
                    *(uint8_t *)d = *(const uint8_t *)s;
                }
            }
            break;
            
        case CPU_TYPE_386:
        case CPU_TYPE_486:
            /* 386/486: Use dword copy for better performance */
            {
                uint32_t *d = (uint32_t *)dst;
                const uint32_t *s = (const uint32_t *)src;
                uint16_t dwords = size >> 2;
                
                while (dwords--) {
                    *d++ = *s++;
                }
                
                /* Handle remaining bytes */
                uint16_t remaining = size & 3;
                if (remaining) {
                    uint8_t *d8 = (uint8_t *)d;
                    const uint8_t *s8 = (const uint8_t *)s;
                    while (remaining--) {
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
 * Process RX packet with copy-break optimization
 */
int copybreak_process_rx(uint8_t device_id, void *packet_data, uint16_t packet_size)
{
    stats.packets_processed++;
    
    /* Update rolling average packet size */
    stats.avg_packet_size = (stats.avg_packet_size * 7 + packet_size) / 8;
    
    /* Copy-break decision */
    if (should_copy(packet_size)) {
        /* Small packet - use copy-break */
        void *buffer = buffer_pool_alloc_copybreak(packet_size, stats.current_threshold);
        if (!buffer) {
            /* Pool exhausted - fall back to zero-copy */
            stats.copy_failures++;
            goto use_zerocopy;
        }
        
        /* Copy packet to pool buffer */
        fast_packet_copy(buffer, packet_data, packet_size);
        
        /* Deliver copied packet */
        int result = deliver_packet(device_id, buffer, packet_size, PACKET_COPIED);
        if (result != 0) {
            buffer_pool_free(buffer);
            return result;
        }
        
        stats.packets_copied++;
        
        /* Original buffer can be reused immediately */
        recycle_rx_buffer_immediate(device_id, packet_data);
        
        return 0;
        
    } else {
use_zerocopy:
        /* Large packet - use zero-copy */
        int result = deliver_packet(device_id, packet_data, packet_size, PACKET_ZEROCOPY);
        if (result != 0) {
            stats.zerocopy_failures++;
            return result;
        }
        
        stats.packets_zerocopy++;
        
        /* Buffer will be recycled when application is done with it */
        return 0;
    }
}

/**
 * Process TX packet with copy-break optimization
 */
int copybreak_process_tx(uint8_t device_id, const void *packet_data, uint16_t packet_size)
{
    stats.packets_processed++;
    
    /* Copy-break decision for TX */
    if (should_copy(packet_size)) {
        /* Small packet - copy to DMA-safe buffer */
        void *dma_buffer = get_tx_dma_buffer(device_id, packet_size);
        if (!dma_buffer) {
            stats.copy_failures++;
            return -1;
        }
        
        /* Copy packet to DMA buffer */
        fast_packet_copy(dma_buffer, packet_data, packet_size);
        
        /* Submit for transmission */
        int result = submit_tx_packet(device_id, dma_buffer, packet_size, PACKET_COPIED);
        if (result != 0) {
            free_tx_dma_buffer(device_id, dma_buffer);
            return result;
        }
        
        stats.packets_copied++;
        return 0;
        
    } else {
        /* Large packet - use zero-copy if possible */
        if (!is_dma_safe(packet_data, packet_size)) {
            /* Not DMA safe - must copy anyway */
            void *dma_buffer = get_tx_dma_buffer(device_id, packet_size);
            if (!dma_buffer) {
                stats.copy_failures++;
                return -1;
            }
            
            fast_packet_copy(dma_buffer, packet_data, packet_size);
            
            int result = submit_tx_packet(device_id, dma_buffer, packet_size, PACKET_COPIED);
            if (result != 0) {
                free_tx_dma_buffer(device_id, dma_buffer);
                return result;
            }
            
            stats.packets_copied++;  /* Forced copy */
            return 0;
        }
        
        /* DMA safe - use zero-copy */
        int result = submit_tx_packet(device_id, (void *)packet_data, packet_size, PACKET_ZEROCOPY);
        if (result != 0) {
            stats.zerocopy_failures++;
            return result;
        }
        
        stats.packets_zerocopy++;
        return 0;
    }
}

/**
 * Adaptive threshold adjustment
 * Adjusts threshold based on buffer pool utilization and performance
 */
static void adjust_threshold(void)
{
    if (!config.adaptive_threshold) {
        return;
    }
    
    /* Check buffer pool utilization */
    struct buffer_pool_stats pool_stats;
    buffer_pool_get_stats(BUFFER_SMALL, &pool_stats);
    
    uint16_t old_threshold = stats.current_threshold;
    uint16_t new_threshold = old_threshold;
    
    /* If pool utilization is high, reduce threshold */
    if (pool_stats.utilization > 80) {
        new_threshold = (old_threshold * 9) / 10;  /* Reduce by 10% */
        if (new_threshold < 64) {
            new_threshold = 64;  /* Minimum threshold */
        }
    }
    /* If pool utilization is low and copy rate is low, increase threshold */
    else if (pool_stats.utilization < 30 && stats.packets_processed > 100) {
        uint32_t copy_rate = (stats.packets_copied * 100) / stats.packets_processed;
        if (copy_rate < 50) {  /* Less than 50% copy rate */
            new_threshold = (old_threshold * 11) / 10;  /* Increase by 10% */
            
            /* Cap based on CPU type */
            uint16_t max_threshold;
            switch (config.cpu_type) {
                case CPU_TYPE_286: max_threshold = THRESHOLD_286; break;
                case CPU_TYPE_386: max_threshold = THRESHOLD_386; break;
                case CPU_TYPE_486: max_threshold = THRESHOLD_486; break;
                default: max_threshold = THRESHOLD_PENTIUM; break;
            }
            
            if (new_threshold > max_threshold) {
                new_threshold = max_threshold;
            }
        }
    }
    
    /* Apply threshold change */
    if (new_threshold != old_threshold) {
        stats.current_threshold = new_threshold;
        stats.threshold_adjustments++;
        
        LOG_DEBUG("Adaptive threshold adjusted: %u -> %u (utilization=%u%%)",
                 old_threshold, new_threshold, pool_stats.utilization);
    }
}

/**
 * Periodic maintenance for copy-break system
 * Should be called regularly to adjust thresholds and collect stats
 */
void copybreak_maintenance(void)
{
    adjust_threshold();
    
    /* Reset rolling averages periodically */
    if (stats.packets_processed > 10000) {
        stats.packets_processed /= 2;
        stats.packets_copied /= 2;
        stats.packets_zerocopy /= 2;
        stats.copy_failures /= 2;
        stats.zerocopy_failures /= 2;
    }
}

/**
 * Get copy-break statistics
 */
void copybreak_get_stats(struct copybreak_statistics *user_stats)
{
    if (!user_stats) {
        return;
    }
    
    user_stats->packets_processed = stats.packets_processed;
    user_stats->packets_copied = stats.packets_copied;
    user_stats->packets_zerocopy = stats.packets_zerocopy;
    user_stats->copy_failures = stats.copy_failures;
    user_stats->zerocopy_failures = stats.zerocopy_failures;
    user_stats->current_threshold = stats.current_threshold;
    user_stats->avg_packet_size = stats.avg_packet_size;
    user_stats->threshold_adjustments = stats.threshold_adjustments;
    
    /* Calculate derived statistics */
    if (stats.packets_processed > 0) {
        user_stats->copy_percentage = 
            (stats.packets_copied * 100) / stats.packets_processed;
        user_stats->zerocopy_percentage = 
            (stats.packets_zerocopy * 100) / stats.packets_processed;
    }
    
    if (stats.packets_copied + stats.copy_failures > 0) {
        user_stats->copy_success_rate = 
            (stats.packets_copied * 100) / 
            (stats.packets_copied + stats.copy_failures);
    } else {
        user_stats->copy_success_rate = 100;
    }
}

/**
 * Reset copy-break statistics
 */
void copybreak_reset_stats(void)
{
    memset(&stats, 0, sizeof(stats));
    stats.current_threshold = config.threshold;
}

/**
 * Set copy-break threshold manually
 */
void copybreak_set_threshold(uint16_t threshold)
{
    if (threshold < 64) threshold = 64;     /* Minimum */
    if (threshold > 1500) threshold = 1500; /* Maximum (MTU) */
    
    config.threshold = threshold;
    stats.current_threshold = threshold;
    
    LOG_INFO("Copy-break threshold set to %u bytes", threshold);
}

/**
 * Get current copy-break threshold
 */
uint16_t copybreak_get_threshold(void)
{
    return stats.current_threshold;
}

/**
 * Enable/disable adaptive threshold
 */
void copybreak_set_adaptive(bool enable)
{
    config.adaptive_threshold = enable;
    if (!enable) {
        stats.current_threshold = config.threshold;  /* Reset to base */
    }
    
    LOG_INFO("Adaptive threshold %s", enable ? "enabled" : "disabled");
}

/**
 * Check copy-break health
 */
int copybreak_health_check(void)
{
    if (stats.packets_processed < 100) {
        return 0;  /* Not enough data */
    }
    
    int health_score = 0;
    
    /* Check failure rates */
    uint32_t total_failures = stats.copy_failures + stats.zerocopy_failures;
    uint8_t failure_rate = (total_failures * 100) / stats.packets_processed;
    
    if (failure_rate > 10) {
        health_score -= 3;  /* High failure rate */
    } else if (failure_rate > 5) {
        health_score -= 1;  /* Moderate failure rate */
    }
    
    /* Check threshold effectiveness */
    uint8_t copy_rate = (stats.packets_copied * 100) / stats.packets_processed;
    if (copy_rate > 90) {
        health_score -= 2;  /* Threshold may be too high */
    } else if (copy_rate < 10) {
        health_score -= 1;  /* Threshold may be too low */
    }
    
    /* Check average packet size vs threshold */
    if (stats.avg_packet_size > stats.current_threshold * 2) {
        health_score -= 1;  /* Threshold may be too low for traffic */
    }
    
    return health_score;
}

/**
 * Weak symbol implementations for integration points
 */

int __attribute__((weak)) deliver_packet(uint8_t device_id, void *buffer, 
                                        uint16_t size, packet_type_t type)
{
    LOG_DEBUG("Deliver packet: device %u, size %u, type %d", device_id, size, type);
    return 0;
}

void __attribute__((weak)) recycle_rx_buffer_immediate(uint8_t device_id, void *buffer)
{
    LOG_DEBUG("Recycle RX buffer: device %u, buffer %p", device_id, buffer);
}

void* __attribute__((weak)) get_tx_dma_buffer(uint8_t device_id, uint16_t size)
{
    return buffer_pool_alloc(size);
}

void __attribute__((weak)) free_tx_dma_buffer(uint8_t device_id, void *buffer)
{
    buffer_pool_free(buffer);
}

int __attribute__((weak)) submit_tx_packet(uint8_t device_id, void *buffer, 
                                          uint16_t size, packet_type_t type)
{
    LOG_DEBUG("Submit TX packet: device %u, size %u, type %d", device_id, size, type);
    return 0;
}

bool __attribute__((weak)) is_dma_safe(const void *buffer, uint16_t size)
{
    /* Conservative: assume not DMA safe unless proven otherwise */
    return false;
}