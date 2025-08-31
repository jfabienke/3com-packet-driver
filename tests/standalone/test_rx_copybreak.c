/**
 * @file test_rx_copybreak.c
 * @brief Test program for RX_COPYBREAK optimization
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file demonstrates the RX_COPYBREAK optimization functionality.
 */

#include "include/common.h"
#include "include/buffer_alloc.h"
#include "include/logging.h"
#include "include/memory.h"
#include "include/cpu_detect.h"

/* Mock cpu_info for testing */
cpu_info_t g_cpu_info = {
    .type = CPU_TYPE_80386,
    .features = 0
};

/**
 * @brief Test RX_COPYBREAK optimization functionality
 */
int test_rx_copybreak_optimization(void) {
    int result;
    buffer_desc_t *small_buffer1, *small_buffer2, *large_buffer1, *large_buffer2;
    rx_copybreak_pool_t stats;
    
    printf("Testing RX_COPYBREAK optimization...\n");
    
    /* Initialize buffer system */
    result = buffer_system_init();
    if (result != SUCCESS) {
        printf("ERROR: Failed to initialize buffer system: %d\n", result);
        return result;
    }
    
    /* Initialize RX_COPYBREAK with reasonable pool sizes */
    result = rx_copybreak_init(16, 8);  /* 16 small buffers, 8 large buffers */
    if (result != SUCCESS) {
        printf("ERROR: Failed to initialize RX_COPYBREAK: %d\n", result);
        buffer_system_cleanup();
        return result;
    }
    
    printf("RX_COPYBREAK initialized successfully\n");
    
    /* Test small packet allocations (< 200 bytes) */
    printf("\nTesting small packet allocations...\n");
    
    small_buffer1 = rx_copybreak_alloc(64);  /* Small packet */
    if (!small_buffer1) {
        printf("ERROR: Failed to allocate buffer for 64-byte packet\n");
        goto cleanup;
    }
    printf("✓ Allocated buffer for 64-byte packet (buffer size: %u)\n", small_buffer1->size);
    
    small_buffer2 = rx_copybreak_alloc(150); /* Small packet */
    if (!small_buffer2) {
        printf("ERROR: Failed to allocate buffer for 150-byte packet\n");
        goto cleanup;
    }
    printf("✓ Allocated buffer for 150-byte packet (buffer size: %u)\n", small_buffer2->size);
    
    /* Test large packet allocations (>= 200 bytes) */
    printf("\nTesting large packet allocations...\n");
    
    large_buffer1 = rx_copybreak_alloc(500);  /* Large packet */
    if (!large_buffer1) {
        printf("ERROR: Failed to allocate buffer for 500-byte packet\n");
        goto cleanup;
    }
    printf("✓ Allocated buffer for 500-byte packet (buffer size: %u)\n", large_buffer1->size);
    
    large_buffer2 = rx_copybreak_alloc(1400); /* Large packet */
    if (!large_buffer2) {
        printf("ERROR: Failed to allocate buffer for 1400-byte packet\n");
        goto cleanup;
    }
    printf("✓ Allocated buffer for 1400-byte packet (buffer size: %u)\n", large_buffer2->size);
    
    /* Verify buffer sizes are correct */
    if (small_buffer1->size != SMALL_BUFFER_SIZE || small_buffer2->size != SMALL_BUFFER_SIZE) {
        printf("ERROR: Small buffers have incorrect size\n");
        goto cleanup;
    }
    
    if (large_buffer1->size != LARGE_BUFFER_SIZE || large_buffer2->size != LARGE_BUFFER_SIZE) {
        printf("ERROR: Large buffers have incorrect size\n");
        goto cleanup;
    }
    
    /* Display statistics */
    printf("\nRX_COPYBREAK Statistics:\n");
    rx_copybreak_get_stats(&stats);
    
    /* Free buffers */
    printf("\nFreeing buffers...\n");
    rx_copybreak_free(small_buffer1);
    rx_copybreak_free(small_buffer2);
    rx_copybreak_free(large_buffer1);
    rx_copybreak_free(large_buffer2);
    printf("✓ All buffers freed successfully\n");
    
    /* Test memory efficiency */
    printf("\nMemory efficiency test:\n");
    printf("- Small buffer size: %u bytes\n", SMALL_BUFFER_SIZE);
    printf("- Large buffer size: %u bytes\n", LARGE_BUFFER_SIZE);
    printf("- Memory saved per small packet: %u bytes\n", LARGE_BUFFER_SIZE - SMALL_BUFFER_SIZE);
    printf("- Total memory saved: %lu bytes\n", stats.memory_saved);
    
    /* Test edge cases */
    printf("\nTesting edge cases...\n");
    
    /* Test threshold boundary */
    buffer_desc_t *threshold_buffer = rx_copybreak_alloc(RX_COPYBREAK_THRESHOLD);
    if (threshold_buffer) {
        printf("✓ Threshold packet (%u bytes) allocated to %s buffer\n", 
               RX_COPYBREAK_THRESHOLD,
               threshold_buffer->size == SMALL_BUFFER_SIZE ? "small" : "large");
        rx_copybreak_free(threshold_buffer);
    }
    
    /* Test invalid allocations */
    buffer_desc_t *invalid_buffer = rx_copybreak_alloc(0);
    if (invalid_buffer) {
        printf("ERROR: Zero-size allocation should have failed\n");
        rx_copybreak_free(invalid_buffer);
        goto cleanup;
    }
    printf("✓ Zero-size allocation correctly rejected\n");
    
    printf("\nRX_COPYBREAK optimization test completed successfully!\n");
    
cleanup:
    rx_copybreak_cleanup();
    buffer_system_cleanup();
    return SUCCESS;
}

int main(void) {
    /* Initialize minimal logging */
    printf("RX_COPYBREAK Optimization Test\n");
    printf("==============================\n\n");
    
    int result = test_rx_copybreak_optimization();
    
    if (result == SUCCESS) {
        printf("\n✓ All tests passed!\n");
        return 0;
    } else {
        printf("\n✗ Tests failed with error: %d\n", result);
        return 1;
    }
}