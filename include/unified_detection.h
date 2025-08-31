/**
 * @file unified_detection.h
 * @brief Unified NIC detection framework with PnP and legacy support
 *
 * 3Com Packet Driver - Comprehensive Detection System
 *
 * This framework handles the complex task of detecting 3Com NICs that may have
 * PnP disabled in EEPROM, requiring both ISAPnP and legacy detection methods.
 * 
 * Key features:
 * - Supports cards with PnP disabled via EEPROM configuration
 * - Deduplicates cards found by multiple detection methods
 * - Provides diagnostic information about why cards were detected
 * - Handles resource assignment for both PnP and fixed configurations
 *
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef UNIFIED_DETECTION_H
#define UNIFIED_DETECTION_H

#include <stdint.h>
#include <stdbool.h>
#include "nic_init.h"
#include "common.h"

/* Maximum number of NICs to track */
#define MAX_DETECTED_NICS 8

/* Maximum detection retry attempts */
#define MAX_DETECTION_RETRIES 3

/* Detection method flags */
#define DETECT_METHOD_PNP_BIOS  0x01
#define DETECT_METHOD_ISAPNP    0x02
#define DETECT_METHOD_LEGACY    0x04

/* 3C509B EEPROM Configuration Constants */
#define EEPROM_INTERNAL_CONFIG_LOW   0x12  /* Internal Config Register low word */
#define EEPROM_INTERNAL_CONFIG_HIGH  0x13  /* Internal Config Register high word */
#define EEPROM_CHECKSUM_OFFSET       0x1F  /* Checksum location */

/* Internal Configuration Register bit definitions */
#define INTERNAL_CONFIG_ISA_ACTIVATION_SHIFT  18  /* Bits 19-18 */
#define INTERNAL_CONFIG_ISA_ACTIVATION_MASK   0x03

/* 3C509B ID Port Commands */
#define ID_PORT_CANCEL_ID_STATE      0x00  /* Cancel ID state (minimal reset) */
#define ID_PORT_GLOBAL_RESET         0xC0  /* Global reset (aggressive) */
#define ID_PORT_SELECT_TAG           0xD0  /* Select tagged adapter */
#define ID_PORT_ACTIVATE_AND_SET_IO  0xE0  /* Activate with I/O base */

/**
 * @brief PnP mode configuration from EEPROM bits 19-18
 * 
 * These values correspond to the ISA ACTIVATION SELECT bits in the
 * Internal Configuration Register (EEPROM words 0x12-0x13)
 * Reference: 3Com EtherLink III Technical Reference, Chapter 7
 */
typedef enum {
    PNP_MODE_BOTH_DEFAULT = 0x00,  /* Both mechanisms enabled (PnP priority) */
    PNP_MODE_LEGACY_ONLY  = 0x01,  /* PnP DISABLED in EEPROM - legacy only */
    PNP_MODE_PNP_ONLY     = 0x02,  /* Legacy disabled, PnP only */
    PNP_MODE_BOTH_ALT     = 0x03   /* Both mechanisms enabled */
} card_pnp_mode_t;

/**
 * @brief Detailed information about a detected card
 * 
 * Tracks how each card was detected and its configuration state
 */
typedef struct {
    /* Card identity */
    uint8_t mac[6];                /* MAC address (primary identifier) */
    uint8_t isapnp_serial[9];      /* ISAPnP serial ID if available */
    uint16_t eeprom_checksum;      /* EEPROM checksum for validation */
    uint16_t vendor_id;            /* Vendor ID (0x10B7 for 3Com) */
    uint16_t device_id;            /* Device ID */
    
    /* Detection tracking */
    bool found_by_isapnp;          /* Detected via ISAPnP protocol */
    bool found_by_legacy;          /* Detected via legacy ID port */
    uint8_t csn;                   /* Card Select Number (ISAPnP) */
    uint16_t id_port;              /* ID port used (legacy) */
    uint8_t detection_methods;     /* Bitmask of detection methods */
    
    /* Configuration */
    card_pnp_mode_t pnp_mode;      /* PnP mode from EEPROM */
    uint16_t io_base;              /* Assigned I/O base address */
    uint8_t irq;                   /* Assigned IRQ */
    bool resources_fixed;          /* True if resources are EEPROM-fixed */
    bool resources_conflict;       /* True if resource conflict detected */
    
    /* Card type */
    nic_type_t nic_type;           /* 3C509B, 3C515-TX, etc. */
    uint32_t capabilities;         /* Hardware capabilities */
    
    /* Validation and diagnostics */
    bool verified;                 /* EEPROM successfully read */
    bool activated;                /* Card successfully activated */
    char detection_notes[64];      /* Human-readable detection info */
    uint32_t detection_timestamp;  /* When card was detected */
} tracked_card_t;

/**
 * @brief Overall detection state and statistics
 */
typedef struct {
    /* System capabilities */
    bool has_pnp_bios;             /* PnP BIOS present */
    int pnp_bios_nodes;            /* Number of PnP nodes reported */
    bool has_isa_bridge;           /* ISA bridge detected */
    
    /* Detection results */
    int cards_found;               /* Total unique cards found */
    tracked_card_t cards[MAX_DETECTED_NICS];  /* Card information */
    
    /* Detection state tracking */
    bool isapnp_initiated;         /* ISAPnP initiation key sent */
    bool legacy_id_state_active;   /* 3C509B ID state activated */
    bool cards_need_reset;         /* Cards modified and need reset */
    
    /* Detection statistics */
    int isapnp_attempts;           /* ISAPnP detection attempts */
    int isapnp_cards_found;        /* Cards found via ISAPnP */
    int legacy_attempts;           /* Legacy detection attempts */
    int legacy_cards_found;        /* Cards found via legacy */
    int duplicates_found;          /* Cards found by multiple methods */
    int pnp_disabled_cards;        /* Cards with PnP disabled in EEPROM */
    
    /* Timing information */
    uint32_t detection_start_time; /* Detection start timestamp */
    uint32_t detection_duration;   /* Total detection time in ms */
    uint32_t isapnp_duration;      /* Time spent in ISAPnP detection */
    uint32_t legacy_duration;      /* Time spent in legacy detection */
    
    /* Error tracking */
    int errors_encountered;        /* Number of errors during detection */
    int warnings_generated;        /* Number of warnings */
    char last_error[128];          /* Last error message */
    
    /* Safety */
    volatile bool detection_in_progress;  /* Detection lock */
} detection_state_t;

/**
 * @brief Detection configuration options
 */
typedef struct {
    bool skip_pnp_bios;           /* Skip PnP BIOS check */
    bool skip_isapnp;              /* Skip ISAPnP detection */
    bool force_legacy;             /* Always run legacy detection */
    bool verbose_logging;          /* Enable verbose logging */
    bool strict_deduplication;     /* Strict duplicate checking */
    uint16_t preferred_io_base;    /* Preferred I/O base if configurable */
    uint8_t preferred_irq;         /* Preferred IRQ if configurable */
} detection_config_t;

/* Main detection functions */

/**
 * @brief Perform comprehensive NIC detection
 * 
 * Runs all detection methods in sequence:
 * 1. PnP BIOS check (informational)
 * 2. ISAPnP detection (opportunistic)
 * 3. Legacy detection (mandatory)
 * 4. Reconciliation and verification
 * 
 * @param info_list Output array for detected NICs
 * @param max_nics Maximum NICs to detect
 * @param config Optional configuration (NULL for defaults)
 * @return Number of NICs detected, negative on error
 */
int unified_nic_detection(nic_detect_info_t *info_list, int max_nics,
                          const detection_config_t *config);

/**
 * @brief Perform quick detection (ISAPnP only)
 * 
 * @param info_list Output array for detected NICs
 * @param max_nics Maximum NICs to detect
 * @return Number of NICs detected, negative on error
 */
int quick_nic_detection(nic_detect_info_t *info_list, int max_nics);

/* Individual detection methods */

/**
 * @brief Check system PnP capabilities
 * 
 * @param state Detection state to update
 * @return true if PnP BIOS present
 */
bool check_system_capabilities(detection_state_t *state);

/**
 * @brief Perform ISAPnP detection
 * 
 * @param state Detection state to update
 * @return Number of new cards found
 */
int perform_isapnp_detection(detection_state_t *state);

/**
 * @brief Perform legacy detection (mandatory)
 * 
 * @param state Detection state to update
 * @return Number of new cards found
 */
int perform_legacy_detection(detection_state_t *state);

/**
 * @brief Reconcile cards found by multiple methods
 * 
 * @param state Detection state to update
 */
void reconcile_detected_cards(detection_state_t *state);

/* Card management functions */

/**
 * @brief Find card by MAC address
 * 
 * @param state Detection state
 * @param mac MAC address to search for
 * @return Pointer to card or NULL if not found
 */
tracked_card_t* find_card_by_mac(const detection_state_t *state, 
                                 const uint8_t *mac);

/**
 * @brief Find card by ISAPnP serial
 * 
 * @param state Detection state
 * @param serial ISAPnP serial to search for
 * @return Pointer to card or NULL if not found
 */
tracked_card_t* find_card_by_serial(const detection_state_t *state,
                                    const uint8_t *serial);

/**
 * @brief Check if card is duplicate
 * 
 * @param state Detection state
 * @param card Card to check
 * @return true if duplicate found
 */
bool is_duplicate_card(const detection_state_t *state,
                       const tracked_card_t *card);

/* EEPROM verification functions */

/**
 * @brief Read and verify card EEPROM configuration
 * 
 * @param card Card to verify
 * @return true if verification successful
 */
bool verify_card_configuration(tracked_card_t *card);

/**
 * @brief Read PnP mode from card EEPROM
 * 
 * @param io_base Card I/O base address
 * @return PnP mode configuration
 */
card_pnp_mode_t read_card_pnp_mode(uint16_t io_base);

/* Reset and safety functions */

/**
 * @brief Perform global reset of all cards
 * 
 * Resets both ISAPnP and legacy card states
 */
void global_card_reset(void);

/**
 * @brief Reset detection state
 * 
 * @param state State to reset
 */
void reset_detection_state(detection_state_t *state);

/* Diagnostic and reporting functions */

/**
 * @brief Print comprehensive detection report
 * 
 * @param state Detection state to report
 */
void print_detection_report(const detection_state_t *state);

/**
 * @brief Log detection statistics
 * 
 * @param state Detection state
 */
void log_detection_statistics(const detection_state_t *state);

/**
 * @brief Get human-readable PnP mode description
 * 
 * @param mode PnP mode
 * @return Mode description string
 */
const char* get_pnp_mode_string(card_pnp_mode_t mode);

/**
 * @brief Get detection method description
 * 
 * @param methods Detection method bitmask
 * @return Method description string
 */
const char* get_detection_method_string(uint8_t methods);

/* Conversion functions */

/**
 * @brief Convert tracked card to NIC info structure
 * 
 * @param card Source card information
 * @param info Destination NIC info structure
 */
void convert_card_to_nic_info(const tracked_card_t *card,
                              nic_detect_info_t *info);

/**
 * @brief Convert detection state to NIC info array
 * 
 * @param state Source detection state
 * @param info_list Destination array
 * @param max_nics Maximum entries to convert
 * @return Number of entries converted
 */
int convert_state_to_nic_info(const detection_state_t *state,
                              nic_detect_info_t *info_list,
                              int max_nics);

/* Thread safety */

/**
 * @brief Acquire detection lock
 * 
 * @return true if lock acquired, false if already locked
 */
bool acquire_detection_lock(void);

/**
 * @brief Release detection lock
 */
void release_detection_lock(void);

/* Error codes */
#define DETECT_SUCCESS           0
#define DETECT_ERR_IN_PROGRESS  -1
#define DETECT_ERR_NO_CARDS     -2
#define DETECT_ERR_LOCK_FAILED  -3
#define DETECT_ERR_RESET_FAILED -4
#define DETECT_ERR_INVALID_PARAM -5

#endif /* UNIFIED_DETECTION_H */