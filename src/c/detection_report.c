/**
 * @file detection_report.c
 * @brief Diagnostic reporting for NIC detection
 *
 * 3Com Packet Driver - Detection Reporting Module
 *
 * Provides comprehensive diagnostic output for:
 * - Detection results and statistics
 * - Card configuration details
 * - Error and warning analysis
 * - Recommendations for resolving issues
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include "../../include/unified_detection.h"
#include "../../include/logging.h"
#include <stdio.h>
#include <string.h>

/* Report formatting constants */
#define SEPARATOR_LINE "=========================================="
#define REPORT_WIDTH   78

/* Internal functions */
static void print_card_details(const tracked_card_t *card, int index);
static void print_detection_summary(const detection_state_t *state);
static void print_error_analysis(const detection_state_t *state);
static void print_recommendations(const detection_state_t *state);
static const char* get_nic_type_string(nic_type_t type);
static const char* get_state_string(uint8_t state);

/**
 * @brief Print comprehensive detection report
 */
void print_detection_report(const detection_state_t *state) {
    if (!state) {
        return;
    }
    
    printf("\n%s\n", SEPARATOR_LINE);
    printf("          3COM NIC DETECTION REPORT\n");
    printf("%s\n\n", SEPARATOR_LINE);
    
    /* System information */
    printf("SYSTEM INFORMATION:\n");
    printf("  PnP BIOS present: %s\n", state->has_pnp_bios ? "Yes" : "No");
    if (state->has_pnp_bios) {
        printf("  PnP BIOS nodes: %d\n", state->pnp_bios_nodes);
    }
    printf("  ISA bridge present: %s\n", state->has_isa_bridge ? "Yes" : "No");
    printf("\n");
    
    /* Detection summary */
    print_detection_summary(state);
    
    /* Card details */
    if (state->cards_found > 0) {
        printf("DETECTED CARDS:\n");
        printf("--------------\n");
        for (int i = 0; i < state->cards_found; i++) {
            print_card_details(&state->cards[i], i + 1);
        }
    } else {
        printf("NO CARDS DETECTED\n\n");
    }
    
    /* Error analysis */
    if (state->errors_encountered > 0 || state->warnings_generated > 0) {
        print_error_analysis(state);
    }
    
    /* Recommendations */
    print_recommendations(state);
    
    printf("%s\n", SEPARATOR_LINE);
}

/**
 * @brief Print detection summary statistics
 */
static void print_detection_summary(const detection_state_t *state) {
    printf("DETECTION SUMMARY:\n");
    printf("  Total cards found: %d\n", state->cards_found);
    printf("  Detection methods used:\n");
    printf("    - ISAPnP: %d attempts, %d cards found\n", 
           state->isapnp_attempts, state->isapnp_cards_found);
    printf("    - Legacy: %d attempts, %d cards found\n",
           state->legacy_attempts, state->legacy_cards_found);
    printf("  Special cases:\n");
    printf("    - Cards with PnP disabled: %d\n", state->pnp_disabled_cards);
    printf("    - Duplicate detections: %d\n", state->duplicates_found);
    printf("  Detection time: %lu ms total\n", (unsigned long)state->detection_duration);
    printf("    - ISAPnP: %lu ms\n", (unsigned long)state->isapnp_duration);
    printf("    - Legacy: %lu ms\n", (unsigned long)state->legacy_duration);
    printf("\n");
}

/**
 * @brief Print detailed information for a single card
 */
static void print_card_details(const tracked_card_t *card, int index) {
    printf("Card #%d:\n", index);
    printf("  Type: %s\n", get_nic_type_string(card->nic_type));
    printf("  MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
           card->mac[0], card->mac[1], card->mac[2],
           card->mac[3], card->mac[4], card->mac[5]);
    
    /* Vendor and device IDs */
    printf("  Vendor ID: 0x%04X (3Com)\n", card->vendor_id);
    printf("  Device ID: 0x%04X\n", card->device_id);
    
    /* Detection methods */
    printf("  Detection methods: %s\n", 
           get_detection_method_string(card->detection_methods));
    
    /* PnP configuration */
    printf("  PnP Mode: %s\n", get_pnp_mode_string(card->pnp_mode));
    if (card->pnp_mode == PNP_MODE_LEGACY_ONLY) {
        printf("    ** PnP is DISABLED in EEPROM **\n");
        printf("    ** Run 3C5X9CFG.EXE to enable if needed **\n");
    }
    
    /* Resource assignment */
    if (card->io_base) {
        printf("  I/O Base: 0x%04X\n", card->io_base);
    } else {
        printf("  I/O Base: Not assigned\n");
    }
    
    if (card->irq) {
        printf("  IRQ: %d\n", card->irq);
    } else {
        printf("  IRQ: Not assigned\n");
    }
    
    /* ISAPnP specific info */
    if (card->found_by_isapnp) {
        printf("  ISAPnP CSN: %d\n", card->csn);
        if (card->isapnp_serial[0]) {
            printf("  ISAPnP Serial: ");
            for (int i = 0; i < 9; i++) {
                printf("%02X", card->isapnp_serial[i]);
            }
            printf("\n");
        }
    }
    
    /* Legacy specific info */
    if (card->found_by_legacy) {
        printf("  Legacy ID Port: 0x%03X\n", card->id_port);
    }
    
    /* Status and verification */
    printf("  State: %s\n", get_state_string(card->state));
    printf("  EEPROM verified: %s\n", card->verified ? "Yes" : "No");
    if (card->verified) {
        printf("  EEPROM checksum: 0x%04X\n", card->eeprom_checksum);
    }
    
    /* Resource conflicts */
    if (card->resources_conflict) {
        printf("  ** RESOURCE CONFLICT DETECTED **\n");
    }
    if (card->resources_fixed) {
        printf("  Resources fixed in EEPROM (non-PnP mode)\n");
    }
    
    /* Detection notes */
    if (card->detection_notes[0]) {
        printf("  Notes: %s\n", card->detection_notes);
    }
    
    printf("\n");
}

/**
 * @brief Print error and warning analysis
 */
static void print_error_analysis(const detection_state_t *state) {
    printf("DIAGNOSTICS:\n");
    printf("-----------\n");
    
    if (state->errors_encountered > 0) {
        printf("  Errors encountered: %d\n", state->errors_encountered);
        if (state->last_error[0]) {
            printf("  Last error: %s\n", state->last_error);
        }
    }
    
    if (state->warnings_generated > 0) {
        printf("  Warnings generated: %d\n", state->warnings_generated);
    }
    
    /* Analyze specific error conditions */
    for (int i = 0; i < state->cards_found; i++) {
        const tracked_card_t *card = &state->cards[i];
        
        /* Check for PnP mode mismatches */
        if (card->pnp_mode == PNP_MODE_PNP_ONLY && card->found_by_legacy && !card->found_by_isapnp) {
            printf("  Card %d: PnP-only mode but ISAPnP detection failed\n", i + 1);
            printf("    - Check ISAPnP configuration\n");
            printf("    - Verify no resource conflicts\n");
        }
        
        if (card->pnp_mode == PNP_MODE_LEGACY_ONLY && card->found_by_isapnp) {
            printf("  Card %d: Legacy-only mode but responded to ISAPnP\n", i + 1);
            printf("    - EEPROM configuration may be corrupted\n");
            printf("    - Run 3C5X9CFG.EXE to verify settings\n");
        }
        
        /* Check for resource conflicts */
        if (card->resources_conflict) {
            printf("  Card %d: Resource conflict detected\n", i + 1);
            printf("    - I/O base 0x%04X may be in use\n", card->io_base);
            printf("    - Try different I/O base assignment\n");
        }
    }
    
    printf("\n");
}

/**
 * @brief Print recommendations based on detection results
 */
static void print_recommendations(const detection_state_t *state) {
    bool has_recommendations = false;
    
    /* Check if we should print recommendations */
    if (state->cards_found == 0 || 
        state->pnp_disabled_cards > 0 ||
        state->errors_encountered > 0 ||
        state->warnings_generated > 0) {
        has_recommendations = true;
    }
    
    if (!has_recommendations) {
        return;
    }
    
    printf("RECOMMENDATIONS:\n");
    printf("---------------\n");
    
    /* No cards found */
    if (state->cards_found == 0) {
        printf("  No cards detected. Please check:\n");
        printf("    1. Cards are properly seated in ISA/EISA slots\n");
        printf("    2. Cards are not disabled in system BIOS\n");
        printf("    3. No hardware conflicts with other devices\n");
        printf("    4. Try running with /FORCE_LEGACY option\n");
    }
    
    /* PnP disabled cards */
    if (state->pnp_disabled_cards > 0) {
        printf("  %d card(s) have PnP disabled in EEPROM:\n", state->pnp_disabled_cards);
        printf("    - These cards will not be detected by Windows 95+\n");
        printf("    - Run 3C5X9CFG.EXE to enable PnP if desired\n");
        printf("    - Current configuration works fine for DOS\n");
    }
    
    /* ISAPnP failures */
    if (state->isapnp_attempts > 0 && state->isapnp_cards_found == 0 && 
        state->legacy_cards_found > 0) {
        printf("  ISAPnP detection found no cards but legacy did:\n");
        printf("    - This is normal if PnP is disabled in EEPROM\n");
        printf("    - No action needed for DOS operation\n");
    }
    
    /* Resource conflicts */
    bool has_conflicts = false;
    for (int i = 0; i < state->cards_found; i++) {
        if (state->cards[i].resources_conflict) {
            has_conflicts = true;
            break;
        }
    }
    
    if (has_conflicts) {
        printf("  Resource conflicts detected:\n");
        printf("    - Use different I/O base addresses\n");
        printf("    - Common free addresses: 0x300, 0x320, 0x340\n");
        printf("    - Check for conflicts with sound cards, etc.\n");
    }
    
    /* Multiple cards */
    if (state->cards_found > 1) {
        printf("  Multiple cards detected:\n");
        printf("    - Ensure unique I/O and IRQ assignments\n");
        printf("    - Consider using packet driver multiplexer\n");
        printf("    - Test each card individually first\n");
    }
    
    printf("\n");
}

/**
 * @brief Get NIC type string
 */
static const char* get_nic_type_string(nic_type_t type) {
    switch (type) {
        case NIC_TYPE_3C509B:
            return "3C509B (10 Mbps)";
        case NIC_TYPE_3C515_TX:
            return "3C515-TX (100 Mbps)";
        default:
            return "Unknown";
    }
}

/**
 * @brief Get state string
 */
static const char* get_state_string(uint8_t state) {
    switch (state) {
        case HW_STATE_UNCONFIGURED:
            return "Unconfigured";
        case HW_STATE_CONFIGURED:
            return "Configured";
        case HW_STATE_ACTIVE:
            return "Active";
        default:
            return "Unknown";
    }
}