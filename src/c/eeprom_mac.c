/**
 * @file eeprom_mac.c
 * @brief EEPROM MAC address reading and validation
 * 
 * Reads MAC address from 3Com NIC EEPROM with checksum validation,
 * sanity checks, and fallback to locally administered address.
 * 
 * EEPROM programming is DISABLED by default for safety. Enable only
 * with ALLOW_EEPROM_WRITE build flag and runtime permission.
 */

/* EEPROM programming safety - only enable if explicitly requested */
#ifndef ALLOW_EEPROM_WRITE
#define ALLOW_EEPROM_WRITE 0
#endif

#include <dos.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "eeprom_mac.h"
#include "hardware.h"
#include "logging.h"
#include "common.h"

/* 3Com EEPROM commands and registers */
#define EEPROM_CMD              0x0A    /* EEPROM command register */
#define EEPROM_DATA             0x0C    /* EEPROM data register */

/* EEPROM commands */
#define EEPROM_READ_CMD         0x80    /* Read command (7-bit address follows) */
#define EEPROM_BUSY             0x8000  /* EEPROM busy bit */
#define EEPROM_CMD_READ         0x0200  /* Alternative read command */

/* EEPROM layout for 3Com NICs */
#define EEPROM_NODE_ADDR_0      0x00    /* MAC bytes 0-1 */
#define EEPROM_NODE_ADDR_1      0x01    /* MAC bytes 2-3 */
#define EEPROM_NODE_ADDR_2      0x02    /* MAC bytes 4-5 */
#define EEPROM_CAPABILITIES     0x03    /* Capabilities word */
#define EEPROM_CHECKSUM_OFFSET  0x0F    /* Checksum location (varies by model) */
#define EEPROM_SIZE             0x40    /* Typical EEPROM size (64 words) */

/* MAC address validation */
#define MAC_MULTICAST_BIT       0x01    /* Bit 0 of first byte */
#define MAC_LOCAL_ADMIN_BIT     0x02    /* Bit 1 of first byte */

/* Timing parameters */
#define EEPROM_DELAY_US         162     /* Delay for EEPROM timing */
#define EEPROM_TIMEOUT          1000    /* Timeout in iterations */

/**
 * @brief Wait for EEPROM to be ready
 * 
 * @param iobase NIC I/O base address
 * @return true if ready, false on timeout
 */
static bool wait_eeprom_ready(uint16_t iobase) {
    int timeout = EEPROM_TIMEOUT;
    uint16_t status;
    
    while (timeout-- > 0) {
        status = inw(iobase + EEPROM_CMD);
        if (!(status & EEPROM_BUSY)) {
            return true;
        }
        delay_us(EEPROM_DELAY_US);
    }
    
    LOG_ERROR("EEPROM timeout waiting for ready");
    return false;
}

/**
 * @brief Read word from EEPROM
 * 
 * @param iobase NIC I/O base address
 * @param offset EEPROM word offset
 * @param value Output: word value
 * @return true on success, false on failure
 */
static bool read_eeprom_word(uint16_t iobase, uint8_t offset, uint16_t *value) {
    uint16_t cmd;
    
    if (!value || offset >= EEPROM_SIZE) {
        return false;
    }
    
    /* Wait for EEPROM to be ready */
    if (!wait_eeprom_ready(iobase)) {
        return false;
    }
    
    /* Issue read command with address */
    cmd = EEPROM_CMD_READ | (offset & 0x3F);
    outw(iobase + EEPROM_CMD, cmd);
    
    /* Wait for read to complete */
    if (!wait_eeprom_ready(iobase)) {
        return false;
    }
    
    /* Read data */
    *value = inw(iobase + EEPROM_DATA);
    
    return true;
}

/**
 * @brief Calculate EEPROM checksum
 * 
 * @param iobase NIC I/O base address
 * @param words Number of words to checksum
 * @return Calculated checksum
 */
static uint16_t calculate_eeprom_checksum(uint16_t iobase, int words) {
    uint16_t sum = 0;
    uint16_t value;
    int i;
    
    for (i = 0; i < words; i++) {
        if (!read_eeprom_word(iobase, i, &value)) {
            LOG_WARNING("Failed to read EEPROM word %d for checksum", i);
            return 0xFFFF;
        }
        sum ^= value;  /* 3Com uses XOR checksum */
    }
    
    return sum;
}

/**
 * @brief Validate MAC address
 * 
 * @param mac MAC address to validate
 * @return true if valid, false if invalid
 */
static bool validate_mac_address(const uint8_t *mac) {
    int i;
    uint8_t sum = 0;
    
    if (!mac) {
        return false;
    }
    
    /* Check for all zeros */
    for (i = 0; i < 6; i++) {
        sum |= mac[i];
    }
    if (sum == 0x00) {
        LOG_ERROR("MAC address is all zeros");
        return false;
    }
    
    /* Check for all ones (broadcast) */
    sum = 0xFF;
    for (i = 0; i < 6; i++) {
        sum &= mac[i];
    }
    if (sum == 0xFF) {
        LOG_ERROR("MAC address is broadcast (all FF)");
        return false;
    }
    
    /* Check multicast bit */
    if (mac[0] & MAC_MULTICAST_BIT) {
        LOG_ERROR("MAC address has multicast bit set");
        return false;
    }
    
    /* Check for common invalid patterns */
    if (mac[0] == 0xFF && mac[1] == 0xFF && mac[2] == 0xFF) {
        LOG_ERROR("MAC address OUI is invalid (FF:FF:FF)");
        return false;
    }
    
    /* Check 3Com OUI if not locally administered (advisory only) */
    if (!(mac[0] & MAC_LOCAL_ADMIN_BIT)) {
        /* Common 3Com OUIs: 00:20:AF, 00:50:04, 00:60:08, 00:A0:24, 00:01:02 */
        bool known_3com_oui = ((mac[0] == 0x00 && mac[1] == 0x20 && mac[2] == 0xAF) ||
                               (mac[0] == 0x00 && mac[1] == 0x50 && mac[2] == 0x04) ||
                               (mac[0] == 0x00 && mac[1] == 0x60 && mac[2] == 0x08) ||
                               (mac[0] == 0x00 && mac[1] == 0xA0 && mac[2] == 0x24) ||
                               (mac[0] == 0x00 && mac[1] == 0x01 && mac[2] == 0x02));
        
        if (known_3com_oui) {
            LOG_DEBUG("Recognized 3Com OUI %02X:%02X:%02X", mac[0], mac[1], mac[2]);
        } else {
            LOG_INFO("MAC OUI %02X:%02X:%02X not recognized as 3Com (may be OEM)",
                    mac[0], mac[1], mac[2]);
            /* This is informational only - many OEM boards use different OUIs */
        }
    } else {
        LOG_DEBUG("Locally administered MAC address");
    }
    
    return true;
}

/**
 * @brief Generate locally administered MAC address
 * 
 * Creates a locally administered address based on system parameters.
 * 
 * @param mac Output: Generated MAC address
 */
static void generate_local_mac(uint8_t *mac) {
    uint16_t timer_low, timer_high;
    
    /* Start with locally administered OUI */
    mac[0] = 0x02;  /* Locally administered, unicast */
    mac[1] = 0x3C;  /* '3C' for 3Com */
    mac[2] = 0x0M;  /* 'M' for generated MAC */
    
    /* Use system timer for uniqueness */
    _disable();
    timer_low = inw(0x40);   /* Timer channel 0 */
    timer_high = inw(0x40);  /* Timer channel 0 */
    _enable();
    
    /* Use timer and system ticks for last 3 bytes */
    mac[3] = (uint8_t)(timer_high & 0xFF);
    mac[4] = (uint8_t)(timer_low >> 8);
    mac[5] = (uint8_t)(timer_low & 0xFF);
    
    LOG_WARNING("Generated locally administered MAC: %02X:%02X:%02X:%02X:%02X:%02X",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/**
 * @brief Read and validate MAC address from EEPROM
 * 
 * Comprehensive MAC address reading with validation and fallback.
 * 
 * @param iobase NIC I/O base address
 * @param mac Output: MAC address (6 bytes)
 * @param allow_override Allow MAC override from config
 * @return MAC address status
 */
mac_status_t read_eeprom_mac_address(uint16_t iobase, uint8_t *mac, bool allow_override) {
    uint16_t word0, word1, word2;
    uint16_t checksum_calc, checksum_stored;
    uint8_t temp_mac[6];
    bool checksum_valid = false;
    
    LOG_INFO("Reading MAC address from EEPROM at I/O 0x%04X", iobase);
    
    if (!mac) {
        return MAC_INVALID;
    }
    
    /* Try to read MAC address words */
    if (!read_eeprom_word(iobase, EEPROM_NODE_ADDR_0, &word0) ||
        !read_eeprom_word(iobase, EEPROM_NODE_ADDR_1, &word1) ||
        !read_eeprom_word(iobase, EEPROM_NODE_ADDR_2, &word2)) {
        LOG_ERROR("Failed to read MAC address from EEPROM");
        return MAC_READ_ERROR;
    }
    
    /* Convert words to bytes (3Com uses little-endian in EEPROM) */
    temp_mac[0] = (uint8_t)(word0 & 0xFF);
    temp_mac[1] = (uint8_t)(word0 >> 8);
    temp_mac[2] = (uint8_t)(word1 & 0xFF);
    temp_mac[3] = (uint8_t)(word1 >> 8);
    temp_mac[4] = (uint8_t)(word2 & 0xFF);
    temp_mac[5] = (uint8_t)(word2 >> 8);
    
    LOG_INFO("EEPROM MAC: %02X:%02X:%02X:%02X:%02X:%02X",
            temp_mac[0], temp_mac[1], temp_mac[2],
            temp_mac[3], temp_mac[4], temp_mac[5]);
    
    /* Verify EEPROM checksum (not all models have it) */
    checksum_calc = calculate_eeprom_checksum(iobase, EEPROM_CHECKSUM_OFFSET);
    if (read_eeprom_word(iobase, EEPROM_CHECKSUM_OFFSET, &checksum_stored)) {
        if (checksum_calc == checksum_stored) {
            LOG_INFO("EEPROM checksum valid (0x%04X)", checksum_stored);
            checksum_valid = true;
        } else {
            LOG_WARNING("EEPROM checksum mismatch (calc=0x%04X, stored=0x%04X)",
                       checksum_calc, checksum_stored);
        }
    }
    
    /* Validate MAC address */
    if (!validate_mac_address(temp_mac)) {
        LOG_ERROR("MAC address validation failed");
        
        /* Generate fallback */
        generate_local_mac(mac);
        return MAC_GENERATED;
    }
    
    /* Check if locally administered */
    if (temp_mac[0] & MAC_LOCAL_ADMIN_BIT) {
        LOG_WARNING("MAC address is locally administered");
    }
    
    /* Copy validated MAC */
    memcpy(mac, temp_mac, 6);
    
    /* Check for override if allowed */
    if (allow_override) {
        /* This would check config for MAC override */
        /* Implementation depends on config system */
    }
    
    if (!checksum_valid) {
        return MAC_CHECKSUM_BAD;
    }
    
    return MAC_VALID;
}

/**
 * @brief Program MAC address into NIC registers (runtime only)
 * 
 * Programs MAC address into NIC station address registers.
 * This only affects runtime operation, not permanent EEPROM.
 * 
 * @param iobase NIC I/O base address
 * @param mac MAC address to program
 * @return true on success, false on failure
 */
bool program_mac_address(uint16_t iobase, const uint8_t *mac) {
    int i;
    
    if (!mac) {
        return false;
    }
    
    LOG_INFO("Programming MAC address to NIC registers: %02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    /* Select register window 2 (station address) */
    outw(iobase + 0x0E, 0x0800 | 2);  /* SelectWindow(2) */
    
    /* Program MAC address registers */
    for (i = 0; i < 6; i++) {
        outb(iobase + i, mac[i]);
    }
    
    /* Return to window 1 */
    outw(iobase + 0x0E, 0x0800 | 1);  /* SelectWindow(1) */
    
    return true;
}

#if ALLOW_EEPROM_WRITE
/**
 * @brief Write MAC address to EEPROM (DANGEROUS - USE WITH EXTREME CAUTION)
 * 
 * Permanently programs MAC address into EEPROM. This is potentially
 * destructive and should only be used for repair/recovery operations.
 * 
 * @param iobase NIC I/O base address
 * @param mac MAC address to write to EEPROM
 * @param allow_write Runtime permission flag
 * @return true on success, false on failure
 */
bool write_mac_to_eeprom(uint16_t iobase, const uint8_t *mac, bool allow_write) {
    /* This function is intentionally disabled in normal builds */
    if (!allow_write) {
        LOG_ERROR("EEPROM write not permitted - use explicit allow_write=true");
        return false;
    }
    
    LOG_ERROR("EEPROM programming requested but not implemented for safety");
    LOG_ERROR("This would permanently alter hardware - requires manual implementation");
    LOG_ERROR("and thorough testing on expendable hardware first");
    
    /* Implementation would go here, but is deliberately not provided
     * to prevent accidental permanent damage to hardware */
    
    return false;
}
#endif /* ALLOW_EEPROM_WRITE */

/**
 * @brief Get MAC status string
 * 
 * @param status MAC status code
 * @return Human-readable status string
 */
const char* mac_status_string(mac_status_t status) {
    switch (status) {
        case MAC_VALID:         return "Valid";
        case MAC_INVALID:       return "Invalid";
        case MAC_CHECKSUM_BAD:  return "Checksum bad";
        case MAC_GENERATED:     return "Generated";
        case MAC_OVERRIDE:      return "Override";
        case MAC_READ_ERROR:    return "Read error";
        default:                return "Unknown";
    }
}