/**
 * @file test_xms.c
 * @brief Simple XMS detection test
 */

#include <stdio.h>
#include <stdlib.h>
#include "xms_detect.h"
#include "logging.h"

int main(void) {
    int result;
    xms_info_t info;
    uint16_t handle;
    
    printf("XMS Detection Test\n");
    printf("==================\n\n");
    
    /* Initialize logging */
    log_set_level(LOG_LEVEL_DEBUG);
    
    /* Test XMS detection and initialization */
    printf("1. Testing XMS detection and initialization...\n");
    result = xms_detect_and_init();
    if (result == 0) {
        printf("   SUCCESS: XMS initialized\n");
    } else {
        printf("   FAILED: XMS not available (error %d)\n", result);
        return 1;
    }
    
    /* Get XMS information */
    printf("\n2. Getting XMS information...\n");
    result = xms_get_info(&info);
    if (result == 0) {
        printf("   XMS Version: %d.%d\n", info.version_major, info.version_minor);
        printf("   Total memory: %d KB\n", info.total_kb);
        printf("   Free memory: %d KB\n", info.free_kb);
        printf("   Largest block: %d KB\n", info.largest_block_kb);
    } else {
        printf("   FAILED: Cannot get XMS info (error %d)\n", result);
        return 1;
    }
    
    /* Test memory allocation */
    printf("\n3. Testing XMS memory allocation...\n");
    result = xms_allocate(64, &handle); /* Allocate 64 KB */
    if (result == 0) {
        printf("   SUCCESS: Allocated 64 KB, handle = %04X\n", handle);
        
        /* Test memory freeing */
        printf("\n4. Testing XMS memory deallocation...\n");
        result = xms_free(handle);
        if (result == 0) {
            printf("   SUCCESS: Freed handle %04X\n", handle);
        } else {
            printf("   FAILED: Cannot free handle %04X (error %d)\n", handle, result);
        }
    } else {
        printf("   FAILED: Cannot allocate 64 KB (error %d)\n", result);
    }
    
    /* Cleanup */
    printf("\n5. Cleaning up XMS resources...\n");
    result = xms_cleanup();
    if (result == 0) {
        printf("   SUCCESS: XMS cleanup completed\n");
    } else {
        printf("   WARNING: XMS cleanup had issues (error %d)\n", result);
    }
    
    printf("\nXMS test completed successfully!\n");
    return 0;
}