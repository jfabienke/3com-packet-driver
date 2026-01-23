/**
 * @file runtime_config.c
 * @brief Runtime configuration variables and functions for Extension API
 *
 * Provides runtime-adjustable parameters for copy-break threshold,
 * interrupt mitigation, and media mode control.
 */

#include "../include/common.h"
#include "../include/hardware.h"

/* Global runtime configuration variables */
uint16_t g_copy_break_threshold = 256;     /* Default 256 bytes */
uint8_t g_mitigation_batch = 10;           /* Default 10 packets */
uint8_t g_mitigation_timeout = 2;          /* Default 2 ticks */

/**
 * @brief Set hardware media mode
 * @param mode Media mode (0=auto, 1=10baseT, 2=10base2, 3=100baseTX)
 * @return 0 on success, non-zero on error
 */
int hardware_set_media_mode(uint8_t mode) {
    nic_info_t *nic;
    uint16_t media_ctrl;
    
    /* Get primary NIC */
    nic = hardware_get_primary_nic();
    if (!nic) {
        return -1;
    }
    
    /* Validate mode */
    if (mode > 3) {
        return -1;
    }
    
    /* For 3C515, set media through Window 3 */
    if (nic->type == NIC_TYPE_3C515_TX) {
        /* Select window 3 */
        outw(nic->io_base + 0x0E, 0x0803);
        
        /* Read current media options */
        media_ctrl = inw(nic->io_base + 0x08);
        
        /* Clear media bits */
        media_ctrl &= ~0x00FF;
        
        /* Set new media mode */
        switch (mode) {
            case 0:  /* Auto */
                media_ctrl |= 0x0080;  /* Enable auto-select */
                break;
            case 1:  /* 10baseT */
                media_ctrl |= 0x0020;  /* TP/UTP */
                break;
            case 2:  /* 10base2 */
                media_ctrl |= 0x0010;  /* BNC/Coax */
                break;
            case 3:  /* 100baseTX */
                media_ctrl |= 0x0040;  /* 100baseTX */
                break;
        }
        
        /* Write new media options */
        outw(nic->io_base + 0x08, media_ctrl);
        
        /* Reset transceiver */
        outw(nic->io_base + 0x0E, 0x2800);  /* Reset TX */
        outw(nic->io_base + 0x0E, 0x3000);  /* Reset RX */
        
        /* Enable TX/RX */
        outw(nic->io_base + 0x0E, 0x4800);  /* Enable TX */
        outw(nic->io_base + 0x0E, 0x5000);  /* Enable RX */
        
        return 0;
    }
    
    /* For 3C509B, limited media control */
    if (nic->type == NIC_TYPE_3C509B) {
        /* Select window 4 */
        outw(nic->io_base + 0x0E, 0x0804);
        
        /* Read media register */
        media_ctrl = inw(nic->io_base + 0x0A);
        
        /* Only supports 10baseT and 10base2 */
        if (mode == 3) {
            return -1;  /* No 100baseTX on 3C509B */
        }
        
        /* Set media type */
        if (mode == 1 || mode == 0) {
            media_ctrl |= 0x8000;   /* Enable link beat */
            media_ctrl &= ~0x4000;  /* Select TP */
        } else if (mode == 2) {
            media_ctrl &= ~0x8000;  /* Disable link beat */
            media_ctrl |= 0x4000;   /* Select BNC */
        }
        
        /* Write media register */
        outw(nic->io_base + 0x0A, media_ctrl);
        
        return 0;
    }
    
    return -1;  /* Unsupported NIC type */
}

/**
 * @brief Get current copy-break threshold
 * @return Threshold in bytes
 */
uint16_t get_copy_break_threshold(void) {
    return g_copy_break_threshold;
}

/**
 * @brief Get interrupt mitigation parameters
 * @param batch Pointer to store batch size
 * @param timeout Pointer to store timeout value
 */
void get_mitigation_params(uint8_t *batch, uint8_t *timeout) {
    if (batch) {
        *batch = g_mitigation_batch;
    }
    if (timeout) {
        *timeout = g_mitigation_timeout;
    }
}