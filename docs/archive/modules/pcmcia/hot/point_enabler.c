/**
 * @file point_enabler.c
 * @brief Point Enabler implementation for direct PCMCIA controller access
 *
 * Provides fallback functionality when Socket Services is not available.
 * Directly accesses PCMCIA controller hardware for basic functionality.
 */

#include "../include/pcmcia_internal.h"

/* Global context */
extern pcmcia_context_t g_pcmcia_context;

/* Common PCMCIA controller I/O addresses */
#define PCIC_INDEX_REG_PRIMARY    0x3E0
#define PCIC_DATA_REG_PRIMARY     0x3E1
#define PCIC_INDEX_REG_SECONDARY  0x3E2  
#define PCIC_DATA_REG_SECONDARY   0x3E3

/* Alternative controller addresses */
static uint16_t controller_io_bases[] = {
    0x3E0, 0x3E2, 0x3E4, 0x4E0, 0x4E2, 0x4E4, 0
};

/**
 * @brief Initialize Point Enabler mode
 * @return Number of sockets detected, or negative error code
 */
int init_point_enabler_mode(void) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    point_enabler_context_t *pe = &ctx->point_enabler;
    int detected_sockets = 0;
    int i;
    
    log_info("Initializing Point Enabler mode - direct controller access");
    
    /* Clear Point Enabler context */
    memset(pe, 0, sizeof(point_enabler_context_t));
    
    /* Try to detect PCMCIA controller at common locations */
    for (i = 0; controller_io_bases[i] != 0; i++) {
        uint16_t io_base = controller_io_bases[i];
        
        log_debug("Probing for controller at 0x%04X", io_base);
        
        if (detect_intel_82365(io_base)) {
            pe->io_base = io_base;
            pe->controller_type = CONTROLLER_82365;
            log_info("Intel 82365-compatible controller detected at 0x%04X", io_base);
            break;
        } else if (detect_cirrus_logic(io_base)) {
            pe->io_base = io_base;
            pe->controller_type = CONTROLLER_CIRRUS;
            log_info("Cirrus Logic controller detected at 0x%04X", io_base);
            break;
        } else if (detect_vadem(io_base)) {
            pe->io_base = io_base;
            pe->controller_type = CONTROLLER_VADEM;
            log_info("Vadem controller detected at 0x%04X", io_base);
            break;
        }
    }
    
    if (pe->controller_type == CONTROLLER_UNKNOWN) {
        log_error("No supported PCMCIA controller found");
        return PCMCIA_ERR_NO_CONTROLLER;
    }
    
    /* Detect sockets on controller */
    detected_sockets = detect_controller_sockets(pe);
    if (detected_sockets <= 0) {
        log_error("No PCMCIA sockets detected on controller");
        return PCMCIA_ERR_NO_SOCKETS;
    }
    
    ctx->socket_count = detected_sockets;
    
    log_info("Point Enabler initialized: %s controller, %d sockets",
             controller_type_name(pe->controller_type), detected_sockets);
    
    /* Initialize socket information */
    if (init_point_enabler_sockets(pe) < 0) {
        return PCMCIA_ERR_HARDWARE;
    }
    
    /* Scan for cards */
    return scan_point_enabler_sockets(pe);
}

/**
 * @brief Detect Intel 82365-compatible controller
 */
int detect_intel_82365(uint16_t io_base) {
    uint8_t id_rev, test_pattern1, test_pattern2;
    
    /* Test controller responsiveness */
    pcic_write_reg(io_base, 0, 0x0E, 0xAA);  /* Write to scratchpad */
    test_pattern1 = pcic_read_reg(io_base, 0, 0x0E);
    
    pcic_write_reg(io_base, 0, 0x0E, 0x55);  /* Write different pattern */
    test_pattern2 = pcic_read_reg(io_base, 0, 0x0E);
    
    if (test_pattern1 != 0xAA || test_pattern2 != 0x55) {
        return 0;  /* Not responding correctly */
    }
    
    /* Read ID/revision register */
    id_rev = pcic_read_reg(io_base, 0, PCIC_ID_REVISION);
    
    /* Intel 82365 family detection */
    switch (id_rev & 0xF0) {
        case 0x80:
            log_debug("Intel 82365SL detected (ID: 0x%02X)", id_rev);
            return 1;
        case 0x90:
            log_debug("Intel 82365SL-A detected (ID: 0x%02X)", id_rev);
            return 1;
        case 0xA0:
            log_debug("Intel 82365SL-B detected (ID: 0x%02X)", id_rev);
            return 1;
        default:
            /* Check for compatible controllers */
            if ((id_rev & 0x80) == 0x80) {
                log_debug("82365-compatible controller detected (ID: 0x%02X)", id_rev);
                return 1;
            }
            return 0;
    }
}

/**
 * @brief Detect Cirrus Logic controller
 */
int detect_cirrus_logic(uint16_t io_base) {
    uint8_t chip_info, test_pattern;
    
    /* Test basic controller functionality */
    pcic_write_reg(io_base, 0, 0x0E, 0x33);
    test_pattern = pcic_read_reg(io_base, 0, 0x0E);
    
    if (test_pattern != 0x33) {
        return 0;
    }
    
    /* Try to read Cirrus Logic specific register */
    chip_info = pcic_read_reg(io_base, 0, 0x40);  /* Chip info register */
    
    /* Cirrus Logic chips have specific patterns in this register */
    if ((chip_info & 0x80) || ((chip_info & 0x0F) >= 0x02 && (chip_info & 0x0F) <= 0x08)) {
        log_debug("Cirrus Logic controller detected (info: 0x%02X)", chip_info);
        return 1;
    }
    
    return 0;
}

/**
 * @brief Detect Vadem controller
 */
int detect_vadem(uint16_t io_base) {
    uint8_t id_reg, test_pattern;
    
    /* Test controller responsiveness */
    pcic_write_reg(io_base, 0, 0x0E, 0x77);
    test_pattern = pcic_read_reg(io_base, 0, 0x0E);
    
    if (test_pattern != 0x77) {
        return 0;
    }
    
    /* Check for Vadem signature */
    id_reg = pcic_read_reg(io_base, 0, PCIC_ID_REVISION);
    
    /* Vadem controllers typically have ID in 0x60-0x6F range */
    if ((id_reg & 0xF0) == 0x60) {
        log_debug("Vadem controller detected (ID: 0x%02X)", id_reg);
        return 1;
    }
    
    return 0;
}

/**
 * @brief Detect sockets on controller
 */
int detect_controller_sockets(point_enabler_context_t *ctx) {
    int socket, detected = 0;
    uint8_t status;
    
    /* Try up to 4 sockets (maximum for most controllers) */
    for (socket = 0; socket < MAX_PCMCIA_SOCKETS; socket++) {
        /* Try to access socket registers */
        pcic_write_reg(ctx->io_base, socket, 0x0E, 0x00);
        status = pcic_read_reg(ctx->io_base, socket, PCIC_STATUS);
        
        /* If we can read a reasonable status, socket exists */
        /* Status register should have reserved bits as 0 */
        if ((status & 0x0F) == 0) {
            ctx->sockets[socket].socket_id = socket;
            ctx->sockets[socket].controller_type = ctx->controller_type;
            ctx->sockets[socket].controller_base = ctx->io_base;
            ctx->sockets[socket].status = status;
            detected++;
            
            log_debug("Socket %d detected (status: 0x%02X)", socket, status);
        } else {
            /* Socket doesn't exist or not accessible */
            break;
        }
    }
    
    ctx->socket_count = detected;
    return detected;
}

/**
 * @brief Initialize Point Enabler socket information
 */
static int init_point_enabler_sockets(point_enabler_context_t *pe) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    int i;
    
    /* Copy socket info to main context */
    ctx->sockets = (socket_info_t*)malloc(pe->socket_count * sizeof(socket_info_t));
    if (!ctx->sockets) {
        return PCMCIA_ERR_MEMORY;
    }
    
    for (i = 0; i < pe->socket_count; i++) {
        ctx->sockets[i] = pe->sockets[i];
        ctx->socket_status[i] = get_socket_status_pe(i);
        
        /* Initialize socket to known state */
        reset_socket_pe(i);
        
        log_debug("Point Enabler socket %d initialized", i);
    }
    
    return 0;
}

/**
 * @brief Scan Point Enabler sockets for cards
 */
static int scan_point_enabler_sockets(point_enabler_context_t *pe) {
    int i, cards_found = 0;
    
    log_info("Scanning Point Enabler sockets for cards...");
    
    for (i = 0; i < pe->socket_count; i++) {
        uint8_t status = get_socket_status_pe(i);
        
        if (is_card_present_pe(i, status)) {
            log_info("Card detected in socket %d (Point Enabler)", i);
            
            /* Enable socket power */
            enable_socket_pe(i);
            
            /* Try to identify the card */
            if (identify_card_in_socket_pe(i) >= 0) {
                cards_found++;
            }
        } else {
            log_debug("Socket %d is empty (Point Enabler)", i);
        }
    }
    
    log_info("Point Enabler scan complete: %d cards found", cards_found);
    return cards_found;
}

/**
 * @brief Get socket status using Point Enabler
 */
uint8_t get_socket_status_pe(uint8_t socket) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    point_enabler_context_t *pe = &ctx->point_enabler;
    uint8_t pcic_status, status = 0;
    
    if (socket >= pe->socket_count) {
        return 0;
    }
    
    pcic_status = pcic_read_reg(pe->io_base, socket, PCIC_STATUS);
    
    /* Convert PCIC status to standard format */
    if ((pcic_status & (PCIC_STATUS_CD1 | PCIC_STATUS_CD2)) == 
        (PCIC_STATUS_CD1 | PCIC_STATUS_CD2)) {
        status |= SOCKET_STATUS_CARD_DETECT;
    }
    
    if (pcic_status & PCIC_STATUS_READY) {
        status |= SOCKET_STATUS_READY_CHANGE;
    }
    
    if (pcic_status & PCIC_STATUS_WP) {
        status |= SOCKET_STATUS_WRITE_PROTECT;
    }
    
    return status;
}

/**
 * @brief Check if card is present in socket
 */
static bool is_card_present_pe(uint8_t socket, uint8_t status) {
    return (status & SOCKET_STATUS_CARD_DETECT) != 0;
}

/**
 * @brief Enable socket using Point Enabler
 */
static int enable_socket_pe(uint8_t socket) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    point_enabler_context_t *pe = &ctx->point_enabler;
    
    if (socket >= pe->socket_count) {
        return PCMCIA_ERR_INVALID_PARAM;
    }
    
    log_debug("Enabling socket %d (Point Enabler)", socket);
    
    /* Power up socket - 5V VCC */
    pcic_write_reg(pe->io_base, socket, PCIC_POWER_CONTROL, 
                   PCIC_POWER_VCC_5V | PCIC_POWER_OUTPUT);
    
    /* Wait for power stabilization */
    delay_ms(300);
    
    /* Enable card detect interrupt */
    pcic_write_reg(pe->io_base, socket, PCIC_INT_GEN_CTRL, 0x01);
    
    return 0;
}

/**
 * @brief Reset socket using Point Enabler
 */
int reset_socket_pe(uint8_t socket) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    point_enabler_context_t *pe = &ctx->point_enabler;
    
    if (socket >= pe->socket_count) {
        return PCMCIA_ERR_INVALID_PARAM;
    }
    
    log_debug("Resetting socket %d (Point Enabler)", socket);
    
    /* Power down socket first */
    pcic_write_reg(pe->io_base, socket, PCIC_POWER_CONTROL, PCIC_POWER_OFF);
    delay_ms(100);
    
    /* Clear any pending interrupts */
    pcic_read_reg(pe->io_base, socket, PCIC_CARD_STATUS);
    pcic_read_reg(pe->io_base, socket, PCIC_CARD_CHANGE);
    
    /* Reset socket state */
    pcic_write_reg(pe->io_base, socket, PCIC_INT_GEN_CTRL, 0x00);
    
    return 0;
}

/**
 * @brief Set socket configuration using Point Enabler
 */
int set_socket_configuration_pe(uint8_t socket, uint8_t config) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    point_enabler_context_t *pe = &ctx->point_enabler;
    
    if (socket >= pe->socket_count) {
        return PCMCIA_ERR_INVALID_PARAM;
    }
    
    pcic_write_reg(pe->io_base, socket, PCIC_POWER_CONTROL, config);
    
    return 0;
}

/**
 * @brief Map I/O window using Point Enabler
 */
int map_io_window_pe(uint8_t socket, uint8_t window, uint16_t base, uint16_t size) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    point_enabler_context_t *pe = &ctx->point_enabler;
    uint8_t reg_base;
    
    if (socket >= pe->socket_count || window > 1) {
        return PCMCIA_ERR_INVALID_PARAM;
    }
    
    log_debug("Mapping I/O window %d for socket %d: 0x%04X-0x%04X (Point Enabler)", 
             window, socket, base, base + size - 1);
    
    /* Calculate register base for this window */
    reg_base = (window == 0) ? PCIC_IO_WIN0_START_LOW : PCIC_IO_WIN1_START_LOW;
    
    /* Set I/O window start address */
    pcic_write_reg(pe->io_base, socket, reg_base, (uint8_t)(base & 0xFF));
    pcic_write_reg(pe->io_base, socket, reg_base + 1, (uint8_t)(base >> 8));
    
    /* Set I/O window end address */
    uint16_t end_addr = base + size - 1;
    pcic_write_reg(pe->io_base, socket, reg_base + 2, (uint8_t)(end_addr & 0xFF));
    pcic_write_reg(pe->io_base, socket, reg_base + 3, (uint8_t)(end_addr >> 8));
    
    /* Enable I/O window */
    uint8_t io_control = pcic_read_reg(pe->io_base, socket, 0x07);
    io_control |= (window == 0) ? 0x01 : 0x02;
    pcic_write_reg(pe->io_base, socket, 0x07, io_control);
    
    return 0;
}

/**
 * @brief Identify card in socket using Point Enabler
 */
static int identify_card_in_socket_pe(uint8_t socket) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    socket_info_t *socket_info = &ctx->sockets[socket];
    cis_3com_info_t *cis_info = &socket_info->cis_info;
    int card_type;
    
    /* Wait for card to stabilize */
    delay_ms(500);
    
    /* Parse CIS to identify card */
    card_type = parse_3com_cis(socket, cis_info);
    if (card_type < 0) {
        if (card_type == PCMCIA_ERR_NOT_3COM) {
            log_debug("Non-3Com card in socket %d (Point Enabler)", socket);
        } else {
            log_error("Failed to parse CIS in socket %d (Point Enabler): %s", 
                     socket, pcmcia_error_string(card_type));
        }
        return card_type;
    }
    
    socket_info->inserted_card = card_type;
    
    log_info("Identified %s in socket %d (Point Enabler)", 
             card_type_name(card_type), socket);
    
    return card_type;
}

/**
 * @brief Read PCIC register
 */
uint8_t pcic_read_reg(uint16_t io_base, uint8_t socket, uint8_t reg) {
    uint8_t index = (socket << 6) | (reg & 0x3F);
    
    outb(io_base, index);           /* Write index register */
    return inb(io_base + 1);        /* Read data register */
}

/**
 * @brief Write PCIC register
 */
void pcic_write_reg(uint16_t io_base, uint8_t socket, uint8_t reg, uint8_t value) {
    uint8_t index = (socket << 6) | (reg & 0x3F);
    
    outb(io_base, index);           /* Write index register */
    outb(io_base + 1, value);       /* Write data register */
}

/**
 * @brief Get controller type name
 */
const char* controller_type_name(controller_type_t type) {
    switch (type) {
        case CONTROLLER_82365:
            return "Intel 82365";
        case CONTROLLER_CIRRUS:
            return "Cirrus Logic";
        case CONTROLLER_VADEM:
            return "Vadem";
        case CONTROLLER_RICOH:
            return "Ricoh";
        default:
            return "Unknown";
    }
}

/**
 * @brief Map attribute memory using Point Enabler
 */
uint8_t *map_attribute_memory_pe(uint8_t socket, uint32_t offset, uint32_t size) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    point_enabler_context_t *pe = &ctx->point_enabler;
    static uint8_t cis_buffer[512];  /* Static buffer for CIS data */
    
    if (socket >= pe->socket_count) {
        return NULL;
    }
    
    /* Configure memory window 0 for attribute memory access */
    /* This is a simplified implementation - maps to conventional memory */
    
    /* Window 0 setup for attribute memory (0x0000-0xFFFF card address) */
    pcic_write_reg(pe->io_base, socket, 0x10, 0x00);  /* Mem win 0 start low */
    pcic_write_reg(pe->io_base, socket, 0x11, 0x00);  /* Mem win 0 start high */
    pcic_write_reg(pe->io_base, socket, 0x12, 0xFF);  /* Mem win 0 end low */
    pcic_write_reg(pe->io_base, socket, 0x13, 0x0F);  /* Mem win 0 end high */
    pcic_write_reg(pe->io_base, socket, 0x14, 0x00);  /* Mem win 0 offset low */
    pcic_write_reg(pe->io_base, socket, 0x15, 0x00);  /* Mem win 0 offset high */
    pcic_write_reg(pe->io_base, socket, 0x06, 0x40);  /* Enable, attribute mem */
    
    /* For Point Enabler, we read CIS data byte by byte into our buffer */
    /* This is a simplified approach - real implementation would use
     * proper memory mapping through the controller */
    
    /* Simulate reading CIS data - in practice this would access
     * the mapped memory window */
    memset(cis_buffer, 0xFF, sizeof(cis_buffer));  /* Default CIS pattern */
    
    /* For a real implementation, you would:
     * 1. Set up the memory window properly
     * 2. Map it to a DOS memory segment
     * 3. Read the actual CIS data
     * 4. Return pointer to mapped memory
     */
    
    return cis_buffer;
}

/**
 * @brief Unmap attribute memory using Point Enabler
 */
void unmap_attribute_memory_pe(uint8_t *mapped_ptr) {
    /* For our static buffer implementation, nothing to do */
    /* Real implementation would disable the memory window */
}

/**
 * @brief Check if Point Enabler mode is active
 */
bool is_point_enabler_mode(void) {
    return !g_pcmcia_context.socket_services_available;
}

/**
 * @brief Get Point Enabler context
 */
point_enabler_context_t* get_point_enabler_context(void) {
    return &g_pcmcia_context.point_enabler;
}