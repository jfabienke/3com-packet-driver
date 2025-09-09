/**
 * @file el3_core.c
 * @brief Unified 3Com EtherLink III Core Driver
 *
 * Bus-agnostic core driver logic for the entire 3Com EtherLink III family.
 * Supports 3C509B, 3C515-TX, 3C59x Vortex, 3C90x Boomerang, 3C905B Cyclone,
 * and 3C905C Tornado through capability-driven polymorphism.
 *
 * This file is part of the 3Com Packet Driver project.
 * Copyright (C) 2024
 */

#include <stdint.h>
#include <string.h>
#include <dos.h>
#include "el3_core.h"
#include "el3_caps.h"
#include "../hal/el3_hal.h"
#include "../datapath/el3_datapath.h"
#include "../../include/logging.h"
#include "../../include/common.h"

/* Window definitions */
#define WN0_EEPROM_CMD      0x0A
#define WN0_EEPROM_DATA     0x0C
#define WN1_TX_STATUS       0x0B
#define WN1_TIMER           0x0A
#define WN2_STATION_ADDR    0x00
#define WN3_INTERNAL_CFG    0x00
#define WN3_MAC_CONTROL     0x06
#define WN3_OPTIONS         0x08
#define WN4_MEDIA_STATUS    0x0A
#define WN4_NET_DIAG        0x06
#define WN4_FIFO_DIAG       0x04
#define WN6_CARRIER_LOST    0x00
#define WN6_SQE_ERRORS      0x01

/* Command register commands */
#define CMD_GLOBAL_RESET    (0<<11)
#define CMD_SELECT_WINDOW   (1<<11)
#define CMD_START_COAX      (2<<11)
#define CMD_RX_DISABLE      (3<<11)
#define CMD_RX_ENABLE       (4<<11)
#define CMD_RX_RESET        (5<<11)
#define CMD_TX_DONE         (7<<11)
#define CMD_TX_ENABLE       (9<<11)
#define CMD_TX_DISABLE      (10<<11)
#define CMD_TX_RESET        (11<<11)
#define CMD_ACK_INTERRUPT   (13<<11)
#define CMD_SET_INTR_MASK   (14<<11)
#define CMD_SET_RX_FILTER   (16<<11)
#define CMD_STATS_ENABLE    (21<<11)
#define CMD_STATS_DISABLE   (22<<11)

/* Status register bits */
#define STAT_INT_LATCH      0x0001
#define STAT_ADAPTER_FAIL   0x0002
#define STAT_TX_COMPLETE    0x0004
#define STAT_TX_AVAILABLE   0x0008
#define STAT_RX_COMPLETE    0x0010
#define STAT_UPDATE_STATS   0x0080
#define STAT_CMD_IN_PROGRESS 0x1000

/* RX filter modes */
#define RX_FILTER_STATION   0x01
#define RX_FILTER_MULTICAST 0x02
#define RX_FILTER_BROADCAST 0x04
#define RX_FILTER_PROMISC   0x08
#define RX_FILTER_ALL_MULTI 0x10

/* Global driver state */
static struct el3_dev *g_devices[MAX_EL3_DEVICES];
static int g_device_count = 0;

/* Forward declarations */
static int el3_reset_hardware(struct el3_dev *dev);
static int el3_init_transceiver(struct el3_dev *dev);
static int el3_configure_windows(struct el3_dev *dev);
static int el3_setup_datapath(struct el3_dev *dev);
static void el3_select_generation_ops(struct el3_dev *dev);

/**
 * @brief Initialize a 3Com EtherLink III device
 *
 * Master initialization routine called by bus probers after device discovery.
 * Detects capabilities, configures hardware, and sets up the datapath.
 *
 * @param dev Allocated device structure with resources mapped
 * @return 0 on success, negative error code on failure
 */
int el3_init(struct el3_dev *dev)
{
    int ret;
    
    if (!dev) {
        LOG_ERROR("EL3: NULL device passed to init");
        return -EINVAL;
    }
    
    if (g_device_count >= MAX_EL3_DEVICES) {
        LOG_ERROR("EL3: Maximum device count reached");
        return -ENOSPC;
    }
    
    LOG_INFO("EL3: Initializing %s at I/O 0x%04X IRQ %d",
             dev->name, dev->io_base, dev->irq);
    
    /* Step 1: Detect device capabilities */
    ret = el3_detect_capabilities(dev);
    if (ret < 0) {
        LOG_ERROR("EL3: Failed to detect capabilities");
        return ret;
    }
    
    /* Step 2: Select generation-specific operations */
    el3_select_generation_ops(dev);
    
    /* Step 3: Reset hardware to known state */
    ret = el3_reset_hardware(dev);
    if (ret < 0) {
        LOG_ERROR("EL3: Hardware reset failed");
        return ret;
    }
    
    /* Step 4: Read MAC address from EEPROM */
    ret = el3_read_mac_address(dev);
    if (ret < 0) {
        LOG_ERROR("EL3: Failed to read MAC address");
        return ret;
    }
    
    LOG_INFO("EL3: MAC address: %02X:%02X:%02X:%02X:%02X:%02X",
             dev->mac_addr[0], dev->mac_addr[1], dev->mac_addr[2],
             dev->mac_addr[3], dev->mac_addr[4], dev->mac_addr[5]);
    
    /* Step 5: Configure window registers */
    ret = el3_configure_windows(dev);
    if (ret < 0) {
        LOG_ERROR("EL3: Window configuration failed");
        return ret;
    }
    
    /* Step 6: Initialize transceiver/PHY */
    ret = el3_init_transceiver(dev);
    if (ret < 0) {
        LOG_ERROR("EL3: Transceiver init failed");
        return ret;
    }
    
    /* Step 7: Setup datapath (PIO or DMA) */
    ret = el3_setup_datapath(dev);
    if (ret < 0) {
        LOG_ERROR("EL3: Datapath setup failed");
        return ret;
    }
    
    /* Step 8: Clear statistics */
    el3_update_statistics(dev);
    
    /* Step 9: Enable interrupts */
    el3_set_interrupt_mask(dev, dev->caps.interrupt_mask);
    
    /* Add to global device list */
    g_devices[g_device_count++] = dev;
    dev->initialized = 1;
    
    LOG_INFO("EL3: %s initialized successfully (Gen: %s, Caps: 0x%04X)",
             dev->name,
             el3_generation_name(dev->generation),
             dev->caps.flags);
    
    return 0;
}

/**
 * @brief Start device operation
 */
int el3_start(struct el3_dev *dev)
{
    uint16_t status;
    
    if (!dev || !dev->initialized) {
        return -EINVAL;
    }
    
    LOG_DEBUG("EL3: Starting %s", dev->name);
    
    /* Enable receiver */
    el3_issue_command(dev, CMD_RX_ENABLE);
    
    /* Enable transmitter */
    el3_issue_command(dev, CMD_TX_ENABLE);
    
    /* Set receive filter */
    el3_set_rx_mode(dev);
    
    /* Enable statistics if supported */
    if (dev->caps.has_stats_window) {
        el3_issue_command(dev, CMD_STATS_ENABLE);
    }
    
    /* Clear any pending interrupts */
    status = el3_read16(dev, EL3_STATUS);
    el3_issue_command(dev, CMD_ACK_INTERRUPT | 0xFF);
    
    /* Mark as running */
    dev->running = 1;
    
    return 0;
}

/**
 * @brief Stop device operation
 */
int el3_stop(struct el3_dev *dev)
{
    if (!dev || !dev->initialized) {
        return -EINVAL;
    }
    
    LOG_DEBUG("EL3: Stopping %s", dev->name);
    
    /* Mark as not running */
    dev->running = 0;
    
    /* Disable interrupts */
    el3_set_interrupt_mask(dev, 0);
    
    /* Disable receiver */
    el3_issue_command(dev, CMD_RX_DISABLE);
    
    /* Disable transmitter */
    el3_issue_command(dev, CMD_TX_DISABLE);
    
    /* Disable statistics */
    if (dev->caps.has_stats_window) {
        el3_issue_command(dev, CMD_STATS_DISABLE);
    }
    
    /* Reset TX and RX */
    el3_issue_command(dev, CMD_TX_RESET);
    el3_issue_command(dev, CMD_RX_RESET);
    
    return 0;
}

/**
 * @brief Reset hardware to known state
 */
static int el3_reset_hardware(struct el3_dev *dev)
{
    int timeout;
    uint16_t status;
    
    /* Issue global reset */
    el3_issue_command(dev, CMD_GLOBAL_RESET);
    
    /* Wait for reset to complete (up to 1ms) */
    timeout = 100;
    while (timeout > 0) {
        status = el3_read16(dev, EL3_STATUS);
        if (!(status & STAT_CMD_IN_PROGRESS)) {
            break;
        }
        delay_us(10);
        timeout--;
    }
    
    if (timeout == 0) {
        LOG_ERROR("EL3: Reset timeout");
        return -ETIMEDOUT;
    }
    
    /* Additional delay for hardware to stabilize */
    delay_ms(2);
    
    /* Generation-specific reset if needed */
    if (dev->ops && dev->ops->reset) {
        return dev->ops->reset(dev);
    }
    
    return 0;
}

/**
 * @brief Configure window registers
 */
static int el3_configure_windows(struct el3_dev *dev)
{
    /* Window 0: EEPROM access (already used during init) */
    
    /* Window 2: Station address */
    el3_select_window(dev, 2);
    
    /* Program MAC address */
    el3_write16(dev, WN2_STATION_ADDR + 0, 
                (dev->mac_addr[1] << 8) | dev->mac_addr[0]);
    el3_write16(dev, WN2_STATION_ADDR + 2,
                (dev->mac_addr[3] << 8) | dev->mac_addr[2]);
    el3_write16(dev, WN2_STATION_ADDR + 4,
                (dev->mac_addr[5] << 8) | dev->mac_addr[4]);
    
    /* Window 3: Internal configuration */
    el3_select_window(dev, 3);
    
    /* Set MAC control options */
    if (dev->caps.has_flow_control) {
        el3_write16(dev, WN3_MAC_CONTROL, 0x01); /* Enable flow control */
    }
    
    /* Set driver options */
    if (dev->caps.has_large_packets) {
        el3_write16(dev, WN3_OPTIONS, 0x01); /* Allow large packets */
    }
    
    /* Window 4: Diagnostics and media (configured in transceiver init) */
    
    /* Window 1: Operating window for older cards */
    if (!dev->caps.has_permanent_window1) {
        el3_select_window(dev, 1);
    }
    
    return 0;
}

/**
 * @brief Initialize transceiver/PHY
 */
static int el3_init_transceiver(struct el3_dev *dev)
{
    uint16_t media_status;
    uint16_t net_diag;
    
    el3_select_window(dev, 4);
    
    /* Read current media status */
    media_status = el3_read16(dev, WN4_MEDIA_STATUS);
    net_diag = el3_read16(dev, WN4_NET_DIAG);
    
    LOG_DEBUG("EL3: Media status: 0x%04X, Net diag: 0x%04X", 
              media_status, net_diag);
    
    /* Auto-select media if supported */
    if (dev->caps.has_nway) {
        /* Enable auto-negotiation */
        /* TODO: Implement NWAY auto-negotiation */
    } else {
        /* Use default media from EEPROM */
        /* Already configured by hardware reset */
    }
    
    /* Generation-specific PHY init */
    if (dev->ops && dev->ops->init_phy) {
        return dev->ops->init_phy(dev);
    }
    
    return 0;
}

/**
 * @brief Setup datapath based on capabilities
 */
static int el3_setup_datapath(struct el3_dev *dev)
{
    int ret;
    
    if (dev->caps.has_bus_master) {
        /* Initialize DMA datapath */
        LOG_INFO("EL3: Setting up DMA datapath");
        ret = el3_dma_init(dev);
        if (ret < 0) {
            LOG_ERROR("EL3: DMA init failed, falling back to PIO");
            dev->caps.has_bus_master = 0;
            goto pio_fallback;
        }
        
        /* Set DMA operations */
        dev->start_xmit = el3_dma_xmit;
        dev->rx_poll = el3_dma_rx_poll;
        dev->isr = el3_dma_isr;
        
    } else {
pio_fallback:
        /* Initialize PIO datapath */
        LOG_INFO("EL3: Setting up PIO datapath");
        ret = el3_pio_init(dev);
        if (ret < 0) {
            LOG_ERROR("EL3: PIO init failed");
            return ret;
        }
        
        /* Set PIO operations */
        dev->start_xmit = el3_pio_xmit;
        dev->rx_poll = el3_pio_rx_poll;
        dev->isr = el3_pio_isr;
    }
    
    return 0;
}

/**
 * @brief Select generation-specific operations
 */
static void el3_select_generation_ops(struct el3_dev *dev)
{
    static const struct el3_ops ops_3c509b = {
        .reset = NULL,  /* Use generic reset */
        .init_phy = NULL,  /* No PHY */
        .get_link = NULL,  /* No link detection */
    };
    
    static const struct el3_ops ops_vortex = {
        .reset = NULL,  /* Use generic reset */
        .init_phy = NULL,  /* Basic PHY */
        .get_link = NULL,  /* Basic link */
    };
    
    static const struct el3_ops ops_boomerang = {
        .reset = NULL,
        .init_phy = NULL,
        .get_link = NULL,
    };
    
    static const struct el3_ops ops_cyclone = {
        .reset = NULL,
        .init_phy = NULL,  /* MII PHY */
        .get_link = NULL,  /* MII link detect */
    };
    
    static const struct el3_ops ops_tornado = {
        .reset = NULL,
        .init_phy = NULL,  /* Advanced PHY */
        .get_link = NULL,  /* NWAY status */
    };
    
    switch (dev->generation) {
    case EL3_GEN_3C509B:
        dev->ops = &ops_3c509b;
        break;
    case EL3_GEN_VORTEX:
        dev->ops = &ops_vortex;
        break;
    case EL3_GEN_BOOMERANG:
        dev->ops = &ops_boomerang;
        break;
    case EL3_GEN_CYCLONE:
        dev->ops = &ops_cyclone;
        break;
    case EL3_GEN_TORNADO:
        dev->ops = &ops_tornado;
        break;
    default:
        dev->ops = &ops_3c509b;  /* Safe default */
        break;
    }
}

/**
 * @brief Set receive mode (filters)
 */
int el3_set_rx_mode(struct el3_dev *dev)
{
    uint16_t rx_filter = 0;
    
    if (!dev) {
        return -EINVAL;
    }
    
    /* Always accept our station address */
    rx_filter |= RX_FILTER_STATION;
    
    /* Set promiscuous mode if requested */
    if (dev->rx_mode & RX_MODE_PROMISC) {
        rx_filter |= RX_FILTER_PROMISC;
    } else {
        /* Accept broadcasts */
        if (dev->rx_mode & RX_MODE_BROADCAST) {
            rx_filter |= RX_FILTER_BROADCAST;
        }
        
        /* Accept multicasts */
        if (dev->rx_mode & RX_MODE_ALL_MULTI) {
            rx_filter |= RX_FILTER_ALL_MULTI;
        } else if (dev->rx_mode & RX_MODE_MULTICAST) {
            rx_filter |= RX_FILTER_MULTICAST;
            /* TODO: Program multicast filter */
        }
    }
    
    /* Set the filter */
    el3_issue_command(dev, CMD_SET_RX_FILTER | rx_filter);
    
    return 0;
}

/**
 * @brief Read and clear statistics
 */
void el3_update_statistics(struct el3_dev *dev)
{
    if (!dev || !dev->caps.has_stats_window) {
        return;
    }
    
    /* Select statistics window */
    el3_select_window(dev, 6);
    
    /* Read and accumulate stats (reading clears them) */
    dev->stats.tx_carrier_errors += el3_read8(dev, WN6_CARRIER_LOST);
    dev->stats.tx_heartbeat_errors += el3_read8(dev, WN6_SQE_ERRORS);
    dev->stats.collisions += el3_read8(dev, 0x02);
    dev->stats.tx_window_errors += el3_read8(dev, 0x03);
    dev->stats.rx_fifo_errors += el3_read8(dev, 0x04);
    dev->stats.tx_fifo_errors += el3_read8(dev, 0x05);
    
    /* Upper byte of various counters */
    dev->stats.tx_bytes += el3_read16(dev, 0x0C);
    dev->stats.rx_bytes += el3_read16(dev, 0x0A);
    
    /* Return to operating window */
    if (!dev->caps.has_permanent_window1) {
        el3_select_window(dev, 1);
    }
}

/**
 * @brief Set interrupt mask
 */
void el3_set_interrupt_mask(struct el3_dev *dev, uint16_t mask)
{
    if (!dev) {
        return;
    }
    
    el3_issue_command(dev, CMD_SET_INTR_MASK | mask);
    dev->interrupt_mask = mask;
}

/**
 * @brief Get device by index
 */
struct el3_dev *el3_get_device(int index)
{
    if (index < 0 || index >= g_device_count) {
        return NULL;
    }
    return g_devices[index];
}

/**
 * @brief Get device count
 */
int el3_get_device_count(void)
{
    return g_device_count;
}

/**
 * @brief Get generation name string
 */
const char *el3_generation_name(enum el3_generation gen)
{
    static const char *names[] = {
        "Unknown",
        "3C509B",
        "3C515-TX",
        "Vortex",
        "Boomerang",
        "Cyclone",
        "Tornado"
    };
    
    if (gen < 0 || gen >= sizeof(names)/sizeof(names[0])) {
        return names[0];
    }
    
    return names[gen];
}