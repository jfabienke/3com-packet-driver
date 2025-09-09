/**
 * @file pci_shim.h
 * @brief PCI BIOS shim layer interface
 * 
 * Header file for the PCI BIOS shim that handles broken BIOS implementations
 * by selectively overriding faulty functions with direct mechanism access.
 */

#ifndef _PCI_SHIM_H_
#define _PCI_SHIM_H_

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Install the PCI BIOS shim layer
 * 
 * Hooks INT 1Ah to intercept PCI BIOS calls. Detects broken BIOSes and
 * available PCI mechanisms. Must be called before any PCI operations.
 * 
 * @return true if shim installed successfully, false on error
 */
bool pci_shim_install(void);

/**
 * @brief Uninstall the PCI BIOS shim layer
 * 
 * Removes the INT 1Ah hook and restores original BIOS handler.
 * Should be called during driver cleanup.
 * 
 * @return true if shim uninstalled successfully, false if not installed
 */
bool pci_shim_uninstall(void);

/**
 * @brief Get shim statistics for diagnostics
 * 
 * Returns counters showing how many PCI BIOS calls were intercepted
 * and how many required fallback to direct mechanism access.
 * 
 * @param total_calls Output: Total PCI BIOS calls intercepted (can be NULL)
 * @param fallback_calls Output: Calls that used fallback mechanism (can be NULL)
 */
void pci_shim_get_stats(uint32_t *total_calls, uint32_t *fallback_calls);

/**
 * @brief Force use of specific PCI mechanism
 * 
 * Override automatic mechanism detection. Useful for testing or
 * working around specific hardware issues.
 * 
 * @param mechanism 0=BIOS only, 1=Mechanism #1, 2=Mechanism #2
 * @return Previous mechanism setting
 */
uint8_t pci_shim_force_mechanism(uint8_t mechanism);

/**
 * @brief Check if a specific BIOS function is marked as broken
 * 
 * Query whether the shim has detected a specific PCI BIOS function
 * as broken and will override it.
 * 
 * @param function PCI BIOS function number (e.g., 0x09 for Read Word)
 * @return true if function is marked broken, false if using BIOS
 */
bool pci_shim_is_function_broken(uint8_t function);

/* Enhanced PCI shim statistics structure */
typedef struct {
    uint32_t total_calls;      /* Total PCI BIOS calls */
    uint32_t fallback_calls;   /* Calls using fallback mechanism */
    uint32_t bios_errors;      /* BIOS errors detected */
    uint32_t cache_hits;       /* Config cache hits */
    uint32_t cache_misses;     /* Config cache misses */
    bool in_v86_mode;          /* Running in V86 mode */
    bool cache_enabled;        /* Config caching enabled */
    uint8_t mechanism;         /* Active mechanism (0/1/2) */
} pci_shim_stats_t;

/**
 * @brief Get extended shim statistics
 * 
 * Returns detailed statistics including cache performance and V86 mode status.
 * 
 * @param stats Output: Extended statistics structure
 */
void pci_shim_get_extended_stats(pci_shim_stats_t* stats);

/**
 * @brief Control config space caching
 * 
 * Enable or disable config space caching for performance optimization.
 * Caching is forced on in V86 mode to minimize I/O port access.
 * 
 * @param enabled true to enable caching, false to disable
 */
void pci_shim_set_cache_enabled(bool enabled);

/**
 * @brief Clear config cache
 * 
 * Invalidate all cached config space data. Useful after hot-plug events
 * or when switching between configuration modes.
 */
void pci_shim_clear_cache(void);

/* Enhanced config access functions with V86 awareness and caching */
bool pci_shim_enhanced_install(void);
bool pci_shim_enhanced_uninstall(void);
uint8_t pci_enhanced_read_config_byte(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint8_t pci_enhanced_write_config_byte(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint8_t value);
uint16_t pci_enhanced_read_config_word(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint16_t pci_enhanced_write_config_word(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t value);
uint32_t pci_enhanced_read_config_dword(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint32_t pci_enhanced_write_config_dword(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value);

#endif /* _PCI_SHIM_H_ */