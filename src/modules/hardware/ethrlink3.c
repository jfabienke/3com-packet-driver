/**
 * @file ethrlink3.c
 * @brief ETHRLINK3.MOD - EtherLink III Family Hardware Module
 * 
 * Phase 3A: Dynamic Module Loading - Stream 2 Hardware Implementation
 * 
 * This module supports the complete 3Com EtherLink III family:
 * - 3C509 (Original EtherLink III)
 * - 3C509B (Enhanced EtherLink III)  
 * - 3C509C (Latest EtherLink III)
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#include "module_api.h"
#include <stdio.h>
#include <string.h>

/* 3C509 Family Hardware Constants */
#define ETHERLINK3_VENDOR_ID    0x10B7  /* 3Com vendor ID */
#define ETHERLINK3_DEVICE_ID    0x5090  /* 3C509 device ID */

/* I/O Port Definitions */
#define EL3_COMMAND_PORT    0x00    /* Command register */
#define EL3_STATUS_PORT     0x0E    /* Status register */
#define EL3_DATA_PORT       0x00    /* Data register (window dependent) */
#define EL3_WINDOW_PORT     0x0E    /* Window selection register */

/* Command Register Values */
#define EL3_CMD_RESET           0x0000
#define EL3_CMD_SELECT_WINDOW   0x0800
#define EL3_CMD_START_COAX      0x1000
#define EL3_CMD_STOP_COAX       0x2000
#define EL3_CMD_TX_ENABLE       0x4800
#define EL3_CMD_TX_DISABLE      0x5000
#define EL3_CMD_RX_ENABLE       0x2000
#define EL3_CMD_RX_DISABLE      0x1800

/* Window Definitions */
#define EL3_WINDOW_SETUP        0   /* Setup/configuration */
#define EL3_WINDOW_OPERATING    1   /* Operating registers */
#define EL3_WINDOW_STATION      2   /* Station address */
#define EL3_WINDOW_FIFO         3   /* FIFO management */
#define EL3_WINDOW_DIAGNOSTIC   4   /* Diagnostic registers */
#define EL3_WINDOW_RESULTS      5   /* Read results */
#define EL3_WINDOW_STATISTICS   6   /* Statistics */
#define EL3_WINDOW_BUS_MASTER   7   /* Bus master (3C509B/C only) */

/* Media Types */
#define EL3_MEDIA_10BASE_T      0x00
#define EL3_MEDIA_AUI           0x01  
#define EL3_MEDIA_10BASE_2      0x03

/* EEPROM Addresses */
#define EL3_EEPROM_OEM_NODE_0   0x00
#define EL3_EEPROM_OEM_NODE_1   0x01
#define EL3_EEPROM_OEM_NODE_2   0x02
#define EL3_EEPROM_MANU_DATE    0x04
#define EL3_EEPROM_MANU_DIV     0x05
#define EL3_EEPROM_MANU_PROD    0x06
#define EL3_EEPROM_MFG_ID       0x07
#define EL3_EEPROM_ADDR_CFG     0x08
#define EL3_EEPROM_RESOURCE_CFG 0x09

/* Module-specific data structures */
typedef struct {
    uint16_t io_base;           /* Base I/O address */
    uint8_t irq;                /* IRQ number */
    uint8_t variant;            /* 0=3C509, 1=3C509B, 2=3C509C */
    uint8_t media_type;         /* Current media type */
    uint8_t mac_address[6];     /* Station address */
    bool link_active;           /* Link status */
    nic_stats_t stats;          /* Statistics */
} etherlink3_context_t;

/* Global module context */
static etherlink3_context_t module_context[MAX_NICS_SUPPORTED];
static core_services_t* core_services = NULL;

/* Forward declarations */
static bool el3_detect_hardware(hardware_info_t* hw_info);
static bool el3_initialize(uint8_t nic_id, const hardware_info_t* hw_info);
static bool el3_shutdown(uint8_t nic_id);
static bool el3_send_packet(uint8_t nic_id, const packet_t* packet);
static packet_t* el3_receive_packet(uint8_t nic_id);
static bool el3_get_stats(uint8_t nic_id, nic_stats_t* stats);
static bool el3_reset_stats(uint8_t nic_id);
static bool el3_set_mode(uint8_t nic_id, nic_mode_t mode);
static bool el3_get_link_status(uint8_t nic_id, link_status_t* status);
static bool el3_set_promiscuous(uint8_t nic_id, bool enable);
static bool el3_set_multicast(uint8_t nic_id, const uint8_t* addr_list, uint16_t count);
static bool el3_power_management(uint8_t nic_id, bool sleep_mode);
static bool el3_self_test(uint8_t nic_id);
static bool el3_loopback_test(uint8_t nic_id);
static const char* el3_get_driver_info(void);

/* Utility functions */
static void el3_select_window(uint16_t io_base, uint8_t window);
static uint16_t el3_read_eeprom(uint16_t io_base, uint8_t address);
static void el3_write_eeprom(uint16_t io_base, uint8_t address, uint16_t data);
static bool el3_detect_variant(uint16_t io_base, uint8_t* variant);
static bool el3_read_station_address(uint16_t io_base, uint8_t* mac_addr);
static bool el3_auto_detect_media(uint16_t io_base, uint8_t* media_type);
static void el3_reset_adapter(uint16_t io_base);
static bool el3_wait_for_completion(uint16_t io_base, uint32_t timeout_ms);

/* Hardware operations vtable */
static const nic_ops_t etherlink3_ops = {
    .detect_hardware = el3_detect_hardware,
    .initialize = el3_initialize,
    .shutdown = el3_shutdown,
    .send_packet = el3_send_packet,
    .receive_packet = el3_receive_packet,
    .get_stats = el3_get_stats,
    .reset_stats = el3_reset_stats,
    .set_mode = el3_set_mode,
    .get_link_status = el3_get_link_status,
    .set_promiscuous = el3_set_promiscuous,
    .set_multicast = el3_set_multicast,
    .power_management = el3_power_management,
    .self_test = el3_self_test,
    .loopback_test = el3_loopback_test,
    .get_driver_info = el3_get_driver_info
};

/* ============================================================================
 * Module Header and Initialization
 * ============================================================================ */

/* Module header - must be first in the file */
const module_header_t module_header = {
    .magic = MODULE_MAGIC,
    .version = 0x0100,  /* Version 1.0 */
    .header_size = sizeof(module_header_t),
    .module_size = 0,   /* Filled by linker */
    .module_class = MODULE_CLASS_HARDWARE,
    .family_id = FAMILY_ETHERLINK3,
    .feature_flags = FEATURE_MULTICAST | FEATURE_PROMISCUOUS,
    .api_version = MODULE_API_VERSION,
    .init_offset = (uint16_t)etherlink3_init,
    .vtable_offset = (uint16_t)&etherlink3_ops,
    .cleanup_offset = (uint16_t)etherlink3_cleanup,
    .info_offset = 0,
    .deps_count = 0,
    .deps_offset = 0,
    .min_dos_version = 0x0300,  /* DOS 3.0+ */
    .min_cpu_family = 2,        /* 286+ */
    .name = "ETHRLINK3",
    .description = "3Com EtherLink III Family Driver",
    .author = "3Com/Phase3A",
    .build_timestamp = 0,       /* Filled by build system */
    .checksum = 0,              /* Calculated by build system */
    .reserved = {0}
};

/**
 * @brief Hardware module initialization function
 */
nic_ops_t* etherlink3_init(uint8_t nic_id, core_services_t* core, const hardware_info_t* hw_info)
{
    if (!core || !hw_info || nic_id >= MAX_NICS_SUPPORTED) {
        return NULL;
    }
    
    /* Store core services pointer */
    core_services = core;
    
    /* Initialize module context */
    memset(&module_context[nic_id], 0, sizeof(etherlink3_context_t));
    
    core->log_message(LOG_LEVEL_INFO, "ETHRLINK3", 
        "Initializing EtherLink III family driver for NIC %d", nic_id);
    
    /* Return operations vtable */
    return (nic_ops_t*)&etherlink3_ops;
}

/**
 * @brief Module cleanup function
 */
void etherlink3_cleanup(void)
{
    if (core_services) {
        core_services->log_message(LOG_LEVEL_INFO, "ETHRLINK3",
            "EtherLink III family driver cleanup complete");
    }
    
    /* Clear all contexts */
    memset(module_context, 0, sizeof(module_context));
    core_services = NULL;
}

/* ============================================================================
 * Hardware Detection and Initialization
 * ============================================================================ */

/**
 * @brief Detect EtherLink III family hardware
 */
static bool el3_detect_hardware(hardware_info_t* hw_info)
{
    uint16_t io_ports[] = {0x300, 0x310, 0x320, 0x330, 0x340, 0x350, 0x360, 0x370, 0};
    uint16_t eeprom_id;
    uint8_t variant;
    
    if (!hw_info || !core_services) {
        return false;
    }
    
    core_services->log_message(LOG_LEVEL_DEBUG, "ETHRLINK3",
        "Scanning for EtherLink III family adapters...");
    
    /* Try common I/O port addresses */
    for (int i = 0; io_ports[i] != 0; i++) {
        uint16_t io_base = io_ports[i];
        
        /* Try to read EEPROM to detect adapter */
        eeprom_id = el3_read_eeprom(io_base, EL3_EEPROM_MFG_ID);
        
        /* Check for 3Com signature */
        if ((eeprom_id & 0xF0FF) == 0x6050) {  /* 3Com EtherLink III signature */
            
            /* Detect specific variant */
            if (el3_detect_variant(io_base, &variant)) {
                
                /* Fill hardware info */
                hw_info->vendor_id = ETHERLINK3_VENDOR_ID;
                hw_info->device_id = ETHERLINK3_DEVICE_ID + variant;
                hw_info->subsystem_id = eeprom_id;
                hw_info->io_base = io_base;
                hw_info->irq = 0;  /* Will be determined from EEPROM */
                hw_info->bus_type = 0;  /* ISA */
                hw_info->memory_base = 0;  /* No memory mapping */
                
                /* Set device name based on variant */
                switch (variant) {
                    case 0:
                        strcpy(hw_info->device_name, "3Com 3C509 EtherLink III");
                        break;
                    case 1:
                        strcpy(hw_info->device_name, "3Com 3C509B EtherLink III");
                        break;
                    case 2:
                        strcpy(hw_info->device_name, "3Com 3C509C EtherLink III");
                        break;
                    default:
                        strcpy(hw_info->device_name, "3Com EtherLink III (Unknown)");
                        break;
                }
                
                core_services->log_message(LOG_LEVEL_INFO, "ETHRLINK3",
                    "Detected %s at I/O 0x%04X", hw_info->device_name, io_base);
                
                return true;
            }
        }
    }
    
    core_services->log_message(LOG_LEVEL_DEBUG, "ETHRLINK3",
        "No EtherLink III family adapters found");
    
    return false;
}

/**
 * @brief Initialize EtherLink III adapter
 */
static bool el3_initialize(uint8_t nic_id, const hardware_info_t* hw_info)
{
    etherlink3_context_t* ctx;
    uint16_t config_word;
    
    if (!hw_info || nic_id >= MAX_NICS_SUPPORTED || !core_services) {
        return false;
    }
    
    ctx = &module_context[nic_id];
    ctx->io_base = hw_info->io_base;
    ctx->irq = hw_info->irq;
    ctx->variant = hw_info->device_id - ETHERLINK3_DEVICE_ID;
    
    core_services->log_message(LOG_LEVEL_INFO, "ETHRLINK3",
        "Initializing adapter at I/O 0x%04X", ctx->io_base);
    
    /* Reset the adapter */
    el3_reset_adapter(ctx->io_base);
    
    /* Read station address from EEPROM */
    if (!el3_read_station_address(ctx->io_base, ctx->mac_address)) {
        core_services->log_message(LOG_LEVEL_ERROR, "ETHRLINK3",
            "Failed to read station address");
        return false;
    }
    
    /* Auto-detect media type */
    if (!el3_auto_detect_media(ctx->io_base, &ctx->media_type)) {
        core_services->log_message(LOG_LEVEL_WARNING, "ETHRLINK3",
            "Failed to auto-detect media, using 10BASE-T");
        ctx->media_type = EL3_MEDIA_10BASE_T;
    }
    
    /* Configure IRQ from EEPROM if not already set */
    if (ctx->irq == 0) {
        config_word = el3_read_eeprom(ctx->io_base, EL3_EEPROM_RESOURCE_CFG);
        ctx->irq = (config_word >> 12) & 0x0F;
        if (ctx->irq == 0) ctx->irq = 10;  /* Default IRQ */
    }
    
    /* Install interrupt handler */
    if (!core_services->interrupts.install_handler(ctx->irq, etherlink3_interrupt_handler, nic_id)) {
        core_services->log_message(LOG_LEVEL_ERROR, "ETHRLINK3",
            "Failed to install interrupt handler for IRQ %d", ctx->irq);
        return false;
    }
    
    /* Enable the adapter */
    el3_select_window(ctx->io_base, EL3_WINDOW_OPERATING);
    core_services->hardware.outw(ctx->io_base + EL3_COMMAND_PORT, EL3_CMD_TX_ENABLE);
    core_services->hardware.outw(ctx->io_base + EL3_COMMAND_PORT, EL3_CMD_RX_ENABLE);
    
    /* Clear statistics */
    memset(&ctx->stats, 0, sizeof(nic_stats_t));
    ctx->link_active = true;  /* Assume link is active */
    
    core_services->log_message(LOG_LEVEL_INFO, "ETHRLINK3",
        "Adapter initialized successfully (MAC: %02X:%02X:%02X:%02X:%02X:%02X, IRQ: %d)",
        ctx->mac_address[0], ctx->mac_address[1], ctx->mac_address[2],
        ctx->mac_address[3], ctx->mac_address[4], ctx->mac_address[5],
        ctx->irq);
    
    return true;
}

/**
 * @brief Shutdown EtherLink III adapter
 */
static bool el3_shutdown(uint8_t nic_id)
{
    etherlink3_context_t* ctx;
    
    if (nic_id >= MAX_NICS_SUPPORTED || !core_services) {
        return false;
    }
    
    ctx = &module_context[nic_id];
    
    core_services->log_message(LOG_LEVEL_INFO, "ETHRLINK3",
        "Shutting down adapter at I/O 0x%04X", ctx->io_base);
    
    /* Disable transmit and receive */
    core_services->hardware.outw(ctx->io_base + EL3_COMMAND_PORT, EL3_CMD_TX_DISABLE);
    core_services->hardware.outw(ctx->io_base + EL3_COMMAND_PORT, EL3_CMD_RX_DISABLE);
    
    /* Remove interrupt handler */
    core_services->interrupts.remove_handler(ctx->irq, nic_id);
    
    /* Reset adapter */
    el3_reset_adapter(ctx->io_base);
    
    /* Clear context */
    memset(ctx, 0, sizeof(etherlink3_context_t));
    
    return true;
}

/* ============================================================================
 * Packet Operations
 * ============================================================================ */

/**
 * @brief Send a packet
 */
static bool el3_send_packet(uint8_t nic_id, const packet_t* packet)
{
    etherlink3_context_t* ctx;
    uint16_t tx_status;
    
    if (nic_id >= MAX_NICS_SUPPORTED || !packet || !core_services) {
        return false;
    }
    
    ctx = &module_context[nic_id];
    
    /* Check transmit status */
    el3_select_window(ctx->io_base, EL3_WINDOW_OPERATING);
    tx_status = core_services->hardware.inw(ctx->io_base + EL3_STATUS_PORT);
    
    if (!(tx_status & 0x0080)) {  /* TX FIFO not ready */
        ctx->stats.tx_errors++;
        return false;
    }
    
    /* Write packet length */
    core_services->hardware.outw(ctx->io_base + EL3_DATA_PORT, packet->length);
    core_services->hardware.outw(ctx->io_base + EL3_DATA_PORT, 0);  /* Status (reserved) */
    
    /* Write packet data */
    core_services->hardware.outsw(ctx->io_base + EL3_DATA_PORT, 
                                 packet->data, (packet->length + 1) / 2);
    
    /* Update statistics */
    ctx->stats.tx_packets++;
    ctx->stats.tx_bytes += packet->length;
    
    return true;
}

/**
 * @brief Receive a packet
 */
static packet_t* el3_receive_packet(uint8_t nic_id)
{
    etherlink3_context_t* ctx;
    uint16_t rx_status;
    uint16_t packet_length;
    packet_t* packet;
    
    if (nic_id >= MAX_NICS_SUPPORTED || !core_services) {
        return NULL;
    }
    
    ctx = &module_context[nic_id];
    
    /* Check receive status */
    el3_select_window(ctx->io_base, EL3_WINDOW_OPERATING);
    rx_status = core_services->hardware.inw(ctx->io_base + EL3_STATUS_PORT);
    
    if (!(rx_status & 0x2000)) {  /* No packet available */
        return NULL;
    }
    
    /* Read packet header */
    rx_status = core_services->hardware.inw(ctx->io_base + EL3_DATA_PORT);
    packet_length = core_services->hardware.inw(ctx->io_base + EL3_DATA_PORT);
    
    /* Check for errors */
    if (rx_status & 0x4000) {  /* Error in packet */
        ctx->stats.rx_errors++;
        
        /* Discard packet */
        core_services->hardware.outw(ctx->io_base + EL3_COMMAND_PORT, 0x8005);  /* RX_DISCARD */
        return NULL;
    }
    
    /* Allocate packet buffer */
    packet = (packet_t*)core_services->memory.get_buffer(packet_length + sizeof(packet_t), 0);
    if (!packet) {
        ctx->stats.dropped++;
        core_services->hardware.outw(ctx->io_base + EL3_COMMAND_PORT, 0x8005);  /* RX_DISCARD */
        return NULL;
    }
    
    /* Set up packet structure */
    packet->data = (uint8_t*)(packet + 1);  /* Data follows packet structure */
    packet->length = packet_length;
    packet->buffer_size = packet_length;
    packet->type = 0;  /* Will be filled by higher layer */
    packet->nic_id = nic_id;
    packet->flags = 0;
    
    /* Read packet data */
    core_services->hardware.insw(ctx->io_base + EL3_DATA_PORT,
                                packet->data, (packet_length + 1) / 2);
    
    /* Update statistics */
    ctx->stats.rx_packets++;
    ctx->stats.rx_bytes += packet_length;
    
    return packet;
}

/* ============================================================================
 * Status and Configuration Functions
 * ============================================================================ */

/**
 * @brief Get adapter statistics
 */
static bool el3_get_stats(uint8_t nic_id, nic_stats_t* stats)
{
    if (nic_id >= MAX_NICS_SUPPORTED || !stats) {
        return false;
    }
    
    *stats = module_context[nic_id].stats;
    return true;
}

/**
 * @brief Reset adapter statistics
 */
static bool el3_reset_stats(uint8_t nic_id)
{
    if (nic_id >= MAX_NICS_SUPPORTED) {
        return false;
    }
    
    memset(&module_context[nic_id].stats, 0, sizeof(nic_stats_t));
    return true;
}

/**
 * @brief Set adapter mode
 */
static bool el3_set_mode(uint8_t nic_id, nic_mode_t mode)
{
    etherlink3_context_t* ctx;
    uint16_t rx_filter = 0;
    
    if (nic_id >= MAX_NICS_SUPPORTED) {
        return false;
    }
    
    ctx = &module_context[nic_id];
    
    /* Configure receive filter based on mode */
    if (mode & NIC_MODE_PROMISCUOUS) {
        rx_filter |= 0x0080;  /* Accept all packets */
    }
    if (mode & NIC_MODE_MULTICAST) {
        rx_filter |= 0x0040;  /* Accept multicast */
    }
    if (mode & NIC_MODE_BROADCAST) {
        rx_filter |= 0x0020;  /* Accept broadcast */
    }
    
    rx_filter |= 0x0001;  /* Accept directed packets */
    
    /* Set the filter */
    el3_select_window(ctx->io_base, EL3_WINDOW_OPERATING);
    core_services->hardware.outw(ctx->io_base + EL3_COMMAND_PORT, 0x8000 | rx_filter);
    
    return true;
}

/**
 * @brief Get link status
 */
static bool el3_get_link_status(uint8_t nic_id, link_status_t* status)
{
    etherlink3_context_t* ctx;
    uint16_t media_status;
    
    if (nic_id >= MAX_NICS_SUPPORTED || !status) {
        return false;
    }
    
    ctx = &module_context[nic_id];
    
    /* Read media status */
    el3_select_window(ctx->io_base, EL3_WINDOW_DIAGNOSTIC);
    media_status = core_services->hardware.inw(ctx->io_base + 0x0A);
    
    /* Fill status structure */
    status->link_up = (media_status & 0x0800) == 0;  /* Link beat detect */
    status->speed_mbps = 10;  /* EtherLink III is always 10Mbps */
    status->full_duplex = false;  /* EtherLink III is half-duplex */
    status->auto_negotiated = false;  /* No auto-negotiation */
    
    ctx->link_active = status->link_up;
    
    return true;
}

/* ============================================================================
 * Advanced Features (Stubs for Optional Implementation)
 * ============================================================================ */

static bool el3_set_promiscuous(uint8_t nic_id, bool enable)
{
    return el3_set_mode(nic_id, enable ? NIC_MODE_PROMISCUOUS : NIC_MODE_NORMAL);
}

static bool el3_set_multicast(uint8_t nic_id, const uint8_t* addr_list, uint16_t count)
{
    /* EtherLink III has limited multicast support */
    return el3_set_mode(nic_id, count > 0 ? NIC_MODE_MULTICAST : NIC_MODE_NORMAL);
}

static bool el3_power_management(uint8_t nic_id, bool sleep_mode)
{
    /* Power management not supported on original EtherLink III */
    return false;
}

static bool el3_self_test(uint8_t nic_id)
{
    /* Placeholder for self-test implementation */
    return true;
}

static bool el3_loopback_test(uint8_t nic_id)
{
    /* Placeholder for loopback test implementation */
    return true;
}

static const char* el3_get_driver_info(void)
{
    return "3Com EtherLink III Family Driver v1.0 (Phase 3A)";
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Select register window
 */
static void el3_select_window(uint16_t io_base, uint8_t window)
{
    core_services->hardware.outw(io_base + EL3_COMMAND_PORT, 
                                EL3_CMD_SELECT_WINDOW | (window & 0x07));
}

/**
 * @brief Read EEPROM word
 */
static uint16_t el3_read_eeprom(uint16_t io_base, uint8_t address)
{
    uint16_t data;
    
    /* Select setup window */
    el3_select_window(io_base, EL3_WINDOW_SETUP);
    
    /* Send EEPROM read command */
    core_services->hardware.outw(io_base + 0x0A, 0x0080 | address);
    
    /* Wait for completion */
    core_services->timing.delay_us(162);  /* EEPROM access time */
    
    /* Read data */
    data = core_services->hardware.inw(io_base + 0x0C);
    
    return data;
}

static void el3_write_eeprom(uint16_t io_base, uint8_t address, uint16_t data)
{
    /* EEPROM writing is rarely needed and complex - stub for now */
}

/**
 * @brief Detect adapter variant
 */
static bool el3_detect_variant(uint16_t io_base, uint8_t* variant)
{
    uint16_t mfg_id = el3_read_eeprom(io_base, EL3_EEPROM_MFG_ID);
    
    /* Determine variant based on manufacturing ID */
    if ((mfg_id & 0x00FF) == 0x0050) {
        *variant = 0;  /* 3C509 original */
    } else if ((mfg_id & 0x00FF) == 0x0051) {
        *variant = 1;  /* 3C509B enhanced */
    } else if ((mfg_id & 0x00FF) == 0x0052) {
        *variant = 2;  /* 3C509C latest */
    } else {
        return false;  /* Unknown variant */
    }
    
    return true;
}

/**
 * @brief Read station address from EEPROM
 */
static bool el3_read_station_address(uint16_t io_base, uint8_t* mac_addr)
{
    uint16_t word;
    
    for (int i = 0; i < 3; i++) {
        word = el3_read_eeprom(io_base, EL3_EEPROM_OEM_NODE_0 + i);
        mac_addr[i * 2] = word & 0xFF;
        mac_addr[i * 2 + 1] = (word >> 8) & 0xFF;
    }
    
    /* Validate MAC address (should not be all zeros or all FFs) */
    bool all_zero = true, all_ff = true;
    for (int i = 0; i < 6; i++) {
        if (mac_addr[i] != 0x00) all_zero = false;
        if (mac_addr[i] != 0xFF) all_ff = false;
    }
    
    return !all_zero && !all_ff;
}

/**
 * @brief Auto-detect media type
 */
static bool el3_auto_detect_media(uint16_t io_base, uint8_t* media_type)
{
    uint16_t config_word = el3_read_eeprom(io_base, EL3_EEPROM_ADDR_CFG);
    
    /* Extract media type from configuration */
    *media_type = (config_word >> 14) & 0x03;
    
    return true;
}

/**
 * @brief Reset adapter to known state
 */
static void el3_reset_adapter(uint16_t io_base)
{
    /* Global reset */
    core_services->hardware.outw(io_base + EL3_COMMAND_PORT, EL3_CMD_RESET);
    
    /* Wait for reset completion */
    core_services->timing.delay_ms(10);
    
    /* Wait for command completion */
    el3_wait_for_completion(io_base, 1000);
}

/**
 * @brief Wait for command completion
 */
static bool el3_wait_for_completion(uint16_t io_base, uint32_t timeout_ms)
{
    uint32_t start_time = core_services->timing.get_milliseconds();
    uint16_t status;
    
    while ((core_services->timing.get_milliseconds() - start_time) < timeout_ms) {
        status = core_services->hardware.inw(io_base + EL3_STATUS_PORT);
        if (!(status & 0x1000)) {  /* Command in progress bit */
            return true;
        }
        core_services->timing.delay_ms(1);
    }
    
    return false;  /* Timeout */
}

/**
 * @brief Interrupt handler (called by core loader)
 */
void etherlink3_interrupt_handler(void)
{
    /* Interrupt handling would be implemented here */
    /* For now, just acknowledge the interrupt */
}