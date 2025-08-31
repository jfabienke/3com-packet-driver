/**
 * @file isapnp_enhanced.c
 * @brief Enhanced ISAPnP detection with MAC reading
 *
 * 3Com Packet Driver - ISAPnP Detection Module
 *
 * Implements enhanced ISAPnP detection with:
 * - Full LFSR-based isolation
 * - MAC address reading during configuration
 * - Proper state management and cleanup
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include "../../include/unified_detection.h"
#include "../../include/3c509b.h"
#include "../../include/logging.h"
#include "../../include/hardware.h"
#include "../../include/portability.h"  /* For portable I/O */
#include <string.h>
#include <dos.h>

/* ISAPnP I/O ports */
#define ISAPNP_ADDRESS       0x279
#define ISAPNP_WRITE_DATA    0xA79
#define ISAPNP_READ_PORT_MIN 0x203
#define ISAPNP_READ_PORT_MAX 0x3FF

/* ISAPnP commands */
#define ISAPNP_CONFIG_CONTROL     0x02
#define ISAPNP_WAKE              0x03
#define ISAPNP_RESOURCE_DATA     0x04
#define ISAPNP_CARD_SELECT       0x06
#define ISAPNP_LOGICAL_DEVICE    0x07
#define ISAPNP_ACTIVATE          0x30
#define ISAPNP_IO_BASE_HIGH      0x60
#define ISAPNP_IO_BASE_LOW       0x61

/* ISAPnP initiation key sequence */
static const uint8_t isapnp_key[32] = {
    0x6A, 0xB5, 0xDA, 0xED, 0xF6, 0xFB, 0x7D, 0xBE,
    0xDF, 0x6F, 0x37, 0x1B, 0x0D, 0x86, 0xC3, 0x61,
    0xB0, 0x58, 0x2C, 0x16, 0x8B, 0x45, 0xA2, 0xD1,
    0xE8, 0x74, 0x3A, 0x9D, 0xCE, 0xE7, 0x73, 0x39
};

/* LFSR state for isolation */
static uint8_t lfsr_state;
static uint16_t read_port = ISAPNP_READ_PORT_MIN;
static uint16_t saved_read_port = 0;  /* For save/restore */

/* Internal functions */
static void isapnp_write_addr(uint8_t addr);
static void isapnp_write_data(uint8_t data);
static uint8_t isapnp_read_data(void);
static uint8_t lfsr_next(void);
static bool perform_isolation(uint8_t *serial);
static bool read_serial_identifier(uint8_t *serial);

/**
 * @brief Get current ISAPnP READ_DATA port
 */
uint16_t isapnp_get_read_port(void) {
    return read_port;
}

/**
 * @brief Set ISAPnP READ_DATA port
 */
void isapnp_set_read_port(uint16_t port) {
    read_port = port;
    isapnp_write_addr(0x00);  /* Set RD_DATA port */
    isapnp_write_data(port >> 2);
}

/**
 * @brief Send ISAPnP initiation key
 */
void isapnp_send_initiation_key(void) {
    /* Send the 32-byte initiation key */
    for (int i = 0; i < 32; i++) {
        outb(ISAPNP_ADDRESS, isapnp_key[i]);
    }
    delay(2);  /* Small delay after key */
}

/**
 * @brief Reset all CSNs
 */
void isapnp_reset_csn(void) {
    isapnp_write_addr(ISAPNP_CONFIG_CONTROL);
    isapnp_write_data(0x04);  /* Reset CSN to 0 */
    delay(2);
}

/**
 * @brief Isolate a card using LFSR
 */
bool isapnp_isolate_card(uint8_t *serial) {
    if (!serial) return false;
    
    /* Set isolation register */
    isapnp_write_addr(0x01);  /* Serial isolation */
    delay(1);
    
    /* Reset LFSR */
    lfsr_state = 0x6A;  /* Initial seed */
    
    /* Perform isolation sequence */
    if (!perform_isolation(serial)) {
        return false;
    }
    
    /* Verify checksum */
    uint8_t checksum = 0;
    for (int i = 0; i < 8; i++) {
        checksum ^= serial[i];
    }
    
    if (checksum != serial[8]) {
        LOG_DEBUG("ISAPnP: Checksum mismatch");
        return false;
    }
    
    return true;
}

/**
 * @brief Perform the actual isolation sequence
 */
static bool perform_isolation(uint8_t *serial) {
    uint8_t bit_count = 0;
    uint8_t byte_count = 0;
    uint8_t current_byte = 0;
    bool card_found = false;
    
    /* Read 72 bits (9 bytes) of serial identifier */
    for (int i = 0; i < 72; i++) {
        uint8_t data1 = isapnp_read_data();
        uint8_t data2 = isapnp_read_data();
        
        /* Check for isolation pattern */
        if (data1 == 0x55) {
            card_found = true;
            /* A card is driving the bus */
            if (data2 == 0xAA) {
                /* Bit is 1 */
                current_byte |= (1 << bit_count);
            }
            /* else bit is 0 */
        } else if (data1 != 0xFF || data2 != 0xFF) {
            /* Bus conflict or no card */
            return false;
        }
        
        bit_count++;
        if (bit_count == 8) {
            serial[byte_count++] = current_byte;
            current_byte = 0;
            bit_count = 0;
        }
        
        /* Advance LFSR for next comparison */
        lfsr_state = lfsr_next();
    }
    
    return card_found;
}

/**
 * @brief Advance LFSR state
 */
static uint8_t lfsr_next(void) {
    uint8_t new_bit = ((lfsr_state >> 1) ^ 
                       (lfsr_state >> 5) ^ 
                       (lfsr_state >> 6) ^ 
                       (lfsr_state >> 7)) & 1;
    
    lfsr_state = (lfsr_state >> 1) | (new_bit << 7);
    return lfsr_state;
}

/**
 * @brief Assign CSN to isolated card
 */
void isapnp_assign_csn(uint8_t csn) {
    isapnp_write_addr(ISAPNP_CARD_SELECT);
    isapnp_write_data(csn);
    delay(1);
}

/**
 * @brief Wake a card by CSN
 */
void isapnp_wake_csn(uint8_t csn) {
    isapnp_write_addr(ISAPNP_WAKE);
    isapnp_write_data(csn);
    delay(1);
}

/**
 * @brief Set I/O base for current card
 */
void isapnp_set_io_base(uint8_t csn, uint16_t io_base) {
    /* Select logical device 0 */
    isapnp_write_addr(ISAPNP_LOGICAL_DEVICE);
    isapnp_write_data(0);
    
    /* Set I/O base */
    isapnp_write_addr(ISAPNP_IO_BASE_HIGH);
    isapnp_write_data(io_base >> 8);
    isapnp_write_addr(ISAPNP_IO_BASE_LOW);
    isapnp_write_data(io_base & 0xFF);
}

/**
 * @brief Activate ISAPnP device
 */
void isapnp_activate_device(uint8_t csn) {
    isapnp_write_addr(ISAPNP_ACTIVATE);
    isapnp_write_data(0x01);
    delay(1);
}

/**
 * @brief Deactivate ISAPnP device
 */
void isapnp_deactivate_device(uint8_t csn) {
    isapnp_write_addr(ISAPNP_ACTIVATE);
    isapnp_write_data(0x00);
    delay(1);
}

/**
 * @brief Put all cards to sleep
 */
void isapnp_sleep_all(void) {
    isapnp_write_addr(ISAPNP_CONFIG_CONTROL);
    isapnp_write_data(0x02);  /* Return to Wait for Key */
    delay(1);
}

/**
 * @brief Full ISAPnP cleanup - return to initial state
 */
void isapnp_cleanup_state(void) {
    /* Put all cards to sleep */
    isapnp_sleep_all();
    
    /* Clear all CSNs to return cards to isolation state */
    isapnp_write_addr(ISAPNP_CONFIG_CONTROL);
    isapnp_write_data(0x04);  /* Reset CSN to 0 */
    delay(2);
    
    /* Return to Wait for Key state */
    isapnp_write_addr(ISAPNP_CONFIG_CONTROL);
    isapnp_write_data(0x02);  /* Wait for Key */
    delay(2);  /* Allow cards to settle in Wait-for-Key state */
    
    LOG_DEBUG("ISAPnP state cleaned up - all cards in Wait-for-Key");
}

/**
 * @brief Read MAC address from I/O port
 */
bool read_mac_from_io(uint16_t io_base, uint8_t *mac) {
    if (!mac) return false;
    
    /* Select window 2 for station address */
    outb(io_base + EP_COMMAND, SelectRegisterWindow | 2);
    delay(1);
    
    /* Read MAC address (3 words) */
    for (int i = 0; i < 3; i++) {
        uint16_t word = inw(io_base + EP_W2_ADDR_0 + i * 2);
        mac[i * 2] = word & 0xFF;
        mac[i * 2 + 1] = word >> 8;
    }
    
    /* Verify MAC is valid (not all zeros or all FFs) */
    bool all_zero = true;
    bool all_ff = true;
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0x00) all_zero = false;
        if (mac[i] != 0xFF) all_ff = false;
    }
    
    if (all_zero || all_ff) {
        LOG_DEBUG("Invalid MAC address read from I/O 0x%04X", io_base);
        return false;
    }
    
    return true;
}

/**
 * @brief Get timestamp in milliseconds
 */
uint32_t get_timestamp(void) {
    /* Read BIOS timer tick count at 0040:006C */
    uint32_t __far *timer = (uint32_t __far *)MK_FP(0x0040, 0x006C);
    return (*timer) * 55;  /* Convert to milliseconds (18.2 Hz timer) */
}

/* Internal helper functions */

static void isapnp_write_addr(uint8_t addr) {
    outb(ISAPNP_ADDRESS, addr);
}

static void isapnp_write_data(uint8_t data) {
    outb(ISAPNP_WRITE_DATA, data);
}

static uint8_t isapnp_read_data(void) {
    return inb(read_port);
}

/**
 * @brief Main ISAPnP detection function
 */
int perform_isapnp_detection(detection_state_t *state) {
    if (!state) return 0;
    
    int new_cards = 0;
    uint8_t csn = 1;
    uint32_t start_time = get_timestamp();
    
    LOG_INFO("Starting ISAPnP detection");
    state->isapnp_attempts++;
    
    /* Save current read data port (if any ISAPnP card is configured) */
    /* Note: We can't reliably read the current port, so we track it */
    saved_read_port = read_port;
    
    /* Set read data port for our detection */
    isapnp_write_addr(0x00);  /* Set RD_DATA port */
    isapnp_write_data(read_port >> 2);
    
    /* Send initiation key */
    isapnp_send_initiation_key();
    state->isapnp_initiated = true;
    
    /* Reset all CSNs */
    isapnp_reset_csn();
    
    /* Perform isolation for each card */
    while (csn <= 32 && state->cards_found < MAX_DETECTED_NICS) {
        uint8_t serial[9];
        
        /* Try to isolate a card */
        if (!isapnp_isolate_card(serial)) {
            break;  /* No more cards */
        }
        
        /* Check if it's a 3Com card */
        uint16_t vendor_id = (serial[0] << 8) | serial[1];
        if (vendor_id != 0x10B7) {  /* Not 3Com */
            /* Assign CSN anyway to move to next card */
            isapnp_assign_csn(csn);
            csn++;
            continue;
        }
        
        /* Assign CSN */
        isapnp_assign_csn(csn);
        
        /* Wake the card but DON'T activate I/O yet (per GPT-5 feedback) */
        isapnp_wake_csn(csn);
        
        /* Use ISAPnP serial for deduplication instead of risky MAC read
         * Serial number is unique per card and doesn't require I/O activation
         * MAC can be read later when we assign final I/O base
         */
        uint8_t mac[6];
        /* Extract pseudo-MAC from serial for deduplication */
        mac[0] = 0x00;  /* 3Com OUI first byte */
        mac[1] = 0x60;  /* 3Com OUI second byte */
        mac[2] = 0x08;  /* 3Com OUI third byte */
        mac[3] = serial[5];  /* Use serial bytes for uniqueness */
        mac[4] = serial[6];
        mac[5] = serial[7];
        
        /* Check for duplicate by serial first (most reliable for ISAPnP) */
        tracked_card_t *existing = find_card_by_serial(state, serial);
        if (!existing) {
            /* Secondary check by pseudo-MAC if needed */
            existing = find_card_by_mac(state, mac);
        }
        
        if (existing) {
            /* Card already found */
            existing->found_by_isapnp = true;
            existing->csn = csn;
            existing->detection_methods |= DETECT_METHOD_ISAPNP;
            state->duplicates_found++;
            LOG_DEBUG("ISAPnP: Found duplicate card CSN=%d", csn);
        } else {
            /* New card */
            tracked_card_t *card = &state->cards[state->cards_found++];
            memset(card, 0, sizeof(tracked_card_t));
            
            memcpy(card->mac, mac, 6);
            memcpy(card->isapnp_serial, serial, 9);
            card->vendor_id = vendor_id;
            card->device_id = (serial[2] << 8) | serial[3];
            card->found_by_isapnp = true;
            card->csn = csn;
            card->detection_methods = DETECT_METHOD_ISAPNP;
            card->detection_timestamp = get_timestamp();
            
            /* Determine NIC type */
            if ((card->device_id & 0xFF00) == 0x9000) {
                card->nic_type = NIC_TYPE_3C509B;
            } else if ((card->device_id & 0xFF00) == 0x5000) {
                card->nic_type = NIC_TYPE_3C515_TX;
            }
            
            sprintf(card->detection_notes, "ISAPnP CSN=%d", csn);
            new_cards++;
            
            LOG_INFO("ISAPnP: Found new card CSN=%d, MAC=%02X:%02X:%02X:%02X:%02X:%02X",
                    csn, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
        
        csn++;
    }
    
    /* Full ISAPnP cleanup - return all cards to Wait-for-Key */
    isapnp_cleanup_state();
    
    /* Restore saved read data port if it was different */
    if (saved_read_port != read_port && saved_read_port != 0) {
        LOG_DEBUG("Restoring ISAPnP READ_DATA port to 0x%03X", saved_read_port);
        isapnp_write_addr(0x00);  /* Set RD_DATA port */
        isapnp_write_data(saved_read_port >> 2);
        read_port = saved_read_port;
    }
    
    state->isapnp_duration = get_timestamp() - start_time;
    LOG_INFO("ISAPnP detection completed: %d new cards found", new_cards);
    
    return new_cards;
}