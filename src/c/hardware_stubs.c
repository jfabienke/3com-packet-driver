/**
 * @file hardware_stubs.c
 * @brief Temporary stub implementations for hardware functions
 * 
 * These stubs allow the refactored boot sequence to compile
 * while the full hardware implementation is being developed.
 */

#include <stddef.h>
#include "../include/hardware.h"
#include "../include/logging.h"

/* Global NIC array - will be populated by hardware detection */
static nic_info_t g_nics[MAX_NICS] = {0};
static int g_nic_count = 0;

/**
 * @brief Get the primary (first active) NIC for testing
 * 
 * This is used for DMA capability testing after hardware init.
 * Returns the first NIC that is present and initialized.
 * 
 * @return Pointer to primary NIC or NULL if none available
 */
nic_info_t* hardware_get_primary_nic(void) {
    int i;
    
    for (i = 0; i < g_nic_count; i++) {
        if ((g_nics[i].status & NIC_STATUS_PRESENT) &&
            (g_nics[i].status & NIC_STATUS_INITIALIZED)) {
            LOG_INFO("Primary NIC selected: index %d, type %d", 
                     i, g_nics[i].type);
            return &g_nics[i];
        }
    }
    
    LOG_WARNING("No primary NIC available for testing");
    return NULL;
}

/**
 * @brief Stub for hardware cleanup
 * 
 * This will be replaced by the actual implementation
 */
int hardware_cleanup(void) {
    LOG_INFO("Hardware cleanup (stub)");
    return 0;
}