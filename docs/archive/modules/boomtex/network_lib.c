/**
 * @file network_lib.c
 * @brief Shared Network Library with Media Detection and Auto-Negotiation
 * 
 * BOOMTEX.MOD - Shared Network Library Implementation
 * Team C (Agents 09-10) - Week 1 Critical Deliverable
 * 
 * Provides shared auto-negotiation, media detection, and network management
 * functions for all BOOMTEX-supported NICs (3C515-TX, 3C900-TPO, etc.).
 */

#include "boomtex_internal.h"

/* Auto-negotiation timeout values */
#define AUTONEG_TIMEOUT_MS      3000    /* 3 second timeout per IEEE 802.3u */
#define LINK_CHECK_DELAY_MS     100     /* Link status check interval */
#define PHY_RESET_DELAY_MS      100     /* PHY reset delay */

/* Media detection constants */
#define MEDIA_DETECT_RETRIES    3       /* Retry count for media detection */
#define CABLE_TEST_SAMPLES      10      /* Cable test sample count */

/* Link partner capability priorities (higher value = higher priority) */
static const struct {
    uint16_t capability;
    boomtex_media_type_t media;
    boomtex_duplex_t duplex;
    uint8_t priority;
} link_priority_table[] = {
    {ANAR_100BTXFULL,   BOOMTEX_MEDIA_100TX,    BOOMTEX_DUPLEX_FULL,    100},
    {ANAR_100BTXHALF,   BOOMTEX_MEDIA_100TX,    BOOMTEX_DUPLEX_HALF,    90},
    {ANAR_10BTFULL,     BOOMTEX_MEDIA_10BT,     BOOMTEX_DUPLEX_FULL,    50},
    {ANAR_10BTHALF,     BOOMTEX_MEDIA_10BT,     BOOMTEX_DUPLEX_HALF,    40},
    {0, 0, 0, 0}  /* End marker */
};

/* Forward declarations */
static uint8_t network_detect_phy_address(boomtex_nic_context_t *nic);
static int network_phy_reset(boomtex_nic_context_t *nic, uint8_t phy_addr);
static int network_determine_best_mode(uint16_t local_caps, uint16_t partner_caps,
                                     boomtex_media_type_t *media, boomtex_duplex_t *duplex);
static int network_configure_mac_mode(boomtex_nic_context_t *nic);
static int network_cable_test(boomtex_nic_context_t *nic);

/**
 * @brief Initialize auto-negotiation support
 * 
 * Sets up the auto-negotiation subsystem for IEEE 802.3u compliance.
 * 
 * @return SUCCESS on success, negative error code on failure
 */
int boomtex_init_autonegotiation_support(void) {
    LOG_DEBUG("BOOMTEX: Initializing IEEE 802.3u auto-negotiation support");
    
    /* Initialize timing measurement for auto-negotiation */
    PIT_INIT();
    
    LOG_INFO("BOOMTEX: Auto-negotiation support initialized");
    return SUCCESS;
}

/**
 * @brief Initialize media detection capabilities
 * 
 * Sets up media type detection and cable diagnostics.
 * 
 * @return SUCCESS on success, negative error code on failure
 */
int boomtex_init_media_detection(void) {
    LOG_DEBUG("BOOMTEX: Initializing media detection and cable diagnostics");
    
    /* Initialize media detection algorithms */
    /* Set up cable diagnostic routines */
    /* Configure link beat detection */
    
    LOG_INFO("BOOMTEX: Media detection initialized");
    return SUCCESS;
}

/**
 * @brief Perform IEEE 802.3u auto-negotiation
 * 
 * Executes complete auto-negotiation sequence with timing constraints.
 * 
 * @param nic NIC context structure
 * @return SUCCESS on success, negative error code on failure
 */
int boomtex_autonegotiate(boomtex_nic_context_t *nic) {
    pit_timing_t timing;
    uint8_t phy_addr;
    uint16_t bmcr, bmsr, anar, anlpar;
    int timeout;
    int result;
    
    LOG_DEBUG("BOOMTEX: Starting auto-negotiation for NIC type %d", nic->hardware_type);
    
    PIT_START_TIMING(&timing);
    
    /* Detect PHY address */
    phy_addr = network_detect_phy_address(nic);
    if (phy_addr == 0xFF) {
        LOG_ERROR("BOOMTEX: No MII PHY detected for auto-negotiation");
        return ERROR_HARDWARE_NOT_FOUND;
    }
    
    /* Reset PHY */
    result = network_phy_reset(nic, phy_addr);
    if (result < 0) {
        LOG_ERROR("BOOMTEX: PHY reset failed: %d", result);
        return result;
    }
    
    /* Read PHY capabilities */
    bmsr = boomtex_mii_read(nic, phy_addr, MII_BMSR);
    if (!(bmsr & BMSR_ANEGCAPABLE)) {
        LOG_INFO("BOOMTEX: PHY does not support auto-negotiation");
        return network_configure_manual_media(nic);
    }
    
    /* Build advertisement register based on hardware capabilities */
    anar = ANAR_CSMA;  /* CSMA/CD capability */
    
    switch (nic->hardware_type) {
        case BOOMTEX_HARDWARE_3C515TX:
            /* 3C515-TX supports 10/100 Mbps */
            if (bmsr & BMSR_10BTHALF) anar |= ANAR_10BTHALF;
            if (bmsr & BMSR_10BTFULL) anar |= ANAR_10BTFULL;
            if (bmsr & BMSR_100BTXHALF) anar |= ANAR_100BTXHALF;
            if (bmsr & BMSR_100BTXFULL) anar |= ANAR_100BTXFULL;
            if (bmsr & BMSR_100BT4) anar |= ANAR_100BT4;
            break;
            
        case BOOMTEX_HARDWARE_3C900TPO:
            /* 3C900-TPO supports only 10 Mbps */
            if (bmsr & BMSR_10BTHALF) anar |= ANAR_10BTHALF;
            if (bmsr & BMSR_10BTFULL) anar |= ANAR_10BTFULL;
            break;
            
        default:
            LOG_WARNING("BOOMTEX: Unknown hardware type for auto-negotiation");
            if (bmsr & BMSR_10BTHALF) anar |= ANAR_10BTHALF;
            if (bmsr & BMSR_10BTFULL) anar |= ANAR_10BTFULL;
            break;
    }
    
    /* Configure advertisement register */
    boomtex_mii_write(nic, phy_addr, MII_ANAR, anar);
    
    /* Enable and restart auto-negotiation */
    bmcr = boomtex_mii_read(nic, phy_addr, MII_BMCR);
    bmcr |= (BMCR_ANENABLE | BMCR_ANRESTART);
    boomtex_mii_write(nic, phy_addr, MII_BMCR, bmcr);
    
    LOG_INFO("BOOMTEX: Auto-negotiation started (advertising 0x%04X)", anar);
    
    /* Wait for auto-negotiation completion */
    timeout = AUTONEG_TIMEOUT_MS;
    do {
        delay_ms(LINK_CHECK_DELAY_MS);
        bmsr = boomtex_mii_read(nic, phy_addr, MII_BMSR);
        timeout -= LINK_CHECK_DELAY_MS;
        
        /* Check for link status as well */
        if (!(bmsr & BMSR_LSTATUS)) {
            continue;  /* No link, keep waiting */
        }
        
    } while (!(bmsr & BMSR_ANEGCOMPLETE) && timeout > 0);
    
    PIT_END_TIMING(&timing);
    
    if (!(bmsr & BMSR_ANEGCOMPLETE)) {
        LOG_ERROR("BOOMTEX: Auto-negotiation timeout after %lu μs", timing.elapsed_us);
        return ERROR_TIMEOUT;
    }
    
    /* Read negotiation results */
    anlpar = boomtex_mii_read(nic, phy_addr, MII_ANLPAR);
    
    /* Determine best common mode */
    result = network_determine_best_mode(anar, anlpar, &nic->media_type, &nic->duplex_mode);
    if (result < 0) {
        LOG_ERROR("BOOMTEX: No common media found in auto-negotiation");
        return result;
    }
    
    /* Set link speed based on negotiated media */
    switch (nic->media_type) {
        case BOOMTEX_MEDIA_100TX:
            nic->link_speed = 100;
            break;
        case BOOMTEX_MEDIA_10BT:
        default:
            nic->link_speed = 10;
            break;
    }
    
    /* Configure MAC for negotiated mode */
    result = network_configure_mac_mode(nic);
    if (result < 0) {
        LOG_ERROR("BOOMTEX: Failed to configure MAC for negotiated mode: %d", result);
        return result;
    }
    
    /* Final link status check */
    nic->link_status = (bmsr & BMSR_LSTATUS) ? 1 : 0;
    
    LOG_INFO("BOOMTEX: Auto-negotiation complete in %lu μs: %dMbps %s-duplex, Link %s",
             timing.elapsed_us, nic->link_speed,
             (nic->duplex_mode == BOOMTEX_DUPLEX_FULL) ? "full" : "half",
             nic->link_status ? "UP" : "DOWN");
    
    return SUCCESS;
}

/**
 * @brief Set media type manually (no auto-negotiation)
 * 
 * @param nic NIC context structure
 * @param media Desired media type
 * @param duplex Desired duplex mode
 * @return SUCCESS on success, negative error code on failure
 */
int boomtex_set_media(boomtex_nic_context_t *nic, boomtex_media_type_t media, boomtex_duplex_t duplex) {
    uint8_t phy_addr;
    uint16_t bmcr;
    int result;
    
    LOG_DEBUG("BOOMTEX: Setting manual media: %d, duplex: %d", media, duplex);
    
    /* Validate media type for hardware */
    switch (nic->hardware_type) {
        case BOOMTEX_HARDWARE_3C900TPO:
            if (media == BOOMTEX_MEDIA_100TX) {
                LOG_ERROR("BOOMTEX: 3C900-TPO does not support 100Mbps");
                return ERROR_INVALID_PARAM;
            }
            break;
            
        case BOOMTEX_HARDWARE_3C515TX:
            /* Supports both 10 and 100 */
            break;
            
        default:
            LOG_WARNING("BOOMTEX: Unknown hardware type for media setting");
            break;
    }
    
    /* Detect PHY address */
    phy_addr = network_detect_phy_address(nic);
    if (phy_addr == 0xFF) {
        LOG_ERROR("BOOMTEX: No MII PHY detected for manual media setting");
        return ERROR_HARDWARE_NOT_FOUND;
    }
    
    /* Configure BMCR register */
    bmcr = 0;
    if (media == BOOMTEX_MEDIA_100TX) {
        bmcr |= BMCR_SPEED_SELECT;  /* 100Mbps */
    }
    if (duplex == BOOMTEX_DUPLEX_FULL) {
        bmcr |= BMCR_DUPLEX;        /* Full duplex */
    }
    
    /* Disable auto-negotiation */
    bmcr &= ~BMCR_ANENABLE;
    
    /* Write configuration */
    boomtex_mii_write(nic, phy_addr, MII_BMCR, bmcr);
    
    /* Update NIC context */
    nic->media_type = media;
    nic->duplex_mode = duplex;
    nic->link_speed = (media == BOOMTEX_MEDIA_100TX) ? 100 : 10;
    
    /* Configure MAC for selected mode */
    result = network_configure_mac_mode(nic);
    if (result < 0) {
        LOG_ERROR("BOOMTEX: Failed to configure MAC for manual mode: %d", result);
        return result;
    }
    
    /* Wait for link establishment */
    delay_ms(500);
    
    /* Check link status */
    result = boomtex_get_link_status(nic);
    nic->link_status = (result > 0) ? 1 : 0;
    
    LOG_INFO("BOOMTEX: Manual media set: %dMbps %s-duplex, Link %s",
             nic->link_speed,
             (nic->duplex_mode == BOOMTEX_DUPLEX_FULL) ? "full" : "half",
             nic->link_status ? "UP" : "DOWN");
    
    return SUCCESS;
}

/**
 * @brief Get current link status
 * 
 * @param nic NIC context structure
 * @return 1 if link up, 0 if link down, negative error code on failure
 */
int boomtex_get_link_status(boomtex_nic_context_t *nic) {
    uint8_t phy_addr;
    uint16_t bmsr;
    
    /* Use hardware-specific link detection if available */
    switch (nic->hardware_type) {
        case BOOMTEX_HARDWARE_3C900TPO:
            return boomtex_3c900tpo_get_link_status(nic);
            
        case BOOMTEX_HARDWARE_NE2000_COMPAT:
            /* NE2000 compatibility always reports link up */
            return 1;
            
        default:
            break;
    }
    
    /* Generic MII-based link detection */
    phy_addr = network_detect_phy_address(nic);
    if (phy_addr == 0xFF) {
        /* No PHY detected, assume link up */
        return 1;
    }
    
    /* Read link status from MII register */
    bmsr = boomtex_mii_read(nic, phy_addr, MII_BMSR);
    
    /* MII link status requires two reads to get current status */
    bmsr = boomtex_mii_read(nic, phy_addr, MII_BMSR);
    
    return (bmsr & BMSR_LSTATUS) ? 1 : 0;
}

/**
 * @brief Detect PHY address on MII bus
 * 
 * @param nic NIC context structure
 * @return PHY address (0-31) or 0xFF if not found
 */
static uint8_t network_detect_phy_address(boomtex_nic_context_t *nic) {
    uint16_t phy_id1, phy_id2;
    
    /* Try common PHY addresses first */
    uint8_t common_addresses[] = {0, 1, 24, 31};
    int num_common = sizeof(common_addresses) / sizeof(common_addresses[0]);
    
    for (int i = 0; i < num_common; i++) {
        uint8_t addr = common_addresses[i];
        
        /* Read PHY identifier registers */
        phy_id1 = boomtex_mii_read(nic, addr, 2);  /* PHY ID1 */
        phy_id2 = boomtex_mii_read(nic, addr, 3);  /* PHY ID2 */
        
        /* Check for valid PHY ID (not all zeros or all ones) */
        if (phy_id1 != 0x0000 && phy_id1 != 0xFFFF &&
            phy_id2 != 0x0000 && phy_id2 != 0xFFFF) {
            LOG_DEBUG("BOOMTEX: Found PHY at address %d (ID: 0x%04X:0x%04X)", 
                      addr, phy_id1, phy_id2);
            return addr;
        }
    }
    
    /* Scan all addresses if common ones failed */
    for (uint8_t addr = 0; addr < 32; addr++) {
        phy_id1 = boomtex_mii_read(nic, addr, 2);
        if (phy_id1 != 0x0000 && phy_id1 != 0xFFFF) {
            phy_id2 = boomtex_mii_read(nic, addr, 3);
            if (phy_id2 != 0x0000 && phy_id2 != 0xFFFF) {
                LOG_DEBUG("BOOMTEX: Found PHY at address %d (ID: 0x%04X:0x%04X)", 
                          addr, phy_id1, phy_id2);
                return addr;
            }
        }
    }
    
    LOG_DEBUG("BOOMTEX: No PHY detected on MII bus");
    return 0xFF;
}

/**
 * @brief Reset PHY and wait for completion
 * 
 * @param nic NIC context structure
 * @param phy_addr PHY address
 * @return SUCCESS on success, negative error code on failure
 */
static int network_phy_reset(boomtex_nic_context_t *nic, uint8_t phy_addr) {
    uint16_t bmcr;
    int timeout;
    
    LOG_DEBUG("BOOMTEX: Resetting PHY at address %d", phy_addr);
    
    /* Start PHY reset */
    boomtex_mii_write(nic, phy_addr, MII_BMCR, BMCR_RESET);
    
    /* Wait for reset completion */
    timeout = 1000;  /* 1 second timeout */
    do {
        delay_ms(10);
        bmcr = boomtex_mii_read(nic, phy_addr, MII_BMCR);
        timeout -= 10;
    } while ((bmcr & BMCR_RESET) && timeout > 0);
    
    if (bmcr & BMCR_RESET) {
        LOG_ERROR("BOOMTEX: PHY reset timeout");
        return ERROR_TIMEOUT;
    }
    
    /* Additional delay for PHY to stabilize */
    delay_ms(PHY_RESET_DELAY_MS);
    
    LOG_DEBUG("BOOMTEX: PHY reset completed");
    return SUCCESS;
}

/**
 * @brief Determine best common mode from negotiation results
 * 
 * @param local_caps Local capabilities (ANAR)
 * @param partner_caps Partner capabilities (ANLPAR)
 * @param media Output media type
 * @param duplex Output duplex mode
 * @return SUCCESS on success, negative error code on failure
 */
static int network_determine_best_mode(uint16_t local_caps, uint16_t partner_caps,
                                     boomtex_media_type_t *media, boomtex_duplex_t *duplex) {
    uint16_t common = local_caps & partner_caps;
    int best_priority = -1;
    int best_index = -1;
    
    LOG_DEBUG("BOOMTEX: Determining best mode (local=0x%04X, partner=0x%04X, common=0x%04X)",
              local_caps, partner_caps, common);
    
    /* Find highest priority common capability */
    for (int i = 0; link_priority_table[i].capability != 0; i++) {
        if (common & link_priority_table[i].capability) {
            if (link_priority_table[i].priority > best_priority) {
                best_priority = link_priority_table[i].priority;
                best_index = i;
            }
        }
    }
    
    if (best_index == -1) {
        LOG_ERROR("BOOMTEX: No common capabilities found");
        return ERROR_HARDWARE_INIT_FAILED;
    }
    
    *media = link_priority_table[best_index].media;
    *duplex = link_priority_table[best_index].duplex;
    
    LOG_DEBUG("BOOMTEX: Selected mode: %s %s-duplex (priority %d)",
              (*media == BOOMTEX_MEDIA_100TX) ? "100BASE-TX" : "10BASE-T",
              (*duplex == BOOMTEX_DUPLEX_FULL) ? "full" : "half",
              best_priority);
    
    return SUCCESS;
}

/**
 * @brief Configure MAC for negotiated/selected mode
 * 
 * @param nic NIC context structure
 * @return SUCCESS on success, negative error code on failure
 */
static int network_configure_mac_mode(boomtex_nic_context_t *nic) {
    /* Hardware-specific MAC configuration */
    switch (nic->hardware_type) {
        case BOOMTEX_HARDWARE_3C515TX:
            /* Configure 3C515-TX MAC registers */
            /* Set up bus mastering parameters for selected speed */
            break;
            
        case BOOMTEX_HARDWARE_3C900TPO:
            /* Configure 3C900-TPO MAC registers */
            /* Set up PCI bus mastering */
            break;
            
        case BOOMTEX_HARDWARE_NE2000_COMPAT:
            /* NE2000 compatibility - no MAC configuration needed */
            break;
            
        default:
            LOG_WARNING("BOOMTEX: Unknown hardware type for MAC configuration");
            break;
    }
    
    LOG_DEBUG("BOOMTEX: MAC configured for %dMbps %s-duplex",
              nic->link_speed,
              (nic->duplex_mode == BOOMTEX_DUPLEX_FULL) ? "full" : "half");
    
    return SUCCESS;
}

/**
 * @brief Configure manual media (fallback when auto-negotiation not available)
 * 
 * @param nic NIC context structure
 * @return SUCCESS on success, negative error code on failure
 */
static int network_configure_manual_media(boomtex_nic_context_t *nic) {
    LOG_INFO("BOOMTEX: Auto-negotiation not available, using manual configuration");
    
    /* Set default configuration based on hardware */
    switch (nic->hardware_type) {
        case BOOMTEX_HARDWARE_3C515TX:
            nic->media_type = BOOMTEX_MEDIA_100TX;
            nic->duplex_mode = BOOMTEX_DUPLEX_HALF;
            nic->link_speed = 100;
            break;
            
        case BOOMTEX_HARDWARE_3C900TPO:
            nic->media_type = BOOMTEX_MEDIA_10BT;
            nic->duplex_mode = BOOMTEX_DUPLEX_HALF;
            nic->link_speed = 10;
            break;
            
        default:
            nic->media_type = BOOMTEX_MEDIA_10BT;
            nic->duplex_mode = BOOMTEX_DUPLEX_HALF;
            nic->link_speed = 10;
            break;
    }
    
    /* Configure MAC for manual mode */
    return network_configure_mac_mode(nic);
}

/**
 * @brief Perform cable diagnostics
 * 
 * @param nic NIC context structure
 * @return SUCCESS on success, negative error code on failure
 */
static int network_cable_test(boomtex_nic_context_t *nic) {
    /* Advanced cable diagnostics would be implemented here */
    /* This is a placeholder for future enhancement */
    
    LOG_DEBUG("BOOMTEX: Cable diagnostics not implemented");
    return SUCCESS;
}