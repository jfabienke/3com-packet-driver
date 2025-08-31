/**
 * @file device_registry.c
 * @brief Device Registry Service Implementation
 * 
 * Implementation of the device registry that tracks all detected hardware
 * devices and their claimed state to prevent double-attach scenarios.
 * 
 * ARCHITECTURE: Two-phase model recommended by GPT-5
 * 1. Detection: Discover and register device candidates (non-invasive)
 * 2. Attach: Driver verifies, claims, and configures specific device
 */

#include "device_registry.h"
#include "../../include/common.h"
#include "../../include/hardware.h"
#include <stddef.h>
#include <string.h>

/* Global device registry state */
static device_entry_t g_device_registry[MAX_REGISTRY_DEVICES];
static int g_registry_count = 0;
static uint8_t g_registry_initialized = 0;

/* Internal helper functions */
static int find_registry_slot(void);
static int find_device_by_id(int registry_id);
static int validate_registry_id(int registry_id);

/**
 * @brief Initialize device registry
 */
int device_registry_init(void)
{
    int i;
    
    if (g_registry_initialized) {
        return SUCCESS;
    }
    
    /* Clear all registry entries */
    for (i = 0; i < MAX_REGISTRY_DEVICES; i++) {
        memset(&g_device_registry[i], 0, sizeof(device_entry_t));
        g_device_registry[i].claimed = 0;
        g_device_registry[i].owner_module_id = 0;
        g_device_registry[i].verified = 0;
    }
    
    g_registry_count = 0;
    g_registry_initialized = 1;
    
    return SUCCESS;
}

/**
 * @brief Register a detected device
 */
int device_registry_add(const device_entry_t *entry)
{
    int slot;
    
    if (!g_registry_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!entry) {
        return ERROR_INVALID_PARAMETER;
    }
    
    /* Check for duplicate device (same bus location) */
    if (device_registry_find_by_location(entry->bus_type, entry->io_base,
                                        entry->pci_bus, entry->pci_device,
                                        entry->pci_function) >= 0) {
        return ERROR_DEVICE_EXISTS;
    }
    
    /* Find available slot */
    slot = find_registry_slot();
    if (slot < 0) {
        return ERROR_REGISTRY_FULL;
    }
    
    /* Copy device information */
    memcpy(&g_device_registry[slot], entry, sizeof(device_entry_t));
    
    /* Ensure device starts unclaimed */
    g_device_registry[slot].claimed = 0;
    g_device_registry[slot].owner_module_id = 0;
    g_device_registry[slot].verified = 0;
    
    g_registry_count++;
    
    return slot; /* Return registry ID */
}

/**
 * @brief Claim a device atomically
 */
int device_registry_claim(int registry_id, uint16_t module_id)
{
    int slot;
    
    if (!g_registry_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    if (validate_registry_id(registry_id) != SUCCESS) {
        return ERROR_INVALID_PARAMETER;
    }
    
    if (module_id == 0) {
        return ERROR_INVALID_PARAMETER;
    }
    
    slot = find_device_by_id(registry_id);
    if (slot < 0) {
        return ERROR_DEVICE_NOT_FOUND;
    }
    
    /* Atomic claim check */
    if (g_device_registry[slot].claimed) {
        return ERROR_DEVICE_BUSY;
    }
    
    /* Claim device */
    g_device_registry[slot].claimed = 1;
    g_device_registry[slot].owner_module_id = module_id;
    
    return SUCCESS;
}

/**
 * @brief Release a claimed device
 */
int device_registry_release(int registry_id, uint16_t module_id)
{
    int slot;
    
    if (!g_registry_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    if (validate_registry_id(registry_id) != SUCCESS) {
        return ERROR_INVALID_PARAMETER;
    }
    
    slot = find_device_by_id(registry_id);
    if (slot < 0) {
        return ERROR_DEVICE_NOT_FOUND;
    }
    
    /* Verify ownership */
    if (!g_device_registry[slot].claimed) {
        return ERROR_DEVICE_NOT_CLAIMED;
    }
    
    if (g_device_registry[slot].owner_module_id != module_id) {
        return ERROR_ACCESS_DENIED;
    }
    
    /* Release device */
    g_device_registry[slot].claimed = 0;
    g_device_registry[slot].owner_module_id = 0;
    g_device_registry[slot].verified = 0;
    
    return SUCCESS;
}

/**
 * @brief Mark device as verified by driver
 */
int device_registry_verify(int registry_id, uint16_t module_id)
{
    int slot;
    
    if (!g_registry_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    if (validate_registry_id(registry_id) != SUCCESS) {
        return ERROR_INVALID_PARAMETER;
    }
    
    slot = find_device_by_id(registry_id);
    if (slot < 0) {
        return ERROR_DEVICE_NOT_FOUND;
    }
    
    /* Verify ownership */
    if (!g_device_registry[slot].claimed || 
        g_device_registry[slot].owner_module_id != module_id) {
        return ERROR_ACCESS_DENIED;
    }
    
    /* Mark as verified */
    g_device_registry[slot].verified = 1;
    
    return SUCCESS;
}

/**
 * @brief Query registry for matching devices
 */
int device_registry_query(const device_filter_t *filter, 
                         int *results, int max_results)
{
    int i, result_count = 0;
    device_entry_t *entry;
    
    if (!g_registry_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!filter || !results || max_results <= 0) {
        return ERROR_INVALID_PARAMETER;
    }
    
    for (i = 0; i < MAX_REGISTRY_DEVICES && result_count < max_results; i++) {
        entry = &g_device_registry[i];
        
        /* Skip empty slots */
        if (entry->device_id == 0 && entry->vendor_id == 0) {
            continue;
        }
        
        /* Apply filters */
        if (filter->vendor_id != 0 && entry->vendor_id != filter->vendor_id) {
            continue;
        }
        
        if (filter->device_id != 0 && entry->device_id != filter->device_id) {
            continue;
        }
        
        if (filter->bus_type != 0 && entry->bus_type != filter->bus_type) {
            continue;
        }
        
        if (filter->claimed_state != 0xFF) {
            if (filter->claimed_state != entry->claimed) {
                continue;
            }
        }
        
        /* Match found */
        results[result_count++] = i;
    }
    
    return result_count;
}

/**
 * @brief Get device entry by registry ID
 */
const device_entry_t* device_registry_get(int registry_id)
{
    int slot;
    
    if (!g_registry_initialized) {
        return NULL;
    }
    
    if (validate_registry_id(registry_id) != SUCCESS) {
        return NULL;
    }
    
    slot = find_device_by_id(registry_id);
    if (slot < 0) {
        return NULL;
    }
    
    return &g_device_registry[slot];
}

/**
 * @brief Iterate all devices with callback
 */
int device_registry_iterate(device_callback_t callback, void *user_data)
{
    int i, processed = 0;
    device_entry_t *entry;
    
    if (!g_registry_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!callback) {
        return ERROR_INVALID_PARAMETER;
    }
    
    for (i = 0; i < MAX_REGISTRY_DEVICES; i++) {
        entry = &g_device_registry[i];
        
        /* Skip empty slots */
        if (entry->device_id == 0 && entry->vendor_id == 0) {
            continue;
        }
        
        processed++;
        
        /* Call callback - stop iteration if non-zero returned */
        if (callback(entry, user_data) != 0) {
            break;
        }
    }
    
    return processed;
}

/**
 * @brief Get registry statistics
 */
int device_registry_get_stats(int *total_devices, int *claimed_devices, int *verified_devices)
{
    int i, total = 0, claimed = 0, verified = 0;
    device_entry_t *entry;
    
    if (!g_registry_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!total_devices || !claimed_devices || !verified_devices) {
        return ERROR_INVALID_PARAMETER;
    }
    
    for (i = 0; i < MAX_REGISTRY_DEVICES; i++) {
        entry = &g_device_registry[i];
        
        /* Skip empty slots */
        if (entry->device_id == 0 && entry->vendor_id == 0) {
            continue;
        }
        
        total++;
        
        if (entry->claimed) {
            claimed++;
        }
        
        if (entry->verified) {
            verified++;
        }
    }
    
    *total_devices = total;
    *claimed_devices = claimed;
    *verified_devices = verified;
    
    return SUCCESS;
}

/**
 * @brief Find device by bus location
 */
int device_registry_find_by_location(uint8_t bus_type, uint16_t io_base,
                                    uint8_t pci_bus, uint8_t pci_device, 
                                    uint8_t pci_function)
{
    int i;
    device_entry_t *entry;
    
    if (!g_registry_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    for (i = 0; i < MAX_REGISTRY_DEVICES; i++) {
        entry = &g_device_registry[i];
        
        /* Skip empty slots */
        if (entry->device_id == 0 && entry->vendor_id == 0) {
            continue;
        }
        
        /* Match bus type */
        if (entry->bus_type != bus_type) {
            continue;
        }
        
        /* Bus-specific matching */
        switch (bus_type) {
            case BUS_TYPE_ISA:
                if (entry->io_base == io_base) {
                    return i;
                }
                break;
                
            case BUS_TYPE_PCI:
            case BUS_TYPE_PCMCIA:
                if (entry->pci_bus == pci_bus &&
                    entry->pci_device == pci_device &&
                    entry->pci_function == pci_function) {
                    return i;
                }
                break;
        }
    }
    
    return ERROR_DEVICE_NOT_FOUND;
}

/**
 * @brief Find device by MAC address
 */
int device_registry_find_by_mac(const uint8_t *mac_address)
{
    int i;
    device_entry_t *entry;
    
    if (!g_registry_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    if (!mac_address) {
        return ERROR_INVALID_PARAMETER;
    }
    
    for (i = 0; i < MAX_REGISTRY_DEVICES; i++) {
        entry = &g_device_registry[i];
        
        /* Skip empty slots */
        if (entry->device_id == 0 && entry->vendor_id == 0) {
            continue;
        }
        
        /* Skip entries without MAC */
        if (entry->mac_address[0] == 0 && entry->mac_address[1] == 0 &&
            entry->mac_address[2] == 0 && entry->mac_address[3] == 0 &&
            entry->mac_address[4] == 0 && entry->mac_address[5] == 0) {
            continue;
        }
        
        /* Compare MAC address */
        if (memcmp(entry->mac_address, mac_address, 6) == 0) {
            return i;
        }
    }
    
    return ERROR_DEVICE_NOT_FOUND;
}

/* Internal helper functions */

/**
 * @brief Find available registry slot
 */
static int find_registry_slot(void)
{
    int i;
    
    for (i = 0; i < MAX_REGISTRY_DEVICES; i++) {
        if (g_device_registry[i].device_id == 0 && 
            g_device_registry[i].vendor_id == 0) {
            return i;
        }
    }
    
    return ERROR_REGISTRY_FULL;
}

/**
 * @brief Find device by registry ID
 */
static int find_device_by_id(int registry_id)
{
    if (registry_id < 0 || registry_id >= MAX_REGISTRY_DEVICES) {
        return ERROR_INVALID_PARAMETER;
    }
    
    /* Registry ID is direct array index */
    if (g_device_registry[registry_id].device_id == 0 && 
        g_device_registry[registry_id].vendor_id == 0) {
        return ERROR_DEVICE_NOT_FOUND;
    }
    
    return registry_id;
}

/**
 * @brief Validate registry ID parameter
 */
static int validate_registry_id(int registry_id)
{
    if (registry_id < 0 || registry_id >= MAX_REGISTRY_DEVICES) {
        return ERROR_INVALID_PARAMETER;
    }
    
    return SUCCESS;
}