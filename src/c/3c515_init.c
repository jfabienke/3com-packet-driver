/**
 * @file 3c515_init.c
 * @brief 3Com 3C515-TX NIC driver - Initialization functions (OVERLAY segment)
 *
 * This file contains only the initialization functions that are called once
 * and can be discarded after init:
 * - NIC initialization and hardware setup
 * - EEPROM reading and parsing
 * - Media configuration
 * - DMA ring allocation
 * - MII PHY management
 * - Cache coherency initialization
 *
 * Runtime functions are in 3c515_rt.c (ROOT segment)
 *
 * Updated: 2026-01-28 05:20:00 UTC
 */

#include "3c515.h"
#include "eeprom.h"
#include "medictl.h"
#include "enhring.h"
#include "errhndl.h"
#include "logging.h"
#include "hwchksm.h"
#include "dma.h"
#include "dmamap.h"
#include "cachecoh.h"
#include "cachemgt.h"

/* Ensure TIER_DISABLE_BUS_MASTER is defined for cache tier check */
#ifndef TIER_DISABLE_BUS_MASTER
#define TIER_DISABLE_BUS_MASTER 0
#endif
#include "chipdet.h"
#include "vds.h"
#include "bufaloc.h"
#include "common.h"
#include <stdlib.h>
#include <string.h>

/* Ring size definitions */
#define TX_RING_SIZE 16
#define RX_RING_SIZE 16
#define BUFFER_SIZE  1600
#define EEPROM_SIZE 0x40

/* Hardware configuration timing constants */
#define RESET_TIMEOUT_MS           1000
#define CONFIG_STABILIZATION_MS    100

/* Duplex mode constants */
#define DUPLEX_HALF               0
#define DUPLEX_FULL               1
#define DUPLEX_AUTO               2

/* Link speed constants */
#define SPEED_10MBPS              10
#define SPEED_100MBPS             100
#define SPEED_AUTO                0

/* Private data structure (matches 3c515_rt.c) */
typedef struct _3c515_private_data {
    _3c515_tx_tx_desc_t *tx_ring;
    _3c515_tx_rx_desc_t *rx_ring;
    uint8_t *buffers;
    uint32_t tx_index;
    uint32_t rx_index;
} _3c515_private_data_t;

/* Extended context with VDS physical addresses */
typedef struct {
    _3c515_nic_context_t base;
    uint32_t tx_desc_ring_physical;
    uint32_t rx_desc_ring_physical;
    uint32_t buffers_physical;
    coherency_analysis_t coherency_analysis;
    uint8_t cache_coherency_tier;
    uint8_t cache_management_available;
} extended_nic_context_t;

/* Global NIC context */
static _3c515_nic_context_t g_nic_context;
static bool g_driver_initialized = false;
static extended_nic_context_t g_extended_context;

/* ============================================================================
 * External declarations for runtime functions (in 3c515_rt.c)
 * ============================================================================ */

extern int _3c515_send_packet(nic_info_t *nic, const uint8_t *packet, size_t len);
extern int _3c515_receive_packet(nic_info_t *nic, uint8_t *buffer, size_t *len);
extern void _3c515_handle_interrupt(nic_info_t *nic);
extern int _3c515_check_interrupt(nic_info_t *nic);
extern int _3c515_enable_interrupts(nic_info_t *nic);
extern int _3c515_disable_interrupts(nic_info_t *nic);
extern int _3c515_get_link_status(nic_info_t *nic);
extern int _3c515_get_link_speed(nic_info_t *nic);

/* ============================================================================
 * Forward declarations for init-only functions
 * ============================================================================ */

static void delay_milliseconds(uint32_t ms);
static uint32_t get_system_time_ms(void);
static int read_and_parse_eeprom(_3c515_nic_context_t *ctx);
static int configure_media_type(_3c515_nic_context_t *ctx, media_config_t *media);
static int configure_full_duplex(_3c515_nic_context_t *ctx);
static int setup_interrupt_mask(_3c515_nic_context_t *ctx);
static int configure_bus_master_dma(_3c515_nic_context_t *ctx);
static int enable_hardware_statistics(_3c515_nic_context_t *ctx);
static int setup_link_monitoring(_3c515_nic_context_t *ctx);
static int reset_nic_hardware(_3c515_nic_context_t *ctx);
static int validate_hardware_configuration(_3c515_nic_context_t *ctx);
static int _3c515_initialize_cache_coherency(_3c515_nic_context_t *ctx);

/* MII PHY management functions */
static int mii_read_register(_3c515_nic_context_t *ctx, uint8_t phy_addr, uint8_t reg_addr);
static int mii_write_register(_3c515_nic_context_t *ctx, uint8_t phy_addr, uint8_t reg_addr, uint16_t value);
static int configure_mii_transceiver(_3c515_nic_context_t *ctx);

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static void delay_milliseconds(uint32_t ms) {
    mdelay(ms);
}

static uint32_t get_system_time_ms(void) {
    /* Simple timer implementation */
    return 0;  /* Stub - actual implementation would use BIOS timer */
}

static void *allocate_descriptor_ring(int size, size_t desc_size) {
    void *ring = malloc(size * desc_size);
    if (ring) {
        memset(ring, 0, size * desc_size);
    }
    return ring;
}

/* ============================================================================
 * Operations VTable
 * ============================================================================ */

/* Forward declarations for cleanup and other init functions */
int _3c515_cleanup(nic_info_t *nic);
int _3c515_reset(nic_info_t *nic);
int _3c515_self_test(nic_info_t *nic);

static nic_ops_t _3c515_ops = {
    .init               = NULL,  /* Set to _3c515_init after definition */
    .cleanup            = _3c515_cleanup,
    .reset              = _3c515_reset,
    .configure          = NULL,
    .send_packet        = _3c515_send_packet,
    .receive_packet     = _3c515_receive_packet,
    .check_interrupt    = _3c515_check_interrupt,
    .handle_interrupt   = _3c515_handle_interrupt,
    .enable_interrupts  = _3c515_enable_interrupts,
    .disable_interrupts = _3c515_disable_interrupts,
    .get_link_status    = _3c515_get_link_status,
    .get_link_speed     = _3c515_get_link_speed,
    .set_promiscuous    = NULL,
    .set_multicast      = NULL,
    .self_test          = _3c515_self_test
};

/**
 * @brief Get 3C515 operations vtable
 */
nic_ops_t* get_3c515_ops(void) {
    return &_3c515_ops;
}

/**
 * @brief Get current NIC context
 */
_3c515_nic_context_t *get_3c515_context(void) {
    return g_driver_initialized ? &g_nic_context : NULL;
}

/* ============================================================================
 * Main Initialization Functions
 * ============================================================================ */

/**
 * @brief Complete 3C515-TX hardware initialization sequence
 */
int complete_3c515_initialization(_3c515_nic_context_t *ctx) {
    int result;
    media_config_t media;

    if (!ctx) {
        LOG_ERROR("Invalid NIC context for initialization");
        return -1;
    }

    LOG_INFO("Starting complete 3C515-TX hardware initialization");

    memset(&media, 0, sizeof(media_config_t));

    /* Step 1: Read EEPROM configuration */
    LOG_DEBUG("Step 1: Reading EEPROM configuration");
    result = read_and_parse_eeprom(ctx);
    if (result < 0) {
        LOG_ERROR("Failed to read EEPROM configuration: %d", result);
        ctx->config_errors++;
        return result;
    }

    /* Step 2: Reset hardware to known state */
    LOG_DEBUG("Step 2: Resetting hardware");
    result = reset_nic_hardware(ctx);
    if (result < 0) {
        LOG_ERROR("Failed to reset NIC hardware: %d", result);
        ctx->config_errors++;
        return result;
    }

    /* Step 3: Configure MII transceiver */
    LOG_DEBUG("Step 3: Configuring MII transceiver");
    result = configure_mii_transceiver(ctx);
    if (result < 0) {
        LOG_ERROR("Failed to configure MII transceiver: %d", result);
        ctx->config_errors++;
        return result;
    }

    /* Step 4: Configure media type */
    LOG_DEBUG("Step 4: Configuring media type");
    result = configure_media_type(ctx, &media);
    if (result < 0) {
        LOG_ERROR("Failed to configure media type: %d", result);
        ctx->config_errors++;
        return result;
    }

    /* Step 5: Configure full-duplex if supported */
    LOG_DEBUG("Step 5: Configuring full-duplex support");
    if (media.duplex_mode == DUPLEX_FULL) {
        result = configure_full_duplex(ctx);
        if (result < 0) {
            LOG_WARNING("Failed to configure full-duplex: %d", result);
            media.duplex_mode = DUPLEX_HALF;
        }
    }

    /* Step 6: Configure interrupt mask */
    LOG_DEBUG("Step 6: Setting up interrupt mask");
    result = setup_interrupt_mask(ctx);
    if (result < 0) {
        LOG_ERROR("Failed to setup interrupt mask: %d", result);
        ctx->config_errors++;
        return result;
    }

    /* Step 7: Configure bus master DMA */
    LOG_DEBUG("Step 7: Configuring bus master DMA");
    result = configure_bus_master_dma(ctx);
    if (result < 0) {
        LOG_ERROR("Failed to configure bus master DMA: %d", result);
        ctx->config_errors++;
        return result;
    }

    /* Step 8: Enable hardware statistics */
    LOG_DEBUG("Step 8: Enabling hardware statistics");
    result = enable_hardware_statistics(ctx);
    if (result < 0) {
        LOG_WARNING("Failed to enable hardware statistics: %d", result);
    }

    /* Step 9: Setup link monitoring */
    LOG_DEBUG("Step 9: Setting up link monitoring");
    result = setup_link_monitoring(ctx);
    if (result < 0) {
        LOG_WARNING("Failed to setup link monitoring: %d", result);
    }

    /* Step 10: Initialize cache coherency */
    LOG_DEBUG("Step 10: Initializing cache coherency management");
    result = _3c515_initialize_cache_coherency(ctx);
    if (result < 0) {
        LOG_ERROR("Cache coherency initialization failed: %d", result);
        ctx->config_errors++;
        return result;
    }

    /* Step 11: Validate configuration */
    LOG_DEBUG("Step 11: Validating hardware configuration");
    result = validate_hardware_configuration(ctx);
    if (result < 0) {
        LOG_ERROR("Hardware configuration validation failed: %d", result);
        ctx->config_errors++;
        return result;
    }

    ctx->media_config = media;
    ctx->hardware_ready = 1;
    ctx->driver_active = 1;
    ctx->last_config_validation = get_system_time_ms();

    LOG_INFO("Complete 3C515-TX hardware initialization successful");

    return 0;
}

/**
 * @brief Initialize the 3C515-TX NIC (legacy interface)
 */
int _3c515_init(nic_info_t *nic) {
    int i;
    int result;
    _3c515_private_data_t *priv;
    _3c515_tx_tx_desc_t *tx_ring;
    _3c515_tx_rx_desc_t *rx_ring;
    uint8_t *buffers;

    /* Allocate private data structure */
    priv = malloc(sizeof(_3c515_private_data_t));
    if (!priv) return -1;
    memset(priv, 0, sizeof(_3c515_private_data_t));

    /* Allocate transmit descriptor ring */
    tx_ring = allocate_descriptor_ring(TX_RING_SIZE, sizeof(_3c515_tx_tx_desc_t));
    if (!tx_ring) {
        free(priv);
        return -1;
    }
    priv->tx_ring = tx_ring;
    nic->tx_descriptor_ring = tx_ring;

    /* Allocate receive descriptor ring */
    rx_ring = allocate_descriptor_ring(RX_RING_SIZE, sizeof(_3c515_tx_rx_desc_t));
    if (!rx_ring) {
        free(tx_ring);
        free(priv);
        return -1;
    }
    priv->rx_ring = rx_ring;
    nic->rx_descriptor_ring = rx_ring;

    /* Allocate contiguous buffer memory */
    buffers = malloc((TX_RING_SIZE + RX_RING_SIZE) * BUFFER_SIZE);
    if (!buffers) {
        free(rx_ring);
        free(tx_ring);
        free(priv);
        return -1;
    }
    priv->buffers = buffers;

    /* Store private data */
    nic->private_data = priv;
    nic->private_data_size = sizeof(_3c515_private_data_t);

    /* Initialize transmit descriptor ring */
    for (i = 0; i < TX_RING_SIZE; i++) {
        tx_ring[i].next = (i + 1 < TX_RING_SIZE) ?
            (uint32_t)&tx_ring[i + 1] : 0;
        tx_ring[i].addr = (uint32_t)(buffers + i * BUFFER_SIZE);
        tx_ring[i].status = 0;
        tx_ring[i].length = BUFFER_SIZE;
    }

    /* Initialize receive descriptor ring */
    for (i = 0; i < RX_RING_SIZE; i++) {
        rx_ring[i].next = (i + 1 < RX_RING_SIZE) ?
            (uint32_t)&rx_ring[i + 1] : 0;
        rx_ring[i].addr = (uint32_t)(buffers + (TX_RING_SIZE + i) * BUFFER_SIZE);
        rx_ring[i].status = 0;
        rx_ring[i].length = BUFFER_SIZE;
    }

    /* Reset the NIC */
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_TOTAL_RESET);

    /* Select Window 7 and set descriptor list pointers */
    _3C515_TX_SELECT_WINDOW(nic->io_base, _3C515_TX_WINDOW_7);
    outl(nic->io_base + _3C515_TX_DOWN_LIST_PTR, (uint32_t)tx_ring);
    outl(nic->io_base + _3C515_TX_UP_LIST_PTR, (uint32_t)rx_ring);

    /* Enable transmitter and receiver */
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_TX_ENABLE);
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_RX_ENABLE);

    priv->tx_index = 0;
    priv->rx_index = 0;

    /* Initialize hardware checksumming */
    result = hw_checksum_init(CHECKSUM_MODE_AUTO);
    if (result != 0) {
        LOG_WARNING("Hardware checksum initialization failed: %d", result);
    }

    /* Initialize DMA subsystem */
    result = dma_init();
    if (result != 0) {
        LOG_WARNING("DMA subsystem initialization failed: %d", result);
    }

    /* Set the init pointer in vtable now that _3c515_init is defined */
    _3c515_ops.init = _3c515_init;

    g_driver_initialized = true;

    return 0;
}

/**
 * @brief Cleanup the 3C515-TX NIC
 */
int _3c515_cleanup(nic_info_t *nic) {
    _3c515_private_data_t *priv;

    if (!nic) return -1;

    priv = (_3c515_private_data_t *)nic->private_data;
    if (!priv) return 0;

    /* Disable interrupts */
    _3c515_disable_interrupts(nic);

    /* Disable TX and RX */
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_TX_DISABLE);
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_RX_DISABLE);

    /* Free allocated memory */
    if (priv->buffers) free(priv->buffers);
    if (priv->rx_ring) free(priv->rx_ring);
    if (priv->tx_ring) free(priv->tx_ring);
    free(priv);

    nic->private_data = NULL;
    g_driver_initialized = false;

    return 0;
}

/**
 * @brief Reset the 3C515-TX NIC
 */
int _3c515_reset(nic_info_t *nic) {
    if (!nic) return -1;

    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_TOTAL_RESET);
    delay_milliseconds(RESET_TIMEOUT_MS);

    return 0;
}

/**
 * @brief Self-test for 3C515-TX
 */
int _3c515_self_test(nic_info_t *nic) {
    uint16_t status;

    if (!nic) return -1;

    /* Read status register to verify hardware access */
    status = inw(nic->io_base + _3C515_TX_STATUS_REG);

    /* Basic sanity check - status register should be readable */
    if (status == 0xFFFF) {
        LOG_ERROR("3C515 self-test failed: hardware not responding");
        return -1;
    }

    LOG_INFO("3C515 self-test passed");
    return 0;
}

/* ============================================================================
 * EEPROM and Configuration Functions (Init only)
 * ============================================================================ */

static int read_and_parse_eeprom(_3c515_nic_context_t *ctx) {
    int result;

    result = read_3c515_eeprom(ctx->io_base, &ctx->eeprom_config);
    if (result != EEPROM_SUCCESS) {
        LOG_ERROR("Failed to read 3C515-TX EEPROM: %s", eeprom_error_to_string(result));
        return -1;
    }

    if (!ctx->eeprom_config.data_valid) {
        LOG_ERROR("EEPROM data validation failed");
        return -1;
    }

    LOG_DEBUG("EEPROM configuration read successfully");
    return 0;
}

static int configure_media_type(_3c515_nic_context_t *ctx, media_config_t *media) {
    uint16_t media_ctrl;

    if (!media) return -1;

    media->media_type = ctx->eeprom_config.media_type;
    media->auto_negotiation = ctx->eeprom_config.auto_select;

    if (ctx->eeprom_config.speed_100mbps_cap) {
        media->link_speed = ctx->eeprom_config.auto_select ? SPEED_AUTO : SPEED_100MBPS;
    } else {
        media->link_speed = SPEED_10MBPS;
    }

    if (ctx->eeprom_config.full_duplex_cap) {
        media->duplex_mode = ctx->eeprom_config.auto_select ? DUPLEX_AUTO : DUPLEX_FULL;
    } else {
        media->duplex_mode = DUPLEX_HALF;
    }

    /* Configure Window 4 for media control */
    _3C515_TX_SELECT_WINDOW(ctx->io_base, _3C515_TX_WINDOW_4);
    delay_milliseconds(10);

    media_ctrl = _3C515_TX_MEDIA_10TP | _3C515_TX_MEDIA_LNK;
    outw(ctx->io_base + _3C515_TX_W4_MEDIA, media_ctrl);
    delay_milliseconds(CONFIG_STABILIZATION_MS);

    return 0;
}

static int configure_full_duplex(_3c515_nic_context_t *ctx) {
    uint16_t mac_ctrl;
    uint16_t verify_ctrl;

    if (!ctx->eeprom_config.full_duplex_cap) {
        return -1;
    }

    _3C515_TX_SELECT_WINDOW(ctx->io_base, _3C515_TX_WINDOW_3);
    delay_milliseconds(10);

    mac_ctrl = inw(ctx->io_base + _3C515_TX_W3_MAC_CTRL);
    mac_ctrl |= _3C515_TX_FULL_DUPLEX_BIT;
    outw(ctx->io_base + _3C515_TX_W3_MAC_CTRL, mac_ctrl);
    delay_milliseconds(CONFIG_STABILIZATION_MS);

    verify_ctrl = inw(ctx->io_base + _3C515_TX_W3_MAC_CTRL);
    if (!(verify_ctrl & _3C515_TX_FULL_DUPLEX_BIT)) {
        LOG_ERROR("Failed to enable full-duplex mode");
        return -1;
    }

    ctx->full_duplex_enabled = 1;
    return 0;
}

static int setup_interrupt_mask(_3c515_nic_context_t *ctx) {
    uint16_t mask;

    mask = _3C515_TX_STATUS_TX_COMPLETE | _3C515_TX_STATUS_RX_COMPLETE |
           _3C515_TX_STATUS_UP_COMPLETE | _3C515_TX_STATUS_DOWN_COMPLETE |
           _3C515_TX_STATUS_ADAPTER_FAILURE | _3C515_TX_STATUS_STATS_FULL;

    outw(ctx->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_SET_INTR_ENB | mask);
    delay_milliseconds(10);

    return 0;
}

static int configure_bus_master_dma(_3c515_nic_context_t *ctx) {
    /* Enable bus mastering */
    _3C515_TX_SELECT_WINDOW(ctx->io_base, _3C515_TX_WINDOW_3);
    delay_milliseconds(10);

    ctx->dma_enabled = 1;
    return 0;
}

static int enable_hardware_statistics(_3c515_nic_context_t *ctx) {
    outw(ctx->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_STATS_ENABLE);
    delay_milliseconds(10);

    ctx->stats_enabled = 1;
    return 0;
}

static int setup_link_monitoring(_3c515_nic_context_t *ctx) {
    /* Link monitoring setup */
    _3C515_TX_SELECT_WINDOW(ctx->io_base, _3C515_TX_WINDOW_4);
    delay_milliseconds(10);

    return 0;
}

static int reset_nic_hardware(_3c515_nic_context_t *ctx) {
    outw(ctx->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_TOTAL_RESET);
    delay_milliseconds(RESET_TIMEOUT_MS);

    return 0;
}

static int validate_hardware_configuration(_3c515_nic_context_t *ctx) {
    uint16_t status;

    status = inw(ctx->io_base + _3C515_TX_STATUS_REG);
    if (status == 0xFFFF) {
        LOG_ERROR("Hardware not responding during validation");
        return -1;
    }

    return 0;
}

/* ============================================================================
 * MII PHY Management (Init only)
 * ============================================================================ */

/* MII PHY Register Definitions */
#define MII_CONTROL_REG         0x00
#define MII_STATUS_REG          0x01
#define MII_CTRL_RESET          0x8000
#define MII_CTRL_AUTONEG_EN     0x1000
#define MII_CTRL_RESTART_AN     0x0200
#define MII_STAT_AUTONEG_COMP   0x0020
#define MII_STAT_LINK_UP        0x0004

/* PHY Control register bits */
#define PHY_CTRL_MGMT_CLK       0x0001
#define PHY_CTRL_MGMT_DATA      0x0002
#define PHY_CTRL_MGMT_DIR       0x0004
#define PHY_CTRL_MGMT_OE        0x0008

#define _3C515_W4_PHY_CTRL      0x08

static int mii_read_register(_3c515_nic_context_t *ctx, uint8_t phy_addr, uint8_t reg_addr) {
    /* Simplified MII read - actual implementation would use bit-banging */
    (void)phy_addr;
    (void)reg_addr;

    _3C515_TX_SELECT_WINDOW(ctx->io_base, _3C515_TX_WINDOW_4);
    return inw(ctx->io_base + _3C515_W4_PHY_CTRL);
}

static int mii_write_register(_3c515_nic_context_t *ctx, uint8_t phy_addr, uint8_t reg_addr, uint16_t value) {
    /* Simplified MII write */
    (void)phy_addr;
    (void)reg_addr;

    _3C515_TX_SELECT_WINDOW(ctx->io_base, _3C515_TX_WINDOW_4);
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, value);

    return 0;
}

static int configure_mii_transceiver(_3c515_nic_context_t *ctx) {
    int phy_status;

    /* Reset PHY */
    mii_write_register(ctx, 0, MII_CONTROL_REG, MII_CTRL_RESET);
    delay_milliseconds(100);

    /* Enable auto-negotiation */
    mii_write_register(ctx, 0, MII_CONTROL_REG, MII_CTRL_AUTONEG_EN | MII_CTRL_RESTART_AN);
    delay_milliseconds(100);

    /* Check PHY status */
    phy_status = mii_read_register(ctx, 0, MII_STATUS_REG);
    if (phy_status < 0) {
        LOG_WARNING("MII PHY not responding");
        return -1;
    }

    return 0;
}

/* ============================================================================
 * Cache Coherency Initialization
 * ============================================================================ */

static int _3c515_initialize_cache_coherency(_3c515_nic_context_t *ctx) {
    coherency_analysis_t analysis;

    if (!ctx) {
        LOG_ERROR("Invalid NIC context for cache coherency initialization");
        return -1;
    }

    LOG_INFO("Initializing cache coherency management for 3C515-TX...");

    analysis = perform_complete_coherency_analysis();

    if (analysis.selected_tier == TIER_DISABLE_BUS_MASTER) {
        LOG_ERROR("Cache coherency analysis recommends disabling bus mastering");
        LOG_ERROR("3C515-TX requires DMA operation - system incompatible");
        return -1;
    }

    g_extended_context.coherency_analysis = analysis;
    g_extended_context.cache_coherency_tier = (uint8_t)analysis.selected_tier;
    g_extended_context.cache_management_available = 1;

    LOG_INFO("Cache coherency tier %d selected with confidence %d%%",
             analysis.selected_tier, analysis.confidence);

    return 0;
}
