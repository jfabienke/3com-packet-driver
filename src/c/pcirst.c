/**
 * @file pci_reset.c
 * @brief Robust PCI device reset with timeouts and staged initialization
 * 
 * Implements production-quality device reset sequences with bounded waits,
 * status verification, and escalation strategies for 3Com NICs.
 */

#include <dos.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "pci_bios.h"
#include "pcipwr.h"
#include "pci_reset.h"
#include "hardware.h"
#include "logging.h"
#include "common.h"

/* 3Com NIC reset commands and registers */
#define CMD_GLOBAL_RESET        0x0000  /* Global reset command */
#define CMD_TX_RESET            0x5800  /* TX reset command */
#define CMD_RX_RESET            0x2800  /* RX reset command */
#define CMD_TX_ENABLE           0x4800  /* Enable transmitter */
#define CMD_RX_ENABLE           0x2000  /* Enable receiver */
#define CMD_STATS_ENABLE        0x6800  /* Enable statistics */

/* Status register bits */
#define STATUS_CMD_IN_PROGRESS  0x1000  /* Command in progress */
#define STATUS_TX_COMPLETE      0x0004  /* TX complete */
#define STATUS_RX_COMPLETE      0x0010  /* RX complete */
#define STATUS_UPDATE_STATS     0x0080  /* Statistics updated */

/* Reset timing parameters (milliseconds) */
#define RESET_DELAY_MIN         10      /* Minimum reset delay */
#define RESET_DELAY_MAX         100     /* Maximum reset delay */
#define RESET_TIMEOUT           1000    /* Total reset timeout */
#define CMD_TIMEOUT             100     /* Command completion timeout */
#define POLL_INTERVAL           1       /* Status poll interval */

/* MSI capability registers */
#define PCI_CAP_ID_MSI          0x05    /* Message Signaled Interrupts */
#define PCI_CAP_ID_MSIX         0x11    /* MSI-X */
#define PCI_MSI_FLAGS           0x02    /* MSI flags */
#define PCI_MSI_FLAGS_ENABLE    0x01    /* MSI enable bit */
#define PCI_MSIX_FLAGS          0x02    /* MSI-X flags */
#define PCI_MSIX_FLAGS_ENABLE   0x8000  /* MSI-X enable bit */

/* INTx control */
#define PCI_COMMAND_INTX_DISABLE 0x0400 /* INTx disable bit */

/**
 * @brief Wait for command completion with timeout
 * 
 * @param iobase NIC I/O base address
 * @param timeout_ms Timeout in milliseconds
 * @return true if command completed, false on timeout
 */
static bool wait_for_command(uint16_t iobase, uint32_t timeout_ms) {
    uint32_t elapsed = 0;
    uint16_t status;
    
    while (elapsed < timeout_ms) {
        status = inw(iobase + 0x0E);  /* Read IntStatus */
        
        if (!(status & STATUS_CMD_IN_PROGRESS)) {
            return true;  /* Command completed */
        }
        
        delay_ms(POLL_INTERVAL);
        elapsed += POLL_INTERVAL;
    }
    
    LOG_ERROR("Command timeout after %lu ms (status=0x%04X)", elapsed, status);
    return false;
}

/**
 * @brief Issue command to NIC with verification
 * 
 * @param iobase NIC I/O base address
 * @param command Command to issue
 * @return true on success, false on timeout
 */
static bool issue_command(uint16_t iobase, uint16_t command) {
    /* Issue command */
    outw(iobase + 0x0E, command);
    
    /* Wait for completion */
    return wait_for_command(iobase, CMD_TIMEOUT);
}

/**
 * @brief Perform soft reset on device
 * 
 * @param iobase NIC I/O base address
 * @return true on success, false on failure
 */
static bool soft_reset_device(uint16_t iobase) {
    uint16_t id;

    LOG_INFO("Performing soft reset at I/O 0x%04X", iobase);

    /* Issue global reset command */
    if (!issue_command(iobase, CMD_GLOBAL_RESET)) {
        LOG_ERROR("Global reset command failed");
        return false;
    }

    /* Wait for reset to complete */
    delay_ms(RESET_DELAY_MIN);

    /* Verify device is responsive */
    id = inw(iobase + 0x00);  /* Read device ID */
    if (id == 0xFFFF || id == 0x0000) {
        LOG_ERROR("Device not responding after reset (ID=0x%04X)", id);
        return false;
    }
    
    LOG_INFO("Soft reset successful (ID=0x%04X)", id);
    return true;
}

/**
 * @brief Quiesce DMA operations
 * 
 * @param iobase NIC I/O base address
 * @return true on success, false on failure
 */
static bool quiesce_dma(uint16_t iobase) {
    LOG_DEBUG("Quiescing DMA operations");
    
    /* Disable TX */
    if (!issue_command(iobase, CMD_TX_RESET)) {
        LOG_ERROR("TX reset failed");
        return false;
    }
    
    /* Disable RX */
    if (!issue_command(iobase, CMD_RX_RESET)) {
        LOG_ERROR("RX reset failed");
        return false;
    }
    
    /* Wait for DMA to stop */
    delay_ms(10);
    
    /* Clear any pending interrupts */
    outw(iobase + 0x0E, 0x6FFF);  /* Acknowledge all interrupts */
    
    return true;
}

/**
 * @brief Ensure INTx interrupts are enabled for 3Com NICs
 * 
 * 3Com 3C59x/3C90x families do not support MSI/MSI-X, so we only need
 * to ensure INTx is enabled and properly configured.
 * 
 * @param bus PCI bus number
 * @param device PCI device number
 * @param function PCI function number
 * @return true on success, false on failure
 */
bool pci_enable_intx_interrupts(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t command;
    uint8_t int_pin;
    
    LOG_INFO("Ensuring INTx interrupts enabled for %02X:%02X.%X", bus, device, function);
    
    /* Verify Interrupt Pin is valid first */
    int_pin = pci_read_config_byte(bus, device, function, PCI_INTERRUPT_PIN);
    if (int_pin == 0 || int_pin > 4) {
        LOG_ERROR("Invalid or no Interrupt Pin (0x%02X) - device does not support INTx", int_pin);
        return false;
    }
    
    /* Enable INTx (clear INTx disable bit) */
    command = pci_read_config_word(bus, device, function, PCI_COMMAND);
    if (command & PCI_COMMAND_INTX_DISABLE) {
        LOG_INFO("Enabling INTx interrupts (Pin %c)", 'A' + int_pin - 1);
        command &= ~PCI_COMMAND_INTX_DISABLE;
        pci_write_config_word(bus, device, function, PCI_COMMAND, command);
    } else {
        LOG_DEBUG("INTx already enabled (Pin %c)", 'A' + int_pin - 1);
    }
    
    return true;
}

/**
 * @brief Perform robust device reset with escalation
 * 
 * Comprehensive reset sequence with timeouts, verification, and escalation.
 * 
 * @param bus PCI bus number
 * @param device PCI device number
 * @param function PCI function number
 * @param iobase Device I/O base address
 * @return Reset status code
 */
reset_status_t pci_reset_device(uint8_t bus, uint8_t device, uint8_t function, uint16_t iobase) {
    uint32_t start_time, elapsed;
    uint16_t vendor_id, device_id;
    uint16_t command_save, status;
    bool success = false;
    
    LOG_INFO("=== Starting robust device reset for %02X:%02X.%X ===", bus, device, function);
    
    /* Save PCI command register */
    command_save = pci_read_config_word(bus, device, function, PCI_COMMAND);
    
    /* Stage 1: Disable interrupts and DMA */
    LOG_INFO("Stage 1: Disabling interrupts and DMA");
    
    /* Mask device interrupts */
    outw(iobase + 0x0A, 0x0000);  /* IntEnable = 0 */
    
    /* Disable bus mastering temporarily */
    pci_write_config_word(bus, device, function, PCI_COMMAND, 
                         command_save & ~PCI_CMD_MASTER);
    
    /* Quiesce DMA */
    if (!quiesce_dma(iobase)) {
        LOG_WARNING("DMA quiesce failed - continuing with reset");
    }
    
    /* Stage 2: Soft reset attempt */
    LOG_INFO("Stage 2: Attempting soft reset");
    start_time = 0;  /* TODO: implement get_system_ticks via BIOS tick counter */
    
    if (soft_reset_device(iobase)) {
        success = true;
    } else {
        /* Stage 3: Hard reset via D3->D0 transition */
        LOG_WARNING("Soft reset failed - attempting D3->D0 power cycle");
        
        /* Force D3hot */
        if (pci_set_power_state(bus, device, function, PCI_POWER_D3HOT)) {
            delay_ms(50);  /* D3 settling time */
            
            /* Force back to D0 */
            if (pci_power_on_device(bus, device, function)) {
                delay_ms(RESET_DELAY_MAX);  /* D0 settling time */
                
                /* Verify device is back */
                vendor_id = pci_read_config_word(bus, device, function, PCI_VENDOR_ID);
                device_id = pci_read_config_word(bus, device, function, PCI_DEVICE_ID);
                
                if (vendor_id != 0xFFFF && vendor_id != 0x0000) {
                    LOG_INFO("D3->D0 reset successful (VID:DID=%04X:%04X)", vendor_id, device_id);
                    success = true;
                }
            }
        }
        
        if (!success) {
            /* Stage 4: Function Level Reset if available */
            LOG_ERROR("All reset attempts failed");
            return RESET_FAILED;
        }
    }
    
    elapsed = 0;  /* TODO: implement get_system_ticks via BIOS tick counter */
    LOG_INFO("Reset completed in %lu ms", elapsed);
    
    /* Stage 5: Restore PCI configuration */
    LOG_INFO("Stage 5: Restoring PCI configuration");
    
    /* Clear status bits */
    pci_clear_status_bits(bus, device, function);
    
    /* Restore command register (but not bus master yet) */
    pci_write_config_word(bus, device, function, PCI_COMMAND, 
                         command_save & ~PCI_CMD_MASTER);
    
    /* Ensure INTx is enabled (3Com NICs don't support MSI/MSI-X) */
    pci_enable_intx_interrupts(bus, device, function);
    
    /* Stage 6: Reinitialize device registers */
    LOG_INFO("Stage 6: Reinitializing device registers");
    
    /* Clear internal statistics */
    issue_command(iobase, CMD_STATS_ENABLE);
    
    /* Set RX filter (station address only initially) */
    outw(iobase + 0x08, 0x01);  /* RxFilter = station address */
    
    /* Set TX threshold */
    outw(iobase + 0x1C, 0x8080);  /* TxStartThresh = store-and-forward */
    
    /* Stage 7: Verify device state */
    LOG_INFO("Stage 7: Verifying device state");
    
    /* Read and verify key registers */
    status = inw(iobase + 0x0E);
    if (status & STATUS_CMD_IN_PROGRESS) {
        LOG_WARNING("Command still in progress after reset");
        return RESET_PARTIAL;
    }
    
    /* Check for errors */
    status = pci_read_config_word(bus, device, function, PCI_STATUS);
    if (status & (PCI_STATUS_REC_MASTER_ABORT | PCI_STATUS_REC_TARGET_ABORT | 
                  PCI_STATUS_SIG_SYSTEM_ERROR | PCI_STATUS_DETECTED_PARITY)) {
        LOG_WARNING("PCI errors after reset: 0x%04X", status);
        pci_clear_status_bits(bus, device, function);
    }
    
    LOG_INFO("=== Device reset successful ===");
    return RESET_SUCCESS;
}

/**
 * @brief Enable bus mastering after reset
 * 
 * Must be called after descriptor rings are programmed.
 * 
 * @param bus PCI bus number
 * @param device PCI device number
 * @param function PCI function number
 * @param iobase Device I/O base address
 * @return true on success, false on failure
 */
bool pci_enable_bus_mastering(uint8_t bus, uint8_t device, uint8_t function, uint16_t iobase) {
    uint16_t command;
    
    LOG_INFO("Enabling bus mastering for %02X:%02X.%X", bus, device, function);
    
    /* Verify descriptor rings are programmed (implementation-specific check) */
    /* This would check that DnListPtr and UpListPtr are non-zero */
    
    /* Enable bus mastering in PCI config */
    command = pci_read_config_word(bus, device, function, PCI_COMMAND);
    command |= PCI_CMD_MASTER;
    pci_write_config_word(bus, device, function, PCI_COMMAND, command);
    
    /* Enable TX and RX in device */
    if (!issue_command(iobase, CMD_TX_ENABLE)) {
        LOG_ERROR("Failed to enable TX");
        return false;
    }
    
    if (!issue_command(iobase, CMD_RX_ENABLE)) {
        LOG_ERROR("Failed to enable RX");
        return false;
    }
    
    /* Enable interrupts last */
    outw(iobase + 0x0A, 0x01FF);  /* Enable all interrupt sources */
    
    LOG_INFO("Bus mastering and interrupts enabled");
    return true;
}

/**
 * @brief Get reset status string
 * 
 * @param status Reset status code
 * @return Human-readable status string
 */
const char* pci_reset_status_string(reset_status_t status) {
    switch (status) {
        case RESET_SUCCESS:     return "Success";
        case RESET_TIMEOUT:     return "Timeout";
        case RESET_FAILED:      return "Failed";
        case RESET_PARTIAL:     return "Partial";
        case RESET_NOT_NEEDED:  return "Not needed";
        default:                return "Unknown";
    }
}
