/**
 * @file pci_multiplex.h
 * @brief INT 2Fh multiplex API interface
 * 
 * Header file for the INT 2Fh multiplex handler that provides
 * runtime control of the PCI BIOS shim.
 */

#ifndef _PCI_MULTIPLEX_H_
#define _PCI_MULTIPLEX_H_

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Install INT 2Fh multiplex handler
 * 
 * Hooks INT 2Fh to provide runtime control interface for the PCI shim.
 * Checks for conflicts with existing multiplex IDs.
 * 
 * @return true if handler installed successfully, false on error
 */
bool multiplex_install(void);

/**
 * @brief Uninstall INT 2Fh multiplex handler
 * 
 * Removes the multiplex handler if safe to do so.
 * Checks that no other TSRs have hooked INT 2Fh after us.
 * 
 * @return true if handler uninstalled successfully, false if unsafe
 */
bool multiplex_uninstall(void);

/**
 * @brief Check if shim is enabled
 * 
 * Returns the current enabled state of the PCI BIOS shim.
 * 
 * @return true if shim is enabled, false if disabled
 */
bool multiplex_is_shim_enabled(void);

/**
 * @brief Set shim enabled state
 * 
 * Enables or disables the PCI BIOS shim at runtime.
 * 
 * @param enabled true to enable shim, false to disable
 */
void multiplex_set_shim_enabled(bool enabled);

/**
 * @brief Get multiplex handler statistics
 * 
 * Returns the number of multiplex calls handled.
 * 
 * @param calls Output: Number of multiplex calls (can be NULL)
 */
void multiplex_get_stats(uint32_t *calls);

/**
 * @brief Command-line control interface
 * 
 * Provides a command-line interface for controlling the resident
 * PCI shim via INT 2Fh. Can be compiled as a separate utility.
 * 
 * @param argc Argument count
 * @param argv Argument array
 * @return 0 on success, non-zero on error
 */
int multiplex_control(int argc, char *argv[]);

/* Multiplex API constants for external utilities */
#define PCI_MPLEX_ID            0xB1    /* Multiplex ID */
#define PCI_MPLEX_SIGNATURE     0x5043  /* 'PC' signature */

/* Function codes */
#define PCI_MPLEX_CHECK         0x00    /* Installation check */
#define PCI_MPLEX_ENABLE        0x01    /* Enable shim */
#define PCI_MPLEX_DISABLE       0x02    /* Disable shim */
#define PCI_MPLEX_STATS         0x03    /* Get statistics */
#define PCI_MPLEX_UNINSTALL     0xFF    /* Uninstall */

#endif /* _PCI_MULTIPLEX_H_ */