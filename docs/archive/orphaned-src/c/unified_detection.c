/**
 * @file unified_detection.c
 * @brief Unified NIC detection implementation
 *
 * 3Com Packet Driver - Comprehensive Detection System
 *
 * Implements the three-stage detection strategy:
 * 1. PnP BIOS check (informational only)
 * 2. ISAPnP detection (opportunistic)
 * 3. Legacy detection (MANDATORY)
 * 
 * Critical: Legacy detection must ALWAYS run because cards can have
 * PnP disabled in EEPROM, making them invisible to ISAPnP.
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include "../../include/unified_detection.h"
#include "../../include/chipset_detect.h"
#include "../../include/logging.h"
#include "../../include/hardware.h"
#include "../../include/3c509b.h"
#include "../../include/eeprom.h"
#include "../../include/pnp.h"
#include "../../include/portability.h"  /* For portable interrupt handling */
#include <string.h>
#include <stdio.h>
#include <dos.h>

/* Global detection lock */
static volatile bool detection_lock = false;

/* Forward declarations for internal functions */
static void apply_detection_config(detection_state_t *state, const detection_config_t *config);
static int detection_with_retry(detection_state_t *state, int max_retries);
static void calculate_detection_times(detection_state_t *state);
static void analyze_detection_results(detection_state_t *state);

/**
 * @brief Main unified NIC detection entry point
 * 
 * This is the primary function that coordinates all detection methods.
 * It ALWAYS runs legacy detection regardless of ISAPnP results.
 */
int unified_nic_detection(nic_detect_info_t *info_list, int max_nics,
                          const detection_config_t *config) {
    detection_state_t state = {0};
    int result;
    
    if (!info_list || max_nics <= 0) {
        log_error("Invalid parameters for NIC detection");
        return DETECT_ERR_INVALID_PARAM;
    }
    
    /* Acquire detection lock */
    if (!acquire_detection_lock()) {
        log_error("Detection already in progress");
        return DETECT_ERR_IN_PROGRESS;
    }
    
    /* Initialize state */
    reset_detection_state(&state);
    state.detection_in_progress = true;
    state.detection_start_time = hardware_get_timestamp();
    
    /* Apply configuration if provided */
    if (config) {
        apply_detection_config(&state, config);
    }
    
    log_info("=== Starting Unified NIC Detection ===");
    log_info("Detection strategy: PnP BIOS → ISAPnP → Legacy (mandatory)");
    
    /* Perform detection with retry logic */
    result = detection_with_retry(&state, 
                                  config ? config->force_legacy ? 1 : MAX_DETECTION_RETRIES 
                                         : MAX_DETECTION_RETRIES);
    
    if (result < 0) {
        log_error("Detection failed after all retries");
        release_detection_lock();
        return result;
    }
    
    /* Calculate timing statistics */
    calculate_detection_times(&state);
    
    /* Analyze results */
    analyze_detection_results(&state);
    
    /* Convert to output format */
    int converted = convert_state_to_nic_info(&state, info_list, max_nics);
    
    /* Print diagnostic report */
    if (!config || config->verbose_logging) {
        print_detection_report(&state);
        log_detection_statistics(&state);
    }
    
    /* Release lock */
    state.detection_in_progress = false;
    release_detection_lock();
    
    log_info("=== Detection Complete: %d cards found ===", state.cards_found);
    
    return converted;
}

/**
 * @brief Quick detection using ISAPnP only
 * 
 * Warning: This will miss cards with PnP disabled in EEPROM!
 * Use only when you know all cards have PnP enabled.
 */
int quick_nic_detection(nic_detect_info_t *info_list, int max_nics) {
    detection_config_t config = {
        .skip_pnp_bios = true,
        .skip_isapnp = false,
        .force_legacy = false,  /* Skipping legacy - risky! */
        .verbose_logging = false
    };
    
    log_warning("Quick detection mode - may miss cards with PnP disabled!");
    
    return unified_nic_detection(info_list, max_nics, &config);
}

/**
 * @brief Detection with retry logic
 */
static int detection_with_retry(detection_state_t *state, int max_retries) {
    int retry_count = 0;
    
    while (retry_count < max_retries) {
        if (retry_count > 0) {
            log_info("Detection retry %d of %d", retry_count, max_retries - 1);
            reset_detection_state(state);
            delay(100 * retry_count);  /* Progressive delay */
        }
        
        /* Step 1: Check system capabilities (informational) */
        check_system_capabilities(state);
        
        /* Step 2: ISAPnP detection (may find nothing if PnP disabled) */
        uint32_t isapnp_start = hardware_get_timestamp();
        uint16_t saved_flags;
        CRITICAL_SECTION_ENTER(saved_flags);  /* Portable interrupt save/disable */
        int isapnp_found = perform_isapnp_detection(state);
        CRITICAL_SECTION_EXIT(saved_flags);  /* Restore previous interrupt state */
        state->isapnp_duration = hardware_get_timestamp() - isapnp_start;
        
        if (isapnp_found > 0) {
            log_info("ISAPnP found %d cards", isapnp_found);
        } else {
            log_info("ISAPnP found no cards (may have PnP disabled)");
        }
        
        /* Step 3: Legacy detection (MANDATORY - always run!) */
        uint32_t legacy_start = hardware_get_timestamp();
        CRITICAL_SECTION_ENTER(saved_flags);  /* Portable interrupt save/disable */
        int legacy_found = perform_legacy_detection(state);
        CRITICAL_SECTION_EXIT(saved_flags);  /* Restore previous interrupt state */
        state->legacy_duration = hardware_get_timestamp() - legacy_start;
        
        log_info("Legacy detection found %d total cards", state->cards_found);
        
        /* Step 4: Reconcile and verify */
        reconcile_detected_cards(state);
        
        /* Check if we found any cards */
        if (state->cards_found > 0) {
            log_info("Detection successful: %d cards found", state->cards_found);
            return state->cards_found;
        }
        
        /* No cards found, retry if allowed */
        if (retry_count < max_retries - 1) {
            log_warning("No cards detected, retrying...");
            selective_card_reset(state);  /* Only reset what we touched */
        }
        
        retry_count++;
    }
    
    log_warning("No cards detected after %d attempts", max_retries);
    return 0;  /* No cards found but not an error */
}

/**
 * @brief Check system PnP capabilities
 */
bool check_system_capabilities(detection_state_t *state) {
    log_info("=== Step 1: System Capability Check ===");
    
    /* Check for PnP BIOS */
    state->has_pnp_bios = has_pnp_isa_bios();
    
    if (state->has_pnp_bios) {
        state->pnp_bios_nodes = count_pnp_isa_nodes();
        log_info("PnP BIOS detected: %d nodes reported", state->pnp_bios_nodes);
    } else {
        log_info("No PnP BIOS detected (normal for pre-1995 systems)");
    }
    
    /* Check for ISA bridge */
    chipset_additional_info_t chipset_info = scan_additional_pci_devices();
    state->has_isa_bridge = chipset_info.has_isa_bridge;
    
    if (state->has_isa_bridge) {
        log_info("ISA bridge detected: %s", chipset_info.isa_bridge_name);
    }
    
    /* Note: This is informational only - does NOT determine detection strategy */
    log_info("System capability check complete (informational only)");
    
    return state->has_pnp_bios;
}

/**
 * @brief Reconcile cards found by multiple methods
 */
void reconcile_detected_cards(detection_state_t *state) {
    log_info("=== Step 4: Reconciliation & Verification ===");
    
    for (int i = 0; i < state->cards_found; i++) {
        tracked_card_t *card = &state->cards[i];
        
        /* Read EEPROM configuration */
        if (card->io_base) {
            card->verified = verify_card_configuration(card);
            
            if (card->verified) {
                /* Analyze why card was detected this way - symmetric handling */
                if (card->found_by_legacy && !card->found_by_isapnp) {
                    if (card->pnp_mode == PNP_MODE_LEGACY_ONLY) {
                        /* DOS-compatible sprintf with bounds checking */
                        sprintf(card->detection_notes,
                                "PnP disabled in EEPROM (expected)");
                        state->pnp_disabled_cards++;
                    } else if (card->pnp_mode == PNP_MODE_PNP_ONLY) {
                        sprintf(card->detection_notes,
                                "ERROR: PnP-only mode but ISAPnP failed");
                        state->errors_encountered++;
                    } else {
                        sprintf(card->detection_notes,
                                "PnP enabled but didn't respond (check)");
                        state->warnings_generated++;
                    }
                } else if (card->found_by_isapnp && card->found_by_legacy) {
                    sprintf(card->detection_notes,
                            "Found by both methods (normal)");
                    state->duplicates_found++;
                } else if (card->found_by_isapnp && !card->found_by_legacy) {
                    if (card->pnp_mode == PNP_MODE_PNP_ONLY) {
                        sprintf(card->detection_notes,
                                "Legacy disabled in EEPROM (expected)");
                    } else if (card->pnp_mode == PNP_MODE_LEGACY_ONLY) {
                        sprintf(card->detection_notes,
                                "ERROR: Legacy-only but not found");
                        state->errors_encountered++;
                    } else {
                        sprintf(card->detection_notes,
                                "Unexpected: legacy not responding");
                        state->warnings_generated++;
                    }
                }
                
                log_info("Card %d [%02X:%02X:%02X:%02X:%02X:%02X]: %s", 
                        i, card->mac[0], card->mac[1], card->mac[2],
                        card->mac[3], card->mac[4], card->mac[5],
                        card->detection_notes);
                
                /* Check for resource conflicts */
                for (int j = 0; j < i; j++) {
                    if (state->cards[j].io_base == card->io_base) {
                        card->resources_conflict = true;
                        log_warning("I/O conflict at 0x%04X between cards %d and %d",
                                   card->io_base, j, i);
                        state->errors_encountered++;
                    }
                    if (state->cards[j].irq == card->irq) {
                        log_warning("IRQ conflict at IRQ %d between cards %d and %d",
                                   card->irq, j, i);
                    }
                }
            } else {
                log_warning("Could not verify card %d configuration", i);
            }
        }
    }
    
    log_info("Reconciliation complete: %d PnP-disabled cards found",
            state->pnp_disabled_cards);
}

/**
 * @brief Find card by MAC address
 */
tracked_card_t* find_card_by_mac(const detection_state_t *state, const uint8_t *mac) {
    if (!state || !mac) {
        return NULL;
    }
    
    for (int i = 0; i < state->cards_found; i++) {
        if (memcmp(state->cards[i].mac, mac, 6) == 0) {
            return (tracked_card_t*)&state->cards[i];
        }
    }
    
    return NULL;
}

/**
 * @brief Find card by ISAPnP serial
 */
tracked_card_t* find_card_by_serial(const detection_state_t *state, const uint8_t *serial) {
    if (!state || !serial) {
        return NULL;
    }
    
    for (int i = 0; i < state->cards_found; i++) {
        /* Check if card has ISAPnP serial (first byte non-zero) */
        if (state->cards[i].isapnp_serial[0] != 0) {
            if (memcmp(state->cards[i].isapnp_serial, serial, 9) == 0) {
                return (tracked_card_t*)&state->cards[i];
            }
        }
    }
    
    return NULL;
}

/**
 * @brief Check if card is duplicate
 */
bool is_duplicate_card(const detection_state_t *state, const tracked_card_t *card) {
    if (!state || !card) {
        return false;
    }
    
    /* Check by MAC if available */
    if (card->mac[0] || card->mac[1] || card->mac[2]) {
        if (find_card_by_mac(state, card->mac)) {
            return true;
        }
    }
    
    /* Check by ISAPnP serial if available */
    if (card->isapnp_serial[0]) {
        if (find_card_by_serial(state, card->isapnp_serial)) {
            return true;
        }
    }
    
    return false;
}

/**
 * @brief Read PnP mode from card EEPROM
 */
card_pnp_mode_t read_card_pnp_mode(uint16_t io_base) {
    /* Read Internal Configuration Register using defined constants */
    uint16_t config_low = nic_read_eeprom_3c509b(io_base, EEPROM_INTERNAL_CONFIG_LOW);
    uint16_t config_high = nic_read_eeprom_3c509b(io_base, EEPROM_INTERNAL_CONFIG_HIGH);
    
    if (config_low == 0xFFFF || config_high == 0xFFFF) {
        log_warning("Failed to read EEPROM at I/O 0x%04X", io_base);
        return PNP_MODE_BOTH_DEFAULT;  /* Assume default */
    }
    
    uint32_t internal_config = ((uint32_t)config_high << 16) | config_low;
    
    /* Extract ISA Activation Select bits using defined constants */
    card_pnp_mode_t mode = (card_pnp_mode_t)
        ((internal_config >> INTERNAL_CONFIG_ISA_ACTIVATION_SHIFT) & 
         INTERNAL_CONFIG_ISA_ACTIVATION_MASK);
    
    log_debug("Card at 0x%04X: Internal Config=0x%08lX, PnP mode=0x%02X", 
             io_base, internal_config, mode);
    
    return mode;
}

/**
 * @brief Verify card EEPROM configuration
 */
bool verify_card_configuration(tracked_card_t *card) {
    if (!card || !card->io_base) {
        return false;
    }
    
    /* Read PnP mode from EEPROM */
    card->pnp_mode = read_card_pnp_mode(card->io_base);
    
    /* Determine if resources are fixed */
    if (card->pnp_mode == PNP_MODE_LEGACY_ONLY) {
        card->resources_fixed = true;
        log_info("Card at 0x%04X has PnP DISABLED - resources fixed in EEPROM",
                card->io_base);
        log_info("  Run 3C5X9CFG.EXE to enable PnP if needed");
    } else {
        card->resources_fixed = false;
        log_debug("Card at 0x%04X has PnP enabled (mode 0x%02X)",
                 card->io_base, card->pnp_mode);
    }
    
    /* Read and store EEPROM checksum for validation */
    card->eeprom_checksum = nic_read_eeprom_3c509b(card->io_base, 0x1F);
    
    return true;
}

/**
 * @brief Selective card reset based on what was touched
 */
void selective_card_reset(detection_state_t *state) {
    log_debug("Performing selective card reset");
    
    /* Save interrupt state and disable during reset */
    uint16_t saved_flags;
    CRITICAL_SECTION_ENTER(saved_flags);
    
    /* Only reset ISAPnP if we initiated it */
    if (state && state->isapnp_initiated) {
        log_debug("Resetting ISAPnP state");
        outb(ISAPNP_ADDRESS, ISAPNP_CONFIG_CONTROL);
        outb(ISAPNP_WRITE_DATA, 0x02);  /* Return to Wait for Key */
        delay(2);
        state->isapnp_initiated = false;
    }
    
    /* Only reset 3C509B ID state if we activated it */
    if (state && state->legacy_id_state_active) {
        log_debug("Canceling 3C509B ID state");
        outb(_3C509B_ID_PORT, ID_PORT_CANCEL_ID_STATE);  /* Minimal reset */
        delay(1);
        state->legacy_id_state_active = false;
    }
    
    /* Restore previous interrupt state */
    CRITICAL_SECTION_EXIT(saved_flags);
    
    if (state) {
        state->cards_need_reset = false;
    }
    
    log_debug("Selective reset complete");
}

/**
 * @brief Global card reset (only when absolutely necessary)
 */
void global_card_reset(void) {
    log_warning("Performing full global reset - may affect other devices");
    
    /* Create temporary state for tracking */
    detection_state_t temp_state = {0};
    temp_state.isapnp_initiated = true;
    temp_state.legacy_id_state_active = true;
    
    selective_card_reset(&temp_state);
}

/**
 * @brief Reset detection state
 */
void reset_detection_state(detection_state_t *state) {
    if (!state) {
        return;
    }
    
    /* Preserve lock status */
    bool was_locked = state->detection_in_progress;
    
    /* Clear everything except the lock */
    memset(state, 0, sizeof(detection_state_t));
    
    /* Restore lock status */
    state->detection_in_progress = was_locked;
}

/**
 * @brief Convert tracked card to NIC info
 */
void convert_card_to_nic_info(const tracked_card_t *card, nic_detect_info_t *info) {
    if (!card || !info) {
        return;
    }
    
    memset(info, 0, sizeof(nic_detect_info_t));
    
    info->type = card->nic_type;
    info->vendor_id = card->vendor_id;
    info->device_id = card->device_id;
    info->io_base = card->io_base;
    info->irq = card->irq;
    info->capabilities = card->capabilities;
    info->detected = true;
    info->pnp_capable = (card->pnp_mode != PNP_MODE_LEGACY_ONLY);
    
    /* Copy MAC address if available */
    if (card->mac[0] || card->mac[1] || card->mac[2]) {
        memcpy(info->mac_address, card->mac, 6);
    }
}

/**
 * @brief Convert detection state to NIC info array
 */
int convert_state_to_nic_info(const detection_state_t *state,
                              nic_detect_info_t *info_list, int max_nics) {
    if (!state || !info_list || max_nics <= 0) {
        return 0;
    }
    
    int count = (state->cards_found < max_nics) ? state->cards_found : max_nics;
    
    for (int i = 0; i < count; i++) {
        convert_card_to_nic_info(&state->cards[i], &info_list[i]);
    }
    
    return count;
}

/**
 * @brief Acquire detection lock
 */
bool acquire_detection_lock(void) {
    /* Simple atomic test-and-set */
    uint16_t saved_flags;
    CRITICAL_SECTION_ENTER(saved_flags);
    if (detection_lock) {
        CRITICAL_SECTION_EXIT(saved_flags);
        return false;
    }
    detection_lock = true;
    CRITICAL_SECTION_EXIT(saved_flags);
    return true;
}

/**
 * @brief Release detection lock
 */
void release_detection_lock(void) {
    detection_lock = false;
}

/**
 * @brief Apply detection configuration
 */
static void apply_detection_config(detection_state_t *state, const detection_config_t *config) {
    if (!state || !config) {
        return;
    }
    
    if (config->verbose_logging) {
        log_set_level(LOG_LEVEL_DEBUG);
    }
    
    if (config->skip_pnp_bios) {
        log_info("Skipping PnP BIOS check per configuration");
    }
    
    if (config->skip_isapnp) {
        log_warning("Skipping ISAPnP detection - may miss PnP-enabled cards!");
    }
    
    if (config->force_legacy) {
        log_info("Forcing legacy detection per configuration");
    }
}

/**
 * @brief Calculate detection timing statistics
 */
static void calculate_detection_times(detection_state_t *state) {
    if (!state) {
        return;
    }
    
    uint32_t now = hardware_get_timestamp();
    state->detection_duration = now - state->detection_start_time;
    
    log_debug("Detection timing: Total=%ums, ISAPnP=%ums, Legacy=%ums",
             state->detection_duration,
             state->isapnp_duration,
             state->legacy_duration);
}

/**
 * @brief Analyze detection results
 */
static void analyze_detection_results(detection_state_t *state) {
    if (!state) {
        return;
    }
    
    /* Count cards by detection method */
    for (int i = 0; i < state->cards_found; i++) {
        if (state->cards[i].found_by_isapnp) {
            state->isapnp_cards_found++;
        }
        if (state->cards[i].found_by_legacy) {
            state->legacy_cards_found++;
        }
    }
    
    /* Generate summary */
    if (state->pnp_disabled_cards > 0) {
        log_warning("%d cards have PnP disabled in EEPROM", state->pnp_disabled_cards);
        log_info("These cards will not be detected by Windows 95+ PnP manager");
        log_info("Run 3C5X9CFG.EXE to enable PnP if desired");
    }
    
    if (state->duplicates_found > 0) {
        log_info("%d cards detected by multiple methods (normal)", state->duplicates_found);
    }
    
    if (state->errors_encountered > 0) {
        log_warning("Detection completed with %d errors", state->errors_encountered);
    }
}

/**
 * @brief Get human-readable PnP mode string
 */
const char* get_pnp_mode_string(card_pnp_mode_t mode) {
    switch (mode) {
        case PNP_MODE_BOTH_DEFAULT:
            return "Both (PnP priority)";
        case PNP_MODE_LEGACY_ONLY:
            return "Legacy only (PnP DISABLED)";
        case PNP_MODE_PNP_ONLY:
            return "PnP only";
        case PNP_MODE_BOTH_ALT:
            return "Both enabled";
        default:
            return "Unknown";
    }
}

/**
 * @brief Get detection method string
 */
const char* get_detection_method_string(uint8_t methods) {
    static char buffer[64];
    
    buffer[0] = '\0';
    
    if (methods & DETECT_METHOD_PNP_BIOS) {
        strcat(buffer, "PnP-BIOS ");
    }
    if (methods & DETECT_METHOD_ISAPNP) {
        strcat(buffer, "ISAPnP ");
    }
    if (methods & DETECT_METHOD_LEGACY) {
        strcat(buffer, "Legacy");
    }
    
    if (buffer[0] == '\0') {
        strcpy(buffer, "None");
    }
    
    return buffer;
}

/**
 * @brief Log detection statistics
 */
void log_detection_statistics(const detection_state_t *state) {
    if (!state) {
        return;
    }
    
    log_info("Detection Statistics:");
    log_info("  Total cards found: %d", state->cards_found);
    log_info("  ISAPnP cards: %d", state->isapnp_cards_found);
    log_info("  Legacy cards: %d", state->legacy_cards_found);
    log_info("  Duplicates: %d", state->duplicates_found);
    log_info("  PnP-disabled cards: %d", state->pnp_disabled_cards);
    log_info("  Detection time: %ums", state->detection_duration);
    
    if (state->errors_encountered > 0) {
        log_info("  Errors: %d", state->errors_encountered);
    }
    if (state->warnings_generated > 0) {
        log_info("  Warnings: %d", state->warnings_generated);
    }
}