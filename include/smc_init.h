/**
 * @file smc_init.h
 * @brief Self-Modifying Code initialization interface
 * 
 * Provides unified initialization for all SMC patches based on
 * one-time V86 detection, eliminating runtime overhead in hot paths.
 */

#ifndef _SMC_INIT_H_
#define _SMC_INIT_H_

#include <stdint.h>

/* Patch identifiers */
#define SMC_PATCH_PCI_IO    0
#define SMC_PATCH_VORTEX_TX 1
#define SMC_PATCH_VORTEX_RX 2
#define SMC_PATCH_ISR       3

/**
 * @brief Initialize all SMC patches based on execution environment
 * 
 * Performs one-time V86 detection and patches all critical code paths.
 * Should be called once during driver initialization.
 * 
 * @return 0 on success, -1 on error
 */
int smc_init_all(void);

/**
 * @brief Get SMC patch statistics
 * 
 * @param v86_mode Output: 1 if V86 detected, 0 if real mode
 * @param patches_applied Output: Number of patches applied
 */
void smc_get_stats(uint8_t *v86_mode, uint32_t *patches_applied);

/**
 * @brief Check if a specific patch was applied
 * 
 * @param patch_id Patch identifier (SMC_PATCH_*)
 * @return 1 if patched, 0 if not
 */
uint8_t smc_is_patched(uint8_t patch_id);

#endif /* _SMC_INIT_H_ */