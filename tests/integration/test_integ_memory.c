/**
 * @file test_integ_memory.c
 * @brief Integration example for three-tier memory management system
 *
 * This file demonstrates how to properly initialize and use the comprehensive
 * three-tier memory management system in the 3Com packet driver.
 */

#include "memory.h"
#include "buffer_alloc.h"
#include "cpu_detect.h"
#include "xms_detect.h"
#include "../common/test_common.h"
#include "logging.h"

/**
 * @brief Initialize the complete memory management system
 * @return 0 on success, negative on error
 */
int memory_system_complete_init(void) {
    int result;
    
    log_info("Initializing comprehensive memory management system");
    
    /* Step 1: Initialize CPU detection (should be done in Phase 1) */
    extern cpu_info_t g_cpu_info;
    if (!g_cpu_info.type) {
        log_warning("CPU detection not completed - some optimizations disabled");
    }
    
    /* Step 2: Initialize three-tier memory system */
    result = memory_init();
    if (result != 0) {
        log_error("Failed to initialize three-tier memory system: %d", result);
        return result;
    }
    
    /* Step 3: Initialize CPU-optimized memory operations */
    result = memory_init_cpu_optimized();
    if (result != 0) {
        log_warning("CPU optimizations disabled: %d", result);
        /* Continue anyway with basic functionality */
    }
    
    /* Step 4: Initialize buffer allocation system */
    result = buffer_system_init_optimized();
    if (result != 0) {
        log_error("Failed to initialize buffer system: %d", result);
        memory_cleanup();
        return result;
    }
    
    /* Step 5: Log system capabilities */
    log_info("Memory system initialization complete:");
    log_info("- XMS Extended Memory (Tier 1): %s", 
             memory_xms_available() ? "Available" : "Not available");
    log_info("- UMB Upper Memory (Tier 2): %s",
             g_memory_system.umb_available ? "Available" : "Not available");
    log_info("- Conventional Memory (Tier 3): Available");
    log_info("- CPU optimizations: %s CPU detected",
             cpu_type_to_string(g_cpu_info.type));
    
    return 0;
}

/**
 * @brief Example of allocating packet buffers for NIC operations
 * @return 0 on success, negative on error
 */
int memory_example_packet_allocation(void) {
    log_info("=== Packet Buffer Allocation Example ===");
    
    /* Allocate buffers for different packet sizes */
    
    /* Small packet buffer (64 bytes) */
    buffer_desc_t *small_packet = buffer_alloc_ethernet_frame(64, BUFFER_TYPE_TX);
    if (small_packet) {
        log_info("Allocated small packet buffer: %p, size: %u bytes",
                 small_packet->data, small_packet->size);
        
        /* Simulate packet data */
        uint8_t small_data[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55}; /* MAC address */
        buffer_set_data(small_packet, small_data, sizeof(small_data));
    }
    
    /* Standard Ethernet frame buffer (1518 bytes) */
    buffer_desc_t *std_packet = buffer_alloc_ethernet_frame(1518, BUFFER_TYPE_RX);
    if (std_packet) {
        log_info("Allocated standard packet buffer: %p, size: %u bytes",
                 std_packet->data, std_packet->size);
        
        /* Pre-zero the buffer for receiving */
        buffer_clear_data(std_packet);
    }
    
    /* DMA-capable buffer for high-performance NICs */
    buffer_desc_t *dma_packet = buffer_alloc_dma(2048, 16);
    if (dma_packet) {
        log_info("Allocated DMA packet buffer: %p, size: %u bytes",
                 dma_packet->data, dma_packet->size);
        
        /* Prefetch data for better cache performance */
        buffer_prefetch_data(dma_packet);
    }
    
    /* Example of copying packet data between buffers */
    if (small_packet && std_packet) {
        int result = buffer_copy_packet_data(std_packet, small_packet);
        if (result == SUCCESS) {
            log_info("Successfully copied packet data from small to standard buffer");
        }
    }
    
    /* Clean up */
    if (small_packet) buffer_free_any(small_packet);
    if (std_packet) buffer_free_any(std_packet);
    if (dma_packet) buffer_free_any(dma_packet);
    
    return 0;
}

/**
 * @brief Example of using direct memory allocation with tier preferences
 * @return 0 on success, negative on error
 */
int memory_example_direct_allocation(void) {
    log_info("=== Direct Memory Allocation Example ===");
    
    /* Large allocation that prefers XMS */
    void *large_buffer = memory_alloc(8192, MEM_TYPE_PACKET_BUFFER, 
                                     MEM_FLAG_DMA_CAPABLE | MEM_FLAG_ALIGNED);
    if (large_buffer) {
        log_info("Allocated large buffer: %p (likely from XMS tier)", large_buffer);
        
        /* Use CPU-optimized operations */
        memory_set_optimized(large_buffer, 0x55, 4096);
        log_info("Filled first 4KB with test pattern using CPU-optimized set");
    }
    
    /* Medium allocation that may use UMB */
    void *medium_buffer = memory_alloc(2048, MEM_TYPE_PACKET_BUFFER, MEM_FLAG_ALIGNED);
    if (medium_buffer) {
        log_info("Allocated medium buffer: %p (may use UMB tier)", medium_buffer);
    }
    
    /* Small allocation that uses conventional memory */
    void *small_buffer = memory_alloc(256, MEM_TYPE_GENERAL, 0);
    if (small_buffer) {
        log_info("Allocated small buffer: %p (conventional tier)", small_buffer);
    }
    
    /* Test CPU-optimized copy between buffers */
    if (large_buffer && medium_buffer) {
        memory_copy_optimized(medium_buffer, large_buffer, 1024);
        log_info("Copied 1KB using CPU-optimized copy");
    }
    
    /* Clean up */
    if (large_buffer) memory_free(large_buffer);
    if (medium_buffer) memory_free(medium_buffer);
    if (small_buffer) memory_free(small_buffer);
    
    return 0;
}

/**
 * @brief Complete memory system demonstration
 * @return 0 on success, negative on error
 */
int memory_complete_demonstration(void) {
    int result;
    
    log_info("=== Three-Tier Memory System Demonstration ===");
    
    /* Initialize the complete system */
    result = memory_system_complete_init();
    if (result != 0) {
        log_error("Memory system initialization failed");
        return result;
    }
    
    /* Run validation tests */
    log_info("Running memory system validation tests...");
    result = memory_run_comprehensive_tests();
    if (result != 0) {
        log_error("Memory system tests failed");
        return result;
    }
    
    /* Demonstrate packet buffer usage */
    result = memory_example_packet_allocation();
    if (result != 0) {
        log_error("Packet allocation example failed");
        return result;
    }
    
    /* Demonstrate direct memory allocation */
    result = memory_example_direct_allocation();
    if (result != 0) {
        log_error("Direct allocation example failed");
        return result;
    }
    
    /* Run stress test */
    log_info("Running memory stress test...");
    result = memory_stress_test();
    if (result != 0) {
        log_error("Memory stress test failed");
        return result;
    }
    
    /* Print final statistics */
    log_info("=== Final Memory Statistics ===");
    memory_print_stats();
    
    /* Display buffer statistics */
    const buffer_stats_t *buf_stats = buffer_get_stats();
    if (buf_stats) {
        log_info("Buffer allocations: %lu, frees: %lu, failures: %lu",
                 buf_stats->total_allocations, buf_stats->total_frees,
                 buf_stats->allocation_failures);
    }
    
    log_info("=== Memory System Demonstration Complete ===");
    return 0;
}

/**
 * @brief Clean up the complete memory system
 */
void memory_system_complete_cleanup(void) {
    log_info("Cleaning up complete memory management system");
    
    /* Clean up buffer system */
    buffer_system_cleanup();
    
    /* Clean up three-tier memory system */
    memory_cleanup();
    
    log_info("Memory system cleanup complete");
}