/**
 * @file hardware_stubs.c
 * @brief Temporary stub implementations for hardware functions
 * 
 * These stubs allow the refactored boot sequence to compile
 * while the full hardware implementation is being developed.
 */

#include <stddef.h>
#include "hardware.h"
#include "logging.h"

/* Size verification - confirm packing is working */
#ifdef __WATCOMC__
/* Watcom doesn't support _Static_assert, use pragma message approximation */
#pragma message("hwstubs.c: sizeof(nic_info_t) verification at runtime")
#endif

/* Global NIC array - will be populated by hardware detection */
/* External linkage to match declarations in hardware.h */
/* Zero-initialization is still guaranteed for globals in C89 */
nic_info_t g_nics[MAX_NICS];
int g_num_nics;

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

    /* Size verification - log actual struct sizes for DGROUP analysis */
    log_info("SIZEOF: nic_info_t=%u, MAX_NICS=%d, g_nics[]=%u bytes",
             (unsigned)sizeof(nic_info_t), MAX_NICS,
             (unsigned)(MAX_NICS * sizeof(nic_info_t)));

    for (i = 0; i < g_num_nics; i++) {
        if ((g_nics[i].status & NIC_STATUS_PRESENT) &&
            (g_nics[i].status & NIC_STATUS_INITIALIZED)) {
            log_info("Primary NIC selected: index %d, type %d",
                     i, g_nics[i].type);
            return &g_nics[i];
        }
    }
    
    log_warning("No primary NIC available for testing");
    return NULL;
}

/**
 * @brief Stub for hardware cleanup
 *
 * This will be replaced by the actual implementation
 */
void hardware_cleanup(void) {
    log_info("Hardware cleanup (stub)");
}

/**
 * @brief Stub for clearing pending NIC interrupts
 *
 * This will be replaced by the actual implementation
 * which reads/acknowledges pending interrupt status.
 *
 * @param nic Pointer to NIC info structure
 * @return 0 on success, negative on error
 */
int hardware_clear_interrupts(nic_info_t *nic) {
    if (!nic) {
        return -1;
    }
    log_info("Hardware clear interrupts (stub)");
    return 0;
}
