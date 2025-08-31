/**
 * @file media_control.h
 * @brief Media control and transceiver selection for 3Com 3c509 family
 *
 * This header provides comprehensive media control functionality for all 3c509
 * family variants, including transceiver selection, media detection, and
 * Window 4 register operations for the 3Com packet driver.
 *
 * PHASE 0A IMPLEMENTATION:
 * - Core transceiver selection with Window 4 operations
 * - Auto-media selection for combo variants
 * - Media-specific link beat detection  
 * - Low-level register configuration
 * - Safety validation and error handling
 * - Window management utilities
 *
 * SUPPORTED VARIANTS:
 * - 3c509B-Combo: Auto-select between 10BaseT/10Base2/AUI
 * - 3c509B-TP: 10BaseT only with link detection
 * - 3c509B-BNC: 10Base2 only coaxial
 * - 3c509B-AUI: AUI only with external transceiver
 * - 3c509B-FL: Fiber link variant
 * - 3c515-TX: Fast Ethernet with auto-negotiation
 *
 * @note This implementation builds on the existing window architecture and
 *       integrates with the enhanced nic_info_t structures from nic_defs.h.
 */

#ifndef _MEDIA_CONTROL_H_
#define _MEDIA_CONTROL_H_

#include "common.h"
#include "nic_defs.h"
#include "media_types.h"
#include "3c509b.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Media Control Error Codes ---

/**
 * @brief Media control specific error codes
 * 
 * These extend the common error codes with media-specific conditions.
 */
#define MEDIA_ERROR_NONE                    0       /* No error */
#define MEDIA_ERROR_INVALID_MEDIA          -100     /* Invalid media type */
#define MEDIA_ERROR_MEDIA_NOT_SUPPORTED    -101     /* Media not supported by variant */
#define MEDIA_ERROR_NO_LINK                -102     /* No link detected */
#define MEDIA_ERROR_LINK_TEST_FAILED       -103     /* Link test failed */
#define MEDIA_ERROR_AUTO_DETECT_FAILED     -104     /* Auto-detection failed */
#define MEDIA_ERROR_REGISTER_ACCESS        -105     /* Register access failed */
#define MEDIA_ERROR_WINDOW_TIMEOUT         -106     /* Window selection timeout */
#define MEDIA_ERROR_TRANSCEIVER_FAULT      -107     /* Transceiver fault detected */
#define MEDIA_ERROR_MEDIA_CONFLICT         -108     /* Media configuration conflict */
#define MEDIA_ERROR_VALIDATION_FAILED      -109     /* Media validation failed */

// --- Media Detection State Constants ---

/**
 * @brief Media detection timing constants
 * 
 * These constants define timeouts and delays for reliable media detection.
 */
#define MEDIA_DETECT_TIMEOUT_MS             5000    /* Overall detection timeout */
#define MEDIA_LINK_TEST_TIMEOUT_MS          3000    /* Link test timeout */
#define MEDIA_SWITCH_DELAY_MS               100     /* Delay after media switch */
#define MEDIA_STABILIZATION_DELAY_MS        500     /* Media stabilization delay */
#define WINDOW_SELECT_TIMEOUT_MS            100     /* Window selection timeout */
#define LINK_BEAT_CHECK_INTERVAL_MS         100     /* Link beat check interval */
#define AUTO_DETECT_RETRY_COUNT             3       /* Auto-detection retry attempts */

/**
 * @brief Media test duration constants
 * 
 * These define how long to test each media type during auto-detection.
 */
#define MEDIA_TEST_DURATION_10BASET_MS      2000    /* 10BaseT test duration */
#define MEDIA_TEST_DURATION_10BASE2_MS      1000    /* 10Base2 test duration */
#define MEDIA_TEST_DURATION_AUI_MS          1500    /* AUI test duration */
#define MEDIA_TEST_DURATION_FIBER_MS        2500    /* Fiber test duration */

// --- Media Control Flags ---

/**
 * @brief Media control operation flags
 * 
 * These flags control the behavior of media control operations.
 */
#define MEDIA_CTRL_FLAG_FORCE               0x01    /* Force media selection */
#define MEDIA_CTRL_FLAG_NO_AUTO_DETECT      0x02    /* Disable auto-detection */
#define MEDIA_CTRL_FLAG_PRESERVE_DUPLEX     0x04    /* Preserve duplex setting */
#define MEDIA_CTRL_FLAG_ENABLE_DIAGNOSTICS  0x08    /* Enable diagnostic mode */
#define MEDIA_CTRL_FLAG_QUICK_TEST          0x10    /* Use quick link tests */
#define MEDIA_CTRL_FLAG_VERBOSE_LOGGING     0x20    /* Enable verbose logging */

/**
 * @brief Link test result flags
 * 
 * These flags indicate the results of link testing operations.
 */
#define LINK_TEST_RESULT_LINK_UP            0x01    /* Link is up */
#define LINK_TEST_RESULT_LINK_STABLE        0x02    /* Link is stable */
#define LINK_TEST_RESULT_CARRIER_DETECT     0x04    /* Carrier detected */
#define LINK_TEST_RESULT_JABBER_DETECT      0x08    /* Jabber detected */
#define LINK_TEST_RESULT_SQE_TEST_PASSED    0x10    /* SQE test passed */
#define LINK_TEST_RESULT_COLLISION_DETECT   0x20    /* Collision detection works */

// --- Core Data Structures ---

/**
 * @brief Media detection configuration structure
 * 
 * This structure configures the behavior of media detection operations.
 */
typedef struct {
    uint8_t flags;                          /* Control flags (MEDIA_CTRL_FLAG_*) */
    uint16_t timeout_ms;                    /* Detection timeout in milliseconds */
    uint8_t retry_count;                    /* Number of retry attempts */
    uint16_t test_duration_ms;              /* Test duration per media type */
    media_type_t preferred_media;           /* Preferred media type */
    uint16_t media_priority_mask;           /* Priority mask for media types */
} media_detect_config_t;

/**
 * @brief Link test results structure
 * 
 * This structure contains the results of link testing operations.
 */
typedef struct {
    uint8_t test_flags;                     /* Test result flags */
    media_type_t tested_media;              /* Media type that was tested */
    uint16_t link_status_register;          /* Raw link status register value */
    uint16_t network_diagnostics;           /* Network diagnostics register */
    uint32_t test_duration_ms;              /* Actual test duration */
    uint32_t link_up_time_ms;               /* Time link was up during test */
    uint8_t signal_quality;                 /* Signal quality (0-100) */
} link_test_result_t;

/**
 * @brief Media configuration state structure
 * 
 * This structure tracks the current media configuration state.
 */
typedef struct {
    media_type_t current_media;             /* Currently selected media */
    media_type_t detected_media;            /* Auto-detected media */
    uint8_t detection_state;                /* Detection state flags */
    uint8_t last_window;                    /* Last selected window */
    uint16_t media_control_register;        /* Current media control register value */
    uint32_t last_config_time;              /* Timestamp of last configuration */
    uint32_t link_up_time;                  /* Time when link came up */
    uint8_t error_count;                    /* Number of configuration errors */
} media_config_state_t;

// --- Window Management Functions ---

/**
 * @brief Safely select a register window with timeout protection
 * @param nic Pointer to NIC information structure
 * @param window Window number to select (0-7)
 * @param timeout_ms Timeout in milliseconds
 * @return SUCCESS on success, error code on failure
 */
int safe_select_window(nic_info_t *nic, uint8_t window, uint32_t timeout_ms);

/**
 * @brief Get the currently selected window
 * @param nic Pointer to NIC information structure
 * @return Current window number, or -1 on error
 */
int get_current_window(nic_info_t *nic);

/**
 * @brief Save current window state and select new window
 * @param nic Pointer to NIC information structure
 * @param new_window New window to select
 * @param saved_window Pointer to store current window
 * @return SUCCESS on success, error code on failure
 */
int save_and_select_window(nic_info_t *nic, uint8_t new_window, uint8_t *saved_window);

/**
 * @brief Restore previously saved window
 * @param nic Pointer to NIC information structure
 * @param saved_window Window to restore
 * @return SUCCESS on success, error code on failure
 */
int restore_window(nic_info_t *nic, uint8_t saved_window);

/**
 * @brief Wait for command busy flag to clear
 * @param nic Pointer to NIC information structure
 * @param timeout_ms Timeout in milliseconds
 * @return SUCCESS when ready, ERROR_TIMEOUT on timeout
 */
int wait_for_command_ready(nic_info_t *nic, uint32_t timeout_ms);

// --- Core Media Control Functions ---

/**
 * @brief Select and configure media transceiver with Window 4 operations
 * @param nic Pointer to NIC information structure
 * @param media_type Media type to select
 * @param flags Control flags for selection behavior
 * @return SUCCESS on success, error code on failure
 */
int select_media_transceiver(nic_info_t *nic, media_type_t media_type, uint8_t flags);

/**
 * @brief Automatically detect and select optimal media type for combo cards
 * @param nic Pointer to NIC information structure
 * @param config Pointer to detection configuration (can be NULL for defaults)
 * @return Detected media type, or MEDIA_TYPE_UNKNOWN on failure
 */
media_type_t auto_detect_media(nic_info_t *nic, const media_detect_config_t *config);

/**
 * @brief Test link beat and connection status for specific media type
 * @param nic Pointer to NIC information structure
 * @param media_type Media type to test
 * @param test_duration_ms How long to test the link
 * @param result Pointer to store test results
 * @return SUCCESS if link is good, error code if link fails or test fails
 */
int test_link_beat(nic_info_t *nic, media_type_t media_type, uint32_t test_duration_ms, 
                   link_test_result_t *result);

/**
 * @brief Configure low-level media control registers
 * @param nic Pointer to NIC information structure
 * @param media_type Media type to configure
 * @param enable_full_duplex Enable full duplex mode if supported
 * @return SUCCESS on success, error code on failure
 */
int configure_media_registers(nic_info_t *nic, media_type_t media_type, bool enable_full_duplex);

/**
 * @brief Validate media selection against NIC capabilities and current state
 * @param nic Pointer to NIC information structure
 * @param media_type Media type to validate
 * @param error_msg Buffer for error message (optional)
 * @param error_msg_size Size of error message buffer
 * @return SUCCESS if valid, error code with details if invalid
 */
int validate_media_selection(nic_info_t *nic, media_type_t media_type, 
                           char *error_msg, size_t error_msg_size);

// --- Advanced Media Control Functions ---

/**
 * @brief Initialize media control subsystem for a NIC
 * @param nic Pointer to NIC information structure
 * @return SUCCESS on success, error code on failure
 */
int media_control_init(nic_info_t *nic);

/**
 * @brief Cleanup media control subsystem
 * @param nic Pointer to NIC information structure
 * @return SUCCESS on success, error code on failure
 */
int media_control_cleanup(nic_info_t *nic);

/**
 * @brief Get current media configuration state
 * @param nic Pointer to NIC information structure
 * @param state Pointer to store current state
 * @return SUCCESS on success, error code on failure
 */
int get_media_config_state(nic_info_t *nic, media_config_state_t *state);

/**
 * @brief Force media selection without auto-detection
 * @param nic Pointer to NIC information structure
 * @param media_type Media type to force
 * @return SUCCESS on success, error code on failure
 */
int force_media_selection(nic_info_t *nic, media_type_t media_type);

/**
 * @brief Reset media configuration to default state
 * @param nic Pointer to NIC information structure
 * @return SUCCESS on success, error code on failure
 */
int reset_media_configuration(nic_info_t *nic);

// --- Media-Specific Configuration Functions ---

/**
 * @brief Configure 10BaseT media with link beat detection
 * @param nic Pointer to NIC information structure
 * @param enable_full_duplex Enable full duplex if supported
 * @return SUCCESS on success, error code on failure
 */
int configure_10baset_media(nic_info_t *nic, bool enable_full_duplex);

/**
 * @brief Configure 10Base2 coaxial media
 * @param nic Pointer to NIC information structure
 * @return SUCCESS on success, error code on failure
 */
int configure_10base2_media(nic_info_t *nic);

/**
 * @brief Configure AUI media with SQE test
 * @param nic Pointer to NIC information structure
 * @param enable_sqe_test Enable SQE heartbeat test
 * @return SUCCESS on success, error code on failure
 */
int configure_aui_media(nic_info_t *nic, bool enable_sqe_test);

/**
 * @brief Configure fiber optic media
 * @param nic Pointer to NIC information structure
 * @param enable_full_duplex Enable full duplex mode
 * @return SUCCESS on success, error code on failure
 */
int configure_fiber_media(nic_info_t *nic, bool enable_full_duplex);

// --- Link Detection and Monitoring ---

/**
 * @brief Check if link is currently up for the selected media
 * @param nic Pointer to NIC information structure
 * @return 1 if link is up, 0 if down, negative on error
 */
int check_media_link_status(nic_info_t *nic);

/**
 * @brief Monitor link status changes over time
 * @param nic Pointer to NIC information structure
 * @param monitor_duration_ms How long to monitor
 * @param callback Function to call on link state changes (optional)
 * @return Number of link state changes detected, negative on error
 */
int monitor_link_changes(nic_info_t *nic, uint32_t monitor_duration_ms, 
                        void (*callback)(nic_info_t *nic, bool link_up));

/**
 * @brief Test signal quality for current media
 * @param nic Pointer to NIC information structure
 * @param quality Pointer to store quality measurement (0-100)
 * @return SUCCESS on success, error code on failure
 */
int test_signal_quality(nic_info_t *nic, uint8_t *quality);

// --- Diagnostic and Debug Functions ---

/**
 * @brief Run comprehensive media diagnostics
 * @param nic Pointer to NIC information structure
 * @param test_all_media Test all supported media types
 * @return SUCCESS if all tests pass, error code on failure
 */
int run_media_diagnostics(nic_info_t *nic, bool test_all_media);

/**
 * @brief Dump current media control register values
 * @param nic Pointer to NIC information structure
 * @param buffer Buffer to store formatted output
 * @param buffer_size Size of output buffer
 * @return Number of characters written to buffer
 */
int dump_media_registers(nic_info_t *nic, char *buffer, size_t buffer_size);

/**
 * @brief Get detailed media information string
 * @param nic Pointer to NIC information structure
 * @param buffer Buffer to store media information
 * @param buffer_size Size of buffer
 * @return Number of characters written to buffer
 */
int get_media_info_string(nic_info_t *nic, char *buffer, size_t buffer_size);

// --- Utility Functions ---

/**
 * @brief Check if media type is supported by this NIC variant
 * @param nic Pointer to NIC information structure
 * @param media_type Media type to check
 * @return 1 if supported, 0 if not supported
 */
int is_media_supported_by_nic(nic_info_t *nic, media_type_t media_type);

/**
 * @brief Get the default media type for this NIC variant
 * @param nic Pointer to NIC information structure
 * @return Default media type for this variant
 */
media_type_t get_default_media_for_nic(nic_info_t *nic);

/**
 * @brief Convert media control error code to string
 * @param error_code Error code to convert
 * @return String representation of error code
 */
const char* media_error_to_string(int error_code);

/**
 * @brief Get media priority for auto-detection ordering
 * @param media_type Media type to query
 * @param nic_variant NIC variant identifier
 * @return Priority value (lower = higher priority)
 */
uint8_t get_media_detection_priority(media_type_t media_type, uint8_t nic_variant);

// --- Configuration Defaults ---

/**
 * @brief Default media detection configuration
 */
#define MEDIA_DETECT_CONFIG_DEFAULT { \
    .flags = 0, \
    .timeout_ms = MEDIA_DETECT_TIMEOUT_MS, \
    .retry_count = AUTO_DETECT_RETRY_COUNT, \
    .test_duration_ms = MEDIA_TEST_DURATION_10BASET_MS, \
    .preferred_media = MEDIA_TYPE_UNKNOWN, \
    .media_priority_mask = 0xFFFF \
}

/**
 * @brief Quick media detection configuration for faster detection
 */
#define MEDIA_DETECT_CONFIG_QUICK { \
    .flags = MEDIA_CTRL_FLAG_QUICK_TEST, \
    .timeout_ms = 2000, \
    .retry_count = 1, \
    .test_duration_ms = 500, \
    .preferred_media = MEDIA_TYPE_UNKNOWN, \
    .media_priority_mask = 0xFFFF \
}

#ifdef __cplusplus
}
#endif

#endif /* _MEDIA_CONTROL_H_ */