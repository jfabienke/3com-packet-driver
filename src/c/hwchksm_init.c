/**
 * @file hwchksm_init.c
 * @brief Hardware Checksum Offload - Initialization Functions (OVERLAY Segment)
 *
 * Created: 2026-01-28 09:26:56 CET
 *
 * This file contains checksum system initialization, configuration, and
 * cleanup functions. These functions are only called during driver
 * startup/shutdown and can be placed in an overlay segment to save
 * memory during normal operation.
 *
 * Functions included:
 * - hw_checksum_init / hw_checksum_cleanup
 * - hw_checksum_configure_nic
 * - hw_checksum_detect_capabilities
 * - hw_checksum_self_test
 *
 * Split from hwchksm.c for memory segmentation optimization.
 * Runtime TX/RX functions are in hwchksm_rt.c (ROOT segment).
 *
 * Sprint 2.1: Hardware Checksumming Research Implementation
 *
 * Key findings from research:
 * - 3C515-TX: NO hardware checksumming (ISA generation, pre-1999)
 * - 3C509B: NO hardware checksumming (ISA generation, pre-1999)
 * - Hardware checksumming introduced in PCI Cyclone/Tornado series (post-1999)
 * - Linux 3c59x driver shows HAS_HWCKSM flag for supported chips only
 * - Neither 3C515 nor 3C509B appear in hardware checksum capable lists
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 */

#include "hwchksm.h"
#include "niccap.h"
#include "nicctx.h"
#include "logging.h"
#include "errhndl.h"
#include "pktops.h"
#include "cpudet.h"
#include "main.h"
#include "diag.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================== */
/* GLOBAL STATE DEFINITIONS (extern'd by hwchksm_rt.c)                       */
/* ========================================================================== */

bool checksum_system_initialized = false;
checksum_mode_t global_checksum_mode = CHECKSUM_MODE_AUTO;
checksum_stats_t global_checksum_stats = {0};

/* Performance optimization settings */
uint16_t checksum_optimization_flags = CHECKSUM_OPT_ALIGN_16BIT | CHECKSUM_OPT_UNROLL_LOOPS;

/* ========================================================================== */
/* CHECKSUM SYSTEM INITIALIZATION                                            */
/* ========================================================================== */

int hw_checksum_init(checksum_mode_t global_mode) {
    int result;  /* C89: declare at start of block */

    if (checksum_system_initialized) {
        LOG_WARNING("Checksum system already initialized");
        return HW_CHECKSUM_SUCCESS;
    }

    LOG_INFO("Initializing hardware checksum system in mode %s",
             hw_checksum_mode_to_string(global_mode));

    /* Set global configuration */
    global_checksum_mode = global_mode;

    /* Clear statistics */
    memset(&global_checksum_stats, 0, sizeof(checksum_stats_t));

    /* Run self-test to verify implementation */
    result = hw_checksum_self_test();
    if (result != HW_CHECKSUM_SUCCESS) {
        LOG_ERROR("Checksum self-test failed: %d", result);
        return result;
    }

    checksum_system_initialized = true;
    LOG_INFO("Hardware checksum system initialized successfully");

    return HW_CHECKSUM_SUCCESS;
}

void hw_checksum_cleanup(void) {
    if (!checksum_system_initialized) {
        return;
    }

    LOG_INFO("Cleaning up hardware checksum system");

    /* Print final statistics */
    if (global_checksum_stats.tx_checksums_calculated > 0 ||
        global_checksum_stats.rx_checksums_validated > 0) {
        LOG_INFO("Final checksum statistics:");
        hw_checksum_print_stats();
    }

    checksum_system_initialized = false;
}

int hw_checksum_configure_nic(nic_context_t *ctx, checksum_mode_t mode) {
    if (!ctx) {
        return HW_CHECKSUM_INVALID_PARAM;
    }

    LOG_DEBUG("Configuring checksum mode %s for NIC %s",
              hw_checksum_mode_to_string(mode), nic_type_to_string(ctx->nic_type));

    /* Check if hardware checksumming is requested but not supported */
    if (mode == CHECKSUM_MODE_HARDWARE) {
        if (!nic_has_capability(ctx, NIC_CAP_HWCSUM)) {
            LOG_WARNING("Hardware checksumming requested but not supported by %s", nic_type_to_string(ctx->nic_type));
            return HW_CHECKSUM_NOT_SUPPORTED;
        }
    }

    /* For 3C515-TX and 3C509B, force software mode */
    if (ctx->nic_type == NIC_TYPE_3C515_TX || ctx->nic_type == NIC_TYPE_3C509B) {
        if (mode == CHECKSUM_MODE_HARDWARE) {
            LOG_WARNING("Forcing software checksum mode for %s (no hardware support)", nic_type_to_string(ctx->nic_type));
            mode = CHECKSUM_MODE_SOFTWARE;
        } else if (mode == CHECKSUM_MODE_AUTO) {
            mode = CHECKSUM_MODE_SOFTWARE;
            LOG_DEBUG("Auto-selecting software checksum mode for %s", nic_type_to_string(ctx->nic_type));
        }
    }

    return HW_CHECKSUM_SUCCESS;
}

/* ========================================================================== */
/* CAPABILITY DETECTION AND CONFIGURATION                                    */
/* ========================================================================== */

uint32_t hw_checksum_detect_capabilities(nic_context_t *ctx) {
    if (!ctx) {
        return 0;
    }

    /* 3C515-TX and 3C509B do not support hardware checksumming */
    if (ctx->nic_type == NIC_TYPE_3C515_TX || ctx->nic_type == NIC_TYPE_3C509B) {
        LOG_DEBUG("NIC %s: No hardware checksum capabilities (ISA generation)", nic_type_to_string(ctx->nic_type));
        return 0;
    }

    /* Check for hardware checksum capability flag */
    if (!nic_has_capability(ctx, NIC_CAP_HWCSUM)) {
        LOG_DEBUG("NIC %s: No hardware checksum capability flag set", nic_type_to_string(ctx->nic_type));
        return 0;
    }

    /* If we reach here, hardware might support checksumming */
    /* This is a placeholder for future NIC support */
    LOG_DEBUG("NIC %s: Hardware checksum capabilities detected (placeholder)", nic_type_to_string(ctx->nic_type));
    return (1 << CHECKSUM_PROTO_IP) | (1 << CHECKSUM_PROTO_TCP) | (1 << CHECKSUM_PROTO_UDP);
}

/* ========================================================================== */
/* SELF-TEST AND DIAGNOSTICS                                                 */
/* ========================================================================== */

int hw_checksum_self_test(void) {
    /* C89: declare all variables at start of block */
    uint8_t test_ip_header[] = {
        0x45, 0x00, 0x00, 0x1C,  /* Version, IHL, TOS, Total Length */
        0x00, 0x01, 0x00, 0x00,  /* ID, Flags, Fragment Offset */
        0x40, 0x11, 0x00, 0x00,  /* TTL, Protocol (UDP), Checksum (will be calculated) */
        0xC0, 0xA8, 0x01, 0x01,  /* Source IP: 192.168.1.1 */
        0xC0, 0xA8, 0x01, 0x02   /* Dest IP: 192.168.1.2 */
    };
    uint16_t expected_ip_checksum = 0xB861;  /* Known correct value */
    int result;
    uint16_t calculated_checksum;
    checksum_result_t validation_result;

    /* Calculate IP checksum */
    result = hw_checksum_calculate_ip(test_ip_header, 20);
    if (result != HW_CHECKSUM_SUCCESS) {
        LOG_ERROR("IP checksum calculation failed");
        return result;
    }

    calculated_checksum = (test_ip_header[10] << 8) | test_ip_header[11];
    if (calculated_checksum != expected_ip_checksum) {
        LOG_ERROR("IP checksum mismatch: expected 0x%04X, got 0x%04X",
                  expected_ip_checksum, calculated_checksum);
        return HW_CHECKSUM_ERROR;
    }

    /* Validate the checksum */
    validation_result = hw_checksum_validate_ip(test_ip_header, 20);
    if (validation_result != CHECKSUM_RESULT_VALID) {
        LOG_ERROR("IP checksum validation failed");
        return HW_CHECKSUM_ERROR;
    }

    LOG_INFO("Checksum self-test passed");
    return HW_CHECKSUM_SUCCESS;
}
