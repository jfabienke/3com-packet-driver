/**
 * @file test_dma_mapping.c
 * @brief Standalone test runner for DMA mapping functionality
 *
 * This provides a standalone executable to test the DMA mapping layer
 * outside of the full driver context.
 */

#include <stdio.h>
#include <stdlib.h>
#include "include/dma_mapping_test.h"
#include "include/logging.h"

/* Simple logging implementation for testing */
void log_info(const char *format, ...) {
    printf("INFO: ");
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

void log_error(const char *format, ...) {
    printf("ERROR: ");
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

void log_debug(const char *format, ...) {
    printf("DEBUG: ");
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

void log_warning(const char *format, ...) {
    printf("WARNING: ");
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

int main(int argc, char *argv[]) {
    printf("3Com Packet Driver - DMA Mapping Test Suite\n");
    printf("==========================================\n\n");
    
    int result = run_dma_mapping_tests();
    
    printf("\n");
    if (result == 0) {
        printf("DMA mapping test suite PASSED\n");
        return EXIT_SUCCESS;
    } else {
        printf("DMA mapping test suite FAILED\n");
        return EXIT_FAILURE;
    }
}