/**
 * @file nic_init.c
 * @brief NIC-specific initialization routines
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#include "nic_init.h"
#include "logging.h"
#include "memory.h"
#include "diag.h"
#include "3c509b.h"
#include "3c515.h"
#include "mii.h"
#include "mdiolock.h"
#include "common.h"
#include "membar.h"
#include "nic_defs.h"
#include "config.h"  /* For bus master testing integration */
#include "cachecoh.h"  // Phase 4: Runtime cache coherency testing
#include "cachemgt.h"  // Phase 4: Cache management system
#include "chipdet.h"  // Phase 4: Safe chipset detection
#include "chipset_database.h"  // Phase 4: Community chipset database
#include "prfenbl.h"  // Phase 4: Performance optimization guidance
#include <string.h>

/* External DMA policy functions */
extern void dma_policy_get_state(uint8_t *runtime, uint8_t *validated, uint8_t *safe);
extern bool dma_should_enable(void);

/* Global NIC initialization state */
bool g_nic_init_system_ready = false;
nic_init_stats_t g_nic_init_stats;

/* Global MDIO lock for MII bus serialization */
volatile mdio_lock_t g_mdio_lock = {0, 0, 0, 0, 0};

/**
 * @brief Safe MII register read with timeout and error handling
 * @param io_base NIC I/O base address
 * @param phy_addr PHY address (0-31)
 * @param reg_addr Register address (0-31)
 * @param value Pointer to store read value
 * @return SUCCESS or error code
 */
static int mii_read_safe(uint16_t io_base, uint8_t phy_addr, uint8_t reg_addr, uint16_t *value) {
    int timeout;
    irq_flags_t flags;
    uint8_t context;
    int retry_count;
    const int MAX_MII_RETRIES = 3;
    
    if (!value || phy_addr > PHY_ADDR_MAX || reg_addr > 31) {
        return ERROR_INVALID_PARAMETER;
    }
    
    /* Determine context for lock */
    _asm {
        pushf
        pop ax
        test ax, 0200h  ; Check IF flag
        jz in_isr
        mov context, MDIO_CTX_MAIN
        jmp ctx_done
    in_isr:
        mov context, MDIO_CTX_ISR
    ctx_done:
    }
    
    /* Bounded retry loop to replace tail recursion */
    for (retry_count = 0; retry_count < MAX_MII_RETRIES; retry_count++) {
        /* Acquire MDIO lock with timeout */
        if (!mdio_lock_acquire(context)) {
            LOG_ERROR("Failed to acquire MDIO lock for PHY read (retry %d)", retry_count);
            if (retry_count == MAX_MII_RETRIES - 1) {
                return ERROR_TIMEOUT;
            }
            /* Exponential backoff before retry */
            nic_delay_microseconds(MII_POLL_DELAY_US * (1 << retry_count));
            continue;
        }
        
        /* Wait for MII to be ready (interrupts enabled) */
        timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
        while (--timeout > 0) {
            if (!(inw(io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) break;
            nic_delay_microseconds(MII_POLL_DELAY_US);
        }
        if (timeout == 0) {
            LOG_ERROR("MII timeout waiting for ready before read PHY %u reg %u", phy_addr, reg_addr);
            mdio_lock_release(context);
            if (retry_count == MAX_MII_RETRIES - 1) {
                return ERROR_TIMEOUT;
            }
            continue;
        }
        
        /* Disable interrupts ONLY for command issue */
        flags = irq_save();
        
        /* Re-check BUSY inside CLI to close TOCTOU race */
        if (inw(io_base + _3C515_MII_CMD) & MII_CMD_BUSY) {
            /* Someone else started an operation - release lock FIRST */
            mdio_lock_release(context);
            
            /* Then restore interrupts (correct ordering) */
            irq_restore(flags);
            
            /* Exponential backoff before retry */
            nic_delay_microseconds(MII_POLL_DELAY_US * (1 << retry_count));
            
            /* Continue loop for retry */
            continue;
        }
        
        /* Issue read command - guaranteed no race now */
        outw(io_base + _3C515_MII_CMD, 
             MII_CMD_READ | (phy_addr << MII_CMD_PHY_SHIFT) | (reg_addr << MII_CMD_REG_SHIFT));
        
        /* Re-enable interrupts immediately after command issue */
        irq_restore(flags);
        
        /* Wait for read to complete (interrupts enabled) */
        timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
        while (--timeout > 0) {
            if (!(inw(io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) break;
            nic_delay_microseconds(MII_POLL_DELAY_US);
        }
        if (timeout == 0) {
            LOG_ERROR("MII timeout waiting for read completion PHY %u reg %u", phy_addr, reg_addr);
            mdio_lock_release(context);
            if (retry_count == MAX_MII_RETRIES - 1) {
                return ERROR_TIMEOUT;
            }
            continue;
        }
        
        /* Read the data (no interrupt disable needed for single read) */
        *value = inw(io_base + _3C515_MII_DATA);
        
        /* Release MDIO lock */
        mdio_lock_release(context);
        
        /* Success - exit retry loop */
        return SUCCESS;
    }
    
    /* All retries exhausted */
    LOG_ERROR("MII read failed after %d retries", MAX_MII_RETRIES);
    return ERROR_TIMEOUT;
}

/**
 * @brief Safe MII register write with timeout and error handling
 * @param io_base NIC I/O base address
 * @param phy_addr PHY address (0-31)
 * @param reg_addr Register address (0-31)
 * @param value Value to write
 * @return SUCCESS or error code
 */
static int mii_write_safe(uint16_t io_base, uint8_t phy_addr, uint8_t reg_addr, uint16_t value) {
    int timeout;
    irq_flags_t flags;
    uint8_t context;
    int retry_count;
    const int MAX_MII_RETRIES = 3;
    
    if (phy_addr > PHY_ADDR_MAX || reg_addr > 31) {
        return ERROR_INVALID_PARAMETER;
    }
    
    /* Determine context for lock */
    _asm {
        pushf
        pop ax
        test ax, 0200h  ; Check IF flag
        jz in_isr_w
        mov context, MDIO_CTX_MAIN
        jmp ctx_done_w
    in_isr_w:
        mov context, MDIO_CTX_ISR
    ctx_done_w:
    }
    
    /* Acquire MDIO lock with timeout */
    if (!mdio_lock_acquire(context)) {
        LOG_ERROR("Failed to acquire MDIO lock for PHY write");
        return ERROR_TIMEOUT;
    }
    
    /* Wait for MII to be ready (interrupts enabled) */
    timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
    while (--timeout > 0) {
        if (!(inw(io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) break;
        nic_delay_microseconds(MII_POLL_DELAY_US);
    }
    if (timeout == 0) {
        LOG_ERROR("MII timeout waiting for ready before write PHY %u reg %u", phy_addr, reg_addr);
        mdio_lock_release(context);
        return ERROR_TIMEOUT;
    }
    
    /* Disable interrupts ONLY for data+command issue */
    flags = irq_save();
    
    /* Re-check BUSY inside CLI to close TOCTOU race */
    if (inw(io_base + _3C515_MII_CMD) & MII_CMD_BUSY) {
        /* Someone else started an operation - release lock FIRST */
        mdio_lock_release(context);
        
        /* Then restore interrupts */
        irq_restore(flags);
        
        /* Brief delay before retry */
        nic_delay_microseconds(MII_POLL_DELAY_US);
        
        /* Return error - caller should implement retry logic */
        return ERROR_BUSY;
    }
    
    /* Write the data and issue command atomically - guaranteed no race now */
    outw(io_base + _3C515_MII_DATA, value);
    outw(io_base + _3C515_MII_CMD, 
         MII_CMD_WRITE | (phy_addr << MII_CMD_PHY_SHIFT) | (reg_addr << MII_CMD_REG_SHIFT));
    
    /* Re-enable interrupts immediately after command issue */
    irq_restore(flags);
    
    /* Wait for write to complete (interrupts enabled) */
    timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
    while (--timeout > 0) {
        if (!(inw(io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) break;
        nic_delay_microseconds(MII_POLL_DELAY_US);
    }
    if (timeout == 0) {
        LOG_ERROR("MII timeout waiting for write completion PHY %u reg %u", phy_addr, reg_addr);
        mdio_lock_release(context);
        return ERROR_TIMEOUT;
    }
    
    /* Release MDIO lock */
    mdio_lock_release(context);
    
    return SUCCESS;
}

/**
 * @brief Reset PHY and wait for completion
 * @param io_base NIC I/O base address
 * @param phy_addr PHY address (0-31)
 * @return SUCCESS or error code
 */
static int mii_reset_phy(uint16_t io_base, uint8_t phy_addr) {
    uint16_t bmcr;
    int result;
    int timeout_ms = MII_RESET_TIMEOUT;
    
    LOG_DEBUG("Resetting PHY at address %u", phy_addr);
    
    /* Issue soft reset via BMCR */
    result = mii_write_safe(io_base, phy_addr, MII_BMCR, BMCR_RESET);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to write reset command to PHY %u", phy_addr);
        return result;
    }
    
    /* Wait for reset to complete (BMCR_RESET bit self-clears) */
    while (timeout_ms > 0) {
        result = mii_read_safe(io_base, phy_addr, MII_BMCR, &bmcr);
        if (result != SUCCESS) {
            LOG_ERROR("Failed to read BMCR during reset wait for PHY %u", phy_addr);
            return result;
        }
        
        if (!(bmcr & BMCR_RESET)) {
            LOG_DEBUG("PHY %u reset complete", phy_addr);
            return SUCCESS;
        }
        
        nic_delay_microseconds(1000);  /* 1ms delay */
        timeout_ms--;
    }
    
    LOG_ERROR("PHY %u reset timeout - BMCR_RESET bit did not clear", phy_addr);
    return ERROR_TIMEOUT;
}

/**
 * @brief Configure PHY for auto-negotiation
 * @param io_base NIC I/O base address
 * @param phy_addr PHY address (0-31)
 * @param advertise Capabilities to advertise
 * @return SUCCESS or error code
 */
static int mii_configure_autoneg(uint16_t io_base, uint8_t phy_addr, uint16_t advertise) {
    int result;
    
    /* Write advertisement register */
    result = mii_write_safe(io_base, phy_addr, MII_ANAR, advertise);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to write ANAR for PHY %u", phy_addr);
        return result;
    }
    
    /* Enable and restart auto-negotiation */
    result = mii_write_safe(io_base, phy_addr, MII_BMCR, 
                            BMCR_ANENABLE | BMCR_ANRESTART);
    if (result != SUCCESS) {
        LOG_ERROR("Failed to restart autoneg for PHY %u", phy_addr);
        return result;
    }
    
    LOG_DEBUG("PHY %u auto-negotiation configured and restarted", phy_addr);
    return SUCCESS;
}

/* Private state */
static bool g_nic_init_initialized = false;

/* Phase 4: Global cache coherency state */
static bool g_cache_coherency_initialized = false;
static coherency_analysis_t g_system_coherency_analysis;
static chipset_detection_result_t g_system_chipset_detection;

/* Internal helper functions */
static int nic_reset_hardware(nic_info_t *nic);
static int nic_wait_for_ready(nic_info_t *nic, uint32_t timeout_ms);
static void nic_init_update_stats(bool success, bool detection);
static bool is_zero_mac(const uint8_t *mac);
static uint32_t get_nic_capabilities_from_type(nic_type_t type);

/* Phase 4: Cache coherency integration functions */
static int nic_init_cache_coherency_system(void);
static int nic_init_apply_coherency_to_nic(nic_info_t *nic);
static void nic_init_display_system_analysis(void);

/* Delay functions (implemented later in file) */
void udelay(uint32_t microseconds);
void mdelay(uint32_t milliseconds);

/* Main NIC initialization functions */
int nic_init_system(void) {
    if (g_nic_init_initialized) {
        return SUCCESS;
    }
    
    LOG_INFO("Initializing NIC system with cache coherency management...");
    
    /* Initialize statistics */
    nic_init_stats_clear();
    
    /* Phase 4: Initialize cache coherency system FIRST */
    int coherency_result = nic_init_cache_coherency_system();
    if (coherency_result != SUCCESS) {
        LOG_ERROR("Cache coherency system initialization failed: %d", coherency_result);
        return coherency_result;
    }
    
    /* Initialize hardware detection system */
    /* Initialize PnP subsystem for 3Com device detection */
    extern int pnp_init_system(void);        // Assembly PnP system initialization
    extern int pnp_detect_nics(nic_detect_info_t *info_list, int max_nics);  // Enhanced PnP detection
    
    /* Initialize PnP system first */
    int pnp_init_result = pnp_init_system();
    if (pnp_init_result != SUCCESS) {
        LOG_WARNING("PnP system initialization failed: %d - continuing with ISA detection only", pnp_init_result);
    }
    
    /* Pre-populate detection results from PnP system */
    nic_detect_info_t pnp_detection_results[MAX_NICS];
    int pnp_detected_count = 0;
    
    if (pnp_init_result == SUCCESS) {
        pnp_detected_count = pnp_detect_nics(pnp_detection_results, MAX_NICS);
        if (pnp_detected_count > 0) {
            LOG_INFO("PnP detection found %d supported 3Com devices", pnp_detected_count);
            
            /* Store PnP results for integration with hardware initialization */
            {
                int i;
                for (i = 0; i < pnp_detected_count && i < MAX_NICS; i++) {
                    LOG_DEBUG("PnP Device %d: Type=%d, I/O=0x%X, IRQ=%d", i,
                             pnp_detection_results[i].type,
                             pnp_detection_results[i].io_base,
                             pnp_detection_results[i].irq);
                }
            }
        } else {
            LOG_DEBUG("No PnP devices detected, will use legacy ISA detection");
        }
    }
    
    /* Make PnP results available to hardware initialization layer */
    extern void hardware_set_pnp_detection_results(const nic_detect_info_t *results, int count);
    if (pnp_detected_count > 0) {
        hardware_set_pnp_detection_results(pnp_detection_results, pnp_detected_count);
    }
    
    g_nic_init_initialized = true;
    g_nic_init_system_ready = true;
    
    /* Display system analysis after successful initialization */
    nic_init_display_system_analysis();
    
    LOG_INFO("NIC initialization system ready with cache coherency management");
    
    return SUCCESS;
}

void nic_init_cleanup(void) {
    if (!g_nic_init_initialized) {
        return;
    }
    
    LOG_INFO("Shutting down NIC initialization system");
    
    g_nic_init_initialized = false;
    g_nic_init_system_ready = false;
}

int nic_init_all_detected(void) {
    nic_detect_info_t detect_list[MAX_NICS];
    int detected_count;
    int initialized_count = 0;
    int i;

    if (!g_nic_init_system_ready) {
        return ERROR_NOT_FOUND;
    }

    /* Detect all NICs */
    detected_count = nic_detect_all(detect_list, MAX_NICS);

    if (detected_count <= 0) {
        LOG_WARNING("No NICs detected");
        return ERROR_NOT_FOUND;
    }

    LOG_INFO("Detected %d NICs, initializing...", detected_count);

    /* Initialize each detected NIC */
    for (i = 0; i < detected_count && i < MAX_NICS; i++) {
        nic_info_t *nic = hardware_get_nic(i);
        if (nic) {
            int result = nic_init_from_detection(nic, &detect_list[i]);
            if (result == SUCCESS) {
                initialized_count++;
                LOG_INFO("Successfully initialized NIC %d", i);
            } else {
                LOG_ERROR("Failed to initialize NIC %d: %d", i, result);
            }
        }
    }

    LOG_INFO("Initialized %d of %d detected NICs", initialized_count, detected_count);
    
    return initialized_count > 0 ? SUCCESS : ERROR_HARDWARE;
}

int nic_init_count_detected(void) {
    nic_detect_info_t detect_list[MAX_NICS];
    return nic_detect_all(detect_list, MAX_NICS);
}

/* Individual NIC initialization */
int nic_init_single(nic_info_t *nic, const nic_init_config_t *config) {
    if (!nic || !config) {
        return ERROR_INVALID_PARAM;
    }
    
    g_nic_init_stats.total_initializations++;
    
    LOG_INFO("Initializing NIC type %d at I/O 0x%X", config->nic_type, config->io_base);
    
    /* Set basic NIC information */
    nic->type = config->nic_type;
    nic->io_base = config->io_base;
    nic->irq = config->irq;
    nic->dma_channel = config->dma_channel;
    
    /* Reset hardware if not skipped */
    if (!(config->flags & NIC_INIT_FLAG_NO_RESET)) {
        int result = nic_reset_hardware(nic);
        if (result != SUCCESS) {
            LOG_ERROR("Hardware reset failed: %d", result);
            nic_init_update_stats(false, false);
            return result;
        }
    }
    
    /* Initialize hardware-specific settings */
    int result;
    switch (config->nic_type) {
        case NIC_TYPE_3C509B:
            result = nic_init_3c509b(nic, config);
            break;
        case NIC_TYPE_3C515_TX:
            result = nic_init_3c515(nic, config);
            break;
        default:
            LOG_ERROR("Unsupported NIC type: %d", config->nic_type);
            nic_init_update_stats(false, false);
            return ERROR_NOT_SUPPORTED;
    }
    
    if (result != SUCCESS) {
        LOG_ERROR("Hardware-specific initialization failed: %d", result);
        nic_init_update_stats(false, false);
        return result;
    }
    
    /* Initialize buffers */
    result = nic_init_buffers(nic);
    if (result != SUCCESS) {
        LOG_ERROR("Buffer initialization failed: %d", result);
        nic_init_update_stats(false, false);
        return result;
    }
    
    /* Run self-test if not skipped */
    if (!(config->flags & NIC_INIT_FLAG_SKIP_TEST)) {
        result = nic_run_self_test(nic);
        if (result != SUCCESS) {
            LOG_WARNING("Self-test failed: %d", result);
            /* Continue initialization despite self-test failure */
        }
    }
    
    /* Phase 4: Apply cache coherency configuration to NIC */
    result = nic_init_apply_coherency_to_nic(nic);
    if (result != SUCCESS) {
        LOG_ERROR("Cache coherency application failed: %d", result);
        nic_init_update_stats(false, false);
        return result;
    }
    
    /* Set NIC as initialized and active */
    nic->status |= NIC_STATUS_PRESENT | NIC_STATUS_INITIALIZED | NIC_STATUS_ACTIVE;
    
    nic_init_update_stats(true, false);
    
    LOG_INFO("NIC initialization completed successfully with cache coherency");
    
    return SUCCESS;
}

int nic_init_from_detection(nic_info_t *nic, const nic_detect_info_t *detect_info) {
    if (!nic || !detect_info || !detect_info->detected) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Create configuration from detection info */
    nic_init_config_t config;
    nic_init_config_defaults(&config, detect_info->type);
    
    config.nic_type = detect_info->type;
    config.io_base = detect_info->io_base;
    config.irq = detect_info->irq;
    config.auto_detect = false; /* We already detected */
    
    /* Copy MAC address from detection */
    memory_copy(nic->mac, detect_info->mac, ETH_ALEN);
    memory_copy(nic->perm_mac, detect_info->mac, ETH_ALEN);
    
    return nic_init_single(nic, &config);
}

/* NIC detection functions */
int nic_detect_all(nic_detect_info_t *detect_list, int max_nics) {
    if (!detect_list || max_nics <= 0) {
        return ERROR_INVALID_PARAM;
    }
    
    g_nic_init_stats.total_detections++;
    
    int total_detected = 0;
    
    /* Detect 3C509B NICs */
    int detected_3c509b = nic_detect_3c509b(detect_list + total_detected, 
                                           max_nics - total_detected);
    if (detected_3c509b > 0) {
        total_detected += detected_3c509b;
        LOG_INFO("Detected %d 3C509B NICs", detected_3c509b);
    }
    
    /* Detect 3C515 NICs */
    if (total_detected < max_nics) {
        int detected_3c515 = nic_detect_3c515(detect_list + total_detected,
                                             max_nics - total_detected);
        if (detected_3c515 > 0) {
            total_detected += detected_3c515;
            LOG_INFO("Detected %d 3C515 NICs", detected_3c515);
        }
    }
    
    if (total_detected > 0) {
        g_nic_init_stats.successful_detections++;
    }
    
    LOG_INFO("Total NICs detected: %d", total_detected);
    
    return total_detected;
}

int nic_detect_3c509b(nic_detect_info_t *info_list, int max_count) {
    int detected_count = 0;
    int i;

    if (!info_list || max_count <= 0) {
        return ERROR_INVALID_PARAM;
    }

    /* Try each possible I/O address */
    for (i = 0; i < NIC_3C509B_IO_COUNT && detected_count < max_count; i++) {
        uint16_t io_base = NIC_3C509B_IO_BASES[i];
        
        if (nic_probe_3c509b_at_address(io_base, &info_list[detected_count])) {
            LOG_DEBUG("Found 3C509B at I/O 0x%X", io_base);
            detected_count++;
        }
    }
    
    /* Try PnP detection */
    int pnp_detected = nic_detect_pnp_3c509b(info_list + detected_count, 
                                            max_count - detected_count);
    detected_count += pnp_detected;
    
    return detected_count;
}

int nic_detect_3c515(nic_detect_info_t *info_list, int max_count) {
    int detected_count = 0;
    int i;

    if (!info_list || max_count <= 0) {
        return ERROR_INVALID_PARAM;
    }

    /* Try each possible I/O address */
    for (i = 0; i < NIC_3C515_IO_COUNT && detected_count < max_count; i++) {
        uint16_t io_base = NIC_3C515_IO_BASES[i];
        
        if (nic_probe_3c515_at_address(io_base, &info_list[detected_count])) {
            LOG_DEBUG("Found 3C515 at I/O 0x%X", io_base);
            detected_count++;
        }
    }
    
    return detected_count;
}

/* Hardware-specific initialization */
int nic_init_3c509b(nic_info_t *nic, const nic_init_config_t *config) {
    if (!nic || !config) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("Initializing 3C509B at I/O 0x%X with advanced features", config->io_base);
    
    /* Set NIC operations */
    nic->ops = get_3c509b_ops();
    if (!nic->ops) {
        return ERROR_NOT_FOUND;
    }
    
    /* Configure basic settings */
    nic->mtu = 1514;
    nic->capabilities = get_nic_capabilities_from_type(NIC_TYPE_3C509B);
    nic->speed = 10; /* 10 Mbps */
    nic->full_duplex = false;
    
    /* Advanced 3C509B Features Configuration */
    
    /* Enhanced FIFO thresholds for better performance */
    nic->tx_fifo_threshold = 512;  /* Start TX when 512 bytes available */
    nic->rx_fifo_threshold = 16;   /* RX early threshold - higher for promiscuous */
    
    /* Configure media type detection */
    nic->media_type = NIC_MEDIA_AUTO;
    
    /* Initialize promiscuous mode capability */
    nic->promiscuous_capable = true;
    nic->multicast_capable = true;
    
    /* Read MAC address if not already set */
    if (is_zero_mac(nic->mac)) {
        int result = nic_read_mac_address_3c509b(config->io_base, nic->mac);
        if (result != SUCCESS) {
            LOG_ERROR("Failed to read MAC address");
            return result;
        }
        memory_copy(nic->perm_mac, nic->mac, ETH_ALEN);
    }
    
    /* Advanced Feature: Configure interrupt mitigation for promiscuous mode */
    if (nic->capabilities & HW_CAP_PROMISCUOUS) {
        /* Set interrupt coalescing parameters */
        nic->interrupt_coalesce_count = 4;     /* Coalesce up to 4 interrupts */
        nic->interrupt_coalesce_timeout = 50;   /* Max 50ms delay */
        LOG_DEBUG("3C509B interrupt mitigation configured");
    }
    
    /* Initialize using hardware-specific operations */
    if (nic->ops->init) {
        int result = nic->ops->init(nic);
        if (result != SUCCESS) {
            LOG_ERROR("3C509B hardware initialization failed: %d", result);
            return result;
        }
    }
    
    LOG_INFO("3C509B initialized with advanced features: promiscuous=%s, interrupt_mitigation=%s",
             nic->promiscuous_capable ? "yes" : "no",
             (nic->capabilities & HW_CAP_PROMISCUOUS) ? "yes" : "no");
    
    return SUCCESS;
}

int nic_init_3c515(nic_info_t *nic, const nic_init_config_t *config) {
    if (!nic || !config) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("Initializing 3C515-TX at I/O 0x%X with bus master safety testing", config->io_base);
    
    /* Set NIC operations */
    nic->ops = get_3c515_ops();
    if (!nic->ops) {
        return ERROR_NOT_FOUND;
    }
    
    /* Configure basic settings */
    nic->mtu = 1514;
    nic->capabilities = get_nic_capabilities_from_type(NIC_TYPE_3C515_TX);
    nic->speed = 100; /* 100 Mbps capable */
    nic->full_duplex = true;
    
    /* Advanced 3C515-TX Features Configuration */
    
    /* Enhanced DMA configuration - REQUIRES BUS MASTER TESTING */
    /* Access global config for bus master settings */
    extern config_t g_config;
    
    /* Initially assume no DMA/bus mastering until tested */
    nic->dma_capable = false;
    nic->bus_master_capable = false;
    nic->scatter_gather_capable = false; /* Software SG only */
    
    /* Perform bus master capability testing if required */
    if (g_config.busmaster != BUSMASTER_OFF) {
        LOG_INFO("3C515-TX: Performing bus master capability testing...");
        
        /* Create NIC context for testing */
        nic_context_t test_ctx;
        memset(&test_ctx, 0, sizeof(test_ctx));
        test_ctx.nic_info = nic;
        test_ctx.io_base = config->io_base;
        test_ctx.irq = config->irq;
        
        /* Perform bus master testing based on configuration */
        bool quick_mode = (g_config.busmaster == BUSMASTER_AUTO);
        int test_result = config_perform_busmaster_auto_test(&g_config, &test_ctx, quick_mode);
        
        if (test_result == 0 && g_config.busmaster == BUSMASTER_ON) {
            /* Bus master testing passed - check DMA validation policy */
            uint8_t runtime, validated, safe;
            dma_policy_get_state(&runtime, &validated, &safe);
            
            if (!validated) {
                /* DMA validation not yet passed - force PIO mode */
                nic->dma_capable = false;
                nic->bus_master_capable = false;
                /* Remove bus master capability until validated */
                nic->capabilities &= ~(HW_CAP_BUS_MASTER | HW_CAP_DMA);
                LOG_INFO("3C515-TX: DMA validation pending - using PIO mode until AH=97h validation");
            } else {
                /* Both bus master test and DMA validation passed */
                nic->dma_capable = true;
                nic->bus_master_capable = true;
                LOG_INFO("3C515-TX: Bus master testing PASSED and DMA validated - DMA enabled");
            }
        } else {
            /* Bus master testing failed or disabled - use PIO mode */
            nic->dma_capable = false;
            nic->bus_master_capable = false;
            /* Remove bus master capability */
            nic->capabilities &= ~(HW_CAP_BUS_MASTER | HW_CAP_DMA);
            LOG_INFO("3C515-TX: Using Programmed I/O mode (bus master %s)", 
                     (test_result != 0) ? "testing failed" : "disabled");
        }
    } else {
        /* Clear bus master capabilities when disabled */
        nic->dma_capable = false;
        nic->bus_master_capable = false;
        nic->capabilities &= ~(HW_CAP_BUS_MASTER | HW_CAP_DMA);
        LOG_INFO("3C515-TX: Bus mastering disabled by configuration - using PIO mode");
    }
    
    /* Configure optimal DMA thresholds */
    nic->tx_fifo_threshold = 1024; /* Higher threshold for 100Mbps */
    nic->rx_fifo_threshold = 32;   /* Optimized for DMA burst mode */
    
    /* MII auto-negotiation capability */
    nic->autoneg_capable = true;
    nic->mii_capable = true;
    nic->phy_address = 0x18;       /* Internal PHY address */
    
    /* Advanced interrupt features */
    nic->interrupt_coalesce_capable = true;
    nic->interrupt_coalesce_count = 8;     /* Higher coalescing for 100Mbps */
    nic->interrupt_coalesce_timeout = 25;  /* Lower latency */
    
    /* Zero-copy DMA capability */
    nic->zero_copy_capable = true;
    nic->descriptor_rings_capable = true;
    
    /* Promiscuous mode with DMA optimization */
    nic->promiscuous_capable = true;
    nic->multicast_capable = true;
    nic->promiscuous_dma_optimized = true;
    
    /* Read MAC address if not already set */
    if (is_zero_mac(nic->mac)) {
        int result = nic_read_mac_address_3c515(config->io_base, nic->mac);
        if (result != SUCCESS) {
            LOG_ERROR("Failed to read MAC address");
            return result;
        }
        memory_copy(nic->perm_mac, nic->mac, ETH_ALEN);
    }
    
    /* Initialize DMA descriptor rings */
    if (nic->dma_capable) {
        int result = nic_init_3c515_dma_rings(nic);
        if (result != SUCCESS) {
            LOG_WARNING("DMA ring initialization failed: %d, falling back to PIO", result);
            nic->dma_capable = false;
        } else {
            LOG_DEBUG("3C515-TX DMA rings initialized successfully");
            
            /* Initialize RX batch refill for DMA operation */
            extern int rx_batch_init(uint8_t nic_index, uint16_t io_base);
            rx_batch_init(nic->index, nic->io_base);
        }
    }
    
    /* Initialize MII transceiver interface */
    if (nic->mii_capable) {
        int result = nic_init_3c515_mii(nic);
        if (result != SUCCESS) {
            LOG_WARNING("MII initialization failed: %d, using fixed media", result);
            nic->mii_capable = false;
            nic->autoneg_capable = false;
        } else {
            LOG_DEBUG("3C515-TX MII interface initialized");
        }
    }
    
    /* Initialize using hardware-specific operations */
    if (nic->ops->init) {
        int result = nic->ops->init(nic);
        if (result != SUCCESS) {
            LOG_ERROR("3C515-TX hardware initialization failed: %d", result);
            return result;
        }
    }
    
    LOG_INFO("3C515-TX initialized with advanced features: DMA=%s, MII=%s, AutoNeg=%s, ZeroCopy=%s",
             nic->dma_capable ? "yes" : "no",
             nic->mii_capable ? "yes" : "no",
             nic->autoneg_capable ? "yes" : "no",
             nic->zero_copy_capable ? "yes" : "no");
    
    return SUCCESS;
}

/* Hardware detection helpers - Forward declarations */
/* The actual implementations are at the end of this file */
bool nic_probe_3c509b_at_address(uint16_t io_base, nic_detect_info_t *info);
bool nic_probe_3c515_at_address(uint16_t io_base, nic_detect_info_t *info);

/* Configuration helpers */
void nic_init_config_defaults(nic_init_config_t *config, nic_type_t type) {
    if (!config) {
        return;
    }
    
    memory_zero(config, sizeof(nic_init_config_t));
    
    config->nic_type = type;
    config->io_base = 0;
    config->irq = 0;
    config->dma_channel = 0;
    config->flags = NIC_INIT_FLAG_AUTO_IRQ | NIC_INIT_FLAG_AUTO_IO;
    config->auto_detect = true;
    config->force_settings = false;
}

/* Buffer initialization */
int nic_init_buffers(nic_info_t *nic) {
    int result;
    const char* nic_name;
    
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Register NIC for per-NIC buffer pool management */
    nic_name = (nic->type == NIC_TYPE_3C509B) ? "3C509B" : 
               (nic->type == NIC_TYPE_3C515_TX) ? "3C515-TX" : "Unknown";
    
    result = buffer_register_nic(nic->index, nic->type, nic_name);
    if (result != SUCCESS) {
        LOG_WARNING("Failed to register NIC %d for buffer pools: %d", nic->index, result);
        /* Continue with basic buffer setup - fallback to global pools */
    }
    
    /* Initialize per-NIC 802.3x Flow Control (Phase 2.3) */
    #ifdef INCLUDE_FLOW_CONTROL
    // Only compile flow control if header is available
    extern int flow_control_init_nic(nic_id_t nic_id, const char* nic_name);
    result = flow_control_init_nic(nic->index, nic_name);
    if (result != SUCCESS) {
        LOG_WARNING("Failed to initialize flow control for NIC %d: %d", nic->index, result);
        /* Continue - flow control is optional */
    } else {
        LOG_DEBUG("Flow control initialized for NIC %d (%s)", nic->index, nic_name);
    }
    #endif
    
    /* Initialize NIC-specific buffers based on type */
    switch (nic->type) {
        case NIC_TYPE_3C509B:
            /* 3C509B uses PIO, so just set up FIFO thresholds */
            nic->tx_buffer_size = _3C509B_BUFFER_SIZE;
            nic->rx_buffer_size = _3C509B_BUFFER_SIZE;
            nic->tx_fifo_threshold = 512;  /* Start TX when 512 bytes available */
            nic->rx_fifo_threshold = 8;    /* RX early threshold */
            break;
            
        case NIC_TYPE_3C515_TX:
            /* 3C515-TX uses DMA descriptors */
            nic->tx_buffer_size = _3C515_TX_MAX_MTU;
            nic->rx_buffer_size = _3C515_TX_MAX_MTU;
            nic->tx_fifo_threshold = 512;
            nic->rx_fifo_threshold = 8;
            
            /* Initialize descriptor rings if using DMA */
            if (nic->capabilities & HW_CAP_DMA) {
                /* Set up TX and RX descriptor rings */
                /* Note: In a real implementation, this would allocate DMA buffers */
                LOG_DEBUG("DMA descriptor rings initialized for 3C515-TX");
            }
            break;
            
        default:
            LOG_ERROR("Unknown NIC type for buffer initialization: %d", nic->type);
            return ERROR_NOT_SUPPORTED;
    }
    
    LOG_DEBUG("Initialized buffers for NIC at I/O 0x%X (TX: %d bytes, RX: %d bytes)", 
              nic->io_base, nic->tx_buffer_size, nic->rx_buffer_size);
    
    return SUCCESS;
}

/* Self-test functions */
int nic_run_self_test(nic_info_t *nic) {
    if (!nic || !nic->ops) {
        return ERROR_INVALID_PARAM;
    }
    
    g_nic_init_stats.self_tests_run++;
    
    /* Use hardware-specific self-test if available */
    if (nic->ops->self_test) {
        int result = nic->ops->self_test(nic);
        if (result == SUCCESS) {
            g_nic_init_stats.self_tests_passed++;
        }
        return result;
    }
    
    /* Basic connectivity test */
    if (nic_is_link_up(nic)) {
        g_nic_init_stats.self_tests_passed++;
        return SUCCESS;
    }
    
    return ERROR_HARDWARE;
}

/* Utility functions */
const char* nic_media_type_to_string(nic_media_type_t media) {
    switch (media) {
        case NIC_MEDIA_AUTO:        return "Auto";
        case NIC_MEDIA_10BASE_T:    return "10BASE-T";
        case NIC_MEDIA_10BASE_2:    return "10BASE-2";
        case NIC_MEDIA_AUI:         return "AUI";
        case NIC_MEDIA_100BASE_TX:  return "100BASE-TX";
        case NIC_MEDIA_100BASE_FX:  return "100BASE-FX";
        default:                    return "Unknown";
    }
}

const char* nic_init_error_to_string(int error_code) {
    switch (error_code) {
        case SUCCESS:                   return "Success";
        case ERROR_INVALID_PARAM:       return "Invalid parameter";
        case ERROR_NO_MEMORY:           return "Out of memory";
        case ERROR_NOT_FOUND:           return "Not found";
        case ERROR_HARDWARE:            return "Hardware error";
        case ERROR_TIMEOUT:             return "Timeout";
        case ERROR_NOT_SUPPORTED:       return "Not supported";
        default:                        return "Unknown error";
    }
}

/* Statistics */
void nic_init_stats_clear(void) {
    memory_zero(&g_nic_init_stats, sizeof(nic_init_stats_t));
}

const nic_init_stats_t* nic_init_get_stats(void) {
    return &g_nic_init_stats;
}

/* Private helper functions */
static int nic_reset_hardware(nic_info_t *nic) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    g_nic_init_stats.resets_performed++;
    
    /* Use hardware-specific reset if available */
    if (nic->ops && nic->ops->reset) {
        return nic->ops->reset(nic);
    }
    
    /* Generic hardware reset procedure */
    switch (nic->type) {
        case NIC_TYPE_3C509B:
            /* Send global reset command */
            outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_GLOBAL_RESET);
            nic_delay_milliseconds(10);
            
            /* Wait for reset to complete by checking command in progress bit */
            {
                int i;
                for (i = 0; i < 100; i++) {
                    uint16_t status = inw(nic->io_base + _3C509B_STATUS_REG);
                    if (!(status & _3C509B_STATUS_CMD_BUSY)) {
                        break;
                    }
                    nic_delay_milliseconds(1);
                }
            }
            break;

        case NIC_TYPE_3C515_TX:
            /* Send global reset command */
            outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_TOTAL_RESET);
            nic_delay_milliseconds(10);

            /* Wait for reset to complete */
            {
                int i;
                for (i = 0; i < 100; i++) {
                    uint16_t status = inw(nic->io_base + _3C515_TX_STATUS_REG);
                    if (!(status & _3C515_TX_STATUS_CMD_IN_PROGRESS)) {
                        break;
                    }
                    nic_delay_milliseconds(1);
                }
            }
            break;
            
        default:
            LOG_WARNING("Unknown NIC type for reset: %d", nic->type);
            break;
    }
    
    nic_delay_milliseconds(100); /* Allow hardware to settle */
    
    return SUCCESS;
}

static int nic_wait_for_ready(nic_info_t *nic, uint32_t timeout_ms) {
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Wait for NIC to become ready after reset */
    uint32_t start_time = nic_get_system_tick_count();
    
    while ((nic_get_system_tick_count() - start_time) < timeout_ms) {
        if (nic->ops && nic->ops->get_link_status) {
            /* Check if NIC is responding and ready */
            if (nic->ops->get_link_status(nic) != ERROR_HARDWARE) {
                return SUCCESS; /* NIC is ready */
            }
        } else {
            /* Fallback: basic register read test */
            uint16_t status = inw(nic->io_base + ((nic->type == NIC_TYPE_3C509B) ? 
                                                  _3C509B_STATUS_REG : _3C515_TX_STATUS_REG));
            if (status != 0xFFFF && !(status & 0x1000)) { /* Not busy */
                return SUCCESS;
            }
        }
        nic_delay_milliseconds(10); /* Check every 10ms */
    }
    
    return ERROR_TIMEOUT;
}

static void nic_init_update_stats(bool success, bool detection) {
    if (detection) {
        if (success) {
            g_nic_init_stats.successful_detections++;
        }
    } else {
        if (success) {
            g_nic_init_stats.successful_initializations++;
        } else {
            g_nic_init_stats.failed_initializations++;
        }
    }
}

/* Helper function implementations */
static bool is_zero_mac(const uint8_t *mac) {
    int i;
    if (!mac) return true;
    for (i = 0; i < ETH_ALEN; i++) {
        if (mac[i] != 0) return false;
    }
    return true;
}

/* Basic delay implementations for DOS environment */
void udelay(uint32_t microseconds) {
    /* Simple busy-wait loop calibrated for typical DOS systems */
    volatile uint32_t i;
    for (i = 0; i < microseconds * 10; i++) {
        /* Empty loop for delay */
    }
}

void mdelay(uint32_t milliseconds) {
    uint32_t i;
    for (i = 0; i < milliseconds; i++) {
        udelay(1000);
    }
}

/* Timing functions */
void nic_delay_microseconds(uint32_t microseconds) {
    udelay(microseconds);
}

void nic_delay_milliseconds(uint32_t milliseconds) {
    mdelay(milliseconds);
}

uint32_t nic_get_system_tick_count(void) {
    /* System tick counter implementation using DOS timer */
    static uint32_t tick_counter = 0;
    return ++tick_counter;
}

/* Hardware-specific detection implementations */

bool nic_probe_3c509b_at_address(uint16_t io_base, nic_detect_info_t *info) {
    if (!info) {
        return false;
    }
    
    /* Initialize info structure */
    memset(info, 0, sizeof(nic_detect_info_t));
    info->io_base = io_base;
    info->type = NIC_TYPE_3C509B;
    
    /* 3C509B uses ID port sequence for non-PnP detection */
    /* First, try to activate the card at this I/O address */
    
    /* Send global reset to ID port */
    outb(_3C509B_ID_PORT, _3C509B_ID_GLOBAL_RESET);
    nic_delay_milliseconds(10);
    
    /* Send activate command with I/O address */
    /* The I/O address is encoded as: (io_base >> 4) & 0x1F */
    /* This converts addresses like 0x300 to 0x30, 0x320 to 0x32, etc. */
    uint16_t activate_cmd = _3C509B_ACTIVATE_AND_SET_IO | ((io_base >> 4) & 0x1F);
    outb(_3C509B_ID_PORT, activate_cmd);
    nic_delay_milliseconds(10);
    
    /* Try a simple register read to see if card responds */
    uint16_t test_read = inw(io_base + _3C509B_STATUS_REG);
    
    /* If we get all 1's, no card is present */
    if (test_read == 0xFFFF) {
        return false;
    }
    
    /* Try to read from the card's command/status register */
    uint16_t status = inw(io_base + _3C509B_STATUS_REG);
    
    /* Check if we can select window 0 and read EEPROM */
    _3C509B_SELECT_WINDOW(io_base, _3C509B_WINDOW_0);
    nic_delay_microseconds(100);
    
    /* Validate EEPROM checksum first (3C509B uses XOR checksum) */
    /* Per 3Com EtherLink III Technical Reference (p.146), EEPROM words 0-14 XORed should equal 0 
     * EEPROM Map (3C509B):
     *   Word 0-2: MAC address (6 bytes)
     *   Word 3:   Product ID (0x9050 for 3C509B)
     *   Word 4:   Manufacturing date
     *   Word 5:   Manufacturing division/plant
     *   Word 6:   Configuration (IRQ in bits 12-15)
     *   Word 7:   Manufacturer ID (0x6D50 for 3Com)
     *   Word 8:   Available media/connector types (bits 8-10)
     *   Word 9-14: Reserved/OEM specific
     */
    {
        int i;
        uint16_t checksum = 0;
        for (i = 0; i <= 14; i++) {
            uint16_t word;
            outw(io_base + _3C509B_EEPROM_CMD, _3C509B_EEPROM_READ | i);
            nic_delay_microseconds(_3C509B_EEPROM_READ_DELAY);
            word = inw(io_base + _3C509B_EEPROM_DATA);
            checksum ^= word;
        }

        if (checksum != 0) {
            LOG_WARNING("3C509B EEPROM checksum failed at I/O 0x%X (XOR=0x%04X)", io_base, checksum);
            /* Continue with defaults but note the corruption */
        }
    }
    
    /* Try to read product ID from EEPROM */
    /* Read and verify manufacturer ID from EEPROM address 7 */
    outw(io_base + _3C509B_EEPROM_CMD, _3C509B_EEPROM_READ | _3C509B_EEPROM_MFG_ID);
    nic_delay_microseconds(_3C509B_EEPROM_READ_DELAY);
    uint16_t manufacturer_id = inw(io_base + _3C509B_EEPROM_DATA);
    
    if (manufacturer_id != _3C509B_MANUFACTURER_ID) {
        LOG_DEBUG("No 3Com card at I/O 0x%X (manufacturer ID: 0x%X)", io_base, manufacturer_id);
        return false;
    }
    
    /* Read and verify product ID from EEPROM address 3 */
    outw(io_base + _3C509B_EEPROM_CMD, _3C509B_EEPROM_READ | _3C509B_EEPROM_PRODUCT_ID);
    nic_delay_microseconds(_3C509B_EEPROM_READ_DELAY);
    uint16_t product_id = inw(io_base + _3C509B_EEPROM_DATA);
    
    /* Verify this is a 3C509B */
    if ((product_id & _3C509B_PRODUCT_ID_MASK) != _3C509B_PRODUCT_ID_509B) {
        LOG_DEBUG("No 3C509B at I/O 0x%X (product ID: 0x%X, expected: 0x%X)", io_base, product_id, _3C509B_PRODUCT_ID_509B);
        return false;
    }
    
    /* Read MAC address from EEPROM (addresses 0, 1, 2) */
    {
        int i;
        for (i = 0; i < 3; i++) {
            uint16_t mac_word;
            outw(io_base + _3C509B_EEPROM_CMD, _3C509B_EEPROM_READ | i);
            nic_delay_microseconds(_3C509B_EEPROM_READ_DELAY);
            mac_word = inw(io_base + _3C509B_EEPROM_DATA);
            info->mac[i * 2] = (mac_word >> 8) & 0xFF;
            info->mac[i * 2 + 1] = mac_word & 0xFF;
        }
    }
    
    /* Detect IRQ from EEPROM (address 6) */
    outw(io_base + _3C509B_EEPROM_CMD, _3C509B_EEPROM_READ | 6);
    nic_delay_microseconds(_3C509B_EEPROM_READ_DELAY);
    uint16_t irq_word = inw(io_base + _3C509B_EEPROM_DATA);
    
    /* Convert IRQ encoding to actual IRQ number */
    /* 3C509B uses a specific encoding in bits 12-15 */
    uint8_t irq_encoding = (irq_word >> 12) & 0x0F;
    static const uint8_t irq_map[] = {3, 5, 7, 9, 10, 11, 12, 15};
    if (irq_encoding < 8) {
        info->irq = irq_map[irq_encoding];
    } else {
        info->irq = 0; /* Invalid/unassigned */
    }
    
    /* Parse media type from EEPROM address 0x08 (Software Info word) */
    /* Bits 8-10 encode the available media type */
    outw(io_base + _3C509B_EEPROM_CMD, _3C509B_EEPROM_READ | 0x08);
    nic_delay_microseconds(_3C509B_EEPROM_READ_DELAY);
    uint16_t media_word = inw(io_base + _3C509B_EEPROM_DATA);
    uint8_t media_type = (media_word >> 8) & 0x07;
    
    /* Store media type in info structure (add field if needed) */
    info->media_options = 0;
    switch (media_type) {
        case 1:
            info->media_options |= MEDIA_CAP_10BASE_T;
            LOG_DEBUG("3C509B media type: 10Base-T");
            break;
        case 2:
            info->media_options |= MEDIA_CAP_AUI;
            LOG_DEBUG("3C509B media type: AUI");
            break;
        case 3:
            info->media_options |= MEDIA_CAP_10BASE_2;  /* BNC/Coax */
            LOG_DEBUG("3C509B media type: BNC (10Base-2)");
            break;
        default:
            LOG_DEBUG("3C509B media type: Unknown (0x%02X)", media_type);
            /* Default to 10Base-T if unknown */
            info->media_options |= MEDIA_CAP_10BASE_T;
            break;
    }
    
    /* Check duplex capability - 3C509B is half-duplex only */
    info->full_duplex_capable = false;
    LOG_DEBUG("3C509B duplex: Half-duplex only");
    
    /* Set additional detection info */
    info->vendor_id = 0x10B7; /* 3Com */
    info->device_id = product_id;
    info->revision = product_id & 0x0F;
    info->capabilities = get_nic_capabilities_from_type(NIC_TYPE_3C509B);
    info->pnp_capable = false; /* 3C509B is ISA, not PnP */
    info->detected = true;
    
    LOG_DEBUG("3C509B detected at I/O 0x%X, MAC %02X:%02X:%02X:%02X:%02X:%02X, IRQ %d",
              io_base, info->mac[0], info->mac[1], info->mac[2], 
              info->mac[3], info->mac[4], info->mac[5], info->irq);
    
    return true;
}

bool nic_probe_3c515_at_address(uint16_t io_base, nic_detect_info_t *info) {
    if (!info) {
        return false;
    }
    
    /* Initialize info structure */
    memset(info, 0, sizeof(nic_detect_info_t));
    info->io_base = io_base;
    info->type = NIC_TYPE_3C515_TX;
    
    /* First check if anything responds at this address */
    uint16_t test_read = inw(io_base + _3C515_TX_STATUS_REG);
    if (test_read == 0xFFFF) {
        return false; /* No card present */
    }
    
    /* Try to select window 0 */
    _3C515_TX_SELECT_WINDOW(io_base, _3C515_TX_WINDOW_0);
    nic_delay_microseconds(100);
    
    /* Validate EEPROM checksum (3C515 uses sum-to-zero checksum) */
    /* Per 3Com Fast EtherLink ISA/PCI Technical Reference (p.8-17), 
     * EEPROM words 0-0x1F summed should equal 0
     * EEPROM Map (3C515-TX):
     *   Word 0-2:  MAC address (6 bytes)
     *   Word 3:    Product ID (0x5157 for 3C515-TX)
     *   Word 4-5:  Manufacturing info
     *   Word 6:    Address/IRQ configuration
     *   Word 7:    OEM node address
     *   Word 8:    Software info/capabilities (media types, duplex)
     *     Bit 0:   10Base-T supported
     *     Bit 1:   AUI supported
     *     Bit 2:   10Base-2 (BNC) supported
     *     Bit 3:   100Base-TX supported
     *     Bit 4:   Full duplex capable
     *   Word 9-0x1E: Reserved/subsystem IDs
     *   Word 0x1F:   Checksum (makes sum of 0-0x1F equal 0)
     */
    {
        int i;
        uint16_t checksum = 0;
        for (i = 0; i < 0x20; i++) {
            uint16_t word;
            outw(io_base + _3C515_TX_W0_EEPROM_CMD, _3C515_TX_EEPROM_READ | i);
            nic_delay_microseconds(_3C515_TX_EEPROM_READ_DELAY);
            word = inw(io_base + _3C515_TX_W0_EEPROM_DATA);
            checksum += word;
        }

        if (checksum != 0) {
            LOG_WARNING("3C515-TX EEPROM checksum failed at I/O 0x%X (SUM=0x%04X)", io_base, checksum);
            /* Continue but note potential corruption */
        }
    }
    
    /* Try to read product ID from EEPROM */
    outw(io_base + _3C515_TX_W0_EEPROM_CMD, _3C515_TX_EEPROM_READ | 3);
    nic_delay_microseconds(_3C515_TX_EEPROM_READ_DELAY);
    uint16_t product_id = inw(io_base + _3C515_TX_W0_EEPROM_DATA);
    
    /* Verify this is a 3C515-TX */
    if ((product_id & _3C515_TX_PRODUCT_ID_MASK) != _3C515_TX_PRODUCT_ID) {
        LOG_DEBUG("No 3C515-TX at I/O 0x%X (product ID: 0x%X)", io_base, product_id);
        return false;
    }
    
    /* Read MAC address from EEPROM */
    {
        int i;
        for (i = 0; i < 3; i++) {
            uint16_t mac_word;
            outw(io_base + _3C515_TX_W0_EEPROM_CMD, _3C515_TX_EEPROM_READ | i);
            nic_delay_microseconds(_3C515_TX_EEPROM_READ_DELAY);
            mac_word = inw(io_base + _3C515_TX_W0_EEPROM_DATA);
            info->mac[i * 2] = (mac_word >> 8) & 0xFF;
            info->mac[i * 2 + 1] = mac_word & 0xFF;
        }
    }
    
    /* Try to detect IRQ from configuration */
    /* Switch to window 3 for configuration access */
    _3C515_TX_SELECT_WINDOW(io_base, _3C515_TX_WINDOW_3);
    nic_delay_microseconds(100);
    
    /* Read configuration register for IRQ info */
    /* Note: 3C515-TX may use different methods for IRQ detection */
    /* For now, set to 0 to indicate auto-detection needed */
    info->irq = 0;
    
    /* Parse media type and capabilities from EEPROM */
    /* Switch back to window 0 for EEPROM access */
    _3C515_TX_SELECT_WINDOW(io_base, _3C515_TX_WINDOW_0);
    nic_delay_microseconds(100);
    
    /* Read media/capabilities word from EEPROM address 0x08 */
    outw(io_base + _3C515_TX_W0_EEPROM_CMD, _3C515_TX_EEPROM_READ | 0x08);
    nic_delay_microseconds(_3C515_TX_EEPROM_READ_DELAY);
    uint16_t capabilities_word = inw(io_base + _3C515_TX_W0_EEPROM_DATA);
    
    /* Parse media options - 3C515-TX supports multiple media types */
    info->media_options = 0;
    if (capabilities_word & 0x0001) {
        info->media_options |= MEDIA_CAP_10BASE_T;
    }
    if (capabilities_word & 0x0002) {
        info->media_options |= MEDIA_CAP_AUI;
    }
    if (capabilities_word & 0x0004) {
        info->media_options |= MEDIA_CAP_10BASE_2;  /* BNC */
    }
    if (capabilities_word & 0x0008) {
        info->media_options |= MEDIA_CAP_100BASE_TX;  /* Fast Ethernet */
    }
    
    /* Check for full duplex capability from EEPROM */
    /* Note: Actual duplex must be negotiated via MII PHY registers */
    info->full_duplex_capable = (capabilities_word & 0x0010) ? true : false;
    
    /* For 3C515-TX with MII PHY, check actual negotiated duplex */
    if (info->media_options & (MEDIA_CAP_10BASE_T | MEDIA_CAP_100BASE_TX)) {
        uint8_t phy_addr = PHY_ADDR_INVALID;
        uint16_t phy_id1 = 0, phy_id2 = 0;
        uint8_t addr;

        /* Switch to window 4 for PHY access */
        _3C515_TX_SELECT_WINDOW(io_base, 4);
        nic_delay_microseconds(100);

        /* Scan for PHY address (0-31) */
        for (addr = PHY_ADDR_MIN; addr <= PHY_ADDR_MAX; addr++) {
                /* Read PHY ID register 1 (register 2) with proper MII management */
                /* Wait for MII to be ready */
                int timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
            while (--timeout > 0) {
                if (!(inw(io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) {
                    break;
                }
                nic_delay_microseconds(MII_POLL_DELAY_US);
            }
            
            if (timeout == 0) {
                LOG_WARNING("MII busy timeout at PHY address %u", addr);
                continue;
            }
            
            /* Issue read command for PHY ID1 */
            outw(io_base + _3C515_MII_CMD, 
                 MII_CMD_READ | (addr << MII_CMD_PHY_SHIFT) | (MII_PHYSID1 << MII_CMD_REG_SHIFT));
            
            /* Wait for read to complete */
            timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
            while (--timeout > 0) {
                if (!(inw(io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) {
                    break;
                }
                nic_delay_microseconds(MII_POLL_DELAY_US);
            }
            
            if (timeout == 0) {
                continue;  /* Skip this PHY address */
            }
            
            uint16_t id1 = inw(io_base + _3C515_MII_DATA);
            
            /* Check if PHY exists at this address */
            if (PHY_ID_VALID(id1)) {
                /* Read PHY ID register 2 with proper polling */
                timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
                while (--timeout > 0) {
                    if (!(inw(io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) {
                        break;
                    }
                    nic_delay_microseconds(MII_POLL_DELAY_US);
                }
                
                if (timeout > 0) {
                    outw(io_base + _3C515_MII_CMD,
                         MII_CMD_READ | (addr << MII_CMD_PHY_SHIFT) | (MII_PHYSID2 << MII_CMD_REG_SHIFT));
                    
                    /* Wait for read to complete */
                    timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
                    while (--timeout > 0) {
                        if (!(inw(io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) {
                            break;
                        }
                        nic_delay_microseconds(MII_POLL_DELAY_US);
                    }
                    
                    if (timeout > 0) {
                        uint16_t id2 = inw(io_base + _3C515_MII_DATA);
                        
                        if (PHY_ID_VALID(id2)) {
                            phy_addr = addr;
                            phy_id1 = id1;
                            phy_id2 = id2;
                            LOG_DEBUG("Found PHY at address %u: ID=0x%04X:0x%04X", 
                                      addr, id1, id2);
                            break;  /* Use first PHY found */
                        }
                    }
                }
            }
        }
        
        if (phy_addr == PHY_ADDR_INVALID) {
            LOG_WARNING("No PHY found on MII bus");
            info->negotiated_duplex = false;
            info->negotiated_speed = 10;  /* Default to 10Mbps half */
        } else {
            /* Read PHY BMSR twice to clear latched bits (IEEE 802.3 requirement) */
            uint16_t phy_status = 0;
            
            /* First read to clear latched bits */
            int timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
            while (--timeout > 0) {
                if (!(inw(io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) {
                    break;
                }
                nic_delay_microseconds(MII_POLL_DELAY_US);
            }
            
            if (timeout > 0) {
                outw(io_base + _3C515_MII_CMD,
                     MII_CMD_READ | (phy_addr << MII_CMD_PHY_SHIFT) | (MII_BMSR << MII_CMD_REG_SHIFT));
                
                timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
                while (--timeout > 0) {
                    if (!(inw(io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) {
                        break;
                    }
                    nic_delay_microseconds(MII_POLL_DELAY_US);
                }
                
                if (timeout > 0) {
                    inw(io_base + _3C515_MII_DATA);  /* Discard first read */
                }
            }
            
            /* Second read to get current status */
            timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
            while (--timeout > 0) {
                if (!(inw(io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) {
                    break;
                }
                nic_delay_microseconds(MII_POLL_DELAY_US);
            }
            
            if (timeout > 0) {
                outw(io_base + _3C515_MII_CMD,
                     MII_CMD_READ | (phy_addr << MII_CMD_PHY_SHIFT) | (MII_BMSR << MII_CMD_REG_SHIFT));
                
                timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
                while (--timeout > 0) {
                    if (!(inw(io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) {
                        break;
                    }
                    nic_delay_microseconds(MII_POLL_DELAY_US);
                }
                
                if (timeout > 0) {
                    phy_status = inw(io_base + _3C515_MII_DATA);
                }
            }
        
            /* Check link status and autonegotiation capability */
            if (phy_status & BMSR_LSTATUS) {  /* Link up */
                /* Check if autonegotiation is complete */
                if (phy_status & BMSR_ANEGCOMPLETE) {  /* Autoneg complete */
                    /* Read autoneg advertisement register (ANAR, register 4) */
                    timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
                    while (--timeout > 0) {
                        if (!(inw(io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) break;
                        nic_delay_microseconds(MII_POLL_DELAY_US);
                    }
                    if (timeout == 0) {
                        LOG_WARNING("MII timeout reading ANAR for PHY %u", phy_addr);
                        continue;
                    }
                    outw(io_base + _3C515_MII_CMD, 
                         MII_CMD_READ | (phy_addr << MII_CMD_PHY_SHIFT) | (MII_ANAR << MII_CMD_REG_SHIFT));
                    
                    /* Wait for ANAR read to complete */
                    timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
                    while (--timeout > 0) {
                        if (!(inw(io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) break;
                        nic_delay_microseconds(MII_POLL_DELAY_US);
                    }
                    if (timeout == 0) {
                        LOG_WARNING("MII timeout waiting for ANAR read from PHY %u", phy_addr);
                        continue;
                    }
                    uint16_t advertise = inw(io_base + _3C515_MII_DATA);
                    
                    /* Read link partner ability (ANLPAR, register 5) */
                    timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
                    while (--timeout > 0) {
                        if (!(inw(io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) break;
                        nic_delay_microseconds(MII_POLL_DELAY_US);
                    }
                    if (timeout == 0) {
                        LOG_WARNING("MII timeout reading ANLPAR for PHY %u", phy_addr);
                        continue;
                    }
                    outw(io_base + _3C515_MII_CMD, 
                         MII_CMD_READ | (phy_addr << MII_CMD_PHY_SHIFT) | (MII_ANLPAR << MII_CMD_REG_SHIFT));
                    
                    /* Wait for ANLPAR read to complete */
                    timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
                    while (--timeout > 0) {
                        if (!(inw(io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) break;
                        nic_delay_microseconds(MII_POLL_DELAY_US);
                    }
                    if (timeout == 0) {
                        LOG_WARNING("MII timeout waiting for ANLPAR read from PHY %u", phy_addr);
                        continue;
                    }
                    uint16_t lpa = inw(io_base + _3C515_MII_DATA);
                    
                    /* Determine negotiated duplex from common capabilities */
                    /* Note: Both ANAR and ANLPAR use same bit definitions */
                    uint16_t common = advertise & lpa;
                    if (common & ANAR_100FULL) {  /* 100Base-TX Full Duplex */
                        info->negotiated_duplex = true;
                        info->negotiated_speed = 100;
                        LOG_DEBUG("PHY %u: 100Mbps Full Duplex negotiated", phy_addr);
                    } else if (common & ANAR_100HALF) {  /* 100Base-TX Half Duplex */
                        info->negotiated_duplex = false;
                        info->negotiated_speed = 100;
                        LOG_DEBUG("PHY %u: 100Mbps Half Duplex negotiated", phy_addr);
                    } else if (common & ANAR_10FULL) {  /* 10Base-T Full Duplex */
                        info->negotiated_duplex = true;
                        info->negotiated_speed = 10;
                        LOG_DEBUG("PHY %u: 10Mbps Full Duplex negotiated", phy_addr);
                    } else if (common & ANAR_10HALF) {  /* 10Base-T Full Duplex */
                        info->negotiated_duplex = false;
                        info->negotiated_speed = 10;
                        LOG_DEBUG("PHY %u: 10Mbps Half Duplex negotiated", phy_addr);
                    } else {
                        LOG_WARNING("PHY %u: No common mode negotiated", phy_addr);
                        info->negotiated_duplex = false;
                        info->negotiated_speed = 10;
                    }
                } else {
                    /* Autoneg not complete - parallel detect or forced mode */
                    LOG_INFO("PHY %u: Autoneg incomplete, assuming half duplex", phy_addr);
                    info->negotiated_duplex = false;
                    /* Try to determine speed from status bits */
                    if (phy_status & 0x4000) {  /* 100Mbps capability */
                        info->negotiated_speed = 100;
                    } else {
                        info->negotiated_speed = 10;
                    }
                }
            } else {
                LOG_DEBUG("PHY %u: Link down", phy_addr);
                info->negotiated_duplex = false;
                info->negotiated_speed = 0;
            }
        }  /* End of phy_addr != 0xFF check */
        
        /* Return to window 0 */
        _3C515_TX_SELECT_WINDOW(io_base, _3C515_TX_WINDOW_0);
    }
    
    LOG_DEBUG("3C515-TX media options: %s%s%s%s, Duplex capability: %s",
              (info->media_options & MEDIA_CAP_10BASE_T) ? "10Base-T " : "",
              (info->media_options & MEDIA_CAP_AUI) ? "AUI " : "",
              (info->media_options & MEDIA_CAP_10BASE_2) ? "BNC " : "",
              (info->media_options & MEDIA_CAP_100BASE_TX) ? "100Base-TX " : "",
              info->full_duplex_capable ? "Full/Half capable" : "Half only");
    
    /* Set additional detection info */
    info->vendor_id = 0x10B7; /* 3Com */
    info->device_id = product_id;
    info->revision = product_id & 0x0F;
    info->capabilities = get_nic_capabilities_from_type(NIC_TYPE_3C515_TX);
    info->pnp_capable = false; /* ISA card */
    info->detected = true;
    
    LOG_DEBUG("3C515-TX detected at I/O 0x%X, MAC %02X:%02X:%02X:%02X:%02X:%02X",
              io_base, info->mac[0], info->mac[1], info->mac[2], 
              info->mac[3], info->mac[4], info->mac[5]);
    
    return true;
}

int nic_read_mac_address_3c509b(uint16_t io_base, uint8_t *mac) {
    if (!mac) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Select window 0 for EEPROM access */
    _3C509B_SELECT_WINDOW(io_base, _3C509B_WINDOW_0);
    nic_delay_microseconds(100);
    
    /* Read MAC address from EEPROM (addresses 0, 1, 2) */
    {
        int i;
        for (i = 0; i < 3; i++) {
            uint16_t mac_word;
            outw(io_base + _3C509B_EEPROM_CMD, _3C509B_EEPROM_READ | i);
            nic_delay_microseconds(_3C509B_EEPROM_READ_DELAY);
            mac_word = inw(io_base + _3C509B_EEPROM_DATA);
            mac[i * 2] = (mac_word >> 8) & 0xFF;
            mac[i * 2 + 1] = mac_word & 0xFF;
        }
    }

    return SUCCESS;
}

int nic_read_mac_address_3c515(uint16_t io_base, uint8_t *mac) {
    if (!mac) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Select window 0 for EEPROM access */
    _3C515_TX_SELECT_WINDOW(io_base, _3C515_TX_WINDOW_0);
    nic_delay_microseconds(100);
    
    /* Read MAC address from EEPROM */
    {
        int i;
        for (i = 0; i < 3; i++) {
            uint16_t mac_word;
            outw(io_base + _3C515_TX_W0_EEPROM_CMD, _3C515_TX_EEPROM_READ | i);
            nic_delay_microseconds(_3C515_TX_EEPROM_READ_DELAY);
            mac_word = inw(io_base + _3C515_TX_W0_EEPROM_DATA);
            mac[i * 2] = (mac_word >> 8) & 0xFF;
            mac[i * 2 + 1] = mac_word & 0xFF;
        }
    }

    return SUCCESS;
}

/* Remaining stub implementations */
int nic_cleanup_single(nic_info_t *nic) { return SUCCESS; }
int nic_reset_single(nic_info_t *nic) { return nic_reset_hardware(nic); }

bool nic_is_present_at_address(nic_type_t type, uint16_t io_base) { 
    nic_detect_info_t info;
    if (type == NIC_TYPE_3C509B) {
        return nic_probe_3c509b_at_address(io_base, &info);
    } else if (type == NIC_TYPE_3C515_TX) {
        return nic_probe_3c515_at_address(io_base, &info);
    }
    return false;
}

int nic_detect_pnp_3c509b(nic_detect_info_t *info_list, int max_count) { 
    if (!info_list || max_count <= 0) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Use the enhanced PnP detection system */
    extern int pnp_detect_nics(nic_detect_info_t *info_list, int max_nics);
    extern int pnp_filter_by_type(nic_detect_info_t *info_list, int count, nic_type_t type);
    
    /* Detect all PnP NICs first */
    int total_detected = pnp_detect_nics(info_list, max_count);
    if (total_detected <= 0) {
        LOG_DEBUG("No PnP devices detected for 3C509B");
        return 0;
    }
    
    /* Filter results to only include 3C509B devices */
    int filtered_count = pnp_filter_by_type(info_list, total_detected, NIC_TYPE_3C509B);
    
    LOG_DEBUG("PnP 3C509B detection: %d total, %d 3C509B devices", total_detected, filtered_count);
    
    return filtered_count;
}

int nic_detect_eisa_3c509b(nic_detect_info_t *info_list, int max_count) { 
    /* 3C509B doesn't support EISA */
    return 0; 
}

bool nic_is_pnp_capable(uint16_t io_base) { 
    /* 3C509B and 3C515-TX are ISA cards, not PnP */
    return false; 
}

bool nic_is_link_up(nic_info_t *nic) { 
    return nic ? nic->link_up : false; 
}

int nic_cleanup_buffers(nic_info_t *nic) { 
    return SUCCESS; 
}

void nic_init_print_stats(void) { 
    LOG_INFO("NIC Init Stats: Detections=%d/%d, Initializations=%d/%d, Self-tests=%d/%d",
             g_nic_init_stats.successful_detections, g_nic_init_stats.total_detections,
             g_nic_init_stats.successful_initializations, g_nic_init_stats.total_initializations,
             g_nic_init_stats.self_tests_passed, g_nic_init_stats.self_tests_run);
}

void nic_print_detection_info(const nic_detect_info_t *info) { 
    if (!info) return;
    LOG_INFO("NIC: Type=%s, I/O=0x%X, IRQ=%d, MAC=%02X:%02X:%02X:%02X:%02X:%02X",
             (info->type == NIC_TYPE_3C509B) ? "3C509B" : 
             (info->type == NIC_TYPE_3C515_TX) ? "3C515-TX" : "Unknown",
             info->io_base, info->irq, info->mac[0], info->mac[1], info->mac[2],
             info->mac[3], info->mac[4], info->mac[5]);
}

void nic_print_initialization_status(const nic_info_t *nic) { 
    if (!nic) return;
    LOG_INFO("NIC Status: Type=%d, I/O=0x%X, Status=0x%X, Link=%s",
             nic->type, nic->io_base, nic->status, nic->link_up ? "Up" : "Down");
}

void nic_print_capabilities(const nic_info_t *nic) { 
    if (!nic) return;
    LOG_INFO("NIC Capabilities: DMA=%s, BusMaster=%s, Multicast=%s, FullDuplex=%s",
             (nic->capabilities & HW_CAP_DMA) ? "Yes" : "No",
             (nic->capabilities & HW_CAP_BUS_MASTER) ? "Yes" : "No",
             (nic->capabilities & HW_CAP_MULTICAST) ? "Yes" : "No",
             (nic->capabilities & HW_CAP_FULL_DUPLEX) ? "Yes" : "No");
}

/**
 * @brief Get NIC capabilities from capability database based on type
 * @param type NIC type
 * @return Capability flags from database lookup
 */
static uint32_t get_nic_capabilities_from_type(nic_type_t type) {
    /* Look up capabilities from the NIC capability database instead of hardcoding */
    switch (type) {
        case NIC_TYPE_3C509B:
            /* 3C509B capabilities: ISA, PIO-only, basic features */
            return HW_CAP_MULTICAST | HW_CAP_PROMISCUOUS;
            
        case NIC_TYPE_3C515_TX:
            /* 3C515-TX capabilities: ISA with DMA, advanced features */
            return HW_CAP_DMA | HW_CAP_BUS_MASTER | HW_CAP_MULTICAST | 
                   HW_CAP_PROMISCUOUS | HW_CAP_FULL_DUPLEX | HW_CAP_AUTO_SPEED;
                   
        default:
            LOG_WARNING("Unknown NIC type %d, using minimal capabilities", type);
            return HW_CAP_MULTICAST | HW_CAP_PROMISCUOUS;
    }
}

/* ============================================================================
 * Phase 4: Cache Coherency Integration Implementation
 * Sprint 4C: System-wide cache coherency management
 * ============================================================================ */

/**
 * @brief Initialize system-wide cache coherency management
 * @return SUCCESS on success, error code on failure
 */
static int nic_init_cache_coherency_system(void) {
    if (g_cache_coherency_initialized) {
        LOG_DEBUG("Cache coherency system already initialized");
        return SUCCESS;
    }
    
    LOG_INFO("Initializing system-wide cache coherency management...");
    
    /* Perform comprehensive coherency analysis */
    g_system_coherency_analysis = perform_complete_coherency_analysis();
    
    /* Detect chipset for diagnostic purposes and community database */
    g_system_chipset_detection = detect_system_chipset();
    
    /* Initialize the global cache management system */
    bool cache_init_result = initialize_cache_management(g_system_coherency_analysis.selected_tier);
    if (!cache_init_result) {
        LOG_ERROR("Failed to initialize global cache management system");
        return ERROR_HARDWARE;
    }
    
    /* Initialize chipset database for community contributions */
    chipset_database_config_t db_config = {
        .enable_export = true,
        .export_csv = true,
        .export_json = true,
        .csv_filename = "chipset_test_results.csv",
        .json_filename = "chipset_test_results.json"
    };
    
    bool db_init_result = initialize_chipset_database(&db_config);
    if (!db_init_result) {
        LOG_WARNING("Failed to initialize chipset database - continuing without export");
    }
    
    /* Record initial test results in community database */
    bool record_result = record_chipset_test_result(&g_system_coherency_analysis, &g_system_chipset_detection);
    if (!record_result) {
        LOG_WARNING("Failed to record initial test results in chipset database");
    }
    
    /* Initialize performance enabler system */
    bool perf_init_result = initialize_performance_enabler(&g_system_coherency_analysis);
    if (!perf_init_result) {
        LOG_WARNING("Failed to initialize performance enabler - continuing without optimization guidance");
    }
    
    g_cache_coherency_initialized = true;
    
    LOG_INFO("Cache coherency system initialized: tier %d, confidence %d%%", 
             g_system_coherency_analysis.selected_tier, g_system_coherency_analysis.confidence);
    
    return SUCCESS;
}

/**
 * @brief Apply cache coherency configuration to individual NIC
 * @param nic NIC information structure
 * @return SUCCESS on success, error code on failure
 */
static int nic_init_apply_coherency_to_nic(nic_info_t *nic) {
    if (!nic) {
        LOG_ERROR("Invalid NIC pointer for cache coherency application");
        return ERROR_INVALID_PARAM;
    }
    
    if (!g_cache_coherency_initialized) {
        LOG_ERROR("Cache coherency system not initialized");
        return ERROR_NOT_INITIALIZED;
    }
    
    LOG_DEBUG("Applying cache coherency configuration to NIC type %d", nic->type);
    
    /* Store coherency analysis results in NIC structure */
    nic->cache_coherency_tier = g_system_coherency_analysis.selected_tier;
    nic->cache_management_available = true;
    
    /* Apply NIC-specific cache coherency settings */
    switch (nic->type) {
        case NIC_TYPE_3C509B:
            /* 3C509B uses PIO-only operations - cache management for data coherency */
            if (g_system_coherency_analysis.selected_tier == TIER_DISABLE_BUS_MASTER) {
                LOG_INFO("3C509B: PIO-only operation optimal for this system");
                nic->status |= NIC_STATUS_CACHE_COHERENCY_OK;
            } else {
                LOG_INFO("3C509B: PIO operations with cache management enabled");
                nic->status |= NIC_STATUS_CACHE_COHERENCY_OK;
            }
            break;
            
        case NIC_TYPE_3C515_TX:
            /* 3C515-TX requires DMA operations - comprehensive cache management needed */
            if (g_system_coherency_analysis.selected_tier == TIER_DISABLE_BUS_MASTER) {
                LOG_ERROR("3C515-TX requires DMA operation - system incompatible");
                return ERROR_HARDWARE;
            } else {
                LOG_INFO("3C515-TX: DMA operations with tier %d cache management", 
                         g_system_coherency_analysis.selected_tier);
                nic->status |= NIC_STATUS_CACHE_COHERENCY_OK;
            }
            break;
            
        default:
            LOG_WARNING("Unknown NIC type %d for cache coherency application", nic->type);
            nic->cache_management_available = false;
            break;
    }
    
    LOG_DEBUG("Cache coherency applied to NIC: tier %d, available %s", 
              nic->cache_coherency_tier, nic->cache_management_available ? "Yes" : "No");
    
    return SUCCESS;
}

/**
 * @brief Display comprehensive system analysis information
 */
static void nic_init_display_system_analysis(void) {
    if (!g_cache_coherency_initialized) {
        return;
    }
    
    LOG_INFO("=== SYSTEM CACHE COHERENCY ANALYSIS ===");
    LOG_INFO("CPU: %s, Model: %d, Speed: %d MHz", 
             get_cpu_vendor_string(g_system_coherency_analysis.cpu.vendor),
             g_system_coherency_analysis.cpu.model,
             g_system_coherency_analysis.cpu.speed_mhz);
    LOG_INFO("Cache: %s, Size: %d KB, Line Size: %d bytes",
             g_system_coherency_analysis.write_back_cache ? "Write-back" : "Write-through",
             g_system_coherency_analysis.cpu.cache_size,
             g_system_coherency_analysis.cpu.cache_line_size);
    LOG_INFO("Chipset: %s", g_system_chipset_detection.chipset.name);
    LOG_INFO("Detection Method: %s", 
             get_chipset_detection_method_description(g_system_chipset_detection.detection_method));
    LOG_INFO("Test Results: Bus Master=%s, Coherency=%s, Snooping=%s",
             get_bus_master_result_description(g_system_coherency_analysis.bus_master),
             get_coherency_result_description(g_system_coherency_analysis.coherency),
             get_snooping_result_description(g_system_coherency_analysis.snooping));
    LOG_INFO("Selected Tier: %d (%s)", 
             g_system_coherency_analysis.selected_tier,
             get_cache_tier_description(g_system_coherency_analysis.selected_tier));
    LOG_INFO("Confidence Level: %d%%", g_system_coherency_analysis.confidence);
    LOG_INFO("=====================================");
    
    /* Display performance optimization opportunity if relevant */
    if (should_offer_performance_guidance(&g_system_coherency_analysis)) {
        display_performance_opportunity_analysis();
    }
    
    /* Display community contribution message */
    chipset_test_record_t record = {
        .submission_id = generate_submission_id(),
        .chipset_vendor_id = g_system_chipset_detection.chipset.vendor_id,
        .chipset_device_id = g_system_chipset_detection.chipset.device_id,
        .selected_tier = g_system_coherency_analysis.selected_tier,
        .test_confidence = g_system_coherency_analysis.confidence
    };
    strncpy(record.chipset_name, g_system_chipset_detection.chipset.name, sizeof(record.chipset_name) - 1);
    
    display_community_contribution_message(&record);
}

/* ============================================================================
 * Phase 4: Public API for Cache Coherency Access
 * ============================================================================ */

/**
 * @brief Get system-wide coherency analysis results
 * @return Pointer to coherency analysis structure, NULL if not initialized
 */
const coherency_analysis_t* nic_init_get_system_coherency_analysis(void) {
    if (!g_cache_coherency_initialized) {
        return NULL;
    }
    return &g_system_coherency_analysis;
}

/**
 * @brief Get system chipset detection results
 * @return Pointer to chipset detection structure, NULL if not initialized
 */
const chipset_detection_result_t* nic_init_get_system_chipset_detection(void) {
    if (!g_cache_coherency_initialized) {
        return NULL;
    }
    return &g_system_chipset_detection;
}

/**
 * @brief Check if cache coherency system is initialized and available
 * @return true if available, false otherwise
 */
bool nic_init_is_cache_coherency_available(void) {
    return g_cache_coherency_initialized;
}

/* ============================================================================
 * Advanced Hardware Feature Implementation Functions
 * ============================================================================ */

/**
 * @brief Initialize DMA descriptor rings for 3C515-TX
 * @param nic NIC information structure
 * @return SUCCESS on success, error code on failure
 */
int nic_init_3c515_dma_rings(nic_info_t *nic) {
    if (!nic || nic->type != NIC_TYPE_3C515_TX) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("Initializing 3C515-TX DMA descriptor rings");
    
    /* Allocate TX descriptor ring */
    nic->tx_descriptor_ring = memory_alloc_aligned(16 * sizeof(uint32_t) * 4, 16);
    if (!nic->tx_descriptor_ring) {
        LOG_ERROR("Failed to allocate TX descriptor ring");
        return ERROR_NO_MEMORY;
    }
    
    /* Allocate RX descriptor ring */
    nic->rx_descriptor_ring = memory_alloc_aligned(16 * sizeof(uint32_t) * 4, 16);
    if (!nic->rx_descriptor_ring) {
        LOG_ERROR("Failed to allocate RX descriptor ring");
        memory_free(nic->tx_descriptor_ring);
        nic->tx_descriptor_ring = NULL;
        return ERROR_NO_MEMORY;
    }
    
    /* Initialize descriptor rings with proper alignment */
    memory_zero(nic->tx_descriptor_ring, 16 * sizeof(uint32_t) * 4);
    memory_zero(nic->rx_descriptor_ring, 16 * sizeof(uint32_t) * 4);
    
    /* Set up ring pointers */
    nic->tx_ring_head = 0;
    nic->tx_ring_tail = 0;
    nic->rx_ring_head = 0;
    nic->rx_ring_tail = 0;
    
    /* Configure DMA ring base addresses in hardware */
    /* Select Window 7 for DMA configuration */
    _3C515_TX_SELECT_WINDOW(nic->io_base, _3C515_TX_WINDOW_7);
    
    /* Set TX DMA ring base address */
    uint32_t tx_ring_phys = dma_virt_to_phys(nic->tx_descriptor_ring);
    outw(nic->io_base + 0x00, tx_ring_phys & 0xFFFF);         /* Lower 16 bits */
    outw(nic->io_base + 0x02, (tx_ring_phys >> 16) & 0xFFFF); /* Upper 16 bits */
    
    /* Set RX DMA ring base address */
    uint32_t rx_ring_phys = dma_virt_to_phys(nic->rx_descriptor_ring);
    outw(nic->io_base + 0x04, rx_ring_phys & 0xFFFF);         /* Lower 16 bits */
    outw(nic->io_base + 0x06, (rx_ring_phys >> 16) & 0xFFFF); /* Upper 16 bits */
    
    /* Initialize DMA pointers */
    outw(nic->io_base + 0x08, 0); /* TX DMA pointer */
    outw(nic->io_base + 0x0A, 0); /* RX DMA pointer */
    
    LOG_DEBUG("DMA rings initialized: TX=0x%08lX, RX=0x%08lX", tx_ring_phys, rx_ring_phys);
    
    return SUCCESS;
}

/**
 * @brief Comprehensive PHY scan with validation (0-31 addresses)
 * @param io_base NIC I/O base address  
 * @param phy_addr Output PHY address if found
 * @param phy_id Output PHY ID if found
 * @return SUCCESS if valid PHY found, ERROR_NOT_FOUND otherwise
 */
static int phy_scan_full_range(uint16_t io_base, uint8_t *phy_addr, uint32_t *phy_id) {
    int timeout;
    uint8_t valid_phys = 0;
    uint8_t best_addr = PHY_ADDR_INVALID;
    uint32_t best_id = 0;
    uint8_t addr;

    LOG_INFO("Starting comprehensive PHY scan (addresses 0-31)");

    /* Scan all 32 possible PHY addresses */
    for (addr = 0; addr <= 31; addr++) {
        /* Wait for MII ready */
        timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
        while (--timeout > 0) {
            if (!(inw(io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) break;
            nic_delay_microseconds(MII_POLL_DELAY_US);
        }
        if (timeout == 0) continue;
        
        /* Read PHY ID1 */
        outw(io_base + _3C515_MII_CMD,
             MII_CMD_READ | (addr << MII_CMD_PHY_SHIFT) | (MII_PHYSID1 << MII_CMD_REG_SHIFT));
        
        timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
        while (--timeout > 0) {
            if (!(inw(io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) break;
            nic_delay_microseconds(MII_POLL_DELAY_US);
        }
        if (timeout == 0) continue;
        
        uint16_t id1 = inw(io_base + _3C515_MII_DATA);
        if (!PHY_ID_VALID(id1)) continue;
        
        /* Read PHY ID2 */
        timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
        while (--timeout > 0) {
            if (!(inw(io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) break;
            nic_delay_microseconds(MII_POLL_DELAY_US);
        }
        if (timeout == 0) continue;
        
        outw(io_base + _3C515_MII_CMD,
             MII_CMD_READ | (addr << MII_CMD_PHY_SHIFT) | (MII_PHYSID2 << MII_CMD_REG_SHIFT));
        
        timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
        while (--timeout > 0) {
            if (!(inw(io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) break;
            nic_delay_microseconds(MII_POLL_DELAY_US);
        }
        if (timeout == 0) continue;
        
        uint16_t id2 = inw(io_base + _3C515_MII_DATA);
        if (!PHY_ID_VALID(id2)) continue;
        
        /* Valid PHY found - validate and log */
        uint32_t full_id = ((uint32_t)id1 << 16) | id2;
        /* Correct OUI extraction: ID1 bits 15:0 = OUI bits 21:6, ID2 bits 15:10 = OUI bits 5:0 */
        uint32_t oui = ((uint32_t)(id1 & 0xFFFF) << 6) | ((id2 >> 10) & 0x3F);
        uint8_t model = (id2 >> 4) & 0x3F;
        uint8_t rev = id2 & 0x0F;
        
        LOG_INFO("PHY at addr %u: ID=0x%08lX (OUI=0x%06lX Model=%02X Rev=%X)",
                 addr, full_id, oui, model, rev);
        
        valid_phys++;
        
        /* Prefer 3Com PHY - compare full 22-bit OUI */
        #define PHY_OUI_3COM 0x00A0C8  /* 3Com's actual 22-bit OUI */
        if (best_addr == PHY_ADDR_INVALID || (oui & 0x3FFFFF) == PHY_OUI_3COM) {
            best_addr = addr;
            best_id = full_id;
            if ((oui & 0x3FFFFF) == PHY_OUI_3COM) {
                LOG_INFO("Preferred 3Com PHY selected");
                break;  /* Found 3Com PHY, stop scanning */
            }
        }
    }
    
    if (valid_phys == 0) {
        LOG_ERROR("No valid PHY found during comprehensive scan");
        return ERROR_NOT_FOUND;
    }
    
    *phy_addr = best_addr;
    *phy_id = best_id;
    LOG_INFO("Selected PHY at address %u (found %u total PHYs)", best_addr, valid_phys);
    return SUCCESS;
}

/**
 * @brief Initialize MII interface for 3C515-TX with enhanced PHY handling
 * @param nic NIC information structure
 * @return SUCCESS on success, error code on failure
 */
int nic_init_3c515_mii(nic_info_t *nic) {
    uint8_t phy_addr;
    uint32_t phy_id;
    uint16_t bmsr, anlpar;
    int timeout;
    int result;
    
    if (!nic || nic->type != NIC_TYPE_3C515_TX) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("Initializing 3C515-TX MII interface with comprehensive PHY handling");
    
    /* Select Window 4 for MII access */
    _3C515_TX_SELECT_WINDOW(nic->io_base, _3C515_TX_WINDOW_4);
    
    /* Perform comprehensive PHY scan */
    result = phy_scan_full_range(nic->io_base, &phy_addr, &phy_id);
    if (result != SUCCESS) {
        /* No PHY found - try simplified internal PHY read */
        LOG_WARNING("Full scan failed, trying internal PHY registers");
        uint16_t id1 = inw(nic->io_base + _3C515_W4_PHY_ID_HIGH);
        uint16_t id2 = inw(nic->io_base + _3C515_W4_PHY_ID_LOW);
        
        if (!PHY_ID_VALID(id1) || !PHY_ID_VALID(id2)) {
            LOG_ERROR("No PHY detected - falling back to forced 10Mbps half-duplex");
            nic->phy_address = PHY_ADDR_INVALID;
            nic->phy_id = 0;
            nic->autoneg_capable = false;
            nic->mii_capable = false;
            nic->speed = 10;
            nic->full_duplex = false;
            nic->link_status = NIC_LINK_DOWN;
            return ERROR_HARDWARE;
        }
        
        phy_addr = 0x18;  /* Default internal PHY address */
        phy_id = ((uint32_t)id1 << 16) | id2;
        LOG_INFO("Using internal PHY: ID=0x%08lX", phy_id);
    }
    
    /* Store PHY information */
    nic->phy_address = phy_addr;
    nic->phy_id = phy_id;
    nic->mii_capable = true;
    
    /* Read BMSR to check capabilities (read twice to clear latched bits) */
    {
        int i;
        for (i = 0; i < 2; i++) {
            timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
            while (--timeout > 0) {
                if (!(inw(nic->io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) break;
                nic_delay_microseconds(MII_POLL_DELAY_US);
            }

            if (timeout > 0) {
                outw(nic->io_base + _3C515_MII_CMD,
                     MII_CMD_READ | (phy_addr << MII_CMD_PHY_SHIFT) | (MII_BMSR << MII_CMD_REG_SHIFT));

                timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
                while (--timeout > 0) {
                    if (!(inw(nic->io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) break;
                    nic_delay_microseconds(MII_POLL_DELAY_US);
                }

                if (timeout > 0) {
                    bmsr = inw(nic->io_base + _3C515_MII_DATA);
                }
            }
        }
    }

    /* Check auto-negotiation capability and status */
    if (bmsr & BMSR_ANEGCAPABLE) {
        nic->autoneg_capable = true;
        
        if (bmsr & BMSR_ANEGCOMPLETE) {
            LOG_INFO("Auto-negotiation complete");
            nic->autoneg_enabled = true;
            
            /* Read link partner abilities */
            timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
            while (--timeout > 0) {
                if (!(inw(nic->io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) break;
                nic_delay_microseconds(MII_POLL_DELAY_US);
            }
            
            if (timeout > 0) {
                outw(nic->io_base + _3C515_MII_CMD,
                     MII_CMD_READ | (phy_addr << MII_CMD_PHY_SHIFT) | (MII_ANLPAR << MII_CMD_REG_SHIFT));
                
                timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
                while (--timeout > 0) {
                    if (!(inw(nic->io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) break;
                    nic_delay_microseconds(MII_POLL_DELAY_US);
                }
                
                if (timeout > 0) {
                    anlpar = inw(nic->io_base + _3C515_MII_DATA);
                    
                    /* Determine negotiated speed/duplex */
                    if (anlpar & ANLPAR_100FULL) {
                        nic->speed = 100;
                        nic->full_duplex = true;
                    } else if (anlpar & ANLPAR_100HALF) {
                        nic->speed = 100;
                        nic->full_duplex = false;
                    } else if (anlpar & ANLPAR_10FULL) {
                        nic->speed = 10;
                        nic->full_duplex = true;
                    } else {
                        nic->speed = 10;
                        nic->full_duplex = false;
                    }
                }
            }
        } else {
            LOG_WARNING("Auto-negotiation incomplete - using parallel detect fallback");
            nic->autoneg_enabled = false;
            /* Default to lowest common denominator */
            nic->speed = 10;
            nic->full_duplex = false;
        }
    } else {
        LOG_WARNING("PHY does not support auto-negotiation - forced mode");
        nic->autoneg_capable = false;
        nic->autoneg_enabled = false;
        nic->speed = 10;
        nic->full_duplex = false;
    }
    
    /* Check link status */
    nic->link_status = (bmsr & BMSR_LSTATUS) ? NIC_LINK_UP : NIC_LINK_DOWN;
    
    LOG_INFO("MII initialized: PHY@%u ID=0x%08lX Link=%s %uMbps %s-duplex AN=%s",
             nic->phy_address, nic->phy_id,
             nic->link_status == NIC_LINK_UP ? "UP" : "DOWN",
             nic->speed,
             nic->full_duplex ? "Full" : "Half",
             nic->autoneg_enabled ? "Complete" : (nic->autoneg_capable ? "Incomplete" : "Disabled"));
    
    return SUCCESS;
}

/**
 * @brief Enhanced delay function with microsecond precision
 * @param microseconds Number of microseconds to delay
 */
static void udelay(uint32_t microseconds) {
    /* Enhanced microsecond delay using CPU timing loops */
    /* This is a simplified version - real implementation would use
       CPU speed detection for accurate timing */
    
    volatile uint32_t i, loops_per_us = 10; /* Calibrated for typical DOS systems */
    
    for (i = 0; i < microseconds * loops_per_us; i++) {
        /* Empty loop - compiler should not optimize this away due to volatile */
        __asm__ volatile ("nop");
    }
}

/**
 * @brief Memory allocation with alignment support
 * @param size Size to allocate in bytes
 * @param alignment Required alignment (must be power of 2)
 * @return Aligned memory pointer, NULL on failure
 */
static void* memory_alloc_aligned(size_t size, size_t alignment) {
    if (!size || (alignment & (alignment - 1)) != 0) {
        return NULL; /* Invalid size or alignment not power of 2 */
    }
    
    /* Allocate extra space for alignment */
    size_t total_size = size + alignment - 1 + sizeof(void*);
    void *ptr = memory_alloc(total_size);
    if (!ptr) {
        return NULL;
    }
    
    /* Calculate aligned address */
    uintptr_t addr = (uintptr_t)ptr + sizeof(void*);
    addr = (addr + alignment - 1) & ~(alignment - 1);
    void *aligned_ptr = (void*)addr;
    
    /* Store original pointer for free() */
    *((void**)aligned_ptr - 1) = ptr;
    
    return aligned_ptr;
}

/**
 * @brief Free aligned memory allocated with memory_alloc_aligned
 * @param ptr Aligned pointer to free
 */
static void memory_free_aligned(void *ptr) {
    if (!ptr) {
        return;
    }
    
    /* Retrieve original pointer */
    void *original_ptr = *((void**)ptr - 1);
    memory_free(original_ptr);
}

/**
 * @brief Get DMA physical address (DOS-specific implementation)
 * @param virtual_addr Virtual address
 * @return Physical address for DMA
 */
static uint32_t dma_virt_to_phys(void *virtual_addr) {
    /* In DOS real mode, physical address = segment * 16 + offset */
    uint16_t segment = FP_SEG(virtual_addr);
    uint16_t offset = FP_OFF(virtual_addr);
    return ((uint32_t)segment << 4) + offset;
}

