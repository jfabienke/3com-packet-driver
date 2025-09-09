/**
 * @file el3_hal.c
 * @brief Hardware Abstraction Layer for 3Com EtherLink III
 *
 * Thin abstraction layer for register access during initialization ONLY.
 * NOT used in performance-critical datapath operations.
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include <stdint.h>
#include <dos.h>
#include "../core/el3_core.h"
#include "el3_hal.h"
#include "../../include/logging.h"

/* Window selection tracking */
static uint8_t g_current_windows[MAX_EL3_DEVICES] = {0xFF, 0xFF, 0xFF, 0xFF};

/**
 * @brief Read 8-bit value from register
 * 
 * FOR INITIALIZATION ONLY - NOT FOR DATAPATH
 */
uint8_t el3_read8(struct el3_dev *dev, uint16_t offset)
{
    if (dev->io_mapped) {
        return inportb(dev->io_base + offset);
    } else {
        /* Memory-mapped I/O for future PCI cards */
        return *((volatile uint8_t *)(dev->mem_base + offset));
    }
}

/**
 * @brief Read 16-bit value from register
 * 
 * FOR INITIALIZATION ONLY - NOT FOR DATAPATH
 */
uint16_t el3_read16(struct el3_dev *dev, uint16_t offset)
{
    if (dev->io_mapped) {
        return inportw(dev->io_base + offset);
    } else {
        /* Memory-mapped I/O for future PCI cards */
        return *((volatile uint16_t *)(dev->mem_base + offset));
    }
}

/**
 * @brief Read 32-bit value from register
 * 
 * FOR INITIALIZATION ONLY - NOT FOR DATAPATH
 */
uint32_t el3_read32(struct el3_dev *dev, uint16_t offset)
{
    if (dev->io_mapped) {
        /* Read as two 16-bit values for DOS compatibility */
        uint32_t low = inportw(dev->io_base + offset);
        uint32_t high = inportw(dev->io_base + offset + 2);
        return (high << 16) | low;
    } else {
        /* Memory-mapped I/O for future PCI cards */
        return *((volatile uint32_t *)(dev->mem_base + offset));
    }
}

/**
 * @brief Write 8-bit value to register
 * 
 * FOR INITIALIZATION ONLY - NOT FOR DATAPATH
 */
void el3_write8(struct el3_dev *dev, uint16_t offset, uint8_t value)
{
    if (dev->io_mapped) {
        outportb(dev->io_base + offset, value);
    } else {
        /* Memory-mapped I/O for future PCI cards */
        *((volatile uint8_t *)(dev->mem_base + offset)) = value;
    }
}

/**
 * @brief Write 16-bit value to register
 * 
 * FOR INITIALIZATION ONLY - NOT FOR DATAPATH
 */
void el3_write16(struct el3_dev *dev, uint16_t offset, uint16_t value)
{
    if (dev->io_mapped) {
        outportw(dev->io_base + offset, value);
    } else {
        /* Memory-mapped I/O for future PCI cards */
        *((volatile uint16_t *)(dev->mem_base + offset)) = value;
    }
}

/**
 * @brief Write 32-bit value to register
 * 
 * FOR INITIALIZATION ONLY - NOT FOR DATAPATH
 */
void el3_write32(struct el3_dev *dev, uint16_t offset, uint32_t value)
{
    if (dev->io_mapped) {
        /* Write as two 16-bit values for DOS compatibility */
        outportw(dev->io_base + offset, value & 0xFFFF);
        outportw(dev->io_base + offset + 2, (value >> 16) & 0xFFFF);
    } else {
        /* Memory-mapped I/O for future PCI cards */
        *((volatile uint32_t *)(dev->mem_base + offset)) = value;
    }
}

/**
 * @brief Select register window
 * 
 * FOR INITIALIZATION ONLY - NOT FOR DATAPATH
 * Vortex+ cards have permanent Window 1 for operating mode
 */
void el3_select_window(struct el3_dev *dev, uint8_t window)
{
    static int dev_index = 0;  /* Simple device indexing */
    
    /* Vortex+ cards with permanent Window 1 don't need switching in operation */
    if (dev->caps.has_permanent_window1 && dev->running && window == 1) {
        return;
    }
    
    /* Track current window to minimize switches */
    if (dev->current_window != window) {
        uint16_t cmd = (1 << 11) | window;  /* SelectWindow command */
        
        if (dev->io_mapped) {
            outportw(dev->io_base + EL3_CMD, cmd);
        } else {
            *((volatile uint16_t *)(dev->mem_base + EL3_CMD)) = cmd;
        }
        
        dev->current_window = window;
        
        LOG_DEBUG("EL3-HAL: Window switch to %d", window);
    }
}

/**
 * @brief Issue command to command register
 * 
 * FOR INITIALIZATION AND CONTROL ONLY - NOT FOR DATAPATH
 */
void el3_issue_command(struct el3_dev *dev, uint16_t cmd)
{
    if (dev->io_mapped) {
        outportw(dev->io_base + EL3_CMD, cmd);
    } else {
        *((volatile uint16_t *)(dev->mem_base + EL3_CMD)) = cmd;
    }
    
    /* Some commands need time to complete */
    if ((cmd >> 11) == 0) {  /* Global reset */
        delay_ms(2);
    }
}