/**
 * @file pci_power.c
 * @brief PCI Power Management implementation for DOS packet driver
 * 
 * Handles PCI power states, capability list walking, and device bring-up
 * from D3hot state. Critical for warm reboot scenarios and proper device
 * initialization.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "pci_bios.h"
#include "pci_power.h"
#include "logging.h"
#include "common.h"

/* PCI Capability IDs */
#define PCI_CAP_ID_PM           0x01    /* Power Management */
#define PCI_CAP_ID_AGP          0x02    /* AGP */
#define PCI_CAP_ID_VPD          0x03    /* Vital Product Data */
#define PCI_CAP_ID_MSI          0x05    /* Message Signaled Interrupts */
#define PCI_CAP_ID_VENDOR       0xFF    /* Vendor Specific */

/* Power Management Capability Offsets */
#define PCI_PM_CAP              0x02    /* PM Capabilities */
#define PCI_PM_CTRL             0x04    /* PM Control/Status */
#define PCI_PM_PPB_EXTENSIONS   0x06    /* PPB Support Extensions */
#define PCI_PM_DATA             0x07    /* Data register */

/* Power Management Control/Status bits */
#define PCI_PM_CTRL_STATE_MASK  0x0003  /* Power state mask */
#define PCI_PM_CTRL_STATE_D0    0x0000  /* D0 state */
#define PCI_PM_CTRL_STATE_D1    0x0001  /* D1 state */
#define PCI_PM_CTRL_STATE_D2    0x0002  /* D2 state */
#define PCI_PM_CTRL_STATE_D3HOT 0x0003  /* D3hot state */
#define PCI_PM_CTRL_PME_ENABLE  0x0100  /* PME Enable */
#define PCI_PM_CTRL_DATA_SEL    0x1E00  /* Data Select */
#define PCI_PM_CTRL_DATA_SCALE  0x6000  /* Data Scale */
#define PCI_PM_CTRL_PME_STATUS  0x8000  /* PME Status (write 1 to clear) */

/* Power Management Capabilities bits */
#define PCI_PM_CAP_VERSION      0x0007  /* Version */
#define PCI_PM_CAP_PME_CLOCK    0x0008  /* PME clock required */
#define PCI_PM_CAP_DSI          0x0020  /* Device specific initialization */
#define PCI_PM_CAP_D1           0x0200  /* D1 power state support */
#define PCI_PM_CAP_D2           0x0400  /* D2 power state support */
#define PCI_PM_CAP_PME_D0       0x0800  /* PME# from D0 */
#define PCI_PM_CAP_PME_D1       0x1000  /* PME# from D1 */
#define PCI_PM_CAP_PME_D2       0x2000  /* PME# from D2 */
#define PCI_PM_CAP_PME_D3HOT    0x4000  /* PME# from D3hot */
#define PCI_PM_CAP_PME_D3COLD   0x8000  /* PME# from D3cold */

/**
 * @brief Find PCI capability in device's capability list
 * 
 * Walks the PCI capability linked list looking for specific capability.
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @param cap_id Capability ID to find
 * @return Offset of capability in config space, or 0 if not found
 */
uint8_t pci_find_capability(uint8_t bus, uint8_t device, uint8_t function, uint8_t cap_id) {
    uint8_t pos, id;
    uint16_t status;
    int iterations = 0;
    
    /* Check if device has capabilities list */
    status = pci_read_config_word(bus, device, function, PCI_STATUS);
    if (!(status & PCI_STATUS_CAP_LIST)) {
        LOG_DEBUG("Device %02X:%02X.%X has no capability list", bus, device, function);
        return 0;
    }
    
    /* Get pointer to first capability */
    pos = pci_read_config_byte(bus, device, function, PCI_CAPABILITY_LIST);
    pos &= 0xFC;  /* Mask off reserved bits */
    
    /* Walk the capability list */
    while (pos && iterations < 48) {  /* Max 48 capabilities to prevent infinite loops */
        id = pci_read_config_byte(bus, device, function, pos + PCI_CAP_LIST_ID);
        
        if (id == 0xFF) {
            LOG_DEBUG("Invalid capability ID at offset 0x%02X", pos);
            break;
        }
        
        if (id == cap_id) {
            LOG_DEBUG("Found capability 0x%02X at offset 0x%02X for %02X:%02X.%X",
                     cap_id, pos, bus, device, function);
            return pos;
        }
        
        /* Get next capability pointer */
        pos = pci_read_config_byte(bus, device, function, pos + PCI_CAP_LIST_NEXT);
        pos &= 0xFC;
        iterations++;
    }
    
    if (iterations >= 48) {
        LOG_WARNING("Capability list too long or circular for %02X:%02X.%X",
                   bus, device, function);
    }
    
    LOG_DEBUG("Capability 0x%02X not found for %02X:%02X.%X",
             cap_id, bus, device, function);
    return 0;
}

/**
 * @brief Get current PCI power state
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @return Current power state (0=D0, 1=D1, 2=D2, 3=D3hot), or -1 on error
 */
int pci_get_power_state(uint8_t bus, uint8_t device, uint8_t function) {
    uint8_t pm_offset;
    uint16_t pmcsr;
    
    /* Find Power Management capability */
    pm_offset = pci_find_capability(bus, device, function, PCI_CAP_ID_PM);
    if (!pm_offset) {
        LOG_DEBUG("No PM capability for %02X:%02X.%X", bus, device, function);
        return -1;
    }
    
    /* Read PM Control/Status register */
    pmcsr = pci_read_config_word(bus, device, function, pm_offset + PCI_PM_CTRL);
    
    return (pmcsr & PCI_PM_CTRL_STATE_MASK);
}

/**
 * @brief Set PCI power state
 * 
 * Forces device to specified power state. Includes delays for state transitions.
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @param state Target power state (0=D0, 3=D3hot)
 * @return true on success, false on error
 */
bool pci_set_power_state(uint8_t bus, uint8_t device, uint8_t function, uint8_t state) {
    uint8_t pm_offset;
    uint16_t pmcsr;
    int current_state;
    int retry;
    
    /* Validate state */
    if (state > 3) {
        LOG_ERROR("Invalid power state %d requested", state);
        return false;
    }
    
    /* Find Power Management capability */
    pm_offset = pci_find_capability(bus, device, function, PCI_CAP_ID_PM);
    if (!pm_offset) {
        LOG_WARNING("No PM capability for %02X:%02X.%X - assuming D0",
                   bus, device, function);
        return true;  /* No PM = always in D0 */
    }
    
    /* Get current state */
    current_state = pci_get_power_state(bus, device, function);
    if (current_state == state) {
        LOG_DEBUG("Device already in D%d state", state);
        return true;
    }
    
    LOG_INFO("Transitioning %02X:%02X.%X from D%d to D%d",
            bus, device, function, current_state, state);
    
    /* Read current PM Control/Status */
    pmcsr = pci_read_config_word(bus, device, function, pm_offset + PCI_PM_CTRL);
    
    /* Clear power state bits and set new state */
    pmcsr &= ~PCI_PM_CTRL_STATE_MASK;
    pmcsr |= state;
    
    /* Write new power state */
    if (!pci_write_config_word(bus, device, function, pm_offset + PCI_PM_CTRL, pmcsr)) {
        LOG_ERROR("Failed to write PM control register");
        return false;
    }
    
    /* Delay for state transition */
    if (state == PCI_PM_CTRL_STATE_D0) {
        /* D3hot -> D0 requires longer delay */
        if (current_state == PCI_PM_CTRL_STATE_D3HOT) {
            delay_ms(10);  /* 10ms for D3hot -> D0 */
        } else {
            delay_ms(1);   /* 1ms for D1/D2 -> D0 */
        }
    } else {
        delay_ms(1);       /* 1ms for D0 -> D1/D2/D3 */
    }
    
    /* Verify state change */
    for (retry = 0; retry < 10; retry++) {
        current_state = pci_get_power_state(bus, device, function);
        if (current_state == state) {
            LOG_INFO("Successfully transitioned to D%d", state);
            return true;
        }
        delay_ms(1);
    }
    
    LOG_ERROR("Failed to transition to D%d (stuck in D%d)",
             state, current_state);
    return false;
}

/**
 * @brief Clear PME (Power Management Event) status
 * 
 * Clears PME status bit to remove wake event indication.
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @return true on success, false on error
 */
bool pci_clear_pme_status(uint8_t bus, uint8_t device, uint8_t function) {
    uint8_t pm_offset;
    uint16_t pmcsr;
    
    /* Find Power Management capability */
    pm_offset = pci_find_capability(bus, device, function, PCI_CAP_ID_PM);
    if (!pm_offset) {
        return true;  /* No PM = no PME to clear */
    }
    
    /* Read PM Control/Status */
    pmcsr = pci_read_config_word(bus, device, function, pm_offset + PCI_PM_CTRL);
    
    /* Check if PME Status is set */
    if (pmcsr & PCI_PM_CTRL_PME_STATUS) {
        LOG_INFO("Clearing PME status for %02X:%02X.%X", bus, device, function);
        
        /* Clear PME Status (write 1 to clear) */
        pmcsr |= PCI_PM_CTRL_PME_STATUS;
        
        if (!pci_write_config_word(bus, device, function, pm_offset + PCI_PM_CTRL, pmcsr)) {
            LOG_ERROR("Failed to clear PME status");
            return false;
        }
    }
    
    return true;
}

/**
 * @brief Perform complete power-on sequence for PCI device
 * 
 * Comprehensive bring-up from any power state to D0 with all housekeeping.
 * This is critical for devices that may be in D3hot after warm reboot.
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @return true on success, false on error
 */
bool pci_power_on_device(uint8_t bus, uint8_t device, uint8_t function) {
    int current_state;
    uint8_t pm_offset;
    uint16_t pm_cap;
    
    LOG_INFO("Powering on PCI device %02X:%02X.%X", bus, device, function);
    
    /* Find Power Management capability */
    pm_offset = pci_find_capability(bus, device, function, PCI_CAP_ID_PM);
    if (pm_offset) {
        /* Read PM capabilities */
        pm_cap = pci_read_config_word(bus, device, function, pm_offset + PCI_PM_CAP);
        LOG_DEBUG("PM capability version %d, D1=%d, D2=%d",
                 pm_cap & PCI_PM_CAP_VERSION,
                 (pm_cap & PCI_PM_CAP_D1) ? 1 : 0,
                 (pm_cap & PCI_PM_CAP_D2) ? 1 : 0);
        
        /* Get current power state */
        current_state = pci_get_power_state(bus, device, function);
        if (current_state > 0) {
            LOG_WARNING("Device in D%d state - recovering from warm reboot/OS handoff",
                       current_state);
        }
        
        /* Force to D0 state */
        if (!pci_set_power_state(bus, device, function, PCI_PM_CTRL_STATE_D0)) {
            LOG_ERROR("Failed to set D0 power state");
            return false;
        }
        
        /* Clear PME status */
        if (!pci_clear_pme_status(bus, device, function)) {
            LOG_WARNING("Failed to clear PME status (non-fatal)");
        }
        
        /* Disable PME generation */
        uint16_t pmcsr = pci_read_config_word(bus, device, function, pm_offset + PCI_PM_CTRL);
        pmcsr &= ~PCI_PM_CTRL_PME_ENABLE;
        pci_write_config_word(bus, device, function, pm_offset + PCI_PM_CTRL, pmcsr);
    } else {
        LOG_DEBUG("No PM capability - device should be in D0");
    }
    
    /* Perform standard PCI device setup */
    if (!pci_device_setup(bus, device, function, true, true, true)) {
        LOG_ERROR("Failed to setup PCI device");
        return false;
    }
    
    /* Additional delay for device to stabilize after power-on */
    delay_ms(10);
    
    LOG_INFO("Device %02X:%02X.%X powered on successfully", bus, device, function);
    
    return true;
}

/**
 * @brief Check if device supports specific power state
 * 
 * @param bus Bus number
 * @param device Device number
 * @param function Function number
 * @param state Power state to check (1=D1, 2=D2, 3=D3)
 * @return true if supported, false otherwise
 */
bool pci_power_state_supported(uint8_t bus, uint8_t device, uint8_t function, uint8_t state) {
    uint8_t pm_offset;
    uint16_t pm_cap;
    
    if (state == 0 || state > 3) {
        return (state == 0);  /* D0 always supported */
    }
    
    pm_offset = pci_find_capability(bus, device, function, PCI_CAP_ID_PM);
    if (!pm_offset) {
        return false;
    }
    
    pm_cap = pci_read_config_word(bus, device, function, pm_offset + PCI_PM_CAP);
    
    switch (state) {
        case 1: return (pm_cap & PCI_PM_CAP_D1) ? true : false;
        case 2: return (pm_cap & PCI_PM_CAP_D2) ? true : false;
        case 3: return true;  /* D3hot always supported if PM exists */
        default: return false;
    }
}