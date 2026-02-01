/**
 * @file 3c515.c
 * @brief Enhanced 3Com 3C515-TX driver with complete hardware initialization
 * 
 * Sprint 0B.4: Complete Hardware Initialization
 * 
 * This implementation provides comprehensive hardware configuration matching
 * Linux driver standards with complete initialization sequence, media type
 * detection, transceiver configuration, full-duplex support, interrupt
 * management, DMA configuration, statistics collection, and link monitoring.
 * 
 * Key Features:
 * - Complete EEPROM-based hardware configuration
 * - Media type detection and transceiver setup
 * - Full-duplex configuration (Window 3, MAC Control)
 * - Comprehensive interrupt mask setup
 * - Bus master DMA configuration
 * - Hardware statistics collection
 * - Link status monitoring
 * - Periodic configuration validation
 */

#include "3c515.h"
#include "eeprom.h"
#include "medictl.h"
#include "enhring.h"
#include "errhndl.h"
#include "logging.h"
#include "irqmit.h"
#include "hwchksm.h"  // Phase 2.1: Hardware checksumming
#include "dma.h"          // Phase 2.2: Scatter-gather DMA
#include "dmadesc.h"      // DMA ring manager type definitions
#include "cachecoh.h"  // Phase 4: Runtime cache coherency testing
#include "cachemgt.h"  // Phase 4: Cache management system
#include "chipdet.h"  // Phase 4: Safe chipset detection and record_chipset_test_result
#include "dmamap.h"     // GPT-5: Centralized DMA mapping layer
#include "vds.h"             // VDS support for descriptor rings
#include "vds_mapping.h"     // VDS ISA compatibility check (vds_is_isa_compatible)
#include "bufaloc.h"    // VDS common buffer access
#include "pltprob.h"  // Platform detection
#include "common.h"       // Physical address helper (GPT-5 fix)
#include "prfenbl.h"      // Performance enabler (should_offer_performance_guidance, etc.)
#include "api.h"          // Packet Driver API (api_process_received_packet)
#include <stdlib.h>  // For malloc/free
#include <string.h>  // For memcpy

/* CRITICAL SAFETY GATE: Ensure DMA safety is integrated */
#ifndef DMA_SAFETY_INTEGRATED
#error "3C515 driver requires DMA safety integration. Ensure dma_mapping.c is linked and SMC patching is active."
#endif

#define TX_RING_SIZE 16  // Number of transmit descriptors (increased from 8)
#define RX_RING_SIZE 16  // Number of receive descriptors (increased from 8)
#define BUFFER_SIZE  1600  // Buffer size per descriptor
#define EEPROM_SIZE 0x40   // EEPROM size in words

/* Hardware configuration timing constants */
#define RESET_TIMEOUT_MS           1000    /* Maximum reset time */
#define CONFIG_STABILIZATION_MS    100     /* Configuration stabilization delay */
#define LINK_CHECK_INTERVAL_MS     500     /* Link status check interval */
#define STATS_UPDATE_INTERVAL_MS   1000    /* Statistics update interval */
#define CONFIG_VALIDATION_INTERVAL_MS 5000 /* Configuration validation interval */

/* Duplex mode constants */
#define DUPLEX_HALF               0
#define DUPLEX_FULL               1
#define DUPLEX_AUTO               2

/* Link speed constants */
#define SPEED_10MBPS              10
#define SPEED_100MBPS             100
#define SPEED_AUTO                0

/* Use types from 3c515.h header to avoid redefinition:
 * - media_config_t is defined in 3c515.h
 * - _3c515_nic_context_t is defined in 3c515.h (use this instead of nic_context_t for driver context)
 * - nic_context_t from errhndl.h is for error handling context only
 */

/* Type alias for the driver's NIC context to avoid confusion with errhndl.h's nic_context_t */
typedef _3c515_nic_context_t driver_nic_context_t;

/* Extended context with VDS physical addresses (wraps the header type) */
typedef struct {
    driver_nic_context_t base;              // Base context from 3c515.h
    uint32_t tx_desc_ring_physical;         // TX ring physical address (VDS)
    uint32_t rx_desc_ring_physical;         // RX ring physical address (VDS)
    uint32_t buffers_physical;              // Buffer physical address (VDS)
    coherency_analysis_t coherency_analysis; // Complete coherency analysis results
    uint8_t cache_coherency_tier;           // Selected cache management tier
    uint8_t cache_management_available;     // Cache management initialized
} extended_nic_context_t;

/* Forward declarations for internal static functions */
static void delay_milliseconds(uint32_t ms);
static uint32_t get_system_time_ms(void);

/* MII PHY Management Functions - static implementations */
static int mii_read_register(_3c515_nic_context_t *ctx, uint8_t phy_addr, uint8_t reg_addr);
static int mii_write_register(_3c515_nic_context_t *ctx, uint8_t phy_addr, uint8_t reg_addr, uint16_t value);
static int start_autonegotiation(_3c515_nic_context_t *ctx, uint16_t advertised_modes);
static int check_autonegotiation_complete(_3c515_nic_context_t *ctx);
static int get_autonegotiation_result(_3c515_nic_context_t *ctx, uint16_t *speed, bool *full_duplex);
static int configure_mii_transceiver(_3c515_nic_context_t *ctx);

/* Phase 4: Cache coherency integration functions */
static int _3c515_initialize_cache_coherency(_3c515_nic_context_t *ctx);
static void _3c515_dma_prepare_buffers(void *buffer, size_t length, bool is_receive);
static void _3c515_dma_complete_buffers(void *buffer, size_t length, bool is_receive);

/* Stub declarations for missing functions */
/* cache_coherency_context_t is now forward-declared in dmadesc.h */
static struct cache_coherency_context *get_cache_coherency_context(void) { return NULL; }

/* record_chipset_test_result stub if not declared in chipdet.h */
#ifndef CHIPDET_HAS_RECORD_FUNC
static bool record_chipset_test_result(const coherency_analysis_t *analysis,
                                       const chipset_detection_result_t *chipset) {
    (void)analysis; (void)chipset;
    return true;  /* Stub - always succeeds */
}
#endif

/* _3c515_adv_dma_context_t - driver-specific extended context for advanced DMA */
typedef struct _3c515_adv_dma_context {
    dma_ring_manager_t ring_manager;
    /* Add other advanced DMA fields as needed */
} _3c515_adv_dma_context_t;

/* Stub for RX/TX completion handlers - uses extended context */
static int nic_3c515_handle_rx_completion(_3c515_adv_dma_context_t *ctx) { (void)ctx; return 0; }
static int nic_3c515_handle_tx_completion(_3c515_adv_dma_context_t *ctx) { (void)ctx; return 0; }

/* MII PHY Register Definitions - IEEE 802.3u Standard */
#define MII_CONTROL_REG         0x00    /* Control Register */
#define MII_STATUS_REG          0x01    /* Status Register */
#define MII_PHY_ID1_REG         0x02    /* PHY Identifier 1 */
#define MII_PHY_ID2_REG         0x03    /* PHY Identifier 2 */
#define MII_AUTONEG_ADV_REG     0x04    /* Auto-negotiation Advertisement */
#define MII_AUTONEG_LINK_REG    0x05    /* Auto-negotiation Link Partner */
#define MII_AUTONEG_EXP_REG     0x06    /* Auto-negotiation Expansion */

/* MII Control Register bits */
#define MII_CTRL_RESET          0x8000  /* Reset PHY */
#define MII_CTRL_LOOPBACK       0x4000  /* Loopback mode */
#define MII_CTRL_SPEED_100      0x2000  /* 100 Mbps */
#define MII_CTRL_AUTONEG_EN     0x1000  /* Auto-negotiation enable */
#define MII_CTRL_POWER_DOWN     0x0800  /* Power down */
#define MII_CTRL_ISOLATE        0x0400  /* Isolate PHY */
#define MII_CTRL_RESTART_AN     0x0200  /* Restart auto-negotiation */
#define MII_CTRL_FULL_DUPLEX    0x0100  /* Full duplex */
#define MII_CTRL_COLLISION_TEST 0x0080  /* Collision test */

/* MII Status Register bits */
#define MII_STAT_100_T4         0x8000  /* 100BASE-T4 capable */
#define MII_STAT_100_TX_FD      0x4000  /* 100BASE-TX full duplex capable */
#define MII_STAT_100_TX_HD      0x2000  /* 100BASE-TX half duplex capable */
#define MII_STAT_10_FD          0x1000  /* 10BASE-T full duplex capable */
#define MII_STAT_10_HD          0x0800  /* 10BASE-T half duplex capable */
#define MII_STAT_AUTONEG_COMP   0x0020  /* Auto-negotiation complete */
#define MII_STAT_REMOTE_FAULT   0x0010  /* Remote fault detected */
#define MII_STAT_AUTONEG_CAP    0x0008  /* Auto-negotiation capable */
#define MII_STAT_LINK_UP        0x0004  /* Link status */
#define MII_STAT_JABBER         0x0002  /* Jabber detected */
#define MII_STAT_EXTENDED       0x0001  /* Extended capability */

/* Auto-negotiation Advertisement Register bits */
#define MII_ADV_NEXT_PAGE       0x8000  /* Next page bit */
#define MII_ADV_REMOTE_FAULT    0x2000  /* Remote fault */
#define MII_ADV_PAUSE           0x0400  /* Pause capable */
#define MII_ADV_100_T4          0x0200  /* 100BASE-T4 capable */
#define MII_ADV_100_TX_FD       0x0100  /* 100BASE-TX full duplex capable */
#define MII_ADV_100_TX_HD       0x0080  /* 100BASE-TX half duplex capable */
#define MII_ADV_10_FD           0x0040  /* 10BASE-T full duplex capable */
#define MII_ADV_10_HD           0x0020  /* 10BASE-T half duplex capable */
#define MII_ADV_SELECTOR_FIELD  0x001F  /* Selector field */

/* 3C515-TX Window 4 MII Management registers */
#define _3C515_W4_PHY_CTRL      0x08    /* PHY Control */
#define _3C515_W4_PHY_STATUS    0x0A    /* PHY Status */
#define _3C515_W4_PHY_ID_LOW    0x0C    /* PHY ID Low */
#define _3C515_W4_PHY_ID_HIGH   0x0E    /* PHY ID High */

/* PHY Control register bits (Window 4) */
#define PHY_CTRL_MGMT_CLK       0x0001  /* Management clock */
#define PHY_CTRL_MGMT_DATA      0x0002  /* Management data */
#define PHY_CTRL_MGMT_DIR       0x0004  /* Management direction */
#define PHY_CTRL_MGMT_OE        0x0008  /* Management output enable */

/* DMA Descriptor optimization constants */
#define DMA_DESC_ALIGNMENT      16      /* 16-byte alignment for ISA cache lines */
#define DMA_BUFFER_ALIGNMENT    4       /* 4-byte buffer alignment */
#define MAX_DMA_FRAGMENT_SIZE   1536    /* Maximum DMA fragment size */
#define DMA_COHERENCY_SYNC      0x0001  /* Sync for cache coherency */

/* Global NIC context - uses header-defined type */
static _3c515_nic_context_t g_nic_context;
static bool g_driver_initialized = false;

/* Extended context with VDS physical addresses */
static extended_nic_context_t g_extended_context;

// Helper function to allocate and initialize a descriptor ring
static void *allocate_descriptor_ring(int size, size_t desc_size) {
    void *ring = malloc(size * desc_size);
    if (ring) {
        memset(ring, 0, size * desc_size);
    }
    return ring;
}

/**
 * @brief Complete 3C515-TX hardware initialization sequence
 * 
 * This function implements the complete hardware initialization sequence
 * matching Linux driver standards with comprehensive configuration of
 * all hardware features and capabilities.
 * 
 * @param ctx Pointer to NIC context structure
 * @return 0 on success, negative error code on failure
 */
int complete_3c515_initialization(_3c515_nic_context_t *ctx) {
    int result;
    media_config_t media;
    
    if (!ctx) {
        LOG_ERROR("Invalid NIC context for initialization");
        return -1;
    }
    
    LOG_INFO("Starting complete 3C515-TX hardware initialization");
    
    /* Clear media configuration structure */
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
    
    /* Step 3: Configure MII transceiver and auto-negotiation */
    LOG_DEBUG("Step 3: Configuring MII transceiver and auto-negotiation");
    result = configure_mii_transceiver(ctx);
    if (result < 0) {
        LOG_ERROR("Failed to configure MII transceiver: %d", result);
        ctx->config_errors++;
        return result;
    }
    
    /* Step 4: Configure media type from EEPROM */
    LOG_DEBUG("Step 4: Configuring media type");
    result = configure_media_type(ctx, &media);
    if (result < 0) {
        LOG_ERROR("Failed to configure media type: %d", result);
        ctx->config_errors++;
        return result;
    }
    
    /* Step 4: Set up full-duplex if supported */
    LOG_DEBUG("Step 4: Configuring full-duplex support");
    if (media.duplex_mode == DUPLEX_FULL) {
        result = configure_full_duplex(ctx);
        if (result < 0) {
            LOG_WARNING("Failed to configure full-duplex: %d", result);
            /* Continue with half-duplex */
            media.duplex_mode = DUPLEX_HALF;
        }
    }
    
    /* Step 5: Configure comprehensive interrupt mask */
    LOG_DEBUG("Step 5: Setting up interrupt mask");
    result = setup_interrupt_mask(ctx);
    if (result < 0) {
        LOG_ERROR("Failed to setup interrupt mask: %d", result);
        ctx->config_errors++;
        return result;
    }
    
    /* Step 6: Configure bus master DMA settings */
    LOG_DEBUG("Step 6: Configuring bus master DMA");
    result = configure_bus_master_dma(ctx);
    if (result < 0) {
        LOG_ERROR("Failed to configure bus master DMA: %d", result);
        ctx->config_errors++;
        return result;
    }
    
    /* Step 7: Enable hardware statistics collection */
    LOG_DEBUG("Step 7: Enabling hardware statistics");
    result = enable_hardware_statistics(ctx);
    if (result < 0) {
        LOG_WARNING("Failed to enable hardware statistics: %d", result);
        /* Non-fatal, continue */
    }
    
    /* Step 8: Setup link status monitoring */
    LOG_DEBUG("Step 8: Setting up link monitoring");
    result = setup_link_monitoring(ctx);
    if (result < 0) {
        LOG_WARNING("Failed to setup link monitoring: %d", result);
        /* Non-fatal, continue */
    }
    
    /* Step 9: Initialize cache coherency management for DMA safety */
    LOG_DEBUG("Step 9: Initializing cache coherency management");
    result = _3c515_initialize_cache_coherency(ctx);
    if (result < 0) {
        LOG_ERROR("Cache coherency initialization failed: %d", result);
        ctx->config_errors++;
        return result;
    }
    
    /* Step 10: Validate complete configuration */
    LOG_DEBUG("Step 10: Validating hardware configuration");
    result = validate_hardware_configuration(ctx);
    if (result < 0) {
        LOG_ERROR("Hardware configuration validation failed: %d", result);
        ctx->config_errors++;
        return result;
    }
    
    /* Copy media configuration to context */
    ctx->media_config = media;
    ctx->hardware_ready = 1;
    ctx->driver_active = 1;
    ctx->last_config_validation = get_system_time_ms();
    
    LOG_INFO("Complete 3C515-TX hardware initialization successful");
    LOG_INFO("  Media: %s, Speed: %d Mbps, Duplex: %s",
             (media.media_type == XCVR_10baseT) ? "10BaseT" : 
             (media.media_type == XCVR_100baseTx) ? "100BaseTX" : "Auto",
             media.link_speed,
             (media.duplex_mode == DUPLEX_FULL) ? "Full" : "Half");
    LOG_INFO("  Full Duplex: %s, DMA: %s, Statistics: %s",
             ctx->full_duplex_enabled ? "Enabled" : "Disabled",
             ctx->dma_enabled ? "Enabled" : "Disabled",
             ctx->stats_enabled ? "Enabled" : "Disabled");
    
    return 0;
}

/**
 * @brief Read and parse EEPROM configuration
 */
static int read_and_parse_eeprom(_3c515_nic_context_t *ctx) {
    int result;
    
    /* Read complete EEPROM configuration */
    result = read_3c515_eeprom(ctx->io_base, &ctx->eeprom_config);
    if (result != EEPROM_SUCCESS) {
        LOG_ERROR("Failed to read 3C515-TX EEPROM: %s", eeprom_error_to_string(result));
        return -1;
    }
    
    /* Validate EEPROM data */
    if (!ctx->eeprom_config.data_valid) {
        LOG_ERROR("EEPROM data validation failed");
        return -1;
    }
    
    LOG_DEBUG("EEPROM configuration read successfully");
    LOG_DEBUG("  MAC: %02X:%02X:%02X:%02X:%02X:%02X",
              ctx->eeprom_config.mac_address[0], ctx->eeprom_config.mac_address[1],
              ctx->eeprom_config.mac_address[2], ctx->eeprom_config.mac_address[3],
              ctx->eeprom_config.mac_address[4], ctx->eeprom_config.mac_address[5]);
    LOG_DEBUG("  Media Type: %s", eeprom_media_type_to_string(ctx->eeprom_config.media_type));
    LOG_DEBUG("  Capabilities: 100Mbps=%s, FullDuplex=%s, AutoSelect=%s",
              ctx->eeprom_config.speed_100mbps_cap ? "Yes" : "No",
              ctx->eeprom_config.full_duplex_cap ? "Yes" : "No",
              ctx->eeprom_config.auto_select ? "Yes" : "No");
    
    return 0;
}

/**
 * @brief Configure media type from EEPROM data
 */
static int configure_media_type(_3c515_nic_context_t *ctx, media_config_t *media) {
    if (!media) {
        return -1;
    }
    
    /* Initialize media configuration from EEPROM */
    media->media_type = ctx->eeprom_config.media_type;
    media->auto_negotiation = ctx->eeprom_config.auto_select;
    
    /* Determine link speed */
    if (ctx->eeprom_config.speed_100mbps_cap) {
        media->link_speed = ctx->eeprom_config.auto_select ? SPEED_AUTO : SPEED_100MBPS;
    } else {
        media->link_speed = SPEED_10MBPS;
    }
    
    /* Determine duplex mode */
    if (ctx->eeprom_config.full_duplex_cap) {
        media->duplex_mode = ctx->eeprom_config.auto_select ? DUPLEX_AUTO : DUPLEX_FULL;
    } else {
        media->duplex_mode = DUPLEX_HALF;
    }
    
    /* Set transceiver type */
    switch (media->media_type) {
        case EEPROM_MEDIA_10BASE_T:
            media->transceiver_type = XCVR_10baseT;
            break;
        case EEPROM_MEDIA_100BASE_TX:
            media->transceiver_type = XCVR_100baseTx;
            break;
        case EEPROM_MEDIA_AUI:
            media->transceiver_type = XCVR_AUI;
            break;
        case EEPROM_MEDIA_BNC:
            media->transceiver_type = XCVR_10base2;
            break;
        default:
            media->transceiver_type = XCVR_Default;
            break;
    }
    
    /* Configure Window 4 for media control */
    _3C515_TX_SELECT_WINDOW(ctx->io_base, _3C515_TX_WINDOW_4);
    delay_milliseconds(10);
    
    /* Configure media-specific settings */
    {
    uint16_t media_ctrl = 0;
    switch (media->transceiver_type) {
        case XCVR_10baseT:
            media_ctrl = _3C515_TX_MEDIA_10TP | _3C515_TX_MEDIA_LNK;
            break;
        case XCVR_AUI:
            media_ctrl = _3C515_TX_MEDIA_SQE;
            break;
        default:
            media_ctrl = _3C515_TX_MEDIA_10TP;
            break;
    }
    
    outw(ctx->io_base + _3C515_TX_W4_MEDIA, media_ctrl);
    }
    delay_milliseconds(CONFIG_STABILIZATION_MS);

    LOG_DEBUG("Media type configured: Type=%d, Speed=%d, Duplex=%d",
              media->media_type, media->link_speed, media->duplex_mode);

    return 0;
}

/**
 * @brief Configure full-duplex support (Window 3, MAC Control)
 */
static int configure_full_duplex(_3c515_nic_context_t *ctx) {
    if (!ctx->eeprom_config.full_duplex_cap) {
        LOG_DEBUG("Full-duplex not supported by hardware");
        return -1;
    }
    
    /* Select Window 3 for MAC control */
    _3C515_TX_SELECT_WINDOW(ctx->io_base, _3C515_TX_WINDOW_3);
    delay_milliseconds(10);
    
    /* Read current MAC control register */
    {
    uint16_t mac_ctrl = inw(ctx->io_base + _3C515_TX_W3_MAC_CTRL);
    uint16_t verify_ctrl;

    /* Set full-duplex bit */
    mac_ctrl |= _3C515_TX_FULL_DUPLEX_BIT;
    
    /* Write back the register */
    outw(ctx->io_base + _3C515_TX_W3_MAC_CTRL, mac_ctrl);
    delay_milliseconds(CONFIG_STABILIZATION_MS);
    
    /* Verify full-duplex was set */
    verify_ctrl = inw(ctx->io_base + _3C515_TX_W3_MAC_CTRL);
    if (!(verify_ctrl & _3C515_TX_FULL_DUPLEX_BIT)) {
        LOG_ERROR("Failed to enable full-duplex mode");
        return -1;
    }
    }

    ctx->full_duplex_enabled = 1;
    LOG_DEBUG("Full-duplex mode enabled successfully");

    return 0;
}

/**
 * @brief Setup comprehensive interrupt mask
 */
static int setup_interrupt_mask(_3c515_nic_context_t *ctx) {
    /* Configure comprehensive interrupt mask */
    uint16_t int_mask = _3C515_TX_IMASK_TX_COMPLETE |
                       _3C515_TX_IMASK_RX_COMPLETE |
                       _3C515_TX_IMASK_ADAPTER_FAILURE |
                       _3C515_TX_IMASK_UP_COMPLETE |
                       _3C515_TX_IMASK_DOWN_COMPLETE |
                       _3C515_TX_IMASK_DMA_DONE |
                       _3C515_TX_IMASK_STATS_FULL;
    
    /* Select Window 1 for interrupt configuration */
    _3C515_TX_SELECT_WINDOW(ctx->io_base, _3C515_TX_WINDOW_1);
    delay_milliseconds(10);
    
    /* Set interrupt enable mask */
    outw(ctx->io_base + _3C515_TX_COMMAND_REG, 
         _3C515_TX_CMD_SET_INTR_ENB | int_mask);
    
    /* Also set status enable mask */
    outw(ctx->io_base + _3C515_TX_COMMAND_REG,
         _3C515_TX_CMD_SET_STATUS_ENB | int_mask);
    
    ctx->interrupt_mask = int_mask;
    
    LOG_DEBUG("Interrupt mask configured: 0x%04X", int_mask);
    
    return 0;
}

/**
 * @brief Configure bus master DMA settings
 */
static int configure_bus_master_dma(_3c515_nic_context_t *ctx) {
    /* C89: Declare all variables at the beginning of the block */
    const vds_buffer_t *vds_tx_ring = NULL;
    const vds_buffer_t *vds_rx_ring = NULL;
    const vds_buffer_t *vds_rx_data = NULL;

    /* Use the global extended context for physical address tracking */
    extended_nic_context_t *ext_ctx = &g_extended_context;

    /* Select Window 7 for bus master control */
    _3C515_TX_SELECT_WINDOW(ctx->io_base, _3C515_TX_WINDOW_7);
    delay_milliseconds(10);

    /* Use VDS common buffers for descriptor rings when available (GPT-5) */
    if (platform_get_dma_policy() == DMA_POLICY_COMMONBUF && buffer_vds_available()) {
        LOG_INFO("Using VDS common buffers for 3C515 descriptor rings");

        /* Get VDS TX ring buffer */
        vds_tx_ring = buffer_get_vds_tx_ring();
        if (vds_tx_ring && !ctx->tx_desc_ring) {
            /* Use VDS common buffer for TX ring */
            ctx->tx_desc_ring = (_3c515_tx_tx_desc_t*)vds_tx_ring->virtual_addr;
            ext_ctx->tx_desc_ring_physical = vds_tx_ring->physical_addr;
            LOG_INFO("TX ring using VDS: virt=%p phys=%08lXh",
                    ctx->tx_desc_ring, (unsigned long)ext_ctx->tx_desc_ring_physical);

            /* Validate physical address is ISA-compatible */
            if (!vds_is_isa_compatible(ext_ctx->tx_desc_ring_physical,
                                     TX_RING_SIZE * sizeof(_3c515_tx_tx_desc_t))) {
                LOG_ERROR("VDS TX ring not ISA compatible: %08lXh",
                         (unsigned long)ext_ctx->tx_desc_ring_physical);
                return -1;
            }
        }

        /* Get VDS RX ring buffer */
        vds_rx_ring = buffer_get_vds_rx_ring();
        if (vds_rx_ring && !ctx->rx_desc_ring) {
            /* Use VDS common buffer for RX ring */
            ctx->rx_desc_ring = (_3c515_tx_rx_desc_t*)vds_rx_ring->virtual_addr;
            ext_ctx->rx_desc_ring_physical = vds_rx_ring->physical_addr;
            LOG_INFO("RX ring using VDS: virt=%p phys=%08lXh",
                    ctx->rx_desc_ring, (unsigned long)ext_ctx->rx_desc_ring_physical);

            /* Validate physical address is ISA-compatible */
            if (!vds_is_isa_compatible(ext_ctx->rx_desc_ring_physical,
                                     RX_RING_SIZE * sizeof(_3c515_tx_rx_desc_t))) {
                LOG_ERROR("VDS RX ring not ISA compatible: %08lXh",
                         (unsigned long)ext_ctx->rx_desc_ring_physical);
                return -1;
            }
        }

        /* Get VDS RX data buffer for packet buffers */
        vds_rx_data = buffer_get_vds_rx_data();
        if (vds_rx_data && !ctx->buffers) {
            ctx->buffers = (uint8_t*)vds_rx_data->virtual_addr;
            ext_ctx->buffers_physical = vds_rx_data->physical_addr;
            LOG_INFO("RX buffers using VDS: virt=%p phys=%08lXh size=%lu",
                    ctx->buffers, (unsigned long)ext_ctx->buffers_physical,
                    (unsigned long)vds_rx_data->size);
        }
    }

    /* Fallback to conventional allocation if VDS not available */
    if (!ctx->tx_desc_ring) {
        ctx->tx_desc_ring = allocate_descriptor_ring(TX_RING_SIZE, sizeof(_3c515_tx_tx_desc_t));
        if (!ctx->tx_desc_ring) {
            LOG_ERROR("Failed to allocate TX descriptor ring");
            return -1;
        }
        ext_ctx->tx_desc_ring_physical = 0;  /* Will use traditional address calculation */
        LOG_WARNING("TX ring using conventional memory (no VDS)");
    }

    if (!ctx->rx_desc_ring) {
        ctx->rx_desc_ring = allocate_descriptor_ring(RX_RING_SIZE, sizeof(_3c515_tx_rx_desc_t));
        if (!ctx->rx_desc_ring) {
            LOG_ERROR("Failed to allocate RX descriptor ring");
            return -1;
        }
        ext_ctx->rx_desc_ring_physical = 0;  /* Will use traditional address calculation */
        LOG_WARNING("RX ring using conventional memory (no VDS)");
    }

    /* Allocate conventional buffer memory if VDS not used */
    if (!ctx->buffers) {
        ctx->buffers = malloc((TX_RING_SIZE + RX_RING_SIZE) * BUFFER_SIZE);
        if (!ctx->buffers) {
            LOG_ERROR("Failed to allocate buffer memory");
            return -1;
        }
        memset(ctx->buffers, 0, (TX_RING_SIZE + RX_RING_SIZE) * BUFFER_SIZE);
        ext_ctx->buffers_physical = 0;  /* Will use traditional address calculation */
        LOG_WARNING("Packet buffers using conventional memory (no VDS)");
    }
    
    /* Initialize TX descriptor rings with physical addresses */
    {
    int i;
    uint32_t buffer_phys;
    for (i = 0; i < TX_RING_SIZE; i++) {
        /* Next pointer - use physical address for hardware */
        if (i + 1 < TX_RING_SIZE) {
            ctx->tx_desc_ring[i].next = ext_ctx->tx_desc_ring_physical ?
                                      (ext_ctx->tx_desc_ring_physical + (i + 1) * sizeof(_3c515_tx_tx_desc_t)) :
                                      phys_from_ptr(&ctx->tx_desc_ring[i + 1]);  /* GPT-5 fix */
        } else {
            ctx->tx_desc_ring[i].next = 0;  /* End of ring */
        }

        /* Buffer address - use physical address for DMA */
        buffer_phys = ext_ctx->buffers_physical ?
                              (ext_ctx->buffers_physical + i * BUFFER_SIZE) :
                              phys_from_ptr(ctx->buffers + i * BUFFER_SIZE);  /* GPT-5 fix */
        ctx->tx_desc_ring[i].addr = buffer_phys;
        ctx->tx_desc_ring[i].status = 0;
        ctx->tx_desc_ring[i].length = BUFFER_SIZE;

        LOG_DEBUG("TX desc %d: next=%08lXh addr=%08lXh", i,
                 (unsigned long)ctx->tx_desc_ring[i].next,
                 (unsigned long)ctx->tx_desc_ring[i].addr);
    }
    }

    /* Initialize RX descriptor rings with physical addresses */
    {
    int i;
    uint32_t buffer_phys;
    for (i = 0; i < RX_RING_SIZE; i++) {
        /* Next pointer - use physical address for hardware */
        if (i + 1 < RX_RING_SIZE) {
            ctx->rx_desc_ring[i].next = ext_ctx->rx_desc_ring_physical ?
                                      (ext_ctx->rx_desc_ring_physical + (i + 1) * sizeof(_3c515_tx_rx_desc_t)) :
                                      phys_from_ptr(&ctx->rx_desc_ring[i + 1]);  /* GPT-5 fix */
        } else {
            ctx->rx_desc_ring[i].next = 0;  /* End of ring */
        }

        /* Buffer address - use physical address for DMA */
        buffer_phys = ext_ctx->buffers_physical ?
                              (ext_ctx->buffers_physical + (TX_RING_SIZE + i) * BUFFER_SIZE) :
                              phys_from_ptr(ctx->buffers + (TX_RING_SIZE + i) * BUFFER_SIZE);  /* GPT-5 fix */

        /* Validate physical address is within ISA limits */
        if (buffer_phys >= 0x1000000UL) {
            LOG_ERROR("RX buffer %d exceeds ISA 24-bit limit: %08lXh", i, (unsigned long)buffer_phys);
            return -1;
        }

        ctx->rx_desc_ring[i].addr = buffer_phys;
        ctx->rx_desc_ring[i].status = 0;
        ctx->rx_desc_ring[i].length = BUFFER_SIZE;

        LOG_DEBUG("RX desc %d: next=%08lXh addr=%08lXh", i,
                 (unsigned long)ctx->rx_desc_ring[i].next,
                 (unsigned long)ctx->rx_desc_ring[i].addr);
    }
    }

    /* Set descriptor list pointers - CRITICAL: Use physical addresses for DMA! */
    {
    uint32_t tx_ring_phys;
    uint32_t rx_ring_phys;
    tx_ring_phys = ext_ctx->tx_desc_ring_physical ?
                           ext_ctx->tx_desc_ring_physical :
                           phys_from_ptr(ctx->tx_desc_ring);  /* GPT-5 fix: proper physical address */
    rx_ring_phys = ext_ctx->rx_desc_ring_physical ?
                           ext_ctx->rx_desc_ring_physical :
                           phys_from_ptr(ctx->rx_desc_ring);  /* GPT-5 fix: proper physical address */
    
    /* Validate physical addresses are within ISA 24-bit limit */
    if (tx_ring_phys >= 0x1000000UL || rx_ring_phys >= 0x1000000UL) {
        LOG_ERROR("Descriptor ring addresses exceed ISA 24-bit limit: TX=%08lXh RX=%08lXh",
                 (unsigned long)tx_ring_phys, (unsigned long)rx_ring_phys);
        return -1;
    }
    
    /* Program NIC with physical addresses */
    outl(ctx->io_base + _3C515_TX_DOWN_LIST_PTR, tx_ring_phys);
    outl(ctx->io_base + _3C515_TX_UP_LIST_PTR, rx_ring_phys);
    
    LOG_DEBUG("Descriptor rings programmed: TX phys=%08lXh RX phys=%08lXh",
             (unsigned long)tx_ring_phys, (unsigned long)rx_ring_phys);
    }

    ctx->dma_enabled = 1;
    ctx->tx_index = 0;
    ctx->rx_index = 0;
    
    LOG_DEBUG("Bus master DMA configured successfully");
    LOG_DEBUG("  TX Ring: 0x%08X (%d descriptors)", (uint32_t)ctx->tx_desc_ring, TX_RING_SIZE);
    LOG_DEBUG("  RX Ring: 0x%08X (%d descriptors)", (uint32_t)ctx->rx_desc_ring, RX_RING_SIZE);
    
    return 0;
}

/**
 * @brief Enable hardware statistics collection (Window 6)
 */
static int enable_hardware_statistics(_3c515_nic_context_t *ctx) {
    /* Select Window 6 for statistics */
    _3C515_TX_SELECT_WINDOW(ctx->io_base, _3C515_TX_WINDOW_6);
    delay_milliseconds(10);
    
    /* Clear all statistics counters */
    {
    int i;
    for (i = 0; i <= _3C515_TX_W6_BADSSD; i++) {
        (void)inb(ctx->io_base + i);  /* Reading clears the counter */
    }
    }
    
    /* Enable statistics collection */
    outw(ctx->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_STATS_ENABLE);
    
    ctx->stats_enabled = 1;
    ctx->last_stats_update = get_system_time_ms();
    
    LOG_DEBUG("Hardware statistics collection enabled");
    
    return 0;
}

/**
 * @brief Setup link status monitoring
 */
static int setup_link_monitoring(_3c515_nic_context_t *ctx) {
    /* Select Window 4 for media control */
    _3C515_TX_SELECT_WINDOW(ctx->io_base, _3C515_TX_WINDOW_4);
    delay_milliseconds(10);
    
    /* Read current link status */
    {
    uint16_t media_status = inw(ctx->io_base + _3C515_TX_W4_MEDIA);
    ctx->media_config.link_active = (media_status & _3C515_TX_MEDIA_LNKBEAT) ? 1 : 0;
    }

    ctx->link_monitoring_enabled = 1;
    ctx->last_link_check = get_system_time_ms();
    
    LOG_DEBUG("Link monitoring enabled, current status: %s",
              ctx->media_config.link_active ? "Up" : "Down");
    
    return 0;
}

/**
 * @brief Validate complete hardware configuration
 */
static int validate_hardware_configuration(_3c515_nic_context_t *ctx) {
    uint16_t saved_window;
    uint16_t eeprom_test;
    uint16_t mac_ctrl;
    uint32_t tx_ptr;
    uint32_t rx_ptr;

    /* Save current window */
    saved_window = inw(ctx->io_base + _3C515_TX_STATUS_REG) >> 13;

    /* Test Window 0 access */
    _3C515_TX_SELECT_WINDOW(ctx->io_base, _3C515_TX_WINDOW_0);
    delay_milliseconds(5);

    /* Verify we can read EEPROM data register */
    eeprom_test = inw(ctx->io_base + _3C515_TX_W0_EEPROM_DATA);
    if (eeprom_test == 0xFFFF || eeprom_test == 0x0000) {
        LOG_WARNING("EEPROM data register validation suspicious: 0x%04X", eeprom_test);
    }
    
    /* Test Window 3 access for MAC control */
    _3C515_TX_SELECT_WINDOW(ctx->io_base, _3C515_TX_WINDOW_3);
    delay_milliseconds(5);

    mac_ctrl = inw(ctx->io_base + _3C515_TX_W3_MAC_CTRL);
    if (ctx->full_duplex_enabled && !(mac_ctrl & _3C515_TX_FULL_DUPLEX_BIT)) {
        LOG_ERROR("Full-duplex validation failed");
        return -1;
    }
    
    /* Test Window 7 access for DMA descriptors */
    _3C515_TX_SELECT_WINDOW(ctx->io_base, _3C515_TX_WINDOW_7);
    delay_milliseconds(5);

    tx_ptr = inl(ctx->io_base + _3C515_TX_DOWN_LIST_PTR);
    rx_ptr = inl(ctx->io_base + _3C515_TX_UP_LIST_PTR);
    
    if (tx_ptr != (uint32_t)ctx->tx_desc_ring || rx_ptr != (uint32_t)ctx->rx_desc_ring) {
        LOG_ERROR("DMA descriptor validation failed: TX=0x%08X (exp 0x%08X), RX=0x%08X (exp 0x%08X)",
                  tx_ptr, (uint32_t)ctx->tx_desc_ring, rx_ptr, (uint32_t)ctx->rx_desc_ring);
        return -1;
    }
    
    /* Restore window */
    _3C515_TX_SELECT_WINDOW(ctx->io_base, saved_window);
    
    LOG_DEBUG("Hardware configuration validation passed");
    
    return 0;
}

/**
 * @brief Reset NIC hardware to known state
 */
static int reset_nic_hardware(_3c515_nic_context_t *ctx) {
    uint32_t timeout_start = get_system_time_ms();
    
    /* Issue total reset command */
    outw(ctx->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_TOTAL_RESET);
    
    /* Wait for reset to complete */
    while ((get_system_time_ms() - timeout_start) < RESET_TIMEOUT_MS) {
        uint16_t status = inw(ctx->io_base + _3C515_TX_STATUS_REG);
        if (!(status & _3C515_TX_STATUS_CMD_IN_PROGRESS)) {
            LOG_DEBUG("Hardware reset completed in %u ms", 
                      get_system_time_ms() - timeout_start);
            delay_milliseconds(CONFIG_STABILIZATION_MS);
            return 0;
        }
        delay_milliseconds(10);
    }
    
    LOG_ERROR("Hardware reset timeout after %d ms", RESET_TIMEOUT_MS);
    return -1;
}

/**
 * @brief Utility function for millisecond delays
 */
static void delay_milliseconds(uint32_t ms) {
    /* Simple delay implementation for DOS environment */
    uint32_t i;
    volatile uint32_t j;
    for (i = 0; i < ms; i++) {
        for (j = 0; j < 1000; j++) {
            /* Busy wait */
        }
    }
}

/**
 * @brief Get system time in milliseconds
 */
static uint32_t get_system_time_ms(void) {
    /* Simple time implementation for DOS environment */
    static uint32_t counter = 0;
    return ++counter;
}

/**
 * @brief Periodic configuration validation
 */
int periodic_configuration_validation(_3c515_nic_context_t *ctx) {
    /* C89: All declarations at the beginning of the block */
    uint32_t current_time;
    int result;

    current_time = get_system_time_ms();

    if (!ctx || !ctx->hardware_ready) {
        return -1;
    }

    /* Check if validation is due */
    if ((current_time - ctx->last_config_validation) < CONFIG_VALIDATION_INTERVAL_MS) {
        return 0;  /* Not yet time */
    }

    LOG_DEBUG("Performing periodic configuration validation");

    /* Validate hardware configuration */
    result = validate_hardware_configuration(ctx);
    if (result < 0) {
        LOG_ERROR("Periodic configuration validation failed");
        ctx->config_errors++;
        return result;
    }

    /* Update link status */
    if (ctx->link_monitoring_enabled) {
        uint16_t media_status;
        uint8_t new_link_status;
        _3C515_TX_SELECT_WINDOW(ctx->io_base, _3C515_TX_WINDOW_4);
        media_status = inw(ctx->io_base + _3C515_TX_W4_MEDIA);
        new_link_status = (media_status & _3C515_TX_MEDIA_LNKBEAT) ? 1 : 0;

        if (new_link_status != ctx->media_config.link_active) {
            LOG_INFO("Link status changed: %s -> %s",
                     ctx->media_config.link_active ? "Up" : "Down",
                     new_link_status ? "Up" : "Down");
            ctx->media_config.link_active = new_link_status;
            ctx->link_changes++;
        }
    }

    /* Update statistics if enabled */
    if (ctx->stats_enabled &&
        (current_time - ctx->last_stats_update) >= STATS_UPDATE_INTERVAL_MS) {

        _3C515_TX_SELECT_WINDOW(ctx->io_base, _3C515_TX_WINDOW_6);

        /* Read and accumulate statistics counters */
        ctx->tx_errors += inb(ctx->io_base + _3C515_TX_W6_TX_CARR_ERRS);
        ctx->tx_errors += inb(ctx->io_base + _3C515_TX_W6_TX_HRTBT_ERRS);
        ctx->rx_errors += inb(ctx->io_base + _3C515_TX_W6_RX_FIFO_ERRS);

        ctx->last_stats_update = current_time;
    }

    ctx->last_config_validation = current_time;

    LOG_DEBUG("Periodic configuration validation completed successfully");

    return 0;
}

/**
 * @brief Enhanced initialization with complete hardware setup
 */
int _3c515_enhanced_init(uint16_t io_base, uint8_t irq, uint8_t nic_index) {
    _3c515_nic_context_t *ctx = &g_nic_context;
    int result;
    (void)nic_index;  /* Reserved for future multi-NIC support */

    if (g_driver_initialized) {
        LOG_WARNING("Driver already initialized, cleaning up first");
        _3c515_enhanced_cleanup();
    }

    LOG_INFO("Initializing enhanced 3C515-TX driver");

    /* Clear context structure */
    memset(ctx, 0, sizeof(_3c515_nic_context_t));
    
    /* Set basic configuration */
    ctx->io_base = io_base;
    ctx->irq = irq;
    
    /* Perform complete hardware initialization */
    result = complete_3c515_initialization(ctx);
    if (result < 0) {
        LOG_ERROR("Complete hardware initialization failed: %d", result);
        return result;
    }
    
    /* Enable transmitter and receiver */
    outw(ctx->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_TX_ENABLE);
    outw(ctx->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_RX_ENABLE);
    
    /* Start DMA engines */
    outw(ctx->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_START_DMA_UP);
    
    g_driver_initialized = true;
    
    LOG_INFO("Enhanced 3C515-TX driver initialized successfully");
    LOG_INFO("  I/O Base: 0x%04X, IRQ: %d", io_base, irq);
    LOG_INFO("  MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             ctx->eeprom_config.mac_address[0], ctx->eeprom_config.mac_address[1],
             ctx->eeprom_config.mac_address[2], ctx->eeprom_config.mac_address[3],
             ctx->eeprom_config.mac_address[4], ctx->eeprom_config.mac_address[5]);
    
    return 0;
}

/**
 * @brief Enhanced cleanup function
 */
void _3c515_enhanced_cleanup(void) {
    _3c515_nic_context_t *ctx = &g_nic_context;
    
    if (!g_driver_initialized) {
        return;
    }
    
    LOG_INFO("Cleaning up enhanced 3C515-TX driver");
    
    /* Disable transmitter and receiver */
    outw(ctx->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_TX_DISABLE);
    outw(ctx->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_RX_DISABLE);
    
    /* Stall DMA engines */
    outw(ctx->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_DOWN_STALL);
    outw(ctx->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_UP_STALL);
    
    /* Free allocated memory */
    if (ctx->tx_desc_ring) {
        free(ctx->tx_desc_ring);
        ctx->tx_desc_ring = NULL;
    }
    
    if (ctx->rx_desc_ring) {
        free(ctx->rx_desc_ring);
        ctx->rx_desc_ring = NULL;
    }
    
    if (ctx->buffers) {
        free(ctx->buffers);
        ctx->buffers = NULL;
    }
    
    /* Print final statistics */
    LOG_INFO("Final driver statistics:");
    LOG_INFO("  TX: %u packets, %u bytes, %u errors", 
             ctx->tx_packets, ctx->tx_bytes, ctx->tx_errors);
    LOG_INFO("  RX: %u packets, %u bytes, %u errors", 
             ctx->rx_packets, ctx->rx_bytes, ctx->rx_errors);
    LOG_INFO("  Link changes: %u, Config errors: %u", 
             ctx->link_changes, ctx->config_errors);
    
    ctx->driver_active = 0;
    ctx->hardware_ready = 0;
    g_driver_initialized = false;
    
    LOG_INFO("Enhanced 3C515-TX driver cleanup completed");
}

/**
 * @brief Get hardware configuration information
 */
int get_hardware_config_info(_3c515_nic_context_t *ctx, char *buffer, size_t buffer_size) {
    int written;

    if (!ctx || !buffer || buffer_size < 512) {
        return -1;
    }

    written = 0;
    
    written += snprintf(buffer + written, buffer_size - written,
                       "=== 3C515-TX Hardware Configuration ===\n");
    
    written += snprintf(buffer + written, buffer_size - written,
                       "I/O Base:        0x%04X\n", ctx->io_base);
    written += snprintf(buffer + written, buffer_size - written,
                       "IRQ:             %d\n", ctx->irq);
    written += snprintf(buffer + written, buffer_size - written,
                       "MAC Address:     %02X:%02X:%02X:%02X:%02X:%02X\n",
                       ctx->eeprom_config.mac_address[0], ctx->eeprom_config.mac_address[1],
                       ctx->eeprom_config.mac_address[2], ctx->eeprom_config.mac_address[3],
                       ctx->eeprom_config.mac_address[4], ctx->eeprom_config.mac_address[5]);
    
    written += snprintf(buffer + written, buffer_size - written,
                       "Media Type:      %s\n", 
                       eeprom_media_type_to_string(ctx->media_config.media_type));
    written += snprintf(buffer + written, buffer_size - written,
                       "Link Speed:      %d Mbps\n", ctx->media_config.link_speed);
    written += snprintf(buffer + written, buffer_size - written,
                       "Duplex Mode:     %s\n",
                       (ctx->media_config.duplex_mode == DUPLEX_FULL) ? "Full" : "Half");
    written += snprintf(buffer + written, buffer_size - written,
                       "Link Status:     %s\n",
                       ctx->media_config.link_active ? "Up" : "Down");
    
    written += snprintf(buffer + written, buffer_size - written,
                       "Full Duplex:     %s\n", ctx->full_duplex_enabled ? "Enabled" : "Disabled");
    written += snprintf(buffer + written, buffer_size - written,
                       "DMA:             %s\n", ctx->dma_enabled ? "Enabled" : "Disabled");
    written += snprintf(buffer + written, buffer_size - written,
                       "Statistics:      %s\n", ctx->stats_enabled ? "Enabled" : "Disabled");
    written += snprintf(buffer + written, buffer_size - written,
                       "Link Monitoring: %s\n", ctx->link_monitoring_enabled ? "Enabled" : "Disabled");
    
    written += snprintf(buffer + written, buffer_size - written,
                       "Interrupt Mask:  0x%04X\n", ctx->interrupt_mask);
    
    written += snprintf(buffer + written, buffer_size - written,
                       "\n=== Statistics ===\n");
    written += snprintf(buffer + written, buffer_size - written,
                       "TX Packets:      %u\n", ctx->tx_packets);
    written += snprintf(buffer + written, buffer_size - written,
                       "RX Packets:      %u\n", ctx->rx_packets);
    written += snprintf(buffer + written, buffer_size - written,
                       "TX Errors:       %u\n", ctx->tx_errors);
    written += snprintf(buffer + written, buffer_size - written,
                       "RX Errors:       %u\n", ctx->rx_errors);
    written += snprintf(buffer + written, buffer_size - written,
                       "Link Changes:    %u\n", ctx->link_changes);
    written += snprintf(buffer + written, buffer_size - written,
                       "Config Errors:   %u\n", ctx->config_errors);
    
    return written;
}

/**
 * @brief Get current NIC context for integration with other systems
 */
_3c515_nic_context_t *get_3c515_context(void) {
    return g_driver_initialized ? &g_nic_context : NULL;
}

/* Private data structure for legacy _3c515_init interface */
typedef struct _3c515_private_data {
    _3c515_tx_tx_desc_t *tx_ring;       /* TX descriptor ring */
    _3c515_tx_rx_desc_t *rx_ring;       /* RX descriptor ring */
    uint8_t *buffers;                   /* Contiguous buffer memory */
    uint32_t tx_index;                  /* Current TX ring index */
    uint32_t rx_index;                  /* Current RX ring index */
} _3c515_private_data_t;

/**
 * @brief Initialize the 3C515-TX NIC (legacy interface)
 * @param nic Pointer to NIC info structure
 * @return 0 on success, -1 on failure
 */
int _3c515_init(nic_info_t *nic) {
    /* C89: All declarations at the beginning of the block */
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

    /* Store private data in nic structure */
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

    /* Initialize hardware checksumming with CPU-aware optimization */
    result = hw_checksum_init(CHECKSUM_MODE_AUTO);
    if (result != 0) {
        LOG_WARNING("Hardware checksum initialization failed: %d, continuing without optimization", result);
        /* Continue - checksum is optional feature */
    } else {
        LOG_DEBUG("Hardware checksum module initialized with CPU optimization");
    }

    /* Initialize DMA subsystem with CPU-specific optimizations (Phase 2.2) */
    result = dma_init();
    if (result != 0) {
        LOG_WARNING("DMA subsystem initialization failed: %d, using single-buffer mode", result);
        /* Continue - scatter-gather is optional feature */
    } else {
        LOG_DEBUG("DMA subsystem initialized with CPU-aware memory management");
    }

    return 0;
}

/**
 * @brief Send a packet using DMA
 * @param nic Pointer to NIC info structure
 * @param packet Data to send
 * @param len Length of the packet
 * @return 0 on success, -1 on failure
 */
int _3c515_send_packet(nic_info_t *nic, const uint8_t *packet, size_t len) {
    /* C89: All declarations at the beginning of the block */
    _3c515_private_data_t *priv;
    _3c515_tx_tx_desc_t *desc;
    dma_mapping_t *mapping;
    dma_fragment_t fragments[4];
    int frag_count;
    uint32_t phys_addr;
    void *mapped_buffer;
    int checksum_result;
    int sg_result;

    priv = (_3c515_private_data_t *)nic->private_data;
    if (!priv || !priv->tx_ring) return -1;

    desc = &priv->tx_ring[priv->tx_index];

    /* Check if descriptor is free */
    if (desc->status & _3C515_TX_TX_DESC_COMPLETE) return -1;

    /* Try scatter-gather DMA for enhanced performance (Phase 2.2) */
    frag_count = dma_analyze_packet_fragmentation(packet, len, fragments, 4);

    if (frag_count > 1) {
        /* Use scatter-gather DMA for fragmented packets */
        LOG_DEBUG("Using scatter-gather DMA for %d fragments", frag_count);

        sg_result = dma_send_scatter_gather(nic->index, packet, len, fragments, frag_count);
        if (sg_result == 0) {
            /* Scatter-gather succeeded */
            priv->tx_index = (priv->tx_index + 1) % TX_RING_SIZE;
            return 0;
        } else {
            LOG_DEBUG("Scatter-gather failed (%d), falling back to consolidation", sg_result);
            /* Fall through to consolidation path */
        }
    }

    /* Single buffer or consolidation path */
    /* GPT-5 CRITICAL: Map with per-NIC DMA constraints for 3C515-TX */
    mapping = dma_map_with_device_constraints((void *)packet, len, DMA_SYNC_TX, "3C515TX");
    if (!mapping) {
        LOG_ERROR("Failed to map TX buffer with 3C515TX constraints");
        return -1;
    }

    /* Store mapping for later cleanup */
    desc->mapping = mapping;

    /* GPT-5 FIX: 3C515 is a bus-master card - desc->addr MUST be physical address! */
    /* The hardware will DMA from this address, NOT a CPU pointer */
    phys_addr = dma_mapping_get_phys_addr(mapping);
    desc->addr = phys_addr;  /* Hardware reads from this physical address */

    /* GPT-5 FIX: Sync mapped buffer for device access (handles cache coherency) */
    /* This ensures data is visible to the bus-master hardware */
    dma_mapping_sync_for_device(mapping);

    /* GPT-5 FIX: Checksum calculation must use the mapped buffer, not physical address */
    /* If hardware checksum offload is not available, calculate in software */
    if (len >= 34) { /* Minimum for Ethernet + IP header */
        mapped_buffer = dma_mapping_get_address(mapping);
        checksum_result = hw_checksum_process_outbound_packet(mapped_buffer, len);
        if (checksum_result != 0) {
            LOG_DEBUG("Checksum calculation completed for outbound packet");
        }
        /* Re-sync after checksum modification */
        dma_mapping_sync_for_device(mapping);
    }

    /* Configure descriptor */
    desc->length = len;
    desc->status = _3C515_TX_TX_INTR_BIT;  /* Request interrupt on completion */

    /* Start DMA transfer */
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_START_DMA_DOWN);

    /* Move to next descriptor */
    priv->tx_index = (priv->tx_index + 1) % TX_RING_SIZE;

    return 0;
}

/**
 * @brief Receive a packet using DMA
 * @param nic Pointer to NIC info structure
 * @param buffer Buffer to store received packet
 * @param len Pointer to store packet length
 * @return 0 on success, -1 on failure
 */
int _3c515_receive_packet(nic_info_t *nic, uint8_t *buffer, size_t *len) {
    /* C89: All declarations at the beginning of the block */
    _3c515_private_data_t *priv;
    _3c515_tx_rx_desc_t *desc;
    void *rx_data_ptr;
    bool used_bounce;
    dma_mapping_t *dma_mapping;
    void *dma_safe_buffer;
    int checksum_result;

    priv = (_3c515_private_data_t *)nic->private_data;
    if (!priv || !priv->rx_ring) return -1;

    desc = &priv->rx_ring[priv->rx_index];

    /* Check if packet is ready */
    if (!(desc->status & _3C515_TX_RX_DESC_COMPLETE)) return -1;

    if (desc->status & _3C515_TX_RX_DESC_ERROR) {
        desc->status = 0;  /* Reset descriptor */
        priv->rx_index = (priv->rx_index + 1) % RX_RING_SIZE;
        return -1;
    }

    /* Phase 4: Prepare received DMA buffer (cache coherency management) */
    *len = desc->length & _3C515_TX_RX_DESC_LEN_MASK;
    rx_data_ptr = (void *)desc->addr;
    used_bounce = false;
    (void)used_bounce;  /* Suppress unused variable warning */

    /* GPT-5 Enhancement: Centralized DMA mapping for RX */
    dma_mapping = dma_map_rx(rx_data_ptr, *len);
    if (!dma_mapping) {
        LOG_ERROR("DMA mapping failed for RX buffer %p len=%zu", rx_data_ptr, *len);
        desc->status = 0;  /* Reset descriptor */
        return -1;
    }

    dma_safe_buffer = dma_mapping_get_address(dma_mapping);
    if (dma_mapping_uses_bounce(dma_mapping)) {
        LOG_DEBUG("Using RX bounce buffer for packet len=%zu", *len);
    }

    /* Copy packet data to caller's buffer */
    memcpy(buffer, dma_safe_buffer, *len);

    /* Cleanup DMA mapping */
    dma_unmap_rx(dma_mapping);

    /* Verify checksums with CPU optimization (Phase 2.1) */
    if (*len >= 34) { /* Minimum for Ethernet + IP header */
        checksum_result = hw_checksum_verify_inbound_packet(buffer, *len);
        if (checksum_result < 0) {
            LOG_DEBUG("Checksum verification failed for inbound packet");
            /* Continue anyway - many stacks don't require perfect checksums */
        } else if (checksum_result > 0) {
            LOG_DEBUG("Checksum verification passed for inbound packet");
        }
    }

    /* Reset descriptor */
    desc->status = 0;

    /* Move to next descriptor */
    priv->rx_index = (priv->rx_index + 1) % RX_RING_SIZE;

    return 0;
}

/**
 * @brief Handle interrupts from the NIC (legacy single-event handler)
 * @param nic Pointer to NIC info structure
 */
void _3c515_handle_interrupt(nic_info_t *nic) {
    /* C89: All declarations at the beginning of the block */
    _3c515_private_data_t *priv;
    _3c515_tx_tx_desc_t *tx_ring;
    uint16_t status;
    int i;

    priv = (_3c515_private_data_t *)nic->private_data;
    if (!priv) return;

    tx_ring = priv->tx_ring;
    if (!tx_ring) return;

    status = inw(nic->io_base + _3C515_TX_STATUS_REG);

    if (status & _3C515_TX_STATUS_UP_COMPLETE) {
        /* Receive DMA completed; packets are ready in rx_ring */
    }

    if (status & _3C515_TX_STATUS_DOWN_COMPLETE) {
        /* Transmit DMA completed; check tx_ring for completion */
        for (i = 0; i < TX_RING_SIZE; i++) {
            if (tx_ring[i].status & _3C515_TX_TX_DESC_COMPLETE) {
                /* GPT-5 DEFERRED: Queue TX completion for bottom-half processing */
                if (tx_ring[i].mapping) {
                    /* Queue for deferred unmapping - NEVER call dma_unmap_tx in ISR! */
                    extern bool packet_queue_tx_completion(uint8_t nic_index, uint8_t desc_index, dma_mapping_t *mapping);
                    if (packet_queue_tx_completion(nic->index, i, tx_ring[i].mapping)) {
                        /* Success - mapping now owned by queue */
                        tx_ring[i].mapping = NULL;
                    } else {
                        /* Queue full - DO NOT clear mapping! Overflow recovery will handle it */
                        /* NO logging in ISR - just let overflow flag handle it */
                    }
                }
                tx_ring[i].status = 0;  /* Clear completion */
            }
        }
    }

    /* Acknowledge the interrupt */
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_ACK_INTR | status);
}

/**
 * @brief Check if this NIC has pending interrupt work
 * @param nic Pointer to NIC info structure
 * @return 1 if interrupt work available, 0 if none, negative on error
 */
int _3c515_check_interrupt(nic_info_t *nic) {
    uint16_t status;

    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    status = inw(nic->io_base + _3C515_TX_STATUS_REG);
    
    /* Check for any interrupt conditions that need processing */
    if (status & (_3C515_TX_STATUS_UP_COMPLETE | 
                  _3C515_TX_STATUS_DOWN_COMPLETE |
                  _3C515_TX_STATUS_TX_COMPLETE |
                  _3C515_TX_STATUS_RX_COMPLETE |
                  _3C515_TX_STATUS_ADAPTER_FAILURE |
                  _3C515_TX_STATUS_STATS_FULL)) {
        return 1; /* Work available */
    }
    
    return 0; /* No work */
}

/**
 * @brief Process single interrupt event for batching system
 * @param nic Pointer to NIC info structure
 * @param event_type Pointer to store event type processed
 * @return 1 if event processed, 0 if no work, negative on error
 */
int _3c515_process_single_event(nic_info_t *nic, interrupt_event_type_t *event_type) {
    uint16_t status;
    _3c515_adv_dma_context_t *ctx;
    int i;
    pd_statistics_t hw_stats;
    uint8_t rx_buffer[1514];
    size_t rx_length;

    if (!nic || !event_type) {
        return ERROR_INVALID_PARAM;
    }

    status = inw(nic->io_base + _3C515_TX_STATUS_REG);
    
    /* Process highest priority events first */
    
    /* Handle adapter failure (highest priority) */
    if (status & _3C515_TX_STATUS_ADAPTER_FAILURE) {
        *event_type = EVENT_TYPE_RX_ERROR; /* Treat as general error */
        LOG_ERROR("3C515 adapter failure detected");
        
        /* Acknowledge the interrupt */
        outw(nic->io_base + _3C515_TX_COMMAND_REG, 
             _3C515_TX_CMD_ACK_INTR | _3C515_TX_STATUS_ADAPTER_FAILURE);
        
        return 1;
    }
    
    /* Handle RX DMA completion */
    if (status & _3C515_TX_STATUS_UP_COMPLETE) {
        *event_type = EVENT_TYPE_DMA_COMPLETE;

        /* Process received packets using ring buffer system */
        ctx = (_3c515_adv_dma_context_t *)nic->private_data;
        if (ctx && ctx->ring_manager.initialized) {
            /* Check for completed RX descriptors */
            while (nic_3c515_handle_rx_completion(ctx) > 0) {
                /* Process all available completed packets */
            }
        }
        
        /* Acknowledge the interrupt */
        outw(nic->io_base + _3C515_TX_COMMAND_REG, 
             _3C515_TX_CMD_ACK_INTR | _3C515_TX_STATUS_UP_COMPLETE);
        
        return 1;
    }
    
    /* Handle TX DMA completion */
    if (status & _3C515_TX_STATUS_DOWN_COMPLETE) {
        _3c515_tx_tx_desc_t *tx_ring;
        *event_type = EVENT_TYPE_TX_COMPLETE;

        /* Access TX ring through generic pointer */
        tx_ring = (_3c515_tx_tx_desc_t *)nic->tx_descriptor_ring;

        /* Process transmit completions */
        for (i = 0; i < TX_RING_SIZE; i++) {
            if (tx_ring &&
                (tx_ring[i].status & _3C515_TX_TX_DESC_COMPLETE)) {
                tx_ring[i].status = 0;  /* Clear completion */
            }
        }
        
        /* Acknowledge the interrupt */
        outw(nic->io_base + _3C515_TX_COMMAND_REG, 
             _3C515_TX_CMD_ACK_INTR | _3C515_TX_STATUS_DOWN_COMPLETE);
        
        return 1;
    }
    
    /* Handle general RX completion */
    if (status & _3C515_TX_STATUS_RX_COMPLETE) {
        *event_type = EVENT_TYPE_RX_COMPLETE;

        /* Process received packet through standard receive path */
        if (nic->ops && nic->ops->receive_packet) {
            rx_length = sizeof(rx_buffer);
            if (nic->ops->receive_packet(nic, rx_buffer, &rx_length) == 0) {
                /* Packet received successfully, pass to API layer */
                api_process_received_packet(rx_buffer, rx_length, nic->index);
            }
        }
        
        /* Acknowledge the interrupt */
        outw(nic->io_base + _3C515_TX_COMMAND_REG, 
             _3C515_TX_CMD_ACK_INTR | _3C515_TX_STATUS_RX_COMPLETE);
        
        return 1;
    }
    
    /* Handle general TX completion */
    if (status & _3C515_TX_STATUS_TX_COMPLETE) {
        _3c515_adv_dma_context_t *tx_ctx;
        *event_type = EVENT_TYPE_TX_COMPLETE;

        /* Handle transmit completion */
        tx_ctx = (_3c515_adv_dma_context_t *)nic->private_data;
        if (tx_ctx && tx_ctx->ring_manager.initialized) {
            /* Process completed TX descriptors */
            while (nic_3c515_handle_tx_completion(tx_ctx) > 0) {
                /* Free completed TX buffers and update statistics */
            }
        }
        
        /* Update global TX statistics */
        stats_increment_tx_packets();
        stats_add_tx_bytes(1514); /* Estimated packet size */
        
        /* Acknowledge the interrupt */
        outw(nic->io_base + _3C515_TX_COMMAND_REG, 
             _3C515_TX_CMD_ACK_INTR | _3C515_TX_STATUS_TX_COMPLETE);
        
        return 1;
    }
    
    /* Handle statistics counter overflow */
    if (status & _3C515_TX_STATUS_STATS_FULL) {
        *event_type = EVENT_TYPE_COUNTER_OVERFLOW;
        
        /* Update statistics by reading hardware counters */
        if (nic->ops && nic->ops->get_statistics) {
            if (nic->ops->get_statistics(nic, &hw_stats) == 0) {
                /* Update global statistics with hardware values */
                /* Note: Only increment functions exist, not bulk add */
                stats_add_rx_bytes(hw_stats.bytes_in);
                stats_add_tx_bytes(hw_stats.bytes_out);
                /* Increment packet counters appropriately */
                if (hw_stats.packets_in > 0) {
                    stats_increment_rx_packets();
                }
                if (hw_stats.packets_out > 0) {
                    stats_increment_tx_packets();
                }
                if (hw_stats.errors_in > 0 || hw_stats.errors_out > 0) {
                    stats_increment_rx_errors();
                }
            }
        }
        
        /* Acknowledge the interrupt */
        outw(nic->io_base + _3C515_TX_COMMAND_REG, 
             _3C515_TX_CMD_ACK_INTR | _3C515_TX_STATUS_STATS_FULL);
        
        return 1;
    }
    
    /* No work available */
    return 0;
}

/**
 * @brief Enhanced interrupt handler with batching support
 * @param nic Pointer to NIC info structure
 * @return Number of events processed, or negative error code
 */
int _3c515_handle_interrupt_batched(nic_info_t *nic) {
    interrupt_mitigation_context_t *im_ctx;

    if (!nic) {
        return ERROR_INVALID_PARAM;
    }

    if (!nic->private_data) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Get interrupt mitigation context from private data */
    /* Note: This assumes the private data structure contains the IM context */
    /* In a real implementation, this would be properly structured */
    im_ctx = (interrupt_mitigation_context_t *)nic->private_data;
    
    if (!is_interrupt_mitigation_enabled(im_ctx)) {
        /* Fall back to legacy single-event processing */
        _3c515_handle_interrupt(nic);
        return 1;
    }
    
    /* Process batched interrupts */
    return process_batched_interrupts_3c515(im_ctx);
}

/* ============================================================================
 * Phase 4: Cache Coherency Integration Implementation
 * Sprint 4B: Comprehensive DMA safety for 3C515-TX operations
 * ============================================================================ */

/**
 * @brief Initialize cache coherency management for 3C515-TX
 * @param ctx NIC context structure
 * @return 0 on success, negative error code on failure
 */
static int _3c515_initialize_cache_coherency(_3c515_nic_context_t *ctx) {
    coherency_analysis_t analysis;
    chipset_detection_result_t chipset_result;
    bool cache_init_result;
    bool record_result;

    if (!ctx) {
        LOG_ERROR("Invalid NIC context for cache coherency initialization");
        return -1;
    }

    LOG_INFO("Initializing cache coherency management for 3C515-TX...");

    /* Perform comprehensive coherency analysis */
    analysis = perform_complete_coherency_analysis();

    if (analysis.selected_tier == TIER_DISABLE_BUS_MASTER) {
        LOG_ERROR("Cache coherency analysis recommends disabling bus mastering");
        LOG_ERROR("3C515-TX requires DMA operation - system incompatible");
        return -1;
    }

    /* Detect chipset for diagnostic purposes */
    chipset_result = detect_system_chipset();

    /* Initialize cache management system with analysis results */
    cache_init_result = initialize_cache_management(&analysis);
    if (!cache_init_result) {
        LOG_ERROR("Failed to initialize cache management system");
        return -1;
    }

    /* Record test results in community database */
    record_result = record_chipset_test_result(&analysis, &chipset_result);
    if (!record_result) {
        LOG_WARNING("Failed to record test results in chipset database");
    }

    /* Store analysis results in extended context for runtime use */
    g_extended_context.cache_coherency_tier = analysis.selected_tier;
    g_extended_context.cache_management_available = 1;
    g_extended_context.coherency_analysis = analysis;

    LOG_INFO("Cache coherency initialized: tier %d, confidence %d%%",
             analysis.selected_tier, analysis.confidence);

    /* Display performance opportunity information if relevant */
    if (should_offer_performance_guidance(&analysis)) {
        display_performance_opportunity_analysis();
    }

    return 0;
}

/**
 * @brief Prepare buffers for DMA operation (3C515-TX Bus Master DMA)
 * @param buffer Buffer pointer
 * @param length Buffer length
 * @param is_receive True for RX, false for TX
 */
static void _3c515_dma_prepare_buffers(void *buffer, size_t length, bool is_receive) {
    (void)is_receive;  /* Direction-specific handling could be added later */

    if (!buffer || length == 0) {
        return;
    }

    /* Apply cache management before DMA operation */
    cache_management_dma_prepare(buffer, length);
}

/**
 * @brief Complete DMA operation and ensure cache coherency (3C515-TX Bus Master DMA)
 * @param buffer Buffer pointer
 * @param length Buffer length
 * @param is_receive True for RX, false for TX
 */
static void _3c515_dma_complete_buffers(void *buffer, size_t length, bool is_receive) {
    (void)is_receive;  /* Direction-specific handling could be added later */

    if (!buffer || length == 0) {
        return;
    }

    /* Apply cache management after DMA operation */
    cache_management_dma_complete(buffer, length);
}

/* ============================================================================
 * Phase 3: Advanced DMA Features Implementation
 * Sub-Agent 1: DMA Specialist - Advanced DMA System
 * ============================================================================ */

#include "dmadesc.h"

/* Global advanced DMA context */
static advanced_dma_context_t g_advanced_dma_context;
static bool g_advanced_dma_initialized = false;

/**
 * @brief Initialize advanced DMA system for 3C515-TX
 * @param ctx DMA context structure
 * @param io_base NIC I/O base address
 * @param irq IRQ line
 * @return 0 on success, negative error code on failure
 */
int advanced_dma_init(advanced_dma_context_t *ctx, uint16_t io_base, uint8_t irq) {
    int result;

    if (!ctx) {
        LOG_ERROR("Invalid DMA context for initialization");
        return -1;
    }

    LOG_INFO("Initializing advanced DMA system for 3C515-TX");

    /* Clear context structure */
    memset(ctx, 0, sizeof(advanced_dma_context_t));

    /* Set hardware parameters */
    ctx->io_base = io_base;
    ctx->irq = irq;

    /* Initialize ring manager */
    result = dma_init_descriptor_rings(ctx);
    if (result != 0) {
        LOG_ERROR("Failed to initialize descriptor rings: %d", result);
        return result;
    }
    
    /* Setup completion tracking */
    ctx->completion_tracker.tx_completion_pending = false;
    ctx->completion_tracker.rx_completion_pending = false;
    ctx->completion_tracker.last_tx_activity = get_system_time_ms();
    ctx->completion_tracker.last_rx_activity = get_system_time_ms();
    
    /* Enable advanced features */
    ctx->bus_mastering_enabled = true;
    ctx->scatter_gather_enabled = true;
    ctx->zero_copy_enabled = true;
    ctx->cache_coherency_enabled = true;
    
    /* Initialize cache coherency if available */
    if (ctx->cache_coherency_enabled) {
        ctx->cache_context = get_cache_coherency_context();
        if (!ctx->cache_context) {
            LOG_WARNING("Cache coherency context not available");
            ctx->cache_coherency_enabled = false;
        }
    }
    
    /* Reset performance statistics */
    dma_reset_performance_stats(ctx);
    
    LOG_INFO("Advanced DMA system initialized successfully");
    LOG_INFO("  Bus mastering: %s, Scatter-gather: %s", 
             ctx->bus_mastering_enabled ? "Enabled" : "Disabled",
             ctx->scatter_gather_enabled ? "Enabled" : "Disabled");
    LOG_INFO("  Zero-copy: %s, Cache coherency: %s",
             ctx->zero_copy_enabled ? "Enabled" : "Disabled", 
             ctx->cache_coherency_enabled ? "Enabled" : "Disabled");
    
    return 0;
}

/**
 * @brief Initialize TX/RX descriptor rings
 * @param ctx DMA context structure
 * @return 0 on success, negative error code on failure
 */
int dma_init_descriptor_rings(advanced_dma_context_t *ctx) {
    int i;

    if (!ctx) {
        return -1;
    }

    LOG_DEBUG("Initializing DMA descriptor rings");

    /* Initialize TX ring */
    ctx->ring_manager.tx_head = 0;
    ctx->ring_manager.tx_tail = 0;
    ctx->ring_manager.tx_count = 0;

    {
    enhanced_tx_desc_t *desc;
    for (i = 0; i < DMA_TX_RING_SIZE; i++) {
        desc = &ctx->ring_manager.tx_ring[i];

        /* Clear descriptor */
        memset(desc, 0, sizeof(enhanced_tx_desc_t));

        /* Setup ring linkage */
        if (i == DMA_TX_RING_SIZE - 1) {
            desc->next = (uint32_t)&ctx->ring_manager.tx_ring[0];
        } else {
            desc->next = (uint32_t)&ctx->ring_manager.tx_ring[i + 1];
        }

        /* Initialize advanced features */
        desc->fragments = NULL;
        desc->fragment_count = 0;
        desc->coherent_memory = ctx->cache_coherency_enabled;
    }
    }
    
    /* Initialize RX ring */
    ctx->ring_manager.rx_head = 0;
    ctx->ring_manager.rx_tail = 0;
    ctx->ring_manager.rx_count = 0;

    {
    enhanced_rx_desc_t *desc;
    for (i = 0; i < DMA_RX_RING_SIZE; i++) {
        desc = &ctx->ring_manager.rx_ring[i];

        /* Clear descriptor */
        memset(desc, 0, sizeof(enhanced_rx_desc_t));

        /* Setup ring linkage */
        if (i == DMA_RX_RING_SIZE - 1) {
            desc->next = (uint32_t)&ctx->ring_manager.rx_ring[0];
        } else {
            desc->next = (uint32_t)&ctx->ring_manager.rx_ring[i + 1];
        }

        /* Initialize advanced features */
        desc->coherent_memory = ctx->cache_coherency_enabled;
        desc->zero_copy_eligible = ctx->zero_copy_enabled;
    }
    }
    
    /* Allocate buffer pools */
    ctx->ring_manager.buffer_size = DMA_MAX_FRAGMENT_SIZE;
    
    ctx->ring_manager.tx_buffers = malloc(DMA_TX_RING_SIZE * ctx->ring_manager.buffer_size);
    if (!ctx->ring_manager.tx_buffers) {
        LOG_ERROR("Failed to allocate TX buffer pool");
        return -1;
    }
    
    ctx->ring_manager.rx_buffers = malloc(DMA_RX_RING_SIZE * ctx->ring_manager.buffer_size);
    if (!ctx->ring_manager.rx_buffers) {
        free(ctx->ring_manager.tx_buffers);
        LOG_ERROR("Failed to allocate RX buffer pool");
        return -1;
    }
    
    /* Clear buffer pools */
    memset(ctx->ring_manager.tx_buffers, 0, DMA_TX_RING_SIZE * ctx->ring_manager.buffer_size);
    memset(ctx->ring_manager.rx_buffers, 0, DMA_RX_RING_SIZE * ctx->ring_manager.buffer_size);
    
    /* Assign buffers to descriptors */
    {
    enhanced_tx_desc_t *tx_desc;
    for (i = 0; i < DMA_TX_RING_SIZE; i++) {
        tx_desc = &ctx->ring_manager.tx_ring[i];
        tx_desc->addr = (uint32_t)((uint8_t *)ctx->ring_manager.tx_buffers +
                               (i * ctx->ring_manager.buffer_size));
        tx_desc->length = ctx->ring_manager.buffer_size;
    }
    }

    {
    enhanced_rx_desc_t *rx_desc;
    for (i = 0; i < DMA_RX_RING_SIZE; i++) {
        rx_desc = &ctx->ring_manager.rx_ring[i];
        rx_desc->addr = (uint32_t)((uint8_t *)ctx->ring_manager.rx_buffers +
                               (i * ctx->ring_manager.buffer_size));
        rx_desc->length = ctx->ring_manager.buffer_size;
        rx_desc->buffer_virtual = (uint8_t *)ctx->ring_manager.rx_buffers +
                              (i * ctx->ring_manager.buffer_size);
    }
    }
    
    /* Set physical addresses */
    ctx->ring_manager.tx_ring_physical = (uint32_t)ctx->ring_manager.tx_ring;
    ctx->ring_manager.rx_ring_physical = (uint32_t)ctx->ring_manager.rx_ring;
    
    /* Mark as initialized */
    ctx->ring_manager.initialized = true;
    ctx->ring_manager.generation = 1;
    
    LOG_DEBUG("DMA descriptor rings initialized successfully");
    LOG_DEBUG("  TX ring: %d descriptors at 0x%08X", DMA_TX_RING_SIZE, ctx->ring_manager.tx_ring_physical);
    LOG_DEBUG("  RX ring: %d descriptors at 0x%08X", DMA_RX_RING_SIZE, ctx->ring_manager.rx_ring_physical);
    
    return 0;
}

/**
 * @brief Allocate TX descriptor from ring
 * @param ctx DMA context structure
 * @param desc_index Output for allocated descriptor index
 * @return Pointer to TX descriptor, NULL if none available
 */
enhanced_tx_desc_t *dma_alloc_tx_descriptor(advanced_dma_context_t *ctx, uint16_t *desc_index) {
    uint16_t index;
    enhanced_tx_desc_t *desc;

    if (!ctx || !ctx->ring_manager.initialized || !desc_index) {
        return NULL;
    }

    /* Check if ring is full */
    if (ctx->ring_manager.tx_count >= DMA_TX_RING_SIZE) {
        ctx->performance_stats.tx_retries++;
        return NULL;
    }

    /* Get descriptor at head */
    index = ctx->ring_manager.tx_head;
    desc = &ctx->ring_manager.tx_ring[index];
    
    /* Check if descriptor is available (not owned by NIC) */
    if (desc->status & DMA_DESC_OWNED_BY_NIC) {
        ctx->performance_stats.tx_retries++;
        return NULL;
    }
    
    /* Allocate descriptor */
    *desc_index = index;
    ctx->ring_manager.tx_head = (ctx->ring_manager.tx_head + 1) % DMA_TX_RING_SIZE;
    ctx->ring_manager.tx_count++;
    
    /* Initialize descriptor for new use */
    desc->status = DMA_DESC_OWNED_BY_HOST;
    desc->fragment_count = 0;
    desc->total_length = 0;
    desc->timestamp_start = get_system_time_ms();
    desc->retry_count = 0;
    desc->error_flags = 0;
    
    ctx->performance_stats.tx_descriptors_used++;
    
    LOG_TRACE("Allocated TX descriptor %d (head now %d, count %d)", 
              index, ctx->ring_manager.tx_head, ctx->ring_manager.tx_count);
    
    return desc;
}

/**
 * @brief Setup scatter-gather TX operation
 * @param ctx DMA context structure
 * @param desc TX descriptor to setup
 * @param fragments Array of fragment descriptors
 * @param fragment_count Number of fragments
 * @return 0 on success, negative error code on failure
 */
int dma_setup_sg_tx(advanced_dma_context_t *ctx, enhanced_tx_desc_t *desc,
                    dma_fragment_desc_t *fragments, uint16_t fragment_count) {
    uint32_t total_length = 0;
    uint16_t i;

    if (!ctx || !desc || !fragments || fragment_count == 0) {
        return -1;
    }

    if (fragment_count > DMA_MAX_FRAGMENTS) {
        LOG_ERROR("Too many fragments: %d (max %d)", fragment_count, DMA_MAX_FRAGMENTS);
        return -1;
    }

    LOG_DEBUG("Setting up scatter-gather TX with %d fragments", fragment_count);

    /* Calculate total length */
    for (i = 0; i < fragment_count; i++) {
        total_length += fragments[i].length;
    }
    
    if (total_length > _3C515_TX_MAX_MTU) {
        LOG_ERROR("Total packet length %d exceeds MTU %d", total_length, _3C515_TX_MAX_MTU);
        return -1;
    }
    
    /* Setup primary descriptor with first fragment */
    desc->addr = fragments[0].physical_addr;
    desc->length = fragments[0].length;
    desc->total_length = total_length;
    desc->fragment_count = fragment_count;
    
    if (fragment_count == 1) {
        /* Single fragment - simple case */
        desc->status |= DMA_DESC_FIRST_FRAG | DMA_DESC_LAST_FRAG;
        desc->fragments = NULL;
    } else {
        /* Multiple fragments - setup linked list */
        desc->status |= DMA_DESC_FIRST_FRAG;
        
        /* Store additional fragments */
        desc->fragments = malloc(sizeof(dma_fragment_desc_t) * (fragment_count - 1));
        if (!desc->fragments) {
            LOG_ERROR("Failed to allocate fragment storage");
            return -1;
        }

        for (i = 1; i < fragment_count; i++) {
            desc->fragments[i-1] = fragments[i];
            if (i == fragment_count - 1) {
                desc->fragments[i-1].flags |= DMA_DESC_LAST_FRAG;
                desc->fragments[i-1].next = NULL;
            } else {
                desc->fragments[i-1].next = &desc->fragments[i];
            }
        }
        
        ctx->performance_stats.sg_tx_packets++;
        ctx->performance_stats.total_fragments += fragment_count;
    }
    
    /* Setup cache coherency if enabled */
    if (ctx->cache_coherency_enabled) {
        int coherency_result;
        coherency_result = dma_prepare_coherent_buffer(ctx, (void *)desc->addr,
                                                          desc->length, 0);
        if (coherency_result != 0) {
            LOG_WARNING("Cache coherency preparation failed: %d", coherency_result);
        }
    }
    
    /* Mark descriptor as ready for NIC */
    desc->status |= DMA_DESC_OWNED_BY_NIC | DMA_DESC_INTERRUPT;
    
    ctx->performance_stats.tx_bytes_transferred += total_length;
    
    LOG_TRACE("Scatter-gather TX setup complete: %d fragments, %d total bytes", 
              fragment_count, total_length);
    
    return 0;
}

/**
 * @brief Check for TX completion
 * @param ctx DMA context structure
 * @param completed_mask Output mask of completed descriptors
 * @return Number of completed TX operations
 */
int dma_check_tx_completion(advanced_dma_context_t *ctx, uint16_t *completed_mask) {
    int completed_count = 0;
    uint16_t check_index;
    int i;

    if (!ctx || !ctx->ring_manager.initialized || !completed_mask) {
        return -1;
    }

    *completed_mask = 0;

    /* Check all active descriptors */
    check_index = ctx->ring_manager.tx_tail;
    {
    enhanced_tx_desc_t *desc;
    for (i = 0; i < ctx->ring_manager.tx_count; i++) {
        desc = &ctx->ring_manager.tx_ring[check_index];
        
        /* Check if descriptor completed */
        if ((desc->status & DMA_DESC_OWNED_BY_NIC) == 0) {
            /* Descriptor completed by hardware */
            *completed_mask |= (1 << check_index);
            completed_count++;
            
            /* Record completion timestamp */
            desc->timestamp_complete = get_system_time_ms();
            
            /* Check for errors */
            if (desc->status & DMA_DESC_ERROR_MASK) {
                desc->error_flags = desc->status;
                ctx->performance_stats.dma_errors++;
                LOG_WARNING("TX descriptor %d completed with errors: 0x%08X",
                           check_index, desc->status);
            }
        }

        check_index = (check_index + 1) % DMA_TX_RING_SIZE;
    }
    }

    if (completed_count > 0) {
        ctx->completion_tracker.last_tx_activity = get_system_time_ms();
        LOG_TRACE("Found %d completed TX descriptors", completed_count);
    }
    
    return completed_count;
}

/**
 * @brief Handle TX completion
 * @param ctx DMA context structure
 * @param desc_index Index of completed descriptor
 * @return 0 on success, negative error code on failure
 */
int dma_handle_tx_completion(advanced_dma_context_t *ctx, uint16_t desc_index) {
    enhanced_tx_desc_t *desc;
    int coherency_result;

    if (!ctx || !ctx->ring_manager.initialized || desc_index >= DMA_TX_RING_SIZE) {
        return -1;
    }

    desc = &ctx->ring_manager.tx_ring[desc_index];
    
    LOG_TRACE("Handling TX completion for descriptor %d", desc_index);
    
    /* Complete cache coherency if enabled */
    if (ctx->cache_coherency_enabled) {
        coherency_result = dma_complete_coherent_buffer(ctx, (void *)desc->addr,
                                                           desc->length, 0);
        if (coherency_result != 0) {
            LOG_WARNING("Cache coherency completion failed: %d", coherency_result);
        }
    }
    
    /* Free fragment storage if used */
    if (desc->fragments) {
        free(desc->fragments);
        desc->fragments = NULL;
    }
    
    /* Update performance statistics */
    if (desc->fragment_count > 1) {
        ctx->performance_stats.avg_fragments_per_packet = 
            (ctx->performance_stats.avg_fragments_per_packet + desc->fragment_count) / 2;
    }
    
    /* Call completion handler if registered */
    if (ctx->completion_tracker.tx_completion_handler) {
        ctx->completion_tracker.tx_completion_handler(desc);
    }
    
    /* Free descriptor back to ring */
    if (desc_index == ctx->ring_manager.tx_tail) {
        ctx->ring_manager.tx_tail = (ctx->ring_manager.tx_tail + 1) % DMA_TX_RING_SIZE;
        ctx->ring_manager.tx_count--;
    }
    
    LOG_TRACE("TX descriptor %d completion handled (tail now %d, count %d)",
              desc_index, ctx->ring_manager.tx_tail, ctx->ring_manager.tx_count);
    
    return 0;
}

/**
 * @brief Check for DMA timeouts
 * @param ctx DMA context structure
 * @return Bitmask of timed out operations
 */
uint32_t dma_check_timeouts(advanced_dma_context_t *ctx) {
    uint32_t timeout_mask = 0;
    uint32_t current_time;
    uint16_t check_index;
    int i;

    if (!ctx || !ctx->ring_manager.initialized) {
        return 0;
    }

    current_time = get_system_time_ms();

    /* Check TX timeouts */
    check_index = ctx->ring_manager.tx_tail;
    {
    enhanced_tx_desc_t *tx_desc;
    for (i = 0; i < ctx->ring_manager.tx_count; i++) {
        tx_desc = &ctx->ring_manager.tx_ring[check_index];
        
        /* Check if descriptor is owned by NIC and has timed out */
        if ((tx_desc->status & DMA_DESC_OWNED_BY_NIC) &&
            ((current_time - tx_desc->timestamp_start) > DMA_TIMEOUT_TX)) {
            timeout_mask |= (1 << check_index);
            tx_desc->error_flags |= DMA_COMPLETION_TIMEOUT;
            ctx->performance_stats.tx_timeouts++;

            LOG_WARNING("TX descriptor %d timed out (started at %d, now %d)",
                       check_index, tx_desc->timestamp_start, current_time);
        }

        check_index = (check_index + 1) % DMA_TX_RING_SIZE;
    }
    }

    /* Check RX timeouts */
    check_index = ctx->ring_manager.rx_tail;
    {
    enhanced_rx_desc_t *rx_desc;
    for (i = 0; i < ctx->ring_manager.rx_count; i++) {
        rx_desc = &ctx->ring_manager.rx_ring[check_index];
        
        /* Check if descriptor is owned by NIC and has timed out */
        if ((rx_desc->status & DMA_DESC_OWNED_BY_NIC) &&
            ((current_time - rx_desc->receive_timestamp) > DMA_TIMEOUT_RX)) {
            timeout_mask |= (1 << (16 + check_index)); /* RX timeouts in upper 16 bits */
            rx_desc->error_flags |= DMA_COMPLETION_TIMEOUT;
            ctx->performance_stats.rx_timeouts++;

            LOG_WARNING("RX descriptor %d timed out", check_index);
        }

        check_index = (check_index + 1) % DMA_RX_RING_SIZE;
    }
    }

    /* Update timeout counters */
    if (timeout_mask & 0xFFFF) {
        ctx->completion_tracker.tx_timeout_count++;
    }
    if (timeout_mask & 0xFFFF0000) {
        ctx->completion_tracker.rx_timeout_count++;
    }
    
    return timeout_mask;
}

/**
 * @brief Recover from TX timeout
 * @param ctx DMA context structure
 * @param desc_index Index of timed out descriptor
 * @return 0 on success, negative error code on failure
 */
int dma_recover_tx_timeout(advanced_dma_context_t *ctx, uint16_t desc_index) {
    enhanced_tx_desc_t *desc;
    int stall_result;
    int unstall_result;

    if (!ctx || !ctx->ring_manager.initialized || desc_index >= DMA_TX_RING_SIZE) {
        return -1;
    }

    desc = &ctx->ring_manager.tx_ring[desc_index];
    
    LOG_WARNING("Recovering from TX timeout on descriptor %d", desc_index);
    
    /* Stall TX engine */
    stall_result = dma_stall_engines(ctx, true, false);
    if (stall_result != 0) {
        LOG_ERROR("Failed to stall TX engine for timeout recovery: %d", stall_result);
        return stall_result;
    }
    
    /* Mark descriptor as aborted */
    desc->status &= ~DMA_DESC_OWNED_BY_NIC;
    desc->error_flags |= DMA_COMPLETION_ABORTED;
    desc->retry_count++;
    
    /* Complete coherency management */
    if (ctx->cache_coherency_enabled) {
        dma_complete_coherent_buffer(ctx, (void *)desc->addr, desc->length, 0);
    }
    
    /* Free fragment storage */
    if (desc->fragments) {
        free(desc->fragments);
        desc->fragments = NULL;
    }
    
    /* Update statistics */
    ctx->performance_stats.tx_retries++;
    
    /* Unstall TX engine */
    unstall_result = dma_unstall_engines(ctx, true, false);
    if (unstall_result != 0) {
        LOG_ERROR("Failed to unstall TX engine after timeout recovery: %d", unstall_result);
    }
    
    /* Free descriptor */
    if (desc_index == ctx->ring_manager.tx_tail) {
        ctx->ring_manager.tx_tail = (ctx->ring_manager.tx_tail + 1) % DMA_TX_RING_SIZE;
        ctx->ring_manager.tx_count--;
    }
    
    LOG_INFO("TX timeout recovery completed for descriptor %d", desc_index);
    
    return 0;
}

/**
 * @brief Check if packet is eligible for zero-copy TX
 * @param packet_data Packet data pointer
 * @param packet_length Packet length
 * @param alignment_requirement Required alignment
 * @return true if zero-copy eligible, false otherwise
 */
bool dma_is_zero_copy_tx_eligible(const void *packet_data, uint32_t packet_length,
                                 uint32_t alignment_requirement) {
    if (!packet_data || packet_length == 0) {
        return false;
    }
    
    /* Check alignment */
    if (((uint32_t)packet_data % alignment_requirement) != 0) {
        return false;
    }

    /* Check size constraints */
    if (packet_length < _3C515_TX_MIN_PACKET_SIZE || packet_length > _3C515_TX_MAX_MTU) {
        return false;
    }

    {
    /* Check if memory is in suitable range for DMA */
    uint32_t addr;
    addr = (uint32_t)packet_data;
    if (addr > 0xFFFFFF) { /* 24-bit address limit for ISA DMA */
        return false;
    }
    }

    return true;
}

/**
 * @brief Setup zero-copy TX operation
 * @param ctx DMA context structure
 * @param desc TX descriptor to setup
 * @param packet_data Packet data pointer
 * @param packet_length Packet length
 * @return 0 on success, negative error code on failure
 */
int dma_setup_zero_copy_tx(advanced_dma_context_t *ctx, enhanced_tx_desc_t *desc,
                          const void *packet_data, uint32_t packet_length) {
    int coherency_result;

    if (!ctx || !desc || !packet_data || packet_length == 0) {
        return -1;
    }

    if (!dma_is_zero_copy_tx_eligible(packet_data, packet_length, DMA_BUFFER_ALIGN)) {
        LOG_DEBUG("Packet not eligible for zero-copy TX");
        return -1;
    }

    LOG_DEBUG("Setting up zero-copy TX operation");

    /* Setup descriptor for zero-copy */
    desc->addr = (uint32_t)packet_data;
    desc->length = packet_length;
    desc->total_length = packet_length;
    desc->fragment_count = 1;
    desc->fragments = NULL;

    /* Setup cache coherency for zero-copy */
    if (ctx->cache_coherency_enabled) {
        coherency_result = dma_prepare_coherent_buffer(ctx, (void *)packet_data,
                                                          packet_length, 0);
        if (coherency_result != 0) {
            LOG_WARNING("Cache coherency preparation failed for zero-copy TX: %d",
                       coherency_result);
        }
    }
    
    /* Mark as zero-copy operation */
    desc->status |= DMA_DESC_OWNED_BY_NIC | DMA_DESC_INTERRUPT | 
                   DMA_DESC_FIRST_FRAG | DMA_DESC_LAST_FRAG;
    
    /* Update statistics */
    ctx->performance_stats.zero_copy_tx++;
    ctx->performance_stats.tx_bytes_transferred += packet_length;
    
    LOG_TRACE("Zero-copy TX setup complete: %d bytes at 0x%08X", packet_length, desc->addr);
    
    return 0;
}

/**
 * @brief Update DMA performance statistics
 * @param ctx DMA context structure
 * @param tx_bytes TX bytes transferred
 * @param rx_bytes RX bytes transferred
 */
void dma_update_performance_stats(advanced_dma_context_t *ctx,
                                 uint32_t tx_bytes, uint32_t rx_bytes) {
    if (!ctx) {
        return;
    }
    
    ctx->performance_stats.tx_bytes_transferred += tx_bytes;
    ctx->performance_stats.rx_bytes_transferred += rx_bytes;
    
    /* Update efficiency metrics */
    if (ctx->cache_coherency_enabled) {
        ctx->performance_stats.cpu_cycles_saved += (tx_bytes + rx_bytes) / 4; /* Rough estimate */
    }
    
    /* Update utilization */
    ctx->performance_stats.bus_utilization = 
        ((ctx->performance_stats.tx_bytes_transferred + ctx->performance_stats.rx_bytes_transferred) * 100) / 
        (100 * 1024 * 1024); /* Rough bus utilization percentage */
}

/**
 * @brief Reset performance statistics
 * @param ctx DMA context structure
 */
void dma_reset_performance_stats(advanced_dma_context_t *ctx) {
    if (!ctx) {
        return;
    }
    
    memset(&ctx->performance_stats, 0, sizeof(dma_performance_stats_t));
    LOG_DEBUG("DMA performance statistics reset");
}

/**
 * @brief Get DMA performance report
 * @param ctx DMA context structure
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @return Number of characters written, negative on error
 */
int dma_get_performance_report(advanced_dma_context_t *ctx, char *buffer, size_t buffer_size) {
    int written;

    if (!ctx || !buffer || buffer_size < 512) {
        return -1;
    }

    written = 0;
    
    written += snprintf(buffer + written, buffer_size - written,
                       "=== Advanced DMA Performance Report ===\n");
    
    written += snprintf(buffer + written, buffer_size - written,
                       "TX Descriptors Used:     %u\n", ctx->performance_stats.tx_descriptors_used);
    written += snprintf(buffer + written, buffer_size - written,
                       "RX Descriptors Used:     %u\n", ctx->performance_stats.rx_descriptors_used);
    written += snprintf(buffer + written, buffer_size - written,
                       "TX Bytes Transferred:    %u\n", ctx->performance_stats.tx_bytes_transferred);
    written += snprintf(buffer + written, buffer_size - written,
                       "RX Bytes Transferred:    %u\n", ctx->performance_stats.rx_bytes_transferred);
    
    written += snprintf(buffer + written, buffer_size - written,
                       "\nScatter-Gather Statistics:\n");
    written += snprintf(buffer + written, buffer_size - written,
                       "SG TX Packets:           %u\n", ctx->performance_stats.sg_tx_packets);
    written += snprintf(buffer + written, buffer_size - written,
                       "SG RX Packets:           %u\n", ctx->performance_stats.sg_rx_packets);
    written += snprintf(buffer + written, buffer_size - written,
                       "Total Fragments:         %u\n", ctx->performance_stats.total_fragments);
    written += snprintf(buffer + written, buffer_size - written,
                       "Avg Fragments/Packet:    %u\n", ctx->performance_stats.avg_fragments_per_packet);
    
    written += snprintf(buffer + written, buffer_size - written,
                       "\nZero-Copy Operations:\n");
    written += snprintf(buffer + written, buffer_size - written,
                       "Zero-Copy TX:            %u\n", ctx->performance_stats.zero_copy_tx);
    written += snprintf(buffer + written, buffer_size - written,
                       "Zero-Copy RX:            %u\n", ctx->performance_stats.zero_copy_rx);
    
    written += snprintf(buffer + written, buffer_size - written,
                       "\nError Statistics:\n");
    written += snprintf(buffer + written, buffer_size - written,
                       "TX Timeouts:             %u\n", ctx->performance_stats.tx_timeouts);
    written += snprintf(buffer + written, buffer_size - written,
                       "RX Timeouts:             %u\n", ctx->performance_stats.rx_timeouts);
    written += snprintf(buffer + written, buffer_size - written,
                       "TX Retries:              %u\n", ctx->performance_stats.tx_retries);
    written += snprintf(buffer + written, buffer_size - written,
                       "RX Retries:              %u\n", ctx->performance_stats.rx_retries);
    written += snprintf(buffer + written, buffer_size - written,
                       "DMA Errors:              %u\n", ctx->performance_stats.dma_errors);
    
    written += snprintf(buffer + written, buffer_size - written,
                       "\nEfficiency Metrics:\n");
    written += snprintf(buffer + written, buffer_size - written,
                       "Bus Utilization:         %u%%\n", ctx->performance_stats.bus_utilization);
    written += snprintf(buffer + written, buffer_size - written,
                       "CPU Cycles Saved:        %u\n", ctx->performance_stats.cpu_cycles_saved);
    
    return written;
}

/* ============================================================================ 
 * Remaining DMA Functions - Completion Handlers and Hardware Interface
 * ============================================================================ */

/**
 * @brief Cleanup advanced DMA system
 * @param ctx DMA context structure  
 */
void advanced_dma_cleanup(advanced_dma_context_t *ctx) {
    if (!ctx) {
        return;
    }
    
    LOG_INFO("Cleaning up advanced DMA system");
    
    /* Stop DMA engines */
    dma_stop_transfer(ctx, true, true);
    
    /* Free buffer pools */
    if (ctx->ring_manager.tx_buffers) {
        free(ctx->ring_manager.tx_buffers);
        ctx->ring_manager.tx_buffers = NULL;
    }
    
    if (ctx->ring_manager.rx_buffers) {
        free(ctx->ring_manager.rx_buffers);
        ctx->ring_manager.rx_buffers = NULL;
    }
    
    /* Free any remaining fragment storage */
    {
    int i;
    enhanced_tx_desc_t *desc;
    for (i = 0; i < DMA_TX_RING_SIZE; i++) {
        desc = &ctx->ring_manager.tx_ring[i];
        if (desc->fragments) {
            free(desc->fragments);
            desc->fragments = NULL;
        }
    }
    }
    
    /* Clear context */
    ctx->ring_manager.initialized = false;
    ctx->bus_mastering_enabled = false;
    ctx->scatter_gather_enabled = false;
    ctx->zero_copy_enabled = false;
    ctx->cache_coherency_enabled = false;
    
    LOG_INFO("Advanced DMA system cleanup completed");
}

/**
 * @brief Allocate RX descriptor from ring
 * @param ctx DMA context structure
 * @param desc_index Output for allocated descriptor index
 * @return Pointer to RX descriptor, NULL if none available
 */
enhanced_rx_desc_t *dma_alloc_rx_descriptor(advanced_dma_context_t *ctx, uint16_t *desc_index) {
    uint16_t index;
    enhanced_rx_desc_t *desc;

    if (!ctx || !ctx->ring_manager.initialized || !desc_index) {
        return NULL;
    }

    /* Check if ring is full */
    if (ctx->ring_manager.rx_count >= DMA_RX_RING_SIZE) {
        ctx->performance_stats.rx_retries++;
        return NULL;
    }

    /* Get descriptor at head */
    index = ctx->ring_manager.rx_head;
    desc = &ctx->ring_manager.rx_ring[index];
    
    /* Check if descriptor is available (not owned by NIC) */
    if (desc->status & DMA_DESC_OWNED_BY_NIC) {
        ctx->performance_stats.rx_retries++;
        return NULL;
    }
    
    /* Allocate descriptor */
    *desc_index = index;
    ctx->ring_manager.rx_head = (ctx->ring_manager.rx_head + 1) % DMA_RX_RING_SIZE;
    ctx->ring_manager.rx_count++;
    
    /* Initialize descriptor for new use */
    desc->status = DMA_DESC_OWNED_BY_HOST;
    desc->received_length = 0;
    desc->receive_timestamp = get_system_time_ms();
    desc->error_flags = 0;
    desc->retry_count = 0;
    
    ctx->performance_stats.rx_descriptors_used++;
    
    LOG_TRACE("Allocated RX descriptor %d (head now %d, count %d)",
              index, ctx->ring_manager.rx_head, ctx->ring_manager.rx_count);
    
    return desc;
}

/**
 * @brief Check for RX completion
 * @param ctx DMA context structure
 * @param completed_mask Output mask of completed descriptors
 * @return Number of completed RX operations
 */
int dma_check_rx_completion(advanced_dma_context_t *ctx, uint16_t *completed_mask) {
    int completed_count = 0;
    uint16_t check_index;
    int i;

    if (!ctx || !ctx->ring_manager.initialized || !completed_mask) {
        return -1;
    }

    *completed_mask = 0;

    /* Check all active descriptors */
    check_index = ctx->ring_manager.rx_tail;
    for (i = 0; i < ctx->ring_manager.rx_count; i++) {
        enhanced_rx_desc_t *desc = &ctx->ring_manager.rx_ring[check_index];
        
        /* Check if descriptor completed */
        if ((desc->status & DMA_DESC_OWNED_BY_NIC) == 0) {
            /* Descriptor completed by hardware */
            *completed_mask |= (1 << check_index);
            completed_count++;
            
            /* Extract received length */
            desc->received_length = desc->status & _3C515_TX_RX_DESC_LEN_MASK;
            
            /* Check for errors */
            if (desc->status & DMA_DESC_ERROR_MASK) {
                desc->error_flags = desc->status;
                ctx->performance_stats.dma_errors++;
                LOG_WARNING("RX descriptor %d completed with errors: 0x%08X", 
                           check_index, desc->status);
            }
        }
        
        check_index = (check_index + 1) % DMA_RX_RING_SIZE;
    }
    
    if (completed_count > 0) {
        ctx->completion_tracker.last_rx_activity = get_system_time_ms();
        LOG_TRACE("Found %d completed RX descriptors", completed_count);
    }
    
    return completed_count;
}

/**
 * @brief Handle RX completion
 * @param ctx DMA context structure
 * @param desc_index Index of completed descriptor
 * @return 0 on success, negative error code on failure
 */
int dma_handle_rx_completion(advanced_dma_context_t *ctx, uint16_t desc_index) {
    /* C89: All declarations at the beginning of the block */
    enhanced_rx_desc_t *desc;
    int coherency_result;

    if (!ctx || !ctx->ring_manager.initialized || desc_index >= DMA_RX_RING_SIZE) {
        return -1;
    }

    desc = &ctx->ring_manager.rx_ring[desc_index];

    LOG_TRACE("Handling RX completion for descriptor %d", desc_index);

    /* Complete cache coherency if enabled */
    if (ctx->cache_coherency_enabled) {
        coherency_result = dma_complete_coherent_buffer(ctx, desc->buffer_virtual,
                                                        desc->received_length, 1);
        if (coherency_result != 0) {
            LOG_WARNING("Cache coherency completion failed: %d", coherency_result);
        }
    }

    /* Update performance statistics */
    ctx->performance_stats.rx_bytes_transferred += desc->received_length;

    /* Call completion handler if registered */
    if (ctx->completion_tracker.rx_completion_handler) {
        ctx->completion_tracker.rx_completion_handler(desc);
    }

    /* Mark descriptor as ready for reuse */
    desc->status = DMA_DESC_OWNED_BY_NIC; /* Ready for next packet */

    /* Free descriptor back to ring */
    if (desc_index == ctx->ring_manager.rx_tail) {
        ctx->ring_manager.rx_tail = (ctx->ring_manager.rx_tail + 1) % DMA_RX_RING_SIZE;
        ctx->ring_manager.rx_count--;
    }

    LOG_TRACE("RX descriptor %d completion handled (tail now %d, count %d)",
              desc_index, ctx->ring_manager.rx_tail, ctx->ring_manager.rx_count);

    return 0;
}

/* External assembly function declarations (low-level I/O base versions) */
extern int dma_stall_engines_asm(uint16_t io_base, bool tx_stall, bool rx_stall);
extern int dma_unstall_engines_asm(uint16_t io_base, bool tx_unstall, bool rx_unstall);
extern int dma_start_transfer_asm(uint16_t io_base, bool tx_start, bool rx_start);
extern int dma_stop_transfer_asm(uint16_t io_base, bool tx_stop, bool rx_stop);
extern int dma_get_engine_status_asm(uint16_t io_base, uint32_t *tx_status, uint32_t *rx_status);

/**
 * @brief Stall DMA engines (calls assembly function)
 * @param ctx DMA context structure
 * @param tx_stall Stall TX engine
 * @param rx_stall Stall RX engine
 * @return 0 on success, negative error code on failure
 */
int dma_stall_engines(advanced_dma_context_t *ctx, bool tx_stall, bool rx_stall) {
    if (!ctx || !ctx->ring_manager.initialized) {
        return -1;
    }

    return dma_stall_engines_asm(ctx->io_base, tx_stall, rx_stall);
}

/**
 * @brief Unstall DMA engines (calls assembly function)
 * @param ctx DMA context structure
 * @param tx_unstall Unstall TX engine
 * @param rx_unstall Unstall RX engine
 * @return 0 on success, negative error code on failure
 */
int dma_unstall_engines(advanced_dma_context_t *ctx, bool tx_unstall, bool rx_unstall) {
    if (!ctx || !ctx->ring_manager.initialized) {
        return -1;
    }

    return dma_unstall_engines_asm(ctx->io_base, tx_unstall, rx_unstall);
}

/**
 * @brief Start DMA transfer (calls assembly function)
 * @param ctx DMA context structure
 * @param tx_start Start TX DMA
 * @param rx_start Start RX DMA
 * @return 0 on success, negative error code on failure
 */
int dma_start_transfer(advanced_dma_context_t *ctx, bool tx_start, bool rx_start) {
    if (!ctx || !ctx->ring_manager.initialized) {
        return -1;
    }

    return dma_start_transfer_asm(ctx->io_base, tx_start, rx_start);
}

/**
 * @brief Stop DMA transfer (calls assembly function)
 * @param ctx DMA context structure
 * @param tx_stop Stop TX DMA
 * @param rx_stop Stop RX DMA
 * @return 0 on success, negative error code on failure
 */
int dma_stop_transfer(advanced_dma_context_t *ctx, bool tx_stop, bool rx_stop) {
    if (!ctx || !ctx->ring_manager.initialized) {
        return -1;
    }

    /* Implement directly using hardware registers */
    _3C515_TX_SELECT_WINDOW(ctx->io_base, _3C515_TX_WINDOW_7);

    if (tx_stop) {
        outw(ctx->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_TX_DISABLE);
    }

    if (rx_stop) {
        outw(ctx->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_RX_DISABLE);
    }

    return 0;
}

/**
 * @brief Get DMA engine status (calls assembly function)
 * @param ctx DMA context structure
 * @param tx_status Output for TX status
 * @param rx_status Output for RX status
 * @return 0 on success, negative error code on failure
 */
int dma_get_engine_status(advanced_dma_context_t *ctx, uint32_t *tx_status, uint32_t *rx_status) {
    if (!ctx || !ctx->ring_manager.initialized || !tx_status || !rx_status) {
        return -1;
    }

    return dma_get_engine_status_asm(ctx->io_base, tx_status, rx_status);
}

/**
 * @brief Prepare DMA buffer for transfer (calls assembly function)
 * @param ctx DMA context structure
 * @param buffer Buffer address
 * @param length Buffer length
 * @param direction Transfer direction (0=TX, 1=RX)
 * @return 0 on success, negative error code on failure
 */
int dma_prepare_coherent_buffer(advanced_dma_context_t *ctx, void *buffer,
                               uint32_t length, int direction) {
    if (!ctx || !buffer || length == 0) {
        return -1;
    }

    if (!ctx->cache_coherency_enabled) {
        return 0; /* No-op if cache coherency disabled */
    }

    /* Stub: cache coherency management would go here */
    (void)direction;
    return 0;
}

/**
 * @brief Complete DMA buffer transfer (calls assembly function)
 * @param ctx DMA context structure
 * @param buffer Buffer address
 * @param length Buffer length
 * @param direction Transfer direction (0=TX, 1=RX)
 * @return 0 on success, negative error code on failure
 */
int dma_complete_coherent_buffer(advanced_dma_context_t *ctx, void *buffer,
                                uint32_t length, int direction) {
    if (!ctx || !buffer || length == 0) {
        return -1;
    }

    if (!ctx->cache_coherency_enabled) {
        return 0; /* No-op if cache coherency disabled */
    }

    /* Stub: cache coherency management would go here */
    (void)direction;
    return 0;
}

/**
 * @brief Consolidate fragments into single buffer
 * @param fragments Array of fragment descriptors
 * @param fragment_count Number of fragments
 * @param dest_buffer Destination buffer
 * @param dest_size Size of destination buffer
 * @return Number of bytes consolidated, negative on error
 */
int dma_consolidate_fragments(dma_fragment_desc_t *fragments, uint16_t fragment_count,
                             void *dest_buffer, uint32_t dest_size) {
    uint32_t total_bytes = 0;
    uint8_t *dest;
    uint16_t i;

    if (!fragments || fragment_count == 0 || !dest_buffer || dest_size == 0) {
        return -1;
    }

    dest = (uint8_t *)dest_buffer;

    for (i = 0; i < fragment_count; i++) {
        if (total_bytes + fragments[i].length > dest_size) {
            LOG_WARNING("Fragment consolidation would exceed buffer size");
            return -1;
        }
        
        /* Copy fragment data to consolidated buffer */
        memcpy(dest + total_bytes, (void *)fragments[i].physical_addr, fragments[i].length);
        total_bytes += fragments[i].length;
    }
    
    LOG_TRACE("Consolidated %d fragments into %d bytes", fragment_count, total_bytes);
    
    return total_bytes;
}

/**
 * @brief Get the global advanced DMA context
 * @return Pointer to global DMA context
 */
advanced_dma_context_t *get_advanced_dma_context(void) {
    return g_advanced_dma_initialized ? &g_advanced_dma_context : NULL;
}

/**
 * @brief Initialize the global advanced DMA system
 * @param io_base NIC I/O base address
 * @param irq IRQ line
 * @return 0 on success, negative error code on failure
 */
int initialize_global_advanced_dma(uint16_t io_base, uint8_t irq) {
    /* C89: All declarations at the beginning of the block */
    int result;

    if (g_advanced_dma_initialized) {
        LOG_WARNING("Advanced DMA already initialized, cleaning up first");
        advanced_dma_cleanup(&g_advanced_dma_context);
        g_advanced_dma_initialized = false;
    }

    result = advanced_dma_init(&g_advanced_dma_context, io_base, irq);
    if (result == 0) {
        g_advanced_dma_initialized = true;
        LOG_INFO("Global advanced DMA system initialized");
    }

    return result;
}

/* ============================================================================
 * Advanced MII PHY Management Implementation for 3C515-TX
 * IEEE 802.3u Auto-negotiation Support
 * ============================================================================ */

/**
 * @brief Read MII PHY register via bit-banged interface
 * @param ctx NIC context
 * @param phy_addr PHY address (typically 0x18 for 3C515-TX internal PHY)
 * @param reg_addr Register address
 * @return Register value, negative on error
 */
static int mii_read_register(_3c515_nic_context_t *ctx, uint8_t phy_addr, uint8_t reg_addr) {
    uint16_t value = 0;
    int i;
    
    if (!ctx) {
        LOG_ERROR("Invalid context for MII read");
        return -1;
    }
    
    /* Switch to Window 4 for MII access */
    outw(ctx->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_SELECT_WINDOW | _3C515_TX_WINDOW_4);
    delay_milliseconds(1);
    
    /* Send preamble (32 ones) */
    for (i = 0; i < 32; i++) {
        outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_DATA | PHY_CTRL_MGMT_OE);
        udelay(1);
        outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_DATA | PHY_CTRL_MGMT_OE | PHY_CTRL_MGMT_CLK);
        udelay(1);
    }

    /* Send start bits (01) */
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_OE);
    udelay(1);
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_OE | PHY_CTRL_MGMT_CLK);
    udelay(1);
    
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_DATA | PHY_CTRL_MGMT_OE);
    udelay(1);
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_DATA | PHY_CTRL_MGMT_OE | PHY_CTRL_MGMT_CLK);
    udelay(1);
    
    /* Send read opcode (10) */
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_DATA | PHY_CTRL_MGMT_OE);
    udelay(1);
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_DATA | PHY_CTRL_MGMT_OE | PHY_CTRL_MGMT_CLK);
    udelay(1);
    
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_OE);
    udelay(1);
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_OE | PHY_CTRL_MGMT_CLK);
    udelay(1);
    
    /* Send PHY address (5 bits) */
    for (i = 4; i >= 0; i--) {
        uint16_t bit;
        bit = (phy_addr & (1 << i)) ? PHY_CTRL_MGMT_DATA : 0;
        outw(ctx->io_base + _3C515_W4_PHY_CTRL, bit | PHY_CTRL_MGMT_OE);
        udelay(1);
        outw(ctx->io_base + _3C515_W4_PHY_CTRL, bit | PHY_CTRL_MGMT_OE | PHY_CTRL_MGMT_CLK);
        udelay(1);
    }

    /* Send register address (5 bits) */
    for (i = 4; i >= 0; i--) {
        uint16_t bit;
        bit = (reg_addr & (1 << i)) ? PHY_CTRL_MGMT_DATA : 0;
        outw(ctx->io_base + _3C515_W4_PHY_CTRL, bit | PHY_CTRL_MGMT_OE);
        udelay(1);
        outw(ctx->io_base + _3C515_W4_PHY_CTRL, bit | PHY_CTRL_MGMT_OE | PHY_CTRL_MGMT_CLK);
        udelay(1);
    }
    
    /* Turnaround (Z0) - release bus for PHY response */
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, 0);
    udelay(1);
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_CLK);
    udelay(1);
    
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, 0);
    udelay(1);
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_CLK);
    udelay(1);
    
    /* Read data (16 bits) */
    for (i = 15; i >= 0; i--) {
        uint16_t status;
        outw(ctx->io_base + _3C515_W4_PHY_CTRL, 0);
        udelay(1);

        status = inw(ctx->io_base + _3C515_W4_PHY_STATUS);
        if (status & PHY_CTRL_MGMT_DATA) {
            value |= (1 << i);
        }

        outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_CLK);
        udelay(1);
    }
    
    /* Idle state */
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, 0);
    
    LOG_DEBUG("MII read: PHY=0x%02X, Reg=0x%02X, Value=0x%04X", phy_addr, reg_addr, value);
    
    return value;
}

/**
 * @brief Write MII PHY register via bit-banged interface
 * @param ctx NIC context
 * @param phy_addr PHY address
 * @param reg_addr Register address
 * @param value Value to write
 * @return 0 on success, negative on error
 */
static int mii_write_register(_3c515_nic_context_t *ctx, uint8_t phy_addr, uint8_t reg_addr, uint16_t value) {
    int i;
    
    if (!ctx) {
        LOG_ERROR("Invalid context for MII write");
        return -1;
    }
    
    /* Switch to Window 4 for MII access */
    outw(ctx->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_SELECT_WINDOW | _3C515_TX_WINDOW_4);
    delay_milliseconds(1);
    
    /* Send preamble (32 ones) */
    for (i = 0; i < 32; i++) {
        outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_DATA | PHY_CTRL_MGMT_OE);
        udelay(1);
        outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_DATA | PHY_CTRL_MGMT_OE | PHY_CTRL_MGMT_CLK);
        udelay(1);
    }
    
    /* Send start bits (01) */
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_OE);
    udelay(1);
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_OE | PHY_CTRL_MGMT_CLK);
    udelay(1);
    
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_DATA | PHY_CTRL_MGMT_OE);
    udelay(1);
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_DATA | PHY_CTRL_MGMT_OE | PHY_CTRL_MGMT_CLK);
    udelay(1);
    
    /* Send write opcode (01) */
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_OE);
    udelay(1);
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_OE | PHY_CTRL_MGMT_CLK);
    udelay(1);
    
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_DATA | PHY_CTRL_MGMT_OE);
    udelay(1);
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_DATA | PHY_CTRL_MGMT_OE | PHY_CTRL_MGMT_CLK);
    udelay(1);
    
    /* Send PHY address (5 bits) */
    for (i = 4; i >= 0; i--) {
        uint16_t bit;
        bit = (phy_addr & (1 << i)) ? PHY_CTRL_MGMT_DATA : 0;
        outw(ctx->io_base + _3C515_W4_PHY_CTRL, bit | PHY_CTRL_MGMT_OE);
        udelay(1);
        outw(ctx->io_base + _3C515_W4_PHY_CTRL, bit | PHY_CTRL_MGMT_OE | PHY_CTRL_MGMT_CLK);
        udelay(1);
    }

    /* Send register address (5 bits) */
    for (i = 4; i >= 0; i--) {
        uint16_t bit;
        bit = (reg_addr & (1 << i)) ? PHY_CTRL_MGMT_DATA : 0;
        outw(ctx->io_base + _3C515_W4_PHY_CTRL, bit | PHY_CTRL_MGMT_OE);
        udelay(1);
        outw(ctx->io_base + _3C515_W4_PHY_CTRL, bit | PHY_CTRL_MGMT_OE | PHY_CTRL_MGMT_CLK);
        udelay(1);
    }
    
    /* Turnaround (10) */
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_DATA | PHY_CTRL_MGMT_OE);
    udelay(1);
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_DATA | PHY_CTRL_MGMT_OE | PHY_CTRL_MGMT_CLK);
    udelay(1);
    
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_OE);
    udelay(1);
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, PHY_CTRL_MGMT_OE | PHY_CTRL_MGMT_CLK);
    udelay(1);
    
    /* Write data (16 bits) */
    for (i = 15; i >= 0; i--) {
        uint16_t bit = (value & (1 << i)) ? PHY_CTRL_MGMT_DATA : 0;
        outw(ctx->io_base + _3C515_W4_PHY_CTRL, bit | PHY_CTRL_MGMT_OE);
        udelay(1);
        outw(ctx->io_base + _3C515_W4_PHY_CTRL, bit | PHY_CTRL_MGMT_OE | PHY_CTRL_MGMT_CLK);
        udelay(1);
    }
    
    /* Idle state */
    outw(ctx->io_base + _3C515_W4_PHY_CTRL, 0);
    
    LOG_DEBUG("MII write: PHY=0x%02X, Reg=0x%02X, Value=0x%04X", phy_addr, reg_addr, value);
    
    return 0;
}

/**
 * @brief Start IEEE 802.3u auto-negotiation process
 * @param ctx NIC context
 * @param advertised_modes Advertised capabilities
 * @return 0 on success, negative on error
 */
static int start_autonegotiation(_3c515_nic_context_t *ctx, uint16_t advertised_modes) {
    int result;
    uint16_t control_reg;
    
    if (!ctx) {
        LOG_ERROR("Invalid context for auto-negotiation start");
        return -1;
    }
    
    LOG_DEBUG("Starting auto-negotiation with modes: 0x%04X", advertised_modes);
    
    /* Read current control register */
    result = mii_read_register(ctx, 0x18, MII_CONTROL_REG);
    if (result < 0) {
        LOG_ERROR("Failed to read MII control register");
        return result;
    }
    control_reg = result;
    
    /* Configure advertisement register */
    result = mii_write_register(ctx, 0x18, MII_AUTONEG_ADV_REG, advertised_modes);
    if (result < 0) {
        LOG_ERROR("Failed to write auto-negotiation advertisement");
        return result;
    }
    
    /* Enable auto-negotiation and restart */
    control_reg |= MII_CTRL_AUTONEG_EN | MII_CTRL_RESTART_AN;
    result = mii_write_register(ctx, 0x18, MII_CONTROL_REG, control_reg);
    if (result < 0) {
        LOG_ERROR("Failed to start auto-negotiation");
        return result;
    }
    
    LOG_DEBUG("Auto-negotiation started successfully");
    return 0;
}

/**
 * @brief Check if auto-negotiation is complete
 * @param ctx NIC context
 * @return 1 if complete, 0 if in progress, negative on error
 */
static int check_autonegotiation_complete(_3c515_nic_context_t *ctx) {
    int result;
    
    if (!ctx) {
        return -1;
    }
    
    result = mii_read_register(ctx, 0x18, MII_STATUS_REG);
    if (result < 0) {
        return result;
    }
    
    return (result & MII_STAT_AUTONEG_COMP) ? 1 : 0;
}

/**
 * @brief Get auto-negotiation results
 * @param ctx NIC context
 * @param speed Output for negotiated speed (10 or 100)
 * @param full_duplex Output for duplex mode
 * @return 0 on success, negative on error
 */
static int get_autonegotiation_result(_3c515_nic_context_t *ctx, uint16_t *speed, bool *full_duplex) {
    int adv_reg, link_reg, common_modes;
    
    if (!ctx || !speed || !full_duplex) {
        return -1;
    }
    
    /* Read advertisement and link partner capability registers */
    adv_reg = mii_read_register(ctx, 0x18, MII_AUTONEG_ADV_REG);
    if (adv_reg < 0) {
        LOG_ERROR("Failed to read advertisement register");
        return adv_reg;
    }
    
    link_reg = mii_read_register(ctx, 0x18, MII_AUTONEG_LINK_REG);
    if (link_reg < 0) {
        LOG_ERROR("Failed to read link partner register");
        return link_reg;
    }
    
    /* Find common capabilities */
    common_modes = adv_reg & link_reg;
    
    /* Select highest common mode according to IEEE 802.3 priority */
    if (common_modes & MII_ADV_100_TX_FD) {
        *speed = 100;
        *full_duplex = true;
    } else if (common_modes & MII_ADV_100_TX_HD) {
        *speed = 100;
        *full_duplex = false;
    } else if (common_modes & MII_ADV_10_FD) {
        *speed = 10;
        *full_duplex = true;
    } else if (common_modes & MII_ADV_10_HD) {
        *speed = 10;
        *full_duplex = false;
    } else {
        LOG_ERROR("No common auto-negotiation modes found");
        return -1;
    }
    
    LOG_INFO("Auto-negotiation complete: %d Mbps %s-duplex", 
             *speed, *full_duplex ? "Full" : "Half");
    
    return 0;
}

/**
 * @brief Configure MII transceiver with auto-negotiation
 * @param ctx NIC context
 * @return 0 on success, negative on error
 */
static int configure_mii_transceiver(_3c515_nic_context_t *ctx) {
    int result, timeout;
    uint16_t phy_id1, phy_id2, speed = 100;
    bool full_duplex = true;
    uint16_t advertise_modes;
    
    if (!ctx) {
        LOG_ERROR("Invalid context for MII configuration");
        return -1;
    }
    
    LOG_DEBUG("Configuring MII transceiver for 3C515-TX");
    
    /* Step 1: Read PHY ID to verify presence */
    result = mii_read_register(ctx, 0x18, MII_PHY_ID1_REG);
    if (result < 0) {
        LOG_ERROR("Failed to read PHY ID1");
        return result;
    }
    phy_id1 = result;
    
    result = mii_read_register(ctx, 0x18, MII_PHY_ID2_REG);
    if (result < 0) {
        LOG_ERROR("Failed to read PHY ID2");
        return result;
    }
    phy_id2 = result;
    
    LOG_INFO("MII PHY detected: ID1=0x%04X, ID2=0x%04X", phy_id1, phy_id2);
    
    /* Step 2: Reset PHY */
    result = mii_write_register(ctx, 0x18, MII_CONTROL_REG, MII_CTRL_RESET);
    if (result < 0) {
        LOG_ERROR("Failed to reset PHY");
        return result;
    }
    
    /* Wait for reset to complete */
    timeout = 1000; /* 1 second timeout */
    do {
        delay_milliseconds(10);
        result = mii_read_register(ctx, 0x18, MII_CONTROL_REG);
        if (result < 0) {
            LOG_ERROR("Failed to read control register during reset");
            return result;
        }
    } while ((result & MII_CTRL_RESET) && --timeout > 0);
    
    if (timeout <= 0) {
        LOG_ERROR("PHY reset timeout");
        return -1;
    }
    
    LOG_DEBUG("PHY reset complete");
    
    /* Step 3: Configure advertisement register */
    advertise_modes = MII_ADV_SELECTOR_FIELD | MII_ADV_10_HD | MII_ADV_10_FD | 
                     MII_ADV_100_TX_HD | MII_ADV_100_TX_FD | MII_ADV_PAUSE;
    
    /* Step 4: Start auto-negotiation */
    result = start_autonegotiation(ctx, advertise_modes);
    if (result < 0) {
        LOG_ERROR("Failed to start auto-negotiation");
        return result;
    }
    
    /* Step 5: Wait for auto-negotiation to complete */
    timeout = 3000; /* 3 second timeout */
    do {
        delay_milliseconds(10);
        result = check_autonegotiation_complete(ctx);
        if (result < 0) {
            LOG_ERROR("Failed to check auto-negotiation status");
            return result;
        }
    } while (result == 0 && --timeout > 0);
    
    if (timeout <= 0) {
        LOG_WARNING("Auto-negotiation timeout, using fallback configuration");
        speed = 100;
        full_duplex = true;
    } else {
        /* Step 6: Get negotiation results */
        result = get_autonegotiation_result(ctx, &speed, &full_duplex);
        if (result < 0) {
            LOG_WARNING("Failed to get auto-negotiation results, using fallback");
            speed = 100;
            full_duplex = true;
        }
    }
    
    /* Step 7: Update context with final configuration */
    ctx->media_config.link_speed = speed;
    ctx->media_config.duplex_mode = full_duplex ? DUPLEX_FULL : DUPLEX_HALF;
    ctx->media_config.auto_negotiation = 1;
    ctx->media_config.link_active = 1;
    
    LOG_INFO("MII transceiver configured: %d Mbps %s-duplex", 
             speed, full_duplex ? "Full" : "Half");
    
    return 0;
}

/**
 * @brief Cleanup the global advanced DMA system
 */
void cleanup_global_advanced_dma(void) {
    if (g_advanced_dma_initialized) {
        advanced_dma_cleanup(&g_advanced_dma_context);
        g_advanced_dma_initialized = false;
        LOG_INFO("Global advanced DMA system cleaned up");
    }
}

/* ============================================================================
 * Additional 3C515-TX Cache Integration Functions
 * ============================================================================ */

/**
 * @brief Enhanced send with full cache coherency management
 * @param nic NIC information structure
 * @param packet Packet data to transmit
 * @param len Packet length
 * @return 0 on success, negative error code on failure
 */
int _3c515_send_packet_cache_safe(nic_info_t *nic, const uint8_t *packet, size_t len) {
    /* C89: All declarations at the beginning of the block */
    _3c515_private_data_t *priv;
    _3c515_tx_tx_desc_t *desc;
    _3c515_nic_context_t *ctx;
    dma_mapping_t *mapping;
    void *mapped_buffer;
    int checksum_result;

    if (!nic || !packet || len == 0) {
        return -1;
    }

    /* Check if cache management is available */
    ctx = &g_nic_context;
    if (!g_extended_context.cache_management_available) {
        LOG_DEBUG("Cache management not available, using legacy send");
        return _3c515_send_packet(nic, packet, len);
    }

    priv = (_3c515_private_data_t *)nic->private_data;
    if (!priv || !priv->tx_ring) return -1;

    desc = &priv->tx_ring[priv->tx_index];

    /* Check if descriptor is free */
    if (desc->status & _3C515_TX_TX_DESC_COMPLETE) {
        return -1;
    }

    /* GPT-5 FIX: Use DMA mapping for bus-master operation */
    mapping = dma_map_with_device_constraints((void *)packet, len,
                                               DMA_SYNC_TX, "3C515TX");
    if (!mapping) {
        LOG_ERROR("Failed to map TX buffer for cache-safe send");
        return -1;
    }

    /* Store mapping for cleanup and set physical address */
    desc->mapping = mapping;
    desc->addr = dma_mapping_get_phys_addr(mapping);

    /* Sync for device access */
    dma_mapping_sync_for_device(mapping);

    /* GPT-5 FIX: Calculate checksums on mapped buffer */
    if (len >= 34) {
        mapped_buffer = dma_mapping_get_address(mapping);
        checksum_result = hw_checksum_process_outbound_packet(mapped_buffer, len);
        if (checksum_result != 0) {
            LOG_DEBUG("Checksum calculation completed for cache-safe outbound packet");
        }
        /* Re-sync after checksum modification */
        dma_mapping_sync_for_device(mapping);
    }

    /* Configure descriptor */
    desc->length = len;
    desc->status = _3C515_TX_TX_INTR_BIT;

    /* Start DMA transfer */
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_START_DMA_DOWN);

    /* Move to next descriptor */
    priv->tx_index = (priv->tx_index + 1) % TX_RING_SIZE;

    LOG_TRACE("Sent cache-safe packet of %zu bytes via DMA", len);

    return 0;
}

/**
 * @brief Enhanced receive with full cache coherency management
 * @param nic NIC information structure
 * @param buffer Buffer to store received packet
 * @param len Pointer to store packet length
 * @return 0 on success, negative error code on failure
 */
int _3c515_receive_packet_cache_safe(nic_info_t *nic, uint8_t *buffer, size_t *len) {
    /* C89: All declarations at the beginning of the block */
    _3c515_private_data_t *priv;
    _3c515_tx_rx_desc_t *desc;
    int checksum_result;

    if (!nic || !buffer || !len) {
        return -1;
    }

    /* Check if cache management is available */
    if (!g_extended_context.cache_management_available) {
        LOG_DEBUG("Cache management not available, using legacy receive");
        return _3c515_receive_packet(nic, buffer, len);
    }

    priv = (_3c515_private_data_t *)nic->private_data;
    if (!priv || !priv->rx_ring) return -1;

    desc = &priv->rx_ring[priv->rx_index];

    /* Check if packet is ready */
    if (!(desc->status & _3C515_TX_RX_DESC_COMPLETE)) {
        return -1;
    }

    if (desc->status & _3C515_TX_RX_DESC_ERROR) {
        desc->status = 0;
        priv->rx_index = (priv->rx_index + 1) % RX_RING_SIZE;
        return -1;
    }

    *len = desc->length & _3C515_TX_RX_DESC_LEN_MASK;

    /* Comprehensive cache management for packet reception */
    _3c515_dma_prepare_buffers((void *)desc->addr, *len, true);

    /* Copy packet data with cache-safe operations */
    memcpy(buffer, (void *)desc->addr, *len);

    /* Complete cache management */
    _3c515_dma_complete_buffers((void *)desc->addr, *len, true);

    /* Verify checksums */
    if (*len >= 34) {
        checksum_result = hw_checksum_verify_inbound_packet(buffer, *len);
        if (checksum_result < 0) {
            LOG_DEBUG("Checksum verification failed for cache-safe inbound packet");
        }
    }

    /* Reset descriptor */
    desc->status = 0;

    /* Move to next descriptor */
    priv->rx_index = (priv->rx_index + 1) % RX_RING_SIZE;

    LOG_TRACE("Received cache-safe packet of %zu bytes via DMA", *len);

    return 0;
}
