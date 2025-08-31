/**
 * @file media_control.c
 * @brief Media control and transceiver selection implementation for 3Com 3c509 family
 *
 * This file implements comprehensive media control functionality for all 3c509
 * family variants, including transceiver selection, media detection, and
 * Window 4 register operations for the 3Com packet driver.
 *
 * PHASE 0A IMPLEMENTATION FEATURES:
 * - Core transceiver selection with Window 4 operations
 * - Auto-media selection for combo variants 
 * - Media-specific link beat detection
 * - Low-level register configuration
 * - Safety validation and error handling
 * - Window management utilities
 *
 * This implementation supports all 3c509 family variants and integrates with
 * the existing windowed register architecture while providing robust error
 * handling and comprehensive media detection capabilities.
 */

#include "../include/media_control.h"
#include "../include/3c509b.h"
#include "../include/nic_defs.h"
#include "../include/hardware.h"
#include "../include/logging.h"
#include "../include/common.h"
#include "../include/cpu_optimized.h"
#include <string.h>

// --- Internal Helper Functions ---

static int select_window_safe(nic_info_t *nic, uint8_t window);
static int wait_for_cmd_completion(nic_info_t *nic, uint32_t timeout_ms);
static uint16_t read_media_control_register(nic_info_t *nic);
static int write_media_control_register(nic_info_t *nic, uint16_t value);
static uint16_t read_network_diagnostics_register(nic_info_t *nic);
static int test_media_link_quality(nic_info_t *nic, media_type_t media_type, 
                                  uint32_t test_duration_ms, link_test_result_t *result);
static media_type_t detect_best_media_for_variant(nic_info_t *nic, const media_detect_config_t *config);
static int configure_media_specific_registers(nic_info_t *nic, media_type_t media_type, bool full_duplex);

// --- Window Management Functions ---

/**
 * @brief Safely select a register window with timeout protection
 */
int safe_select_window(nic_info_t *nic, uint8_t window, uint32_t timeout_ms) {
    if (!nic || window > 7) {
        LOG_ERROR("Invalid parameters for window selection");
        return ERROR_INVALID_PARAM;
    }

    // Wait for any pending command to complete first
    int result = wait_for_command_ready(nic, timeout_ms);
    if (result != SUCCESS) {
        LOG_ERROR("Command not ready before window selection");
        return MEDIA_ERROR_WINDOW_TIMEOUT;
    }

    // Issue window select command with CPU-optimized I/O
    uint16_t cmd = _3C509B_CMD_SELECT_WINDOW | window;
    cpu_opt_outw(nic->io_base + _3C509B_COMMAND_REG, cmd);

    // Wait for command completion
    result = wait_for_cmd_completion(nic, timeout_ms);
    if (result != SUCCESS) {
        LOG_ERROR("Window %d selection timeout", window);
        return MEDIA_ERROR_WINDOW_TIMEOUT;
    }

    LOG_TRACE("Selected window %d", window);
    return SUCCESS;
}

/**
 * @brief Get the currently selected window
 */
int get_current_window(nic_info_t *nic) {
    if (!nic) {
        return -1;
    }

    // Note: There's no direct way to read the current window from 3c509B
    // This would need to be tracked in software if needed
    LOG_DEBUG("Current window query - tracking not implemented");
    return -1;
}

/**
 * @brief Save current window state and select new window
 */
int save_and_select_window(nic_info_t *nic, uint8_t new_window, uint8_t *saved_window) {
    if (!nic || !saved_window) {
        return ERROR_INVALID_PARAM;
    }

    // Since we can't read the current window, we'll assume Window 1 as default
    // In a complete implementation, this would be tracked in the NIC structure
    *saved_window = 1; // Default assumption

    return safe_select_window(nic, new_window, WINDOW_SELECT_TIMEOUT_MS);
}

/**
 * @brief Restore previously saved window
 */
int restore_window(nic_info_t *nic, uint8_t saved_window) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    return safe_select_window(nic, saved_window, WINDOW_SELECT_TIMEOUT_MS);
}

/**
 * @brief Wait for command busy flag to clear
 */
int wait_for_command_ready(nic_info_t *nic, uint32_t timeout_ms) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    uint32_t start_time = get_system_timestamp_ms();
    
    while (get_system_timestamp_ms() - start_time < timeout_ms) {
        uint16_t status = cpu_opt_inw(nic->io_base + _3C509B_STATUS_REG);
        
        if (!(status & _3C509B_STATUS_CMD_BUSY)) {
            return SUCCESS;
        }
        
        cpu_opt_udelay(100); // CPU-optimized 100 microsecond delay
    }
    
    LOG_ERROR("Command ready timeout after %u ms", timeout_ms);
    return ERROR_TIMEOUT;
}

// --- Core Media Control Functions ---

/**
 * @brief Select and configure media transceiver with Window 4 operations
 */
int select_media_transceiver(nic_info_t *nic, media_type_t media_type, uint8_t flags) {
    if (!nic) {
        LOG_ERROR("Invalid NIC pointer");
        return ERROR_INVALID_PARAM;
    }

    LOG_INFO("Selecting media transceiver: %s", media_type_to_string(media_type));

    // Validate media type against NIC capabilities
    if (!(flags & MEDIA_CTRL_FLAG_FORCE)) {
        int validation = validate_media_selection(nic, media_type, NULL, 0);
        if (validation != SUCCESS) {
            LOG_ERROR("Media validation failed: %d", validation);
            return validation;
        }
    }

    uint8_t saved_window;
    int result = save_and_select_window(nic, _3C509B_WINDOW_4, &saved_window);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to select Window 4: %d", result);
        return result;
    }

    // Configure the media-specific registers
    result = configure_media_registers(nic, media_type, 
                                     (flags & MEDIA_CTRL_FLAG_PRESERVE_DUPLEX) ? 
                                     nic->full_duplex : false);
    
    if (result != SUCCESS) {
        LOG_ERROR("Failed to configure media registers: %d", result);
        restore_window(nic, saved_window);
        return result;
    }

    // Wait for media to stabilize
    mdelay(MEDIA_SWITCH_DELAY_MS);

    // Test the link if not forcing
    if (!(flags & MEDIA_CTRL_FLAG_NO_AUTO_DETECT)) {
        link_test_result_t test_result;
        result = test_link_beat(nic, media_type, MEDIA_TEST_DURATION_10BASET_MS, &test_result);
        
        if (result != SUCCESS) {
            LOG_WARNING("Link test failed for %s: %d", media_type_to_string(media_type), result);
            if (!(flags & MEDIA_CTRL_FLAG_FORCE)) {
                restore_window(nic, saved_window);
                return MEDIA_ERROR_NO_LINK;
            }
        } else {
            LOG_INFO("Link test passed for %s", media_type_to_string(media_type));
        }
    }

    // Update NIC state
    nic->current_media = media_type;
    nic->media_config_source = (flags & MEDIA_CTRL_FLAG_FORCE) ? 
                              MEDIA_CONFIG_USER_FORCED : MEDIA_CONFIG_AUTO_DETECT;

    restore_window(nic, saved_window);

    LOG_INFO("Successfully selected media: %s", media_type_to_string(media_type));
    return SUCCESS;
}

/**
 * @brief Automatically detect and select optimal media type for combo cards
 */
media_type_t auto_detect_media(nic_info_t *nic, const media_detect_config_t *config) {
    if (!nic) {
        LOG_ERROR("Invalid NIC pointer");
        return MEDIA_TYPE_UNKNOWN;
    }

    // Use default config if none provided
    media_detect_config_t default_config = MEDIA_DETECT_CONFIG_DEFAULT;
    if (!config) {
        config = &default_config;
    }

    LOG_INFO("Starting auto-detection for media types (timeout: %u ms)", config->timeout_ms);

    // Check if this NIC supports auto-detection
    if (!(nic->media_capabilities & MEDIA_CAP_AUTO_SELECT)) {
        LOG_WARNING("NIC does not support auto-detection, using default media");
        return get_default_media_for_nic(nic);
    }

    uint32_t start_time = get_system_timestamp_ms();
    media_type_t detected_media = MEDIA_TYPE_UNKNOWN;

    // Try detection multiple times if configured
    for (uint8_t attempt = 0; attempt < config->retry_count; attempt++) {
        if (get_system_timestamp_ms() - start_time >= config->timeout_ms) {
            LOG_WARNING("Auto-detection timeout reached");
            break;
        }

        LOG_DEBUG("Auto-detection attempt %d/%d", attempt + 1, config->retry_count);

        detected_media = detect_best_media_for_variant(nic, config);
        if (detected_media != MEDIA_TYPE_UNKNOWN) {
            break;
        }

        if (attempt < config->retry_count - 1) {
            mdelay(500); // Wait between attempts
        }
    }

    if (detected_media != MEDIA_TYPE_UNKNOWN) {
        LOG_INFO("Auto-detected media: %s", media_type_to_string(detected_media));
        
        // Configure the detected media
        int result = select_media_transceiver(nic, detected_media, 0);
        if (result != SUCCESS) {
            LOG_ERROR("Failed to configure auto-detected media: %d", result);
            return MEDIA_TYPE_UNKNOWN;
        }
        
        nic->detected_media = detected_media;
        nic->media_detection_state = MEDIA_DETECT_COMPLETED;
    } else {
        LOG_WARNING("Auto-detection failed, no suitable media found");
        nic->media_detection_state = MEDIA_DETECT_FAILED;
    }

    return detected_media;
}

/**
 * @brief Test link beat and connection status for specific media type
 */
int test_link_beat(nic_info_t *nic, media_type_t media_type, uint32_t test_duration_ms, 
                   link_test_result_t *result) {
    if (!nic || !result) {
        return ERROR_INVALID_PARAM;
    }

    LOG_DEBUG("Testing link beat for %s (duration: %u ms)", 
              media_type_to_string(media_type), test_duration_ms);

    // Initialize result structure with CPU-optimized operation
    cpu_opt_memzero(result, sizeof(link_test_result_t));
    result->tested_media = media_type;

    uint8_t saved_window;
    int ret = save_and_select_window(nic, _3C509B_WINDOW_4, &saved_window);
    if (ret != SUCCESS) {
        return ret;
    }

    // Configure for the media type being tested
    ret = configure_media_specific_registers(nic, media_type, false);
    if (ret != SUCCESS) {
        restore_window(nic, saved_window);
        return ret;
    }

    // Allow media to stabilize
    mdelay(MEDIA_STABILIZATION_DELAY_MS);

    uint32_t start_time = get_system_timestamp_ms();
    uint32_t link_up_count = 0;
    uint32_t total_checks = 0;
    
    // Test link status over the specified duration
    while (get_system_timestamp_ms() - start_time < test_duration_ms) {
        uint16_t netdiag = read_network_diagnostics_register(nic);
        result->network_diagnostics = netdiag;
        
        bool link_detected = false;
        
        // Media-specific link detection
        switch (media_type) {
            case MEDIA_TYPE_10BASE_T:
                // Check link beat detection for 10BaseT
                link_detected = (netdiag & 0x0800) != 0; // Link beat bit
                if (link_detected) {
                    result->test_flags |= LINK_TEST_RESULT_LINK_UP;
                }
                break;
                
            case MEDIA_TYPE_10BASE_2:
                // For 10Base2, we can't really detect link, but check for carrier
                link_detected = true; // Assume present if no errors
                result->test_flags |= LINK_TEST_RESULT_CARRIER_DETECT;
                break;
                
            case MEDIA_TYPE_AUI:
                // Check SQE test for AUI
                if (netdiag & 0x0200) { // SQE test bit
                    result->test_flags |= LINK_TEST_RESULT_SQE_TEST_PASSED;
                    link_detected = true;
                }
                break;
                
            case MEDIA_TYPE_10BASE_FL:
                // Fiber link detection
                link_detected = (netdiag & 0x0800) != 0;
                if (link_detected) {
                    result->test_flags |= LINK_TEST_RESULT_LINK_UP;
                }
                break;
                
            default:
                LOG_WARNING("Link test not implemented for media type %d", media_type);
                break;
        }
        
        if (link_detected) {
            link_up_count++;
        }
        
        total_checks++;
        cpu_opt_udelay(LINK_BEAT_CHECK_INTERVAL_MS * 1000);
    }
    
    result->test_duration_ms = get_system_timestamp_ms() - start_time;
    result->link_up_time_ms = (link_up_count * LINK_BEAT_CHECK_INTERVAL_MS);
    
    // Calculate signal quality based on link stability
    if (total_checks > 0) {
        result->signal_quality = (uint8_t)((link_up_count * 100) / total_checks);
    }
    
    // Determine if link is stable (>80% up time)
    if (result->signal_quality > 80) {
        result->test_flags |= LINK_TEST_RESULT_LINK_STABLE;
    }
    
    restore_window(nic, saved_window);
    
    LOG_DEBUG("Link test complete: quality=%d%%, up_time=%u ms", 
              result->signal_quality, result->link_up_time_ms);
    
    return (result->signal_quality > 50) ? SUCCESS : MEDIA_ERROR_NO_LINK;
}

/**
 * @brief Configure low-level media control registers
 */
int configure_media_registers(nic_info_t *nic, media_type_t media_type, bool enable_full_duplex) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    LOG_DEBUG("Configuring media registers for %s (full_duplex=%d)", 
              media_type_to_string(media_type), enable_full_duplex);

    // Ensure we're in Window 4
    int result = safe_select_window(nic, _3C509B_WINDOW_4, WINDOW_SELECT_TIMEOUT_MS);
    if (result != SUCCESS) {
        return result;
    }

    return configure_media_specific_registers(nic, media_type, enable_full_duplex);
}

/**
 * @brief Validate media selection against NIC capabilities and current state
 */
int validate_media_selection(nic_info_t *nic, media_type_t media_type, 
                           char *error_msg, size_t error_msg_size) {
    if (!nic) {
        if (error_msg && error_msg_size > 0) {
            strncpy(error_msg, "Invalid NIC pointer", error_msg_size - 1);
            error_msg[error_msg_size - 1] = '\0';
        }
        return ERROR_INVALID_PARAM;
    }

    // Check if media type is valid
    if (media_type == MEDIA_TYPE_UNKNOWN) {
        if (error_msg && error_msg_size > 0) {
            strncpy(error_msg, "Unknown media type", error_msg_size - 1);
            error_msg[error_msg_size - 1] = '\0';
        }
        return MEDIA_ERROR_INVALID_MEDIA;
    }

    // Check if media is supported by this NIC
    if (!is_media_supported_by_nic(nic, media_type)) {
        if (error_msg && error_msg_size > 0) {
            snprintf(error_msg, error_msg_size, "Media %s not supported by this NIC variant", 
                    media_type_to_string(media_type));
        }
        return MEDIA_ERROR_MEDIA_NOT_SUPPORTED;
    }

    // Additional validation based on NIC state
    if (nic->status & NIC_STATUS_ERROR) {
        if (error_msg && error_msg_size > 0) {
            strncpy(error_msg, "NIC is in error state", error_msg_size - 1);
            error_msg[error_msg_size - 1] = '\0';
        }
        return MEDIA_ERROR_VALIDATION_FAILED;
    }

    LOG_DEBUG("Media validation passed for %s", media_type_to_string(media_type));
    return SUCCESS;
}

// --- Advanced Media Control Functions ---

/**
 * @brief Initialize media control subsystem for a NIC
 */
int media_control_init(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    LOG_DEBUG("Initializing media control for NIC type %d", nic->type);

    // Initialize media-related fields in NIC structure
    nic->current_media = MEDIA_TYPE_UNKNOWN;
    nic->detected_media = MEDIA_TYPE_UNKNOWN;
    nic->media_detection_state = MEDIA_DETECT_NONE;

    // Set default media capabilities based on NIC type
    if (nic->type == NIC_TYPE_3C509B) {
        // Determine capabilities based on product ID or variant
        // For now, assume combo capabilities
        nic->media_capabilities = MEDIA_CAPS_3C509B_COMBO;
    }

    LOG_INFO("Media control initialized for NIC");
    return SUCCESS;
}

/**
 * @brief Cleanup media control subsystem
 */
int media_control_cleanup(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    LOG_DEBUG("Cleaning up media control");

    // Reset media state
    nic->current_media = MEDIA_TYPE_UNKNOWN;
    nic->detected_media = MEDIA_TYPE_UNKNOWN;
    nic->media_detection_state = MEDIA_DETECT_NONE;

    return SUCCESS;
}

/**
 * @brief Get current media configuration state
 */
int get_media_config_state(nic_info_t *nic, media_config_state_t *state) {
    if (!nic || !state) {
        return ERROR_INVALID_PARAM;
    }

    cpu_opt_memzero(state, sizeof(media_config_state_t));
    
    state->current_media = nic->current_media;
    state->detected_media = nic->detected_media;
    state->detection_state = nic->media_detection_state;
    state->last_config_time = get_system_timestamp_ms();

    // Read current media control register if possible
    uint8_t saved_window;
    if (save_and_select_window(nic, _3C509B_WINDOW_4, &saved_window) == SUCCESS) {
        state->media_control_register = read_media_control_register(nic);
        restore_window(nic, saved_window);
    }

    return SUCCESS;
}

// --- Internal Helper Function Implementations ---

/**
 * @brief Select window with internal safety checks
 */
static int select_window_safe(nic_info_t *nic, uint8_t window) {
    return safe_select_window(nic, window, WINDOW_SELECT_TIMEOUT_MS);
}

/**
 * @brief Wait for command completion with timeout
 */
static int wait_for_cmd_completion(nic_info_t *nic, uint32_t timeout_ms) {
    return wait_for_command_ready(nic, timeout_ms);
}

/**
 * @brief Read media control register (Window 4)
 */
static uint16_t read_media_control_register(nic_info_t *nic) {
    return cpu_opt_inw(nic->io_base + _3C509B_MEDIA_CTRL);
}

/**
 * @brief Write media control register (Window 4)
 */
static int write_media_control_register(nic_info_t *nic, uint16_t value) {
    cpu_opt_outw(nic->io_base + _3C509B_MEDIA_CTRL, value);
    return SUCCESS;
}

/**
 * @brief Read network diagnostics register (Window 4)
 */
static uint16_t read_network_diagnostics_register(nic_info_t *nic) {
    return cpu_opt_inw(nic->io_base + _3C509B_W4_NETDIAG);
}

/**
 * @brief Configure media-specific registers for different media types
 */
static int configure_media_specific_registers(nic_info_t *nic, media_type_t media_type, bool full_duplex) {
    uint16_t media_ctrl_value = 0;
    
    switch (media_type) {
        case MEDIA_TYPE_10BASE_T:
            media_ctrl_value = _3C509B_XCVR_10BASE_T;
            if (full_duplex && (nic->media_capabilities & MEDIA_CAP_FULL_DUPLEX)) {
                media_ctrl_value |= _3C509B_FD_ENABLE;
            }
            break;
            
        case MEDIA_TYPE_10BASE_2:
            media_ctrl_value = _3C509B_XCVR_10BASE2;
            // Start coax transceiver with CPU-optimized I/O
            cpu_opt_outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_START_COAX);
            wait_for_cmd_completion(nic, 1000);
            break;
            
        case MEDIA_TYPE_AUI:
            media_ctrl_value = _3C509B_XCVR_AUI_EXT;
            break;
            
        case MEDIA_TYPE_10BASE_FL:
            media_ctrl_value = _3C509B_XCVR_10BASE_T; // Similar to 10BaseT for fiber
            if (full_duplex) {
                media_ctrl_value |= _3C509B_FD_ENABLE;
            }
            break;
            
        default:
            LOG_ERROR("Unsupported media type: %d", media_type);
            return MEDIA_ERROR_MEDIA_NOT_SUPPORTED;
    }
    
    // Write the media control value
    int result = write_media_control_register(nic, media_ctrl_value);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to write media control register");
        return MEDIA_ERROR_REGISTER_ACCESS;
    }
    
    LOG_DEBUG("Configured media registers: 0x%04X", media_ctrl_value);
    return SUCCESS;
}

/**
 * @brief Detect best media for this NIC variant
 */
static media_type_t detect_best_media_for_variant(nic_info_t *nic, const media_detect_config_t *config) {
    // Define media detection priority order
    media_type_t test_order[] = {
        MEDIA_TYPE_10BASE_T,    // Try 10BaseT first (most common)
        MEDIA_TYPE_AUI,         // Then AUI
        MEDIA_TYPE_10BASE_2     // Finally 10Base2
    };
    
    size_t num_media_types = sizeof(test_order) / sizeof(test_order[0]);
    
    for (size_t i = 0; i < num_media_types; i++) {
        media_type_t media = test_order[i];
        
        // Check if this media is supported
        if (!is_media_supported_by_nic(nic, media)) {
            continue;
        }
        
        LOG_DEBUG("Testing media: %s", media_type_to_string(media));
        
        link_test_result_t test_result;
        int result = test_link_beat(nic, media, config->test_duration_ms, &test_result);
        
        if (result == SUCCESS && test_result.signal_quality > 70) {
            LOG_INFO("Detected working media: %s (quality: %d%%)", 
                    media_type_to_string(media), test_result.signal_quality);
            return media;
        }
    }
    
    LOG_WARNING("No working media detected");
    return MEDIA_TYPE_UNKNOWN;
}

// --- Utility Function Implementations ---

/**
 * @brief Check if media type is supported by this NIC variant
 */
int is_media_supported_by_nic(nic_info_t *nic, media_type_t media_type) {
    if (!nic) {
        return 0;
    }

    switch (media_type) {
        case MEDIA_TYPE_10BASE_T:
            return (nic->media_capabilities & MEDIA_CAP_10BASE_T) != 0;
        case MEDIA_TYPE_10BASE_2:
            return (nic->media_capabilities & MEDIA_CAP_10BASE_2) != 0;
        case MEDIA_TYPE_AUI:
            return (nic->media_capabilities & MEDIA_CAP_AUI) != 0;
        case MEDIA_TYPE_10BASE_FL:
            return (nic->media_capabilities & MEDIA_CAP_10BASE_FL) != 0;
        default:
            return 0;
    }
}

/**
 * @brief Get the default media type for this NIC variant
 */
media_type_t get_default_media_for_nic(nic_info_t *nic) {
    if (!nic) {
        return MEDIA_TYPE_UNKNOWN;
    }

    // Return the first supported media type based on priority
    if (nic->media_capabilities & MEDIA_CAP_10BASE_T) {
        return MEDIA_TYPE_10BASE_T;
    }
    if (nic->media_capabilities & MEDIA_CAP_AUI) {
        return MEDIA_TYPE_AUI;
    }
    if (nic->media_capabilities & MEDIA_CAP_10BASE_2) {
        return MEDIA_TYPE_10BASE_2;
    }
    
    return MEDIA_TYPE_UNKNOWN;
}

/**
 * @brief Convert media control error code to string
 */
const char* media_error_to_string(int error_code) {
    switch (error_code) {
        case MEDIA_ERROR_NONE:                  return "No error";
        case MEDIA_ERROR_INVALID_MEDIA:         return "Invalid media type";
        case MEDIA_ERROR_MEDIA_NOT_SUPPORTED:   return "Media not supported";
        case MEDIA_ERROR_NO_LINK:               return "No link detected";
        case MEDIA_ERROR_LINK_TEST_FAILED:      return "Link test failed";
        case MEDIA_ERROR_AUTO_DETECT_FAILED:    return "Auto-detection failed";
        case MEDIA_ERROR_REGISTER_ACCESS:       return "Register access failed";
        case MEDIA_ERROR_WINDOW_TIMEOUT:        return "Window selection timeout";
        case MEDIA_ERROR_TRANSCEIVER_FAULT:     return "Transceiver fault";
        case MEDIA_ERROR_MEDIA_CONFLICT:        return "Media configuration conflict";
        case MEDIA_ERROR_VALIDATION_FAILED:     return "Media validation failed";
        default:                                return "Unknown media error";
    }
}

/**
 * @brief Get media priority for auto-detection ordering
 */
uint8_t get_media_detection_priority(media_type_t media_type, uint8_t nic_variant) {
    // Lower number = higher priority
    switch (media_type) {
        case MEDIA_TYPE_10BASE_T:   return 1;  // Highest priority (most common)
        case MEDIA_TYPE_AUI:        return 2;  // Second priority
        case MEDIA_TYPE_10BASE_2:   return 3;  // Third priority
        case MEDIA_TYPE_10BASE_FL:  return 4;  // Lowest priority (rare)
        default:                    return 255; // No priority
    }
}

/**
 * @brief Check if link is currently up for the selected media
 */
int check_media_link_status(nic_info_t *nic) {
    if (!nic) {
        return -1;
    }

    uint8_t saved_window;
    int result = save_and_select_window(nic, _3C509B_WINDOW_4, &saved_window);
    if (result != SUCCESS) {
        return -1;
    }

    uint16_t netdiag = read_network_diagnostics_register(nic);
    restore_window(nic, saved_window);

    // Check link based on current media type
    switch (nic->current_media) {
        case MEDIA_TYPE_10BASE_T:
        case MEDIA_TYPE_10BASE_FL:
            return (netdiag & 0x0800) ? 1 : 0; // Link beat detection
        case MEDIA_TYPE_AUI:
            return (netdiag & 0x0200) ? 1 : 0; // SQE test
        case MEDIA_TYPE_10BASE_2:
            return 1; // Assume link for coax (no reliable detection)
        default:
            return 0;
    }
}

/**
 * @brief Run comprehensive media diagnostics
 */
int run_media_diagnostics(nic_info_t *nic, bool test_all_media) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    LOG_INFO("Running media diagnostics (test_all=%d)", test_all_media);

    int errors = 0;

    if (test_all_media) {
        // Test all supported media types
        media_type_t test_media[] = {
            MEDIA_TYPE_10BASE_T, MEDIA_TYPE_10BASE_2, 
            MEDIA_TYPE_AUI, MEDIA_TYPE_10BASE_FL
        };
        
        for (size_t i = 0; i < sizeof(test_media) / sizeof(test_media[0]); i++) {
            if (is_media_supported_by_nic(nic, test_media[i])) {
                link_test_result_t result;
                int test_result = test_link_beat(nic, test_media[i], 1000, &result);
                
                LOG_INFO("Media %s test: %s (quality: %d%%)", 
                        media_type_to_string(test_media[i]),
                        (test_result == SUCCESS) ? "PASS" : "FAIL",
                        result.signal_quality);
                
                if (test_result != SUCCESS) {
                    errors++;
                }
            }
        }
    } else {
        // Test only current media
        if (nic->current_media != MEDIA_TYPE_UNKNOWN) {
            link_test_result_t result;
            int test_result = test_link_beat(nic, nic->current_media, 2000, &result);
            
            LOG_INFO("Current media %s test: %s (quality: %d%%)", 
                    media_type_to_string(nic->current_media),
                    (test_result == SUCCESS) ? "PASS" : "FAIL",
                    result.signal_quality);
            
            if (test_result != SUCCESS) {
                errors++;
            }
        } else {
            LOG_WARNING("No current media configured for testing");
            errors++;
        }
    }

    LOG_INFO("Media diagnostics complete: %d errors", errors);
    return (errors == 0) ? SUCCESS : ERROR_HARDWARE;
}

// --- Additional Media-Specific Configuration Functions ---

/**
 * @brief Configure 10BaseT media with link beat detection
 */
int configure_10baset_media(nic_info_t *nic, bool enable_full_duplex) {
    return configure_media_registers(nic, MEDIA_TYPE_10BASE_T, enable_full_duplex);
}

/**
 * @brief Configure 10Base2 coaxial media
 */
int configure_10base2_media(nic_info_t *nic) {
    return configure_media_registers(nic, MEDIA_TYPE_10BASE_2, false);
}

/**
 * @brief Configure AUI media with SQE test
 */
int configure_aui_media(nic_info_t *nic, bool enable_sqe_test) {
    // SQE test is enabled by default in our implementation
    return configure_media_registers(nic, MEDIA_TYPE_AUI, false);
}

/**
 * @brief Configure fiber optic media
 */
int configure_fiber_media(nic_info_t *nic, bool enable_full_duplex) {
    return configure_media_registers(nic, MEDIA_TYPE_10BASE_FL, enable_full_duplex);
}

/**
 * @brief Force media selection without auto-detection
 */
int force_media_selection(nic_info_t *nic, media_type_t media_type) {
    return select_media_transceiver(nic, media_type, MEDIA_CTRL_FLAG_FORCE);
}

/**
 * @brief Reset media configuration to default state
 */
int reset_media_configuration(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    LOG_INFO("Resetting media configuration to defaults");

    // Reset media state
    nic->current_media = MEDIA_TYPE_UNKNOWN;
    nic->detected_media = MEDIA_TYPE_UNKNOWN;
    nic->media_detection_state = MEDIA_DETECT_NONE;
    
    // Get default media for this NIC
    media_type_t default_media = get_default_media_for_nic(nic);
    if (default_media != MEDIA_TYPE_UNKNOWN) {
        return select_media_transceiver(nic, default_media, 0);
    }
    
    return SUCCESS;
}

/**
 * @brief Monitor link status changes over time
 */
int monitor_link_changes(nic_info_t *nic, uint32_t monitor_duration_ms, 
                        void (*callback)(nic_info_t *nic, bool link_up)) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    LOG_DEBUG("Monitoring link changes for %u ms", monitor_duration_ms);

    uint32_t start_time = get_system_timestamp_ms();
    int last_link_status = check_media_link_status(nic);
    int change_count = 0;
    
    while (get_system_timestamp_ms() - start_time < monitor_duration_ms) {
        int current_link_status = check_media_link_status(nic);
        
        if (current_link_status != last_link_status && current_link_status >= 0) {
            change_count++;
            LOG_DEBUG("Link status changed: %s", 
                     current_link_status ? "UP" : "DOWN");
            
            if (callback) {
                callback(nic, current_link_status ? true : false);
            }
            
            last_link_status = current_link_status;
        }
        
        mdelay(100); // Check every 100ms
    }
    
    LOG_DEBUG("Link monitoring complete: %d changes detected", change_count);
    return change_count;
}

/**
 * @brief Test signal quality for current media
 */
int test_signal_quality(nic_info_t *nic, uint8_t *quality) {
    if (!nic || !quality) {
        return ERROR_INVALID_PARAM;
    }

    link_test_result_t test_result;
    int result = test_link_beat(nic, nic->current_media, 1000, &test_result);
    
    if (result == SUCCESS) {
        *quality = test_result.signal_quality;
    } else {
        *quality = 0;
    }
    
    return result;
}

/**
 * @brief Dump current media control register values
 */
int dump_media_registers(nic_info_t *nic, char *buffer, size_t buffer_size) {
    if (!nic || !buffer || buffer_size == 0) {
        return ERROR_INVALID_PARAM;
    }

    uint8_t saved_window;
    int result = save_and_select_window(nic, _3C509B_WINDOW_4, &saved_window);
    if (result != SUCCESS) {
        return result;
    }

    uint16_t media_ctrl = read_media_control_register(nic);
    uint16_t net_diag = read_network_diagnostics_register(nic);
    
    restore_window(nic, saved_window);

    int chars_written = snprintf(buffer, buffer_size,
        "Media Control Registers:\n"
        "  Media Control: 0x%04X\n"
        "  Net Diagnostics: 0x%04X\n"
        "  Current Media: %s\n"
        "  Detected Media: %s\n"
        "  Detection State: 0x%02X\n",
        media_ctrl, net_diag,
        media_type_to_string(nic->current_media),
        media_type_to_string(nic->detected_media),
        nic->media_detection_state);

    return chars_written;
}

/**
 * @brief Get detailed media information string
 */
int get_media_info_string(nic_info_t *nic, char *buffer, size_t buffer_size) {
    if (!nic || !buffer || buffer_size == 0) {
        return ERROR_INVALID_PARAM;
    }

    const char* config_source_str = "Unknown";
    switch (nic->media_config_source) {
        case MEDIA_CONFIG_DEFAULT:      config_source_str = "Default"; break;
        case MEDIA_CONFIG_EEPROM:       config_source_str = "EEPROM"; break;
        case MEDIA_CONFIG_AUTO_DETECT:  config_source_str = "Auto-Detect"; break;
        case MEDIA_CONFIG_USER_FORCED:  config_source_str = "User-Forced"; break;
        case MEDIA_CONFIG_DRIVER_FORCED: config_source_str = "Driver-Forced"; break;
    }

    int link_status = check_media_link_status(nic);
    
    int chars_written = snprintf(buffer, buffer_size,
        "Media: %s | Link: %s | Source: %s | Caps: 0x%04X",
        media_type_to_string(nic->current_media),
        (link_status > 0) ? "UP" : (link_status == 0) ? "DOWN" : "ERROR",
        config_source_str,
        nic->media_capabilities);

    return chars_written;
}