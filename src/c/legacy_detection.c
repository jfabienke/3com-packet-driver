/**
 * @file legacy_detection.c
 * @brief Enhanced legacy detection for 3C509B cards
 *
 * 3Com Packet Driver - Legacy Detection Module
 *
 * Implements the 3C509B ID port protocol with:
 * - Multi-card discovery using tagging
 * - Proper contention handling
 * - EEPROM PnP mode detection
 * - Support for cards with PnP disabled
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include "../../include/unified_detection.h"
#include "../../include/3c509b.h"
#include "../../include/logging.h"
#include "../../include/hardware.h"
#include "../../include/eeprom.h"
#include "../../include/portability.h"  /* For portable I/O */
#include <string.h>
#include <dos.h>

/* Timeout constants */
#define CONTENTION_TIMEOUT_MS  1000  /* Max time for contention read */
#define RESET_WAIT_MS          20    /* Time to wait after reset */

/* 3C509B ID port range - per 3Com Technical Reference Manual
 * Hardware monitors 01x0h pattern (0x100-0x1F0 in 0x10 increments)
 * Writing 0x00 to any of these ports latches it as the ID port
 */
#define ID_PORT_MIN          0x100
#define ID_PORT_MAX          0x1F0
#define ID_PORT_STEP         0x10

/* ID sequence timing (microseconds) */
#define ID_SEQUENCE_DELAY    5
#define CONTENTION_ITERATIONS 255  /* Max iterations for contention */

/* Manufacturer ID for 3Com */
#define MFG_ID_3COM          0x6D50

/* Internal functions */
static bool send_id_sequence(uint16_t id_port);
static bool read_contention_data(uint16_t id_port, uint8_t *id_data);
static bool decode_id_data(uint8_t *id_data, uint16_t *mfg_id, uint16_t *prod_id, uint8_t *mac);
static void activate_card_at_port(uint16_t id_port, uint16_t io_base);
static void deactivate_card(uint16_t io_base);

/**
 * @brief Read 3C509B ID sequence with contention handling
 */
bool read_3c509b_id_sequence(uint16_t id_port, uint16_t *mfg_id, 
                             uint16_t *prod_id, uint8_t *mac) {
    uint8_t id_data[16];  /* Buffer for ID data */
    
    if (!mfg_id || !prod_id || !mac) {
        return false;
    }
    
    /* Send ID read command */
    if (!send_id_sequence(id_port)) {
        return false;
    }
    
    /* Read contention data */
    if (!read_contention_data(id_port, id_data)) {
        return false;
    }
    
    /* Decode the ID data */
    if (!decode_id_data(id_data, mfg_id, prod_id, mac)) {
        return false;
    }
    
    return true;
}

/**
 * @brief Send ID sequence to initiate contention
 */
static bool send_id_sequence(uint16_t id_port) {
    /* Send the ID sequence command (0xFF to trigger response) */
    outb(id_port, 0xFF);
    delay(ID_SEQUENCE_DELAY);
    
    /* Additional writes to ensure proper state */
    outb(id_port, 0x00);
    delay(ID_SEQUENCE_DELAY);
    
    return true;
}

/**
 * @brief Read contention data from ID port
 */
static bool read_contention_data(uint16_t id_port, uint8_t *id_data) {
    bool card_found = false;
    int bit_count = 0;
    int byte_count = 0;
    uint8_t current_byte = 0;
    uint32_t start_time = get_timestamp();
    
    /* The 3C509B sends its ID using contention resolution */
    for (int i = 0; i < CONTENTION_ITERATIONS; i++) {
        /* Check for timeout */
        if ((get_timestamp() - start_time) > CONTENTION_TIMEOUT_MS) {
            LOG_DEBUG("Contention read timeout at port 0x%03X", id_port);
            return false;
        }
        uint8_t byte1 = inb(id_port);
        delay(1);
        uint8_t byte2 = inb(id_port);
        delay(1);
        
        /* Check for contention pattern */
        if (byte1 == 0x55) {
            /* Contention detected - card is responding */
            card_found = true;
            
            if (byte2 == 0xAA) {
                /* Bit is 1 */
                current_byte |= (1 << (7 - bit_count));
            } else if (byte2 == 0x55) {
                /* Bit is 0 */
                /* Nothing to do, bit already 0 */
            } else {
                /* Invalid response */
                return false;
            }
            
            bit_count++;
            if (bit_count == 8) {
                id_data[byte_count++] = current_byte;
                current_byte = 0;
                bit_count = 0;
                
                /* Check if we have enough data */
                if (byte_count >= 16) {
                    break;
                }
            }
        } else if (byte1 == 0xFF && byte2 == 0xFF) {
            /* No card responding */
            if (card_found && byte_count > 0) {
                /* End of data */
                break;
            }
            /* Continue waiting */
        } else {
            /* Bus conflict or error */
            if (card_found) {
                /* Try to recover */
                continue;
            }
        }
    }
    
    return card_found && byte_count >= 10;  /* Need at least 10 bytes */
}

/**
 * @brief Decode ID data into manufacturer ID, product ID, and MAC
 */
static bool decode_id_data(uint8_t *id_data, uint16_t *mfg_id, 
                           uint16_t *prod_id, uint8_t *mac) {
    /* The ID data format:
     * Bytes 0-1: Manufacturer ID (big-endian)
     * Bytes 2-3: Product ID (big-endian)
     * Bytes 4-9: MAC address
     */
    
    *mfg_id = (id_data[0] << 8) | id_data[1];
    *prod_id = (id_data[2] << 8) | id_data[3];
    
    /* Copy MAC address */
    for (int i = 0; i < 6; i++) {
        mac[i] = id_data[4 + i];
    }
    
    /* Validate the data */
    if (*mfg_id == 0x0000 || *mfg_id == 0xFFFF) {
        return false;
    }
    
    /* Check for valid MAC (not all zeros or all FFs) */
    bool all_zero = true;
    bool all_ff = true;
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0x00) all_zero = false;
        if (mac[i] != 0xFF) all_ff = false;
    }
    
    if (all_zero || all_ff) {
        return false;
    }
    
    return true;
}

/**
 * @brief Main legacy detection function with multi-card support
 */
int perform_legacy_detection(detection_state_t *state) {
    if (!state) return 0;
    
    int new_cards = 0;
    uint32_t start_time = get_timestamp();
    uint8_t tag = 1;
    
    LOG_INFO("Starting mandatory legacy detection");
    state->legacy_attempts++;
    
    /* Safe ID ports ordered by likelihood to be free
     * Per 3Com spec: hardware only monitors 01x0h pattern
     * Skip 0x170 (Secondary IDE) and 0x1F0 (Primary IDE)
     */
    static const uint16_t safe_id_ports[] = {
        0x110,  /* Most common, usually free */
        0x120,  /* Usually free */
        0x130,  /* Usually free */
        0x180,  /* Usually free */
        0x190,  /* Usually free */
        0x1A0,  /* Usually free */
        0x1B0,  /* Usually free */
        0x1C0,  /* Usually free */
        0x1D0,  /* Usually free */
        0x1E0,  /* Usually free */
        0x160,  /* Usually free */
        0x100,  /* May conflict with PS/2 POS - try last */
        0x140,  /* May conflict with SCSI - try last */
        0x150   /* May conflict with SCSI - try last */
        /* NEVER use 0x170 (IDE) or 0x1F0 (IDE) */
    };
    int num_ports = sizeof(safe_id_ports) / sizeof(safe_id_ports[0]);
    
    /* Send global reset to put all 3C509B cards in ID state */
    LOG_DEBUG("Sending global reset to safe ID ports");
    for (int i = 0; i < num_ports; i++) {
        outb(safe_id_ports[i], ID_PORT_GLOBAL_RESET);
    }
    delay(RESET_WAIT_MS);  /* Wait for cards to reset */
    state->legacy_id_state_active = true;
    
    /* Discovery loop - find all cards */
    LOG_DEBUG("Starting card discovery loop");
    while (tag <= 8 && state->cards_found < MAX_DETECTED_NICS) {
        bool card_found = false;
        
        for (int port_idx = 0; port_idx < num_ports && !card_found; port_idx++) {
            uint16_t id_port = safe_id_ports[port_idx];
            
            /* Select this port as the ID port (per GPT-5 suggestion) */
            outb(id_port, 0x00);  /* Writing 0x00 latches this as ID port */
            delay(1);
            
            /* Try to read card ID */
            uint16_t manufacturer_id = 0;
            uint16_t product_id = 0;
            uint8_t mac[6];
            
            /* Send ID sequence */
            if (!read_3c509b_id_sequence(id_port, &manufacturer_id, &product_id, mac)) {
                continue;
            }
            
            /* Check if it's a 3Com card */
            if (manufacturer_id != MFG_ID_3COM) {
                LOG_DEBUG("Non-3Com card found at port 0x%03X (MFG ID: 0x%04X)", 
                         id_port, manufacturer_id);
                continue;
            }
            
            /* Check for duplicate */
            tracked_card_t *existing = find_card_by_mac(state, mac);
            
            if (existing) {
                /* Card already found */
                existing->found_by_legacy = true;
                existing->id_port = id_port;
                existing->detection_methods |= DETECT_METHOD_LEGACY;
                state->duplicates_found++;
                LOG_DEBUG("Legacy: Found duplicate card at ID port 0x%03X", id_port);
                
                /* Tag this card so it won't respond again */
                outb(id_port, ID_PORT_SELECT_TAG | tag);
            } else {
                /* New card */
                tracked_card_t *card = &state->cards[state->cards_found++];
                memset(card, 0, sizeof(tracked_card_t));
                
                memcpy(card->mac, mac, 6);
                card->vendor_id = 0x10B7;  /* 3Com */
                card->device_id = product_id;
                card->found_by_legacy = true;
                card->id_port = id_port;
                card->detection_methods = DETECT_METHOD_LEGACY;
                card->detection_timestamp = get_timestamp();
                card->nic_type = NIC_TYPE_3C509B;  /* Only 3C509B uses legacy */
                
                /* Tag this card to prevent further responses */
                outb(id_port, ID_PORT_SELECT_TAG | tag);
                delay(1);
                
                /* Activate card temporarily to read EEPROM */
                uint16_t temp_io = 0x300 + (tag - 1) * 0x20;
                
                /* Ensure I/O base is aligned properly */
                if (temp_io > 0x3E0) {
                    temp_io = 0x3E0;  /* Maximum allowed */
                }
                
                /* Activate with I/O base */
                outb(id_port, ID_PORT_ACTIVATE_AND_SET_IO | (temp_io >> 4));
                delay(2);
                
                /* Save I/O base for later use */
                card->io_base = temp_io;
                
                /* Read PnP mode from EEPROM */
                card->pnp_mode = read_card_pnp_mode(temp_io);
                if (card->pnp_mode == PNP_MODE_LEGACY_ONLY) {
                    state->pnp_disabled_cards++;
                    sprintf(card->detection_notes, "Legacy (PnP disabled in EEPROM)");
                } else {
                    sprintf(card->detection_notes, "Legacy ID port 0x%03X", id_port);
                }
                
                /* Read additional EEPROM data */
                card->eeprom_checksum = nic_read_eeprom_3c509b(temp_io, 
                                                                EEPROM_CHECKSUM_OFFSET);
                card->verified = true;
                
                /* Deactivate card for now */
                deactivate_card(temp_io);
                
                new_cards++;
                card_found = true;
                
                LOG_INFO("Legacy: Found new card at ID port 0x%03X, "
                        "MAC=%02X:%02X:%02X:%02X:%02X:%02X, PnP mode=%s",
                        id_port, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                        get_pnp_mode_string(card->pnp_mode));
            }
            
            tag++;
        }
        
        if (!card_found) {
            LOG_DEBUG("No more cards found in this iteration");
            break;  /* No more cards */
        }
    }
    
    /* Cancel ID state for all untagged cards */
    LOG_DEBUG("Canceling ID state for untagged cards");
    for (int i = 0; i < num_ports; i++) {
        outb(safe_id_ports[i], ID_PORT_CANCEL_ID_STATE);
    }
    state->legacy_id_state_active = false;
    
    state->legacy_duration = get_timestamp() - start_time;
    LOG_INFO("Legacy detection completed: %d new cards found (%d with PnP disabled)",
            new_cards, state->pnp_disabled_cards);
    
    return new_cards;
}

/**
 * @brief Activate card at specified port with I/O base
 */
static void activate_card_at_port(uint16_t id_port, uint16_t io_base) {
    /* Send activate command with I/O base */
    outb(id_port, ID_PORT_ACTIVATE_AND_SET_IO | (io_base >> 4));
    delay(2);
}

/**
 * @brief Deactivate card
 */
static void deactivate_card(uint16_t io_base) {
    /* Select window 0 */
    outb(io_base + EP_COMMAND, SelectRegisterWindow | 0);
    delay(1);
    
    /* Write to configuration control to deactivate */
    outb(io_base + EP_W0_CONFIG_CTRL, 0x00);
    delay(1);
}