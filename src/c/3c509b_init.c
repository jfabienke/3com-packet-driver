/**
 * @file 3c509b_init.c
 * @brief 3Com 3C509B NIC driver - Initialization functions (OVERLAY segment)
 *
 * This file contains only the initialization functions that are called once
 * and can be discarded after init:
 * - NIC initialization
 * - EEPROM access
 * - Media setup
 * - Cache coherency initialization
 *
 * Runtime functions are in 3c509b_rt.c (ROOT segment)
 *
 * Updated: 2026-01-28 05:00:00 UTC
 */

#include "3c509b.h"
#include "hardware.h"
#include "logging.h"
#include "memory.h"
#include "common.h"
#include "medictl.h"
#include "nic_defs.h"
#include "hwchksm.h"
#include "dirpioe.h"
#include "cachecoh.h"
#include "cachemgt.h"
#include "chipdet.h"
#include <string.h>

/* ============================================================================
 * External declarations for runtime functions (in 3c509b_rt.c)
 * ============================================================================ */

/* Register access functions from runtime file */
extern uint16_t _3c509b_read_reg(nic_info_t *nic, uint16_t reg);
extern void _3c509b_write_reg(nic_info_t *nic, uint16_t reg, uint16_t value);
extern void _3c509b_select_window(nic_info_t *nic, uint8_t window);
extern int _3c509b_wait_for_cmd_busy(nic_info_t *nic, uint32_t timeout_ms);
extern void _3c509b_write_command(nic_info_t *nic, uint16_t command);

/* Runtime function declarations for vtable */
extern int _3c509b_send_packet(nic_info_t *nic, const uint8_t *packet, size_t length);
extern int _3c509b_receive_packet(nic_info_t *nic, uint8_t *buffer, size_t *length);
extern int _3c509b_send_packet_direct_pio(nic_info_t *nic, const uint8_t *packet, size_t length);
extern int _3c509b_check_interrupt(nic_info_t *nic);
extern void _3c509b_handle_interrupt(nic_info_t *nic);
extern int _3c509b_enable_interrupts(nic_info_t *nic);
extern int _3c509b_disable_interrupts(nic_info_t *nic);
extern int _3c509b_get_link_status(nic_info_t *nic);
extern int _3c509b_get_link_speed(nic_info_t *nic);
extern int _3c509b_set_promiscuous(nic_info_t *nic, bool enable);
extern int _3c509b_set_multicast(nic_info_t *nic, const uint8_t *mc_list, int count);

/* ============================================================================
 * Forward declarations for init functions
 * ============================================================================ */

int _3c509b_init(nic_info_t *nic);
int _3c509b_cleanup(nic_info_t *nic);
int _3c509b_reset(nic_info_t *nic);
int _3c509b_configure(nic_info_t *nic, const void *config);
int _3c509b_self_test(nic_info_t *nic);

/* Internal init-only helper functions */
static uint16_t _3c509b_read_eeprom(nic_info_t *nic, uint8_t address);
static void _3c509b_write_eeprom(nic_info_t *nic, uint8_t address, uint16_t data);
static int _3c509b_read_mac_from_eeprom(nic_info_t *nic, uint8_t *mac);
static int _3c509b_setup_media(nic_info_t *nic);
static int _3c509b_setup_rx_filter(nic_info_t *nic);
static int _3c509b_initialize_cache_coherency(nic_info_t *nic);

/* ============================================================================
 * Operations VTable (remains in ROOT segment for runtime access)
 * Note: This is defined here but will be placed in ROOT via linker
 * ============================================================================ */

/* 3C509B operations vtable - uses direct PIO for send */
static nic_ops_t _3c509b_ops = {
    .init               = _3c509b_init,
    .cleanup            = _3c509b_cleanup,
    .reset              = _3c509b_reset,
    .configure          = (int (*)(struct nic_info *, const void *))_3c509b_configure,
    .send_packet        = _3c509b_send_packet_direct_pio,
    .receive_packet     = _3c509b_receive_packet,
    .check_interrupt    = _3c509b_check_interrupt,
    .handle_interrupt   = _3c509b_handle_interrupt,
    .enable_interrupts  = _3c509b_enable_interrupts,
    .disable_interrupts = _3c509b_disable_interrupts,
    .get_link_status    = _3c509b_get_link_status,
    .get_link_speed     = _3c509b_get_link_speed,
    .set_promiscuous    = _3c509b_set_promiscuous,
    .set_multicast      = _3c509b_set_multicast,
    .self_test          = _3c509b_self_test
};

/* Public interface functions */
nic_ops_t* get_3c509b_ops(void) {
    return &_3c509b_ops;
}

/* ============================================================================
 * Initialization Functions (OVERLAY - discarded after init)
 * ============================================================================ */

int _3c509b_init(nic_info_t *nic) {
    int result;

    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    LOG_DEBUG("Initializing 3C509B at I/O 0x%X", nic->io_base);

    /* Reset the NIC first */
    result = _3c509b_reset(nic);
    if (result != SUCCESS) {
        LOG_ERROR("3C509B reset failed: %d", result);
        return result;
    }

    /* Read MAC address from EEPROM */
    result = _3c509b_read_mac_from_eeprom(nic, nic->mac);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to read MAC address from EEPROM: %d", result);
        return result;
    }

    /* Copy to permanent MAC */
    memcpy(nic->perm_mac, nic->mac, ETH_ALEN);

    /* Setup media and transceiver */
    result = _3c509b_setup_media(nic);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to setup media: %d", result);
        return result;
    }

    /* Setup RX filter */
    result = _3c509b_setup_rx_filter(nic);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to setup RX filter: %d", result);
        return result;
    }

    /* Select Window 1 for operations */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);

    /* Set up interrupt mask */
    _3c509b_write_command(nic, _3C509B_CMD_SET_INTR_ENABLE |
                         (_3C509B_IMASK_TX_COMPLETE | _3C509B_IMASK_RX_COMPLETE | _3C509B_IMASK_ADAPTER_FAILURE));

    /* Enable RX and TX */
    _3c509b_write_command(nic, _3C509B_CMD_RX_ENABLE);
    result = _3c509b_wait_for_cmd_busy(nic, 1000);
    if (result != SUCCESS) {
        LOG_ERROR("RX enable command timeout");
        return result;
    }

    _3c509b_write_command(nic, _3C509B_CMD_TX_ENABLE);
    result = _3c509b_wait_for_cmd_busy(nic, 1000);
    if (result != SUCCESS) {
        LOG_ERROR("TX enable command timeout");
        return result;
    }

    /* Set initial link status */
    nic->link_up = _3c509b_get_link_status(nic);
    nic->speed = _3c509b_get_link_speed(nic);

    /* Initialize CPU detection for enhanced PIO operations (Phase 1) */
    direct_pio_init_cpu_detection();
    LOG_DEBUG("CPU-optimized PIO initialized: level %d, 32-bit support: %s",
              direct_pio_get_optimization_level(),
              direct_pio_get_cpu_support_info() ? "Yes" : "No");

    /* Initialize hardware checksumming with CPU-aware optimization */
    result = hw_checksum_init(CHECKSUM_MODE_AUTO);
    if (result != SUCCESS) {
        LOG_WARNING("Hardware checksum initialization failed: %d, continuing without optimization", result);
        /* Continue - checksum is optional feature */
    } else {
        LOG_DEBUG("Hardware checksum module initialized with CPU optimization");
    }

    /* Initialize PIO cache coherency for speculative read protection (Sprint 4B) */
    result = _3c509b_initialize_cache_coherency(nic);
    if (result != SUCCESS) {
        LOG_WARNING("Cache coherency init failed: %d, continuing without speculative protection", result);
        /* Non-fatal - continue without speculative protection */
        nic->pio_cache_initialized = 1;
        nic->pio_speculative_protection = 0;
    }

    LOG_INFO("3C509B initialized successfully, link %s, speed %d Mbps",
             nic->link_up ? "UP" : "DOWN", nic->speed);

    return SUCCESS;
}

int _3c509b_cleanup(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    LOG_DEBUG("Cleaning up 3C509B at I/O 0x%X", nic->io_base);

    /* Disable interrupts */
    _3c509b_disable_interrupts(nic);

    /* Disable TX and RX */
    _3c509b_write_command(nic, _3C509B_CMD_RX_DISABLE);
    _3c509b_wait_for_cmd_busy(nic, 500);

    _3c509b_write_command(nic, _3C509B_CMD_TX_DISABLE);
    _3c509b_wait_for_cmd_busy(nic, 500);

    /* Cleanup media control subsystem */
    media_control_cleanup(nic);

    return SUCCESS;
}

int _3c509b_reset(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    LOG_DEBUG("Resetting 3C509B at I/O 0x%X", nic->io_base);

    /* Issue global reset command */
    _3c509b_write_command(nic, _3C509B_CMD_GLOBAL_RESET);

    /* Wait for reset to complete - hardware requires 1ms */
    mdelay(1);

    /* Wait for the NIC to become ready */
    return _3c509b_wait_for_cmd_busy(nic, 5000); /* 5 second timeout */
}

int _3c509b_configure(nic_info_t *nic, const void *config) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    /* Basic configuration - media setup and RX filter already done in init */
    LOG_DEBUG("Configuring 3C509B");

    /* Configuration can include speed/duplex settings, but 3C509B is 10Mbps half-duplex only */
    nic->speed = 10;
    nic->full_duplex = false;
    nic->mtu = _3C509B_MAX_MTU;

    return SUCCESS;
}

int _3c509b_self_test(nic_info_t *nic) {
    uint16_t original_value;
    uint16_t test_value;

    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    LOG_DEBUG("Running 3C509B self-test");

    /* Check if registers are accessible */

    /* Select Window 0 for configuration register access */
    _3c509b_select_window(nic, _3C509B_WINDOW_0);

    original_value = _3c509b_read_reg(nic, _3C509B_W0_CONFIG_CTRL);
    _3c509b_write_reg(nic, _3C509B_W0_CONFIG_CTRL, 0x5AA5);
    test_value = _3c509b_read_reg(nic, _3C509B_W0_CONFIG_CTRL);
    _3c509b_write_reg(nic, _3C509B_W0_CONFIG_CTRL, original_value);

    if (test_value != 0x5AA5) {
        LOG_ERROR("3C509B register test failed: wrote 0x5AA5, read 0x%X", test_value);
        return ERROR_HARDWARE;
    }

    LOG_INFO("3C509B self-test passed");

    return SUCCESS;
}

/* ============================================================================
 * EEPROM Access Functions (Init only)
 * ============================================================================ */

/**
 * Read from EEPROM
 */
static uint16_t _3c509b_read_eeprom(nic_info_t *nic, uint8_t address) {
    /* Select Window 0 for EEPROM access */
    _3c509b_select_window(nic, _3C509B_WINDOW_0);

    /* Write EEPROM read command */
    _3c509b_write_reg(nic, _3C509B_EEPROM_CMD, _3C509B_EEPROM_READ | address);

    /* Wait for EEPROM read to complete */
    udelay(_3C509B_EEPROM_READ_DELAY);

    /* Read the data */
    return _3c509b_read_reg(nic, _3C509B_EEPROM_DATA);
}

/**
 * Write to EEPROM (typically not used in driver operation)
 */
static void _3c509b_write_eeprom(nic_info_t *nic, uint8_t address, uint16_t data) {
    /* Select Window 0 for EEPROM access */
    _3c509b_select_window(nic, _3C509B_WINDOW_0);

    /* Write the data first */
    _3c509b_write_reg(nic, _3C509B_EEPROM_DATA, data);

    /* Write EEPROM write command */
    _3c509b_write_reg(nic, _3C509B_EEPROM_CMD, _3C509B_EEPROM_WRITE | address);

    /* Wait for EEPROM write to complete */
    udelay(_3C509B_EEPROM_READ_DELAY * 10); /* Write takes longer */
}

/**
 * Read MAC address from EEPROM
 */
static int _3c509b_read_mac_from_eeprom(nic_info_t *nic, uint8_t *mac) {
    int i;

    if (!nic || !mac) {
        return ERROR_INVALID_PARAM;
    }

    /* MAC address is stored in EEPROM words 0, 1, 2 */
    for (i = 0; i < 3; i++) {
        uint16_t word = _3c509b_read_eeprom(nic, i);
        mac[i * 2] = word & 0xFF;
        mac[i * 2 + 1] = (word >> 8) & 0xFF;
    }

    LOG_INFO("3C509B MAC address: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return SUCCESS;
}

/* ============================================================================
 * Media Setup Functions (Init only)
 * ============================================================================ */

/**
 * Setup media type and transceiver using enhanced media control
 */
static int _3c509b_setup_media(nic_info_t *nic) {
    int result;
    media_detect_config_t detect_config;
    media_type_t detected;
    link_test_result_t test_result;

    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    LOG_DEBUG("Setting up media for 3C509B using enhanced media control");

    /* Initialize media control subsystem */
    result = media_control_init(nic);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to initialize media control: %d", result);
        return result;
    }

    /* Initialize NIC variant information and capabilities */
    NIC_INFO_INIT_DEFAULTS(nic);

    /* Set default capabilities for 3C509B family */
    nic->media_capabilities = MEDIA_CAPS_3C509B_COMBO;
    nic->variant_id = VARIANT_3C509B_COMBO; /* Default to combo variant */

    /* Try to detect media automatically if this is a combo card */
    if (nic->media_capabilities & MEDIA_CAP_AUTO_SELECT) {
        LOG_INFO("Attempting auto-detection for combo card");

        /* Initialize detection config manually (C89 compatible) */
        detect_config.flags = 0;
        detect_config.timeout_ms = MEDIA_DETECT_TIMEOUT_MS;
        detect_config.retry_count = AUTO_DETECT_RETRY_COUNT;
        detect_config.test_duration_ms = MEDIA_TEST_DURATION_10BASET_MS;
        detect_config.preferred_media = MEDIA_TYPE_UNKNOWN;
        detect_config.media_priority_mask = 0xFFFF;
        detected = auto_detect_media(nic, &detect_config);

        if (detected != MEDIA_TYPE_UNKNOWN) {
            LOG_INFO("Auto-detected media: %s", media_type_to_string(detected));
            nic->current_media = detected;
            nic->media_config_source = MEDIA_CONFIG_AUTO_DETECT;
        } else {
            LOG_WARNING("Auto-detection failed, using default media");
            nic->current_media = MEDIA_TYPE_10BASE_T;
            nic->media_config_source = MEDIA_CONFIG_DEFAULT;
        }
    } else {
        /* For non-combo cards, use the default supported media */
        nic->current_media = get_default_media_for_nic(nic);
        nic->media_config_source = MEDIA_CONFIG_DEFAULT;
        LOG_INFO("Using default media: %s", media_type_to_string(nic->current_media));
    }

    /* Configure the selected media */
    if (nic->current_media != MEDIA_TYPE_UNKNOWN) {
        result = select_media_transceiver(nic, nic->current_media, 0);
        if (result != SUCCESS) {
            LOG_ERROR("Failed to configure media %s: %d",
                     media_type_to_string(nic->current_media), result);

            /* Fallback to 10BaseT if auto-detection failed */
            if (nic->current_media != MEDIA_TYPE_10BASE_T &&
                is_media_supported_by_nic(nic, MEDIA_TYPE_10BASE_T)) {
                LOG_INFO("Falling back to 10BaseT");
                result = select_media_transceiver(nic, MEDIA_TYPE_10BASE_T,
                                                MEDIA_CTRL_FLAG_FORCE);
                if (result == SUCCESS) {
                    nic->current_media = MEDIA_TYPE_10BASE_T;
                    nic->media_config_source = MEDIA_CONFIG_DRIVER_FORCED;
                }
            }
        }
    }

    if (result != SUCCESS) {
        LOG_ERROR("Media setup failed completely");
        return result;
    }

    /* Test the configured media */
    result = test_link_beat(nic, nic->current_media, 2000, &test_result);
    if (result == SUCCESS) {
        LOG_INFO("Media link test passed: quality=%d%%", test_result.signal_quality);
        nic->media_detection_state |= MEDIA_DETECT_COMPLETED;
    } else {
        LOG_WARNING("Media link test failed, but continuing");
        nic->media_detection_state |= MEDIA_DETECT_FAILED;
    }

    LOG_INFO("3C509B media setup complete: %s", media_type_to_string(nic->current_media));
    return SUCCESS;
}

/**
 * Setup RX filter for normal operation
 */
static int _3c509b_setup_rx_filter(nic_info_t *nic) {
    uint16_t filter;

    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    /* Select Window 1 for RX filter */
    _3c509b_select_window(nic, _3C509B_WINDOW_1);

    /* Set basic RX filter: station address + broadcast */
    filter = _3C509B_RX_FILTER_STATION | _3C509B_RX_FILTER_BROADCAST;
    _3c509b_write_command(nic, _3C509B_CMD_SET_RX_FILTER | filter);

    /* Wait for command to complete */
    _3c509b_wait_for_cmd_busy(nic, 1000);

    /* Select Window 2 to program station address */
    _3c509b_select_window(nic, _3C509B_WINDOW_2);

    /* Write MAC address to station address registers */
    {
        int i;
        for (i = 0; i < ETH_ALEN; i++) {
            _3c509b_write_reg(nic, i, nic->mac[i]);
        }
    }

    LOG_DEBUG("3C509B RX filter and station address configured");

    return SUCCESS;
}

/* ============================================================================
 * Cache Coherency Initialization (Init only)
 * ============================================================================ */

/**
 * @brief Initialize cache coherency management for 3C509B PIO operations
 * @note Even PIO-only cards need protection against speculative read pollution
 *       on modern CPUs (Pentium 4+) where the prefetcher can load stale data
 *       into cache before PIO transfers complete.
 */
static int _3c509b_initialize_cache_coherency(nic_info_t *nic) {
    const cpu_info_t *cpu;
    coherency_analysis_t analysis;

    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    /* Get CPU information for cache capability detection */
    cpu = cpu_get_info();
    if (!cpu) {
        LOG_WARNING("CPU detection failed, disabling speculative protection");
        nic->pio_cache_tier = CACHE_TIER_4_FALLBACK;
        nic->pio_cache_confidence = 50;
        nic->pio_cache_initialized = 1;
        nic->pio_speculative_protection = 0;
        return SUCCESS;
    }

    /* Check if CPU has internal cache (486+) */
    if (!(cpu->features & CPU_FEATURE_CACHE)) {
        /* 286/386: No internal cache, no protection needed */
        LOG_INFO("3C509B PIO: No internal cache (CPU family < 4), no speculative protection needed");
        nic->pio_cache_tier = CACHE_TIER_4_FALLBACK;
        nic->pio_cache_confidence = 100;
        nic->pio_cache_initialized = 1;
        nic->pio_speculative_protection = 0;
        return SUCCESS;
    }

    /* 486+ CPU with cache: Run coherency analysis for tier selection */
    analysis = perform_complete_coherency_analysis();

    nic->pio_cache_tier = (uint8_t)analysis.selected_tier;
    nic->pio_cache_confidence = analysis.confidence;
    nic->pio_cache_initialized = 1;

    /* Enable speculative protection for CPUs with cache */
    nic->pio_speculative_protection = 1;

    LOG_INFO("3C509B PIO cache coherency initialized: tier %d, confidence %d%%",
             nic->pio_cache_tier, nic->pio_cache_confidence);
    LOG_INFO("  Speculative read protection: %s",
             nic->pio_speculative_protection ? "ENABLED" : "DISABLED");

    /* Log tier-specific information */
    if (cpu->has_clflush) {
        LOG_DEBUG("  CLFLUSH available: surgical cache line invalidation");
    } else if (cpu->has_wbinvd && cpu->can_wbinvd) {
        LOG_DEBUG("  WBINVD available: full cache flush/invalidate");
    } else {
        LOG_DEBUG("  Using software cache barriers");
    }

    return SUCCESS;
}
