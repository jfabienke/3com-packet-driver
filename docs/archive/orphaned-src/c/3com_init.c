/**
 * @file 3com_init.c
 * @brief Unified initialization for all 3Com PCI generations
 *
 * Provides generation-specific initialization for Vortex, Boomerang,
 * Cyclone, and Tornado NICs. Combines patterns from 3C515, BoomTex,
 * and existing initialization code.
 *
 * 3Com Packet Driver - Unified Initialization
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include "../../include/3com_pci.h"
#include "../../include/hardware.h"
#include "../../include/logging.h"
#include "../../include/nic_init.h"
#include "../../include/cache_coherency.h"
#include "../../include/hw_checksum.h"
#include "../../include/flow_control.h"
#include "../../include/dma.h"
#include <dos.h>
#include <string.h>

/* Function prototypes for generation-specific init */
extern int vortex_init_pio(pci_3com_context_t *ctx);
extern int boomerang_init_dma(pci_3com_context_t *ctx);

/* Vortex PIO functions from 3com_vortex.c */
extern int vortex_start_xmit(pci_3com_context_t *ctx, packet_t *pkt);
extern int vortex_rx(pci_3com_context_t *ctx);
extern int vortex_interrupt_handler(pci_3com_context_t *ctx);

/* Performance optimization functions */
extern int apply_performance_optimizations(pci_3com_context_t *ctx);

/* Media configuration commands */
#define CMD_SELECT_WINDOW   (1<<11)
#define CMD_START_COAX      (2<<11)
#define CMD_STOP_COAX       ((2<<11) | 1)
#define CMD_SET_RX_FILTER   (16<<11)
#define CMD_SET_TX_RECLAIM  (18<<11)
#define CMD_STATS_ENABLE    (21<<11)

/* RX filter modes */
#define RX_FILTER_STATION   0x01    /* Accept station address */
#define RX_FILTER_MULTICAST 0x02    /* Accept multicast */
#define RX_FILTER_BROADCAST 0x04    /* Accept broadcast */
#define RX_FILTER_PROMISC   0x08    /* Promiscuous mode */

/**
 * @brief Read MAC address from EEPROM/registers
 */
static int read_mac_address(pci_3com_context_t *ctx)
{
    uint16_t ioaddr = ctx->base.io_base;
    uint8_t mac[6];
    int i;
    
    /* Select Window 2 for station address */
    select_window(ioaddr, 2);
    
    /* Try reading from registers first (faster) */
    for (i = 0; i < 6; i += 2) {
        uint16_t word = window_read16(ioaddr, 2, i);
        mac[i] = word & 0xFF;
        mac[i + 1] = (word >> 8) & 0xFF;
    }
    
    /* Validate MAC address */
    if (mac[0] == 0x00 && mac[1] == 0x00 && mac[2] == 0x00) {
        /* Invalid, try reading from EEPROM */
        LOG_DEBUG("3Com: Reading MAC from EEPROM");
        
        for (i = 0; i < 3; i++) {
            uint16_t word = read_eeprom(ioaddr, 0x0A + i);
            mac[i * 2] = (word >> 8) & 0xFF;
            mac[i * 2 + 1] = word & 0xFF;
        }
    }
    
    /* Store MAC address */
    memcpy(ctx->base.mac_addr, mac, 6);
    
    LOG_INFO("3Com: MAC address %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    return SUCCESS;
}

/**
 * @brief Configure media type and link settings
 */
static int configure_media(pci_3com_context_t *ctx)
{
    uint16_t ioaddr = ctx->base.io_base;
    uint16_t media_status;
    uint32_t config;
    
    /* Select Window 3 for internal configuration */
    select_window(ioaddr, 3);
    
    /* Read available media options */
    ctx->available_media = window_read16(ioaddr, 3, WN3_OPTIONS);
    
    /* Read current configuration */
    config = window_read32(ioaddr, 3, WN3_CONFIG);
    
    /* Select Window 4 for media status */
    select_window(ioaddr, 4);
    media_status = window_read16(ioaddr, 4, WN4_MEDIA);
    
    /* Determine media type */
    if (ctx->available_media & 0x01) {
        /* 10BaseT available */
        ctx->media_status = media_status;
        ctx->base.link_speed = 10;
        LOG_INFO("3Com: 10BaseT selected");
    }
    
    if (ctx->available_media & 0x08) {
        /* 100BaseTX available (Cyclone/Tornado) */
        ctx->base.link_speed = 100;
        LOG_INFO("3Com: 100BaseTX selected");
    }
    
    /* Check for MII (auto-negotiation capable) */
    if (ctx->capabilities & HAS_MII) {
        /* Attempt auto-negotiation */
        LOG_INFO("3Com: MII PHY detected, attempting auto-negotiation");
        
        uint16_t mii_status = mdio_read(ioaddr, 24, 1);  /* PHY address 24 */
        
        if (mii_status & 0x0020) {  /* Auto-neg complete */
            uint16_t mii_partner = mdio_read(ioaddr, 24, 5);
            
            /* Determine negotiated speed and duplex */
            if (mii_partner & 0x0100) {
                ctx->base.link_speed = 100;
                ctx->full_duplex = 1;
                LOG_INFO("3Com: Negotiated 100Mbps full-duplex");
            } else if (mii_partner & 0x0080) {
                ctx->base.link_speed = 100;
                ctx->full_duplex = 0;
                LOG_INFO("3Com: Negotiated 100Mbps half-duplex");
            } else if (mii_partner & 0x0040) {
                ctx->base.link_speed = 10;
                ctx->full_duplex = 1;
                LOG_INFO("3Com: Negotiated 10Mbps full-duplex");
            } else {
                ctx->base.link_speed = 10;
                ctx->full_duplex = 0;
                LOG_INFO("3Com: Negotiated 10Mbps half-duplex");
            }
        }
    }
    
    /* Configure full duplex if supported */
    if (ctx->full_duplex) {
        /* Select Window 3 for MAC control */
        select_window(ioaddr, 3);
        uint16_t mac_ctrl = window_read16(ioaddr, 3, WN3_MAC_CTRL);
        mac_ctrl |= 0x0020;  /* Enable full duplex */
        window_write16(ioaddr, 3, WN3_MAC_CTRL, mac_ctrl);
    }
    
    return SUCCESS;
}

/**
 * @brief Initialize hardware checksumming for Cyclone/Tornado
 */
static int init_hw_checksum(pci_3com_context_t *ctx)
{
    uint16_t ioaddr = ctx->base.io_base;
    int result;
    
    if (!(ctx->capabilities & HAS_HWCKSM)) {
        return SUCCESS;  /* Not supported */
    }
    
    LOG_INFO("3Com: Enabling hardware checksumming");
    
    /* Initialize the hardware checksum subsystem */
    result = hw_checksum_init(CHECKSUM_MODE_AUTO);
    if (result != HW_CHECKSUM_SUCCESS) {
        LOG_WARNING("3Com: Failed to initialize checksum subsystem: %d", result);
        return SUCCESS;  /* Continue without checksumming */
    }
    
    /* Configure NIC for hardware checksumming */
    result = hw_checksum_configure_nic((nic_context_t *)ctx, CHECKSUM_MODE_HARDWARE);
    if (result != HW_CHECKSUM_SUCCESS) {
        LOG_WARNING("3Com: Failed to configure hardware checksumming: %d", result);
        /* Fall back to software mode */
        hw_checksum_configure_nic((nic_context_t *)ctx, CHECKSUM_MODE_SOFTWARE);
    }
    
    /* Select Window 7 for Cyclone/Tornado checksum configuration */
    select_window(ioaddr, 7);
    
    /* Enable IP/TCP/UDP checksum offload in hardware */
    uint16_t config = window_read16(ioaddr, 7, WN7_CONFIG);
    config |= 0x0003;  /* Enable TX and RX checksum */
    window_write16(ioaddr, 7, WN7_CONFIG, config);
    
    /* Set VLAN EtherType for proper checksum calculation */
    window_write16(ioaddr, 7, WN7_VLAN_TYPE, 0x8100);
    
    /* Enable checksum status in TX/RX descriptors */
    uint16_t desc_ctrl = window_read16(ioaddr, 7, WN7_DESC_CTRL);
    desc_ctrl |= 0x0030;  /* Enable checksum fields in descriptors */
    window_write16(ioaddr, 7, WN7_DESC_CTRL, desc_ctrl);
    
    ctx->base.hw_checksum = 1;
    ctx->checksum_enabled = 1;
    
    LOG_INFO("3Com: Hardware checksumming enabled for Cyclone/Tornado");
    
    return SUCCESS;
}

/**
 * @brief Perform complete hardware reset
 */
static int reset_hardware(pci_3com_context_t *ctx)
{
    uint16_t ioaddr = ctx->base.io_base;
    int timeout;
    
    LOG_DEBUG("3Com: Resetting hardware");
    
    /* Issue global reset */
    outw(ioaddr + EL3_CMD, TotalReset | 0xFF);
    
    /* Wait for reset to complete (up to 1 second) */
    timeout = 100;
    while (timeout > 0) {
        if (!(inw(ioaddr + EL3_CMD) & CMD_IN_PROGRESS)) {
            break;
        }
        delay_ms(10);
        timeout--;
    }
    
    if (timeout == 0) {
        LOG_ERROR("3Com: Hardware reset timeout");
        return ERROR_TIMEOUT;
    }
    
    /* Additional delay for stabilization */
    delay_ms(10);
    
    return SUCCESS;
}

/**
 * @brief Main initialization function for 3Com PCI NICs
 * 
 * @param info Detected NIC information
 * @return 0 on success, negative error code on failure
 */
int init_3com_pci(nic_detect_info_t *info)
{
    pci_3com_context_t *ctx;
    int result;
    
    if (!info) {
        return ERROR_INVALID_PARAMETER;
    }
    
    LOG_INFO("3Com: Initializing %s NIC at I/O 0x%04X",
             get_nic_type_string(info->nic_type), info->io_base);
    
    /* Allocate context structure */
    ctx = (pci_3com_context_t *)mem_alloc(sizeof(pci_3com_context_t));
    if (!ctx) {
        LOG_ERROR("3Com: Failed to allocate context");
        return ERROR_NO_MEMORY;
    }
    
    memset(ctx, 0, sizeof(pci_3com_context_t));
    
    /* Initialize base context */
    ctx->base.io_base = info->io_base;
    ctx->base.irq = info->irq;
    ctx->base.nic_type = info->nic_type;
    
    /* Copy generation info from detection */
    ctx->generation = info->pci_info.generation;
    ctx->capabilities = info->pci_info.hw_capabilities;
    
    /* Reset hardware */
    result = reset_hardware(ctx);
    if (result != SUCCESS) {
        mem_free(ctx);
        return result;
    }
    
    /* Reset window tracking after hardware reset */
    reset_window_tracking(ctx->base.io_base);
    
    /* Read MAC address */
    result = read_mac_address(ctx);
    if (result != SUCCESS) {
        mem_free(ctx);
        return result;
    }
    
    /* Configure media and link settings */
    result = configure_media(ctx);
    if (result != SUCCESS) {
        mem_free(ctx);
        return result;
    }
    
    /* Determine cache coherency requirements */
    if (ctx->generation & (IS_BOOMERANG | IS_CYCLONE | IS_TORNADO)) {
        /* Bus master DMA requires cache coherency */
        coherency_analysis_t analysis;
        analyze_cache_coherency(&analysis);
        ctx->base.cache_tier = analysis.selected_tier;
        LOG_INFO("3Com: Cache coherency tier %d selected", ctx->base.cache_tier);
    } else {
        /* PIO mode doesn't need cache coherency */
        ctx->base.cache_tier = CACHE_TIER_4_FALLBACK;
    }
    
    /* Initialize based on generation */
    if (ctx->generation & IS_VORTEX) {
        /* Vortex uses programmed I/O */
        result = vortex_init_pio(ctx);
    } else if (ctx->generation & (IS_BOOMERANG | IS_CYCLONE | IS_TORNADO)) {
        /* Boomerang and later use bus master DMA */
        result = boomerang_init_dma(ctx);
        
        /* Enable advanced features for Cyclone/Tornado */
        if (ctx->generation & (IS_CYCLONE | IS_TORNADO)) {
            init_hw_checksum(ctx);
            
            /* Initialize flow control for newer generations */
            if (ctx->capabilities & HAS_NWAY) {
                flow_control_config_t fc_config = {
                    .enabled = 1,
                    .mode = FLOW_CONTROL_MODE_AUTO,
                    .pause_time = 100,  /* 100 quanta */
                    .high_watermark = 80,  /* 80% buffer usage */
                    .low_watermark = 20    /* 20% buffer usage */
                };
                
                flow_control_context_t *fc_ctx = (flow_control_context_t *)mem_alloc(sizeof(flow_control_context_t));
                if (fc_ctx) {
                    result = flow_control_init(fc_ctx, (nic_context_t *)ctx, &fc_config);
                    if (result == FLOW_CONTROL_SUCCESS) {
                        ctx->flow_control_ctx = fc_ctx;
                        LOG_INFO("3Com: Flow control enabled for %s", 
                                 get_generation_string(ctx->generation));
                    } else {
                        mem_free(fc_ctx);
                    }
                }
            }
        }
    } else {
        LOG_ERROR("3Com: Unknown generation 0x%02X", ctx->generation);
        result = ERROR_NOT_SUPPORTED;
    }
    
    if (result != SUCCESS) {
        mem_free(ctx);
        return result;
    }
    
    /* Set RX filter - accept station address and broadcast */
    outw(ctx->base.io_base + EL3_CMD, 
         CMD_SET_RX_FILTER | RX_FILTER_STATION | RX_FILTER_BROADCAST);
    
    /* Enable statistics collection */
    outw(ctx->base.io_base + EL3_CMD, CMD_STATS_ENABLE);
    
    /* Apply performance optimizations */
    result = apply_performance_optimizations(ctx);
    if (result != SUCCESS) {
        LOG_WARNING("3Com: Some performance optimizations could not be applied");
        /* Continue anyway - optimizations are not critical */
    }
    
    /* Store context pointer */
    info->driver_context = ctx;
    
    LOG_INFO("3Com: Initialization complete - %s mode, %d Mbps %s-duplex",
             (ctx->generation & IS_VORTEX) ? "PIO" : "DMA",
             ctx->base.link_speed,
             ctx->full_duplex ? "full" : "half");
    
    return SUCCESS;
}

/**
 * @brief Cleanup and shutdown 3Com NIC
 * 
 * @param ctx NIC context
 * @return 0 on success, negative error code on failure
 */
int shutdown_3com_pci(pci_3com_context_t *ctx)
{
    uint16_t ioaddr;
    
    if (!ctx) {
        return ERROR_INVALID_PARAMETER;
    }
    
    ioaddr = ctx->base.io_base;
    
    LOG_INFO("3Com: Shutting down NIC at I/O 0x%04X", ioaddr);
    
    /* Disable interrupts */
    outw(ioaddr + EL3_CMD, SetIntrEnb | 0);
    
    /* Stop TX and RX */
    outw(ioaddr + EL3_CMD, TxDisable);
    outw(ioaddr + EL3_CMD, RxDisable);
    
    /* Reset hardware */
    outw(ioaddr + EL3_CMD, TotalReset | 0xFF);
    
    /* Free descriptor rings if allocated */
    if (ctx->tx_ring) {
        mem_free(ctx->tx_ring);
    }
    if (ctx->rx_ring) {
        mem_free(ctx->rx_ring);
    }
    
    /* Free context */
    mem_free(ctx);
    
    return SUCCESS;
}

/**
 * @brief Get printable string for NIC generation
 */
const char* get_generation_string(uint8_t generation)
{
    if (generation & IS_VORTEX) return "Vortex";
    if (generation & IS_BOOMERANG) return "Boomerang";
    if (generation & IS_CYCLONE) return "Cyclone";
    if (generation & IS_TORNADO) return "Tornado";
    return "Unknown";
}