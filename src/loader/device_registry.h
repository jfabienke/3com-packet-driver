/**
 * @file device_registry.h
 * @brief Device Registry Service Interface
 * 
 * The device registry tracks all detected hardware devices and their
 * claimed state to prevent double-attach scenarios and provide clean
 * ownership semantics.
 * 
 * ARCHITECTURE: Two-phase model recommended by GPT-5
 * 1. Detection: Discover and register device candidates
 * 2. Attach: Driver verifies, claims, and configures specific device
 */

#ifndef DEVICE_REGISTRY_H
#define DEVICE_REGISTRY_H

#include <stdint.h>

#define MAX_REGISTRY_DEVICES    16

/**
 * @brief Device registry entry
 * 
 * Represents a detected hardware device with claim status
 */
typedef struct {
    /* Hardware identification */
    uint16_t device_id;             /* Hardware device ID */
    uint16_t vendor_id;             /* Hardware vendor ID */
    uint16_t subsystem_vendor;      /* Subsystem vendor (PCI) */
    uint16_t subsystem_device;      /* Subsystem device (PCI) */
    uint8_t revision;               /* Hardware revision */
    
    /* Bus and location info */
    uint8_t bus_type;               /* BUS_TYPE_ISA/PCI/PCMCIA */
    uint16_t io_base;               /* I/O base address */
    uint32_t mem_base;              /* Memory base (PCI) */
    uint8_t irq;                    /* IRQ line (0 = unknown) */
    
    /* PCI-specific location */
    uint8_t pci_bus;                /* PCI bus number */
    uint8_t pci_device;             /* PCI device number */
    uint8_t pci_function;           /* PCI function number */
    
    /* Device state */
    uint8_t claimed;                /* 0=free, 1=claimed */
    uint16_t owner_module_id;       /* Module that claimed device */
    uint8_t verified;               /* Driver verified device */
    
    /* Additional info */
    uint8_t mac_address[6];         /* MAC address (if known) */
    uint32_t capabilities;          /* Device capabilities flags */
    
} device_entry_t;

/**
 * @brief Device filter for queries
 */
typedef struct {
    uint16_t vendor_id;             /* Filter by vendor (0 = any) */
    uint16_t device_id;             /* Filter by device (0 = any) */
    uint8_t bus_type;               /* Filter by bus type (0 = any) */
    uint8_t claimed_state;          /* 0=unclaimed, 1=claimed, 0xFF=any */
} device_filter_t;

/**
 * @brief Device registry callback function
 * 
 * @param entry Device entry
 * @param user_data User-provided data
 * @return 0 to continue iteration, non-zero to stop
 */
typedef int (*device_callback_t)(const device_entry_t *entry, void *user_data);

/* Device registry API */

/**
 * @brief Initialize device registry
 * 
 * Must be called before using registry functions.
 * 
 * @return SUCCESS on success, negative error code on failure
 */
int device_registry_init(void);

/**
 * @brief Register a detected device
 * 
 * Adds a device to the registry in unclaimed state.
 * Called by detection service for each discovered device.
 * 
 * @param entry Device information to register
 * @return Device registry ID (>= 0) or negative error code
 */
int device_registry_add(const device_entry_t *entry);

/**
 * @brief Claim a device atomically
 * 
 * Attempts to claim a device for exclusive use by a module.
 * Fails if device is already claimed.
 * 
 * @param registry_id Device registry ID
 * @param module_id Module attempting to claim device
 * @return SUCCESS if claimed, ERROR_DEVICE_BUSY if already claimed
 */
int device_registry_claim(int registry_id, uint16_t module_id);

/**
 * @brief Release a claimed device
 * 
 * Releases device claim, making it available for other modules.
 * Only the owning module can release its own devices.
 * 
 * @param registry_id Device registry ID
 * @param module_id Module releasing the device
 * @return SUCCESS on success, ERROR_ACCESS_DENIED if not owner
 */
int device_registry_release(int registry_id, uint16_t module_id);

/**
 * @brief Mark device as verified by driver
 * 
 * Called by driver after successful attach to indicate
 * device has been verified and configured.
 * 
 * @param registry_id Device registry ID
 * @param module_id Module that verified device
 * @return SUCCESS on success, negative error code on failure
 */
int device_registry_verify(int registry_id, uint16_t module_id);

/**
 * @brief Query registry for matching devices
 * 
 * Returns array of registry IDs for devices matching filter.
 * 
 * @param filter Device filter criteria
 * @param results Array to store registry IDs
 * @param max_results Maximum number of results
 * @return Number of matching devices found
 */
int device_registry_query(const device_filter_t *filter, 
                         int *results, int max_results);

/**
 * @brief Get device entry by registry ID
 * 
 * @param registry_id Device registry ID
 * @return Pointer to device entry or NULL if not found
 */
const device_entry_t* device_registry_get(int registry_id);

/**
 * @brief Iterate all devices with callback
 * 
 * Calls callback function for each device in registry.
 * Iteration stops if callback returns non-zero.
 * 
 * @param callback Callback function
 * @param user_data User data passed to callback
 * @return Number of devices processed
 */
int device_registry_iterate(device_callback_t callback, void *user_data);

/**
 * @brief Get registry statistics
 * 
 * @param total_devices Total devices in registry
 * @param claimed_devices Number of claimed devices
 * @param verified_devices Number of verified devices
 * @return SUCCESS on success
 */
int device_registry_get_stats(int *total_devices, int *claimed_devices, int *verified_devices);

/**
 * @brief Find device by bus location
 * 
 * Convenience function to find device by bus address.
 * 
 * @param bus_type Bus type (BUS_TYPE_*)
 * @param io_base I/O base address (ISA/PCI I/O)
 * @param pci_bus PCI bus number (PCI only)
 * @param pci_device PCI device number (PCI only)
 * @param pci_function PCI function number (PCI only)
 * @return Registry ID or negative error code if not found
 */
int device_registry_find_by_location(uint8_t bus_type, uint16_t io_base,
                                    uint8_t pci_bus, uint8_t pci_device, 
                                    uint8_t pci_function);

/**
 * @brief Find device by MAC address
 * 
 * Convenience function to find device by MAC address.
 * 
 * @param mac_address MAC address to search for
 * @return Registry ID or negative error code if not found
 */
int device_registry_find_by_mac(const uint8_t *mac_address);

#endif /* DEVICE_REGISTRY_H */