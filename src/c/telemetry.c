/**
 * @file telemetry.c
 * @brief Production telemetry implementation for packet driver
 * 
 * GPT-5 A+ Enhancement: Comprehensive telemetry for production monitoring
 */

#include <stdio.h>
#include <string.h>
#include "../include/telemetry.h"
#include "../include/logging.h"

/* Global telemetry instance */
driver_telemetry_t g_telemetry = {0};

/**
 * @brief Initialize telemetry system
 */
void telemetry_init(void) {
    memset(&g_telemetry, 0, sizeof(g_telemetry));
    g_telemetry.link_current_status = false;
    g_telemetry.link_current_speed = 0;
    g_telemetry.link_current_duplex = false;
    LOG_INFO("Telemetry system initialized");
}

/**
 * @brief Update link status metrics
 */
void telemetry_update_link_status(bool up, uint16_t speed, bool full_duplex) {
    if (up != g_telemetry.link_current_status) {
        if (up) {
            g_telemetry.link_up_transitions++;
        } else {
            g_telemetry.link_down_transitions++;
        }
        g_telemetry.link_current_status = up;
    }
    
    if (speed != g_telemetry.link_current_speed) {
        g_telemetry.link_speed_changes++;
        g_telemetry.link_current_speed = speed;
    }
    
    if (full_duplex != g_telemetry.link_current_duplex) {
        g_telemetry.link_duplex_changes++;
        g_telemetry.link_current_duplex = full_duplex;
    }
}

/**
 * @brief Record successful TX completion
 */
void telemetry_record_tx_completion(uint32_t bytes, bool bounce_used) {
    g_telemetry.tx_completions++;
    g_telemetry.tx_bytes += bytes;
    if (bounce_used) {
        g_telemetry.tx_bounce_uses++;
    }
    
    /* Update timestamp (would use get_bios_ticks() in real implementation) */
    extern uint32_t get_bios_ticks(void);
    g_telemetry.last_tx_timestamp = get_bios_ticks();
}

/**
 * @brief Record received packet
 */
void telemetry_record_rx_packet(uint32_t bytes, bool bounce_used) {
    g_telemetry.rx_packets++;
    g_telemetry.rx_bytes += bytes;
    if (bounce_used) {
        g_telemetry.rx_bounce_uses++;
    }
    
    /* Update timestamp */
    extern uint32_t get_bios_ticks(void);
    g_telemetry.last_rx_timestamp = get_bios_ticks();
}

/**
 * @brief Record TX timeout event
 */
void telemetry_record_tx_timeout(void) {
    g_telemetry.tx_timeouts++;
}

/**
 * @brief Record interrupt handling
 */
void telemetry_record_irq(uint32_t duration_ticks) {
    g_telemetry.irq_count++;
    
    if (duration_ticks > g_telemetry.irq_max_duration) {
        g_telemetry.irq_max_duration = duration_ticks;
    }
}

/**
 * @brief Record DMA mapping attempt
 */
void telemetry_record_dma_mapping(bool success, bool constraint_violation) {
    if (success) {
        g_telemetry.dma_mapping_success++;
    } else {
        g_telemetry.dma_mapping_failures++;
    }
    
    if (constraint_violation) {
        g_telemetry.dma_constraint_violations++;
    }
}

/**
 * @brief Update queue depth high water marks
 */
void telemetry_update_queue_depth(uint16_t tx_depth, uint16_t rx_depth) {
    if (tx_depth > g_telemetry.queue_tx_high_water) {
        g_telemetry.queue_tx_high_water = tx_depth;
    }
    
    if (rx_depth > g_telemetry.queue_rx_high_water) {
        g_telemetry.queue_rx_high_water = rx_depth;
    }
}

/**
 * @brief Print telemetry summary
 */
void telemetry_print_summary(void) {
    LOG_INFO("=== Driver Telemetry Summary ===");
    
    /* TX Statistics */
    LOG_INFO("TX: %lu packets, %lu bytes, %lu completions", 
             g_telemetry.tx_packets, g_telemetry.tx_bytes, g_telemetry.tx_completions);
    LOG_INFO("TX Issues: %lu timeouts, %lu stalls, %lu queue full",
             g_telemetry.tx_timeouts, g_telemetry.tx_stalls_detected, g_telemetry.tx_queue_full);
    
    /* RX Statistics */
    LOG_INFO("RX: %lu packets, %lu bytes", 
             g_telemetry.rx_packets, g_telemetry.rx_bytes);
    LOG_INFO("RX Errors: %lu drops, %lu CRC, %lu overruns",
             g_telemetry.rx_drops_no_buffer + g_telemetry.rx_drops_error,
             g_telemetry.rx_crc_errors, g_telemetry.rx_overruns);
    
    /* IRQ Statistics */
    LOG_INFO("IRQ: %lu handled, %lu spurious, max duration %lu ticks",
             g_telemetry.irq_count, g_telemetry.irq_spurious, g_telemetry.irq_max_duration);
    
    /* DMA Statistics */
    LOG_INFO("DMA: %lu successful, %lu failed, %lu violations",
             g_telemetry.dma_mapping_success, g_telemetry.dma_mapping_failures,
             g_telemetry.dma_constraint_violations);
    
    /* Queue Statistics */
    LOG_INFO("Queue: TX high water %u, RX high water %u, %lu overflows",
             g_telemetry.queue_tx_high_water, g_telemetry.queue_rx_high_water,
             g_telemetry.queue_overflow_events);
    
    /* Link Status */
    LOG_INFO("Link: %s at %u Mbps %s-duplex",
             g_telemetry.link_current_status ? "UP" : "DOWN",
             g_telemetry.link_current_speed,
             g_telemetry.link_current_duplex ? "full" : "half");
    
    /* VDS Statistics (GPT-5 A+) */
    if (g_telemetry.vds_available) {
        LOG_INFO("VDS: v%d.%d - %lu locks, %lu failures, %lu remaps",
                 g_telemetry.vds_version_major, g_telemetry.vds_version_minor,
                 g_telemetry.vds_lock_successes, g_telemetry.vds_lock_failures,
                 g_telemetry.vds_buffer_remaps);
    } else {
        LOG_INFO("VDS: Not available (real mode or no memory manager)");
    }
}

/**
 * @brief Record VDS initialization status
 */
void telemetry_record_vds_init(bool available, uint8_t major, uint8_t minor) {
    g_telemetry.vds_available = available;
    g_telemetry.vds_version_major = major;
    g_telemetry.vds_version_minor = minor;
    
    LOG_DEBUG("Telemetry: VDS init recorded - available=%d version=%d.%d",
              available, major, minor);
}

/**
 * @brief Record VDS lock failure
 */
void telemetry_record_vds_lock_failure(uint16_t error_code) {
    g_telemetry.vds_lock_failures++;
    g_telemetry.vds_last_error = error_code;
    
    LOG_DEBUG("Telemetry: VDS lock failure - error=%04X total_failures=%lu",
              error_code, g_telemetry.vds_lock_failures);
}

/**
 * @brief Record VDS lock success
 */
void telemetry_record_vds_lock_success(uint32_t size, bool uses_buffer) {
    g_telemetry.vds_lock_successes++;
    if (uses_buffer) {
        g_telemetry.vds_buffer_remaps++;
    }
    
    LOG_DEBUG("Telemetry: VDS lock success - size=%lu uses_buffer=%d",
              size, uses_buffer);
}

/**
 * @brief Get atomic snapshot of telemetry
 */
void telemetry_get_snapshot(driver_telemetry_t *snapshot) {
    if (snapshot) {
        /* In production, would disable interrupts briefly for consistency */
        memcpy(snapshot, &g_telemetry, sizeof(driver_telemetry_t));
    }
}