/**
 * @file socket_services.c
 * @brief Socket Services INT 1A interface implementation
 *
 * Provides interface to DOS Socket Services using INT 1A software interrupt.
 * Falls back to Point Enabler mode if Socket Services not available.
 */

#include "../include/pcmcia_internal.h"

/* Global context */
extern pcmcia_context_t g_pcmcia_context;

/**
 * @brief Call Socket Services function via INT 1A
 * @param req Pointer to Socket Services request structure
 * @return Socket Services return code
 */
int call_socket_services(socket_services_req_t *req) {
    int result;
    
    if (!req) {
        return SS_BAD_ADAPTER;
    }
    
    __asm {
        push    es
        push    di
        push    si
        push    dx
        push    cx
        push    bx
        
        mov     si, req
        
        /* Load parameters from request structure */
        mov     ax, [si].function      /* Function code */
        mov     bx, [si].socket        /* Socket number */
        les     di, [si].buffer        /* Buffer pointer */
        mov     cx, [si].attributes    /* Attributes */
        
        /* Call Socket Services via INT 1A */
        int     1Ah
        
        /* Store return code */
        mov     result, ax
        
        pop     bx
        pop     cx  
        pop     dx
        pop     si
        pop     di
        pop     es
    }
    
    return result;
}

/**
 * @brief Detect and initialize PCMCIA sockets
 * @return Number of sockets found, or negative error code
 */
int pcmcia_detect_sockets(void) {
    socket_services_req_t req;
    uint16_t adapter_count = 0, socket_count = 0;
    int result;
    pcmcia_context_t *ctx = &g_pcmcia_context;
    
    log_info("Detecting PCMCIA sockets...");
    
    /* Test if Socket Services is available by checking adapter count */
    req.function = SS_GET_ADAPTER_COUNT;
    req.socket = 0;
    req.buffer = (void far*)&adapter_count;
    req.attributes = 0;
    
    result = call_socket_services(&req);
    if (result != SS_SUCCESS) {
        log_info("Socket Services not available (error %d), using Point Enabler mode", result);
        ctx->socket_services_available = false;
        return init_point_enabler_mode();
    }
    
    ctx->socket_services_available = true;
    log_info("Socket Services detected, %d adapters found", adapter_count);
    
    if (adapter_count == 0) {
        log_error("No PCMCIA adapters found");
        return PCMCIA_ERR_NO_SOCKETS;
    }
    
    /* Get total socket count across all adapters */
    req.function = SS_GET_SOCKET_COUNT;
    req.socket = 0;
    req.buffer = (void far*)&socket_count;
    req.attributes = 0;
    
    result = call_socket_services(&req);
    if (result != SS_SUCCESS || socket_count == 0) {
        log_error("Failed to get socket count (error %d)", result);
        return PCMCIA_ERR_NO_SOCKETS;
    }
    
    if (socket_count > MAX_PCMCIA_SOCKETS) {
        log_warning("System has %d sockets, limiting to %d", 
                   socket_count, MAX_PCMCIA_SOCKETS);
        socket_count = MAX_PCMCIA_SOCKETS;
    }
    
    ctx->socket_count = socket_count;
    log_info("Found %d PCMCIA sockets", socket_count);
    
    /* Initialize socket information */
    result = initialize_socket_info();
    if (result < 0) {
        return result;
    }
    
    /* Scan for cards in all sockets */
    result = scan_all_sockets();
    if (result < 0) {
        return result;
    }
    
    return socket_count;
}

/**
 * @brief Initialize socket information structures
 */
static int initialize_socket_info(void) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    socket_services_req_t req;
    int i, result;
    
    /* Allocate socket information array */
    ctx->sockets = (socket_info_t*)malloc(ctx->socket_count * sizeof(socket_info_t));
    if (!ctx->sockets) {
        log_error("Failed to allocate socket information array");
        return PCMCIA_ERR_MEMORY;
    }
    
    /* Initialize each socket */
    for (i = 0; i < ctx->socket_count; i++) {
        socket_info_t *socket = &ctx->sockets[i];
        
        socket->socket_id = i;
        socket->controller_type = CONTROLLER_UNKNOWN;
        socket->controller_base = 0;
        socket->status = 0;
        socket->flags = 0;
        socket->inserted_card = CARD_UNKNOWN;
        
        /* Get socket information from Socket Services */
        req.function = SS_INQUIRE_SOCKET;
        req.socket = i;
        req.buffer = (void far*)socket;
        req.attributes = 0;
        
        result = call_socket_services(&req);
        if (result != SS_SUCCESS) {
            log_warning("Failed to inquire socket %d (error %d)", i, result);
            /* Continue with defaults */
        }
        
        /* Get initial socket status */
        ctx->socket_status[i] = get_socket_status(i);
        
        log_debug("Socket %d initialized, status=0x%02X", 
                 i, ctx->socket_status[i]);
    }
    
    return 0;
}

/**
 * @brief Get current socket status
 * @param socket Socket number
 * @return Socket status byte
 */
uint8_t get_socket_status(uint8_t socket) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    socket_services_req_t req;
    uint8_t status = 0;
    int result;
    
    if (socket >= ctx->socket_count) {
        return 0;
    }
    
    if (ctx->socket_services_available) {
        /* Use Socket Services to get status */
        req.function = SS_GET_SOCKET;
        req.socket = socket;
        req.buffer = (void far*)&status;
        req.attributes = 0;
        
        result = call_socket_services(&req);
        if (result != SS_SUCCESS) {
            log_debug("Failed to get socket %d status (error %d)", socket, result);
            return 0;
        }
    } else {
        /* Use Point Enabler direct access */
        status = get_socket_status_pe(socket);
    }
    
    return status;
}

/**
 * @brief Scan all sockets for inserted cards
 */
static int scan_all_sockets(void) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    int i, cards_found = 0;
    
    log_info("Scanning sockets for inserted cards...");
    
    for (i = 0; i < ctx->socket_count; i++) {
        uint8_t status = get_socket_status(i);
        
        if (status & SOCKET_STATUS_CARD_DETECT) {
            log_info("Card detected in socket %d", i);
            
            /* Try to identify the card */
            if (identify_card_in_socket(i) >= 0) {
                cards_found++;
            }
        } else {
            log_debug("Socket %d is empty", i);
        }
    }
    
    log_info("Initial scan complete: %d cards found", cards_found);
    return cards_found;
}

/**
 * @brief Identify card in specific socket
 * @param socket Socket number
 * @return Card type if successful, negative error code otherwise
 */
static int identify_card_in_socket(uint8_t socket) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    socket_info_t *socket_info;
    cis_3com_info_t *cis_info;
    int card_type;
    
    if (socket >= ctx->socket_count) {
        return PCMCIA_ERR_INVALID_PARAM;
    }
    
    socket_info = &ctx->sockets[socket];
    cis_info = &socket_info->cis_info;
    
    /* Wait for card to stabilize */
    delay_ms(500);
    
    /* Parse CIS to identify card */
    card_type = parse_3com_cis(socket, cis_info);
    if (card_type < 0) {
        if (card_type == PCMCIA_ERR_NOT_3COM) {
            log_debug("Non-3Com card in socket %d", socket);
        } else {
            log_error("Failed to parse CIS in socket %d: %s", 
                     socket, pcmcia_error_string(card_type));
        }
        return card_type;
    }
    
    socket_info->inserted_card = card_type;
    
    log_info("Identified %s in socket %d", 
             card_type_name(card_type), socket);
    
    return card_type;
}

/**
 * @brief Set socket configuration
 * @param socket Socket number
 * @param config Configuration to apply
 * @return 0 on success, negative error code otherwise
 */
int set_socket_configuration(uint8_t socket, uint8_t config) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    socket_services_req_t req;
    int result;
    
    if (socket >= ctx->socket_count) {
        return PCMCIA_ERR_INVALID_PARAM;
    }
    
    if (ctx->socket_services_available) {
        req.function = SS_SET_SOCKET;
        req.socket = socket;
        req.buffer = (void far*)&config;
        req.attributes = 0;
        
        result = call_socket_services(&req);
        if (result != SS_SUCCESS) {
            log_error("Failed to set socket %d configuration (error %d)", 
                     socket, result);
            return PCMCIA_ERR_HARDWARE;
        }
    } else {
        /* Use Point Enabler direct access */
        result = set_socket_configuration_pe(socket, config);
    }
    
    return result;
}

/**
 * @brief Reset socket
 * @param socket Socket number
 * @return 0 on success, negative error code otherwise
 */
int reset_socket(uint8_t socket) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    socket_services_req_t req;
    int result;
    
    if (socket >= ctx->socket_count) {
        return PCMCIA_ERR_INVALID_PARAM;
    }
    
    log_info("Resetting socket %d", socket);
    
    if (ctx->socket_services_available) {
        req.function = SS_RESET_SOCKET;
        req.socket = socket;
        req.buffer = NULL;
        req.attributes = 0;
        
        result = call_socket_services(&req);
        if (result != SS_SUCCESS) {
            log_error("Failed to reset socket %d (error %d)", socket, result);
            return PCMCIA_ERR_HARDWARE;
        }
    } else {
        /* Use Point Enabler direct access */
        result = reset_socket_pe(socket);
    }
    
    /* Wait for reset to complete */
    delay_ms(100);
    
    return result;
}

/**
 * @brief Enable socket for card access
 * @param socket Socket number
 * @return 0 on success, negative error code otherwise
 */
int enable_socket(uint8_t socket) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    uint8_t config;
    int result;
    
    if (socket >= ctx->socket_count) {
        return PCMCIA_ERR_INVALID_PARAM;
    }
    
    log_debug("Enabling socket %d", socket);
    
    /* Power up socket and enable card */
    config = PCIC_POWER_VCC_5V | PCIC_POWER_OUTPUT;
    
    result = set_socket_configuration(socket, config);
    if (result < 0) {
        return result;
    }
    
    /* Wait for power stabilization */
    delay_ms(300);
    
    /* Verify card is ready */
    uint8_t status = get_socket_status(socket);
    if (!(status & SOCKET_STATUS_READY_CHANGE)) {
        log_warning("Socket %d card not ready after enable", socket);
    }
    
    return 0;
}

/**
 * @brief Disable socket
 * @param socket Socket number
 * @return 0 on success, negative error code otherwise
 */
int disable_socket(uint8_t socket) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    uint8_t config;
    int result;
    
    if (socket >= ctx->socket_count) {
        return PCMCIA_ERR_INVALID_PARAM;
    }
    
    log_debug("Disabling socket %d", socket);
    
    /* Power down socket */
    config = PCIC_POWER_OFF;
    
    result = set_socket_configuration(socket, config);
    if (result < 0) {
        return result;
    }
    
    return 0;
}

/**
 * @brief Register callback for socket events
 * @param socket Socket number  
 * @param callback Callback function pointer
 * @return 0 on success, negative error code otherwise
 */
int register_socket_callback(uint8_t socket, void (*callback)(uint8_t socket, uint8_t event)) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    socket_services_req_t req;
    int result;
    
    if (socket >= ctx->socket_count || !callback) {
        return PCMCIA_ERR_INVALID_PARAM;
    }
    
    if (ctx->socket_services_available) {
        req.function = SS_REGISTER_CALLBACK;
        req.socket = socket;
        req.buffer = (void far*)callback;
        req.attributes = 0;
        
        result = call_socket_services(&req);
        if (result != SS_SUCCESS) {
            log_error("Failed to register callback for socket %d (error %d)", 
                     socket, result);
            return PCMCIA_ERR_HARDWARE;
        }
    } else {
        /* Point Enabler mode - callbacks handled by our interrupt handler */
        result = 0;
    }
    
    return result;
}

/**
 * @brief Map I/O window for socket
 * @param socket Socket number
 * @param window Window number (0-1)
 * @param base I/O base address
 * @param size Window size
 * @return 0 on success, negative error code otherwise
 */
int map_io_window(uint8_t socket, uint8_t window, uint16_t base, uint16_t size) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    socket_services_req_t req;
    struct {
        uint8_t window;
        uint16_t base;
        uint16_t size;
    } window_config;
    int result;
    
    if (socket >= ctx->socket_count || window > 1) {
        return PCMCIA_ERR_INVALID_PARAM;
    }
    
    log_debug("Mapping I/O window %d for socket %d: 0x%04X-0x%04X", 
             window, socket, base, base + size - 1);
    
    window_config.window = window;
    window_config.base = base;
    window_config.size = size;
    
    if (ctx->socket_services_available) {
        req.function = SS_SET_WINDOW;
        req.socket = socket;
        req.buffer = (void far*)&window_config;
        req.attributes = 0;  /* I/O window */
        
        result = call_socket_services(&req);
        if (result != SS_SUCCESS) {
            log_error("Failed to map I/O window for socket %d (error %d)", 
                     socket, result);
            return PCMCIA_ERR_HARDWARE;
        }
    } else {
        /* Use Point Enabler direct access */
        result = map_io_window_pe(socket, window, base, size);
    }
    
    return result;
}

/**
 * @brief Get adapter information
 * @param adapter Adapter number
 * @param info Buffer to receive adapter information
 * @return 0 on success, negative error code otherwise
 */
int get_adapter_info(uint8_t adapter, void *info) {
    socket_services_req_t req;
    int result;
    
    req.function = SS_INQUIRE_ADAPTER;
    req.socket = adapter;
    req.buffer = (void far*)info;
    req.attributes = 0;
    
    result = call_socket_services(&req);
    if (result != SS_SUCCESS) {
        log_error("Failed to get adapter %d info (error %d)", adapter, result);
        return PCMCIA_ERR_HARDWARE;
    }
    
    return 0;
}

/**
 * @brief Check if Socket Services is available
 * @return true if Socket Services is available, false otherwise
 */
bool is_socket_services_available(void) {
    return g_pcmcia_context.socket_services_available;
}