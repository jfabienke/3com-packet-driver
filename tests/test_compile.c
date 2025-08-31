/**
 * @file test_compile.c
 * @brief Simple compilation test for GPT-5 fixes
 * 
 * This file tests that all the fixes compile correctly
 * with proper DOS/C89 compatibility.
 */

#include "../include/portability.h"
#include "../include/handle_compact.h"
#include <stdio.h>

/* Test that critical sections work */
void test_critical_sections(void) {
    uint16_t flags;
    uint32_t counter = 0;
    
    CRITICAL_SECTION_ENTER(flags);
    counter++;
    CRITICAL_SECTION_EXIT(flags);
    
    printf("Critical section test passed, counter = %lu\n", counter);
}

/* Test that handle structure works with named struct */
void test_handle_struct(void) {
    handle_compact_t handle;
    
    /* Initialize */
    handle.flags = HANDLE_FLAG_ACTIVE;
    handle.interface = HANDLE_TYPE_ETHERNET | 0x01;
    handle.stats_index = 0;
    handle.callback = NULL;
    handle.packets.combined_count = 0;
    handle.context = NULL;
    
    /* Test named struct access */
    handle.packets.counts.rx_count = 100;
    handle.packets.counts.tx_count = 50;
    
    printf("Handle test: RX=%u TX=%u Combined=%lu\n",
           handle.packets.counts.rx_count,
           handle.packets.counts.tx_count,
           handle.packets.combined_count);
    
    /* Test size */
    if (sizeof(handle_compact_t) == 16) {
        printf("Handle size correct: 16 bytes\n");
    } else {
        printf("ERROR: Handle size is %u bytes, expected 16\n", 
               (unsigned)sizeof(handle_compact_t));
    }
}

/* Test callback with proper calling convention */
void FAR CDECL test_callback(uint8_t FAR *packet, uint16_t length) {
    printf("Callback received packet of length %u\n", length);
}

/* Test FAR pointer usage */
void test_far_pointers(void) {
    handle_compact_t handle;
    void FAR *ptr = &handle;
    
    handle.callback = test_callback;
    handle.context = ptr;
    
    printf("FAR pointer test passed\n");
}

/* Test atomic operations */
void test_atomic_ops(void) {
    uint32_t value = 0;
    uint32_t result;
    uint16_t flags;
    
    ATOMIC32_WRITE(value, 100, flags);
    ATOMIC32_ADD(value, 50, flags);
    ATOMIC32_READ(value, result, flags);
    
    printf("Atomic ops test: value = %lu (expected 150)\n", result);
}

int main(void) {
    printf("=== DOS Compatibility Test Suite ===\n\n");
    
    test_critical_sections();
    test_handle_struct();
    test_far_pointers();
    test_atomic_ops();
    
    printf("\nAll compilation tests completed.\n");
    return 0;
}