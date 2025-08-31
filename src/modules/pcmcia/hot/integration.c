/**
 * @file integration.c
 * @brief Integration functions for NIC modules (PTASK.MOD and BOOMTEX.MOD)
 *
 * Provides integration points between PCMCIA.MOD and the NIC-specific modules
 * for seamless hot-plug operation and resource management.
 */

#include "../include/pcmcia_internal.h"

/* External interfaces to NIC modules */
extern int ptask_init_pcmcia(nic_context_t *ctx, uint16_t io_base, uint8_t irq, uint8_t socket);
extern void ptask_cleanup_pcmcia(uint8_t socket);
extern int boomtex_init_cardbus(nic_context_t *ctx, uint16_t io_base, uint8_t irq, uint8_t socket);
extern void boomtex_cleanup_cardbus(uint8_t socket);

/* PCMCIA context storage for NIC modules */
typedef struct {
    uint8_t socket;
    uint16_t io_base;
    uint8_t irq;
    uint8_t config_index;
    resource_allocation_t resources;
    nic_context_t nic_context;
    bool active;
} ptask_pcmcia_context_t;

typedef struct {
    uint8_t socket;
    uint16_t io_base;
    uint8_t irq;
    resource_allocation_t resources;
    nic_context_t nic_context;
    bool active;
} boomtex_cardbus_context_t;

/* Context storage arrays */
static ptask_pcmcia_context_t ptask_pcmcia_contexts[MAX_PCMCIA_SOCKETS];
static boomtex_cardbus_context_t boomtex_cardbus_contexts[MAX_PCMCIA_SOCKETS];

/**
 * @brief Initialize PTASK.MOD for PCMCIA card
 * @param socket Socket number where card is inserted
 * @param resources Allocated resources for the card
 * @return 0 on success, negative error code otherwise
 */
int initialize_ptask_pcmcia(uint8_t socket, resource_allocation_t *resources) {
    ptask_pcmcia_context_t *ctx;
    int result;
    
    if (socket >= MAX_PCMCIA_SOCKETS || !resources) {
        return PCMCIA_ERR_INVALID_PARAM;
    }
    
    ctx = &ptask_pcmcia_contexts[socket];
    
    /* Check if already active */
    if (ctx->active) {
        log_warning("PTASK already active on socket %d", socket);
        return PCMCIA_ERR_CONFIG;
    }
    
    log_info("Initializing PTASK.MOD for PCMCIA card in socket %d", socket);
    
    /* Store context information */
    ctx->socket = socket;
    ctx->io_base = resources->io_base;
    ctx->irq = resources->irq;
    ctx->config_index = resources->config_index;
    ctx->resources = *resources;
    
    /* Initialize NIC context */
    memset(&ctx->nic_context, 0, sizeof(nic_context_t));
    ctx->nic_context.type = NIC_TYPE_3C509_PCMCIA;
    ctx->nic_context.io_base = resources->io_base;
    ctx->nic_context.irq = resources->irq;
    ctx->nic_context.socket = socket;
    ctx->nic_context.flags |= NIC_FLAG_PCMCIA | NIC_FLAG_HOT_PLUGGABLE;
    
    /* Configure card registers */
    result = configure_pcmcia_card(socket, resources);
    if (result < 0) {
        log_error("Failed to configure PCMCIA card in socket %d", socket);
        return result;
    }
    
    /* Initialize PTASK with PCMCIA extensions */
    result = ptask_init_pcmcia(&ctx->nic_context, resources->io_base, 
                              resources->irq, socket);
    if (result < 0) {
        log_error("PTASK initialization failed for socket %d", socket);
        return result;
    }
    
    /* Set up PCMCIA-specific handlers */
    ctx->nic_context.cleanup = ptask_pcmcia_cleanup_handler;
    ctx->nic_context.suspend = ptask_pcmcia_suspend_handler;
    ctx->nic_context.resume = ptask_pcmcia_resume_handler;
    
    /* Register with packet driver interface */
    result = register_packet_interface(socket, &ctx->nic_context);
    if (result < 0) {
        log_error("Failed to register packet interface for socket %d", socket);
        ptask_cleanup_pcmcia(socket);
        return result;
    }
    
    ctx->active = true;
    
    log_info("PTASK.MOD successfully initialized for socket %d (I/O: 0x%04X, IRQ: %d)", 
             socket, resources->io_base, resources->irq);
    
    return 0;
}

/**
 * @brief Initialize BOOMTEX.MOD for CardBus card
 * @param socket Socket number where card is inserted
 * @param resources Allocated resources for the card
 * @return 0 on success, negative error code otherwise
 */
int initialize_boomtex_cardbus(uint8_t socket, resource_allocation_t *resources) {
    boomtex_cardbus_context_t *ctx;
    int result;
    
    if (socket >= MAX_PCMCIA_SOCKETS || !resources) {
        return PCMCIA_ERR_INVALID_PARAM;
    }
    
    ctx = &boomtex_cardbus_contexts[socket];
    
    /* Check if already active */
    if (ctx->active) {
        log_warning("BOOMTEX already active on socket %d", socket);
        return PCMCIA_ERR_CONFIG;
    }
    
    log_info("Initializing BOOMTEX.MOD for CardBus card in socket %d", socket);
    
    /* Store context information */
    ctx->socket = socket;
    ctx->io_base = resources->io_base;
    ctx->irq = resources->irq;
    ctx->resources = *resources;
    
    /* Initialize NIC context for CardBus (32-bit) */
    memset(&ctx->nic_context, 0, sizeof(nic_context_t));
    ctx->nic_context.type = NIC_TYPE_3C575_CARDBUS;
    ctx->nic_context.io_base = resources->io_base;
    ctx->nic_context.irq = resources->irq;
    ctx->nic_context.socket = socket;
    ctx->nic_context.flags |= NIC_FLAG_CARDBUS | NIC_FLAG_HOT_PLUGGABLE | NIC_FLAG_32BIT;
    
    /* Configure CardBus bridge */
    result = configure_cardbus_bridge(socket, resources);
    if (result < 0) {
        log_error("Failed to configure CardBus bridge for socket %d", socket);
        return result;
    }
    
    /* Initialize BOOMTEX with CardBus extensions */
    result = boomtex_init_cardbus(&ctx->nic_context, resources->io_base, 
                                 resources->irq, socket);
    if (result < 0) {
        log_error("BOOMTEX initialization failed for socket %d", socket);
        return result;
    }
    
    /* Set up CardBus-specific handlers */
    ctx->nic_context.cleanup = boomtex_cardbus_cleanup_handler;
    ctx->nic_context.power_management = boomtex_cardbus_power_handler;
    
    /* Register with packet driver interface */
    result = register_packet_interface(socket, &ctx->nic_context);
    if (result < 0) {
        log_error("Failed to register packet interface for socket %d", socket);
        boomtex_cleanup_cardbus(socket);
        return result;
    }
    
    ctx->active = true;
    
    log_info("BOOMTEX.MOD successfully initialized for socket %d (I/O: 0x%04X, IRQ: %d)", 
             socket, resources->io_base, resources->irq);
    
    return 0;
}

/**
 * @brief Configure PCMCIA card registers
 */
static int configure_pcmcia_card(uint8_t socket, resource_allocation_t *resources) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    int result;
    
    log_debug("Configuring PCMCIA card in socket %d", socket);
    
    /* Map I/O window */
    result = map_io_window(socket, 0, resources->io_base, 16);
    if (result < 0) {
        log_error("Failed to map I/O window for socket %d", socket);
        return result;
    }
    
    /* Configure card for I/O operation */
    if (ctx->socket_services_available) {
        /* Use Socket Services */
        result = configure_card_ss(socket, resources);
    } else {
        /* Use Point Enabler */
        result = configure_card_pe(socket, resources);
    }
    
    if (result < 0) {
        log_error("Failed to configure card registers for socket %d", socket);
        return result;
    }
    
    log_debug("PCMCIA card configuration complete for socket %d", socket);
    return 0;
}

/**
 * @brief Configure CardBus bridge
 */
static int configure_cardbus_bridge(uint8_t socket, resource_allocation_t *resources) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    
    log_debug("Configuring CardBus bridge for socket %d", socket);
    
    /* CardBus configuration is more complex and typically handled
     * by the CardBus bridge driver or Socket Services */
    
    if (ctx->socket_services_available) {
        /* Socket Services handles CardBus configuration */
        socket_services_req_t req;
        
        req.function = SS_SET_SOCKET;
        req.socket = socket;
        req.buffer = (void far*)resources;
        req.attributes = 0x8000;  /* CardBus mode */
        
        if (call_socket_services(&req) != SS_SUCCESS) {
            log_error("Failed to configure CardBus via Socket Services");
            return PCMCIA_ERR_HARDWARE;
        }
    } else {
        /* Point Enabler mode doesn't fully support CardBus */
        log_warning("CardBus support limited in Point Enabler mode");
        /* Basic I/O window mapping */
        return map_io_window_pe(socket, 0, resources->io_base, 256);
    }
    
    log_debug("CardBus bridge configuration complete for socket %d", socket);
    return 0;
}

/**
 * @brief Configure card using Socket Services
 */
static int configure_card_ss(uint8_t socket, resource_allocation_t *resources) {
    socket_services_req_t req;
    
    /* Configure socket for card operation */
    req.function = SS_SET_SOCKET;
    req.socket = socket;
    req.buffer = (void far*)resources;
    req.attributes = 0;
    
    return call_socket_services(&req);
}

/**
 * @brief Configure card using Point Enabler
 */
static int configure_card_pe(uint8_t socket, resource_allocation_t *resources) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    point_enabler_context_t *pe = &ctx->point_enabler;
    
    /* Write configuration index to card's configuration register */
    /* This requires access to the card's configuration space */
    
    /* For Point Enabler, we write directly to the configuration registers */
    /* Configuration register is typically at offset 0x3F0 in attribute memory */
    
    /* This is a simplified implementation - real implementation would
     * properly map attribute memory and write to configuration registers */
    
    log_debug("Writing configuration index %d to card", resources->config_index);
    
    return 0;  /* Assume success for now */
}

/**
 * @brief Cleanup PTASK PCMCIA context
 */
void cleanup_ptask_pcmcia(uint8_t socket) {
    ptask_pcmcia_context_t *ctx;
    
    if (socket >= MAX_PCMCIA_SOCKETS) {
        return;
    }
    
    ctx = &ptask_pcmcia_contexts[socket];
    
    if (!ctx->active) {
        return;
    }
    
    log_info("Cleaning up PTASK PCMCIA context for socket %d", socket);
    
    /* Unregister packet interface */
    unregister_packet_interface(socket);
    
    /* Call PTASK cleanup */
    ptask_cleanup_pcmcia(socket);
    
    /* Free resources */
    free_card_resources(socket, &ctx->resources);
    
    /* Clear context */
    memset(ctx, 0, sizeof(ptask_pcmcia_context_t));
    
    log_debug("PTASK PCMCIA cleanup complete for socket %d", socket);
}

/**
 * @brief Cleanup BOOMTEX CardBus context
 */
void cleanup_boomtex_cardbus(uint8_t socket) {
    boomtex_cardbus_context_t *ctx;
    
    if (socket >= MAX_PCMCIA_SOCKETS) {
        return;
    }
    
    ctx = &boomtex_cardbus_contexts[socket];
    
    if (!ctx->active) {
        return;
    }
    
    log_info("Cleaning up BOOMTEX CardBus context for socket %d", socket);
    
    /* Unregister packet interface */
    unregister_packet_interface(socket);
    
    /* Call BOOMTEX cleanup */
    boomtex_cleanup_cardbus(socket);
    
    /* Free resources */
    free_card_resources(socket, &ctx->resources);
    
    /* Clear context */
    memset(ctx, 0, sizeof(boomtex_cardbus_context_t));
    
    log_debug("BOOMTEX CardBus cleanup complete for socket %d", socket);
}

/**
 * @brief PTASK PCMCIA cleanup handler
 */
static void ptask_pcmcia_cleanup_handler(nic_context_t *nic_ctx) {
    if (nic_ctx && nic_ctx->socket < MAX_PCMCIA_SOCKETS) {
        cleanup_ptask_pcmcia(nic_ctx->socket);
    }
}

/**
 * @brief PTASK PCMCIA suspend handler
 */
static int ptask_pcmcia_suspend_handler(nic_context_t *nic_ctx) {
    if (!nic_ctx || nic_ctx->socket >= MAX_PCMCIA_SOCKETS) {
        return -1;
    }
    
    log_debug("Suspending PTASK PCMCIA on socket %d", nic_ctx->socket);
    
    /* Power down socket while preserving configuration */
    disable_socket(nic_ctx->socket);
    
    return 0;
}

/**
 * @brief PTASK PCMCIA resume handler
 */
static int ptask_pcmcia_resume_handler(nic_context_t *nic_ctx) {
    ptask_pcmcia_context_t *ctx;
    
    if (!nic_ctx || nic_ctx->socket >= MAX_PCMCIA_SOCKETS) {
        return -1;
    }
    
    ctx = &ptask_pcmcia_contexts[nic_ctx->socket];
    
    log_debug("Resuming PTASK PCMCIA on socket %d", nic_ctx->socket);
    
    /* Power up socket and restore configuration */
    enable_socket(nic_ctx->socket);
    configure_pcmcia_card(nic_ctx->socket, &ctx->resources);
    
    return 0;
}

/**
 * @brief BOOMTEX CardBus cleanup handler
 */
static void boomtex_cardbus_cleanup_handler(nic_context_t *nic_ctx) {
    if (nic_ctx && nic_ctx->socket < MAX_PCMCIA_SOCKETS) {
        cleanup_boomtex_cardbus(nic_ctx->socket);
    }
}

/**
 * @brief BOOMTEX CardBus power management handler
 */
static int boomtex_cardbus_power_handler(nic_context_t *nic_ctx, int power_state) {
    if (!nic_ctx || nic_ctx->socket >= MAX_PCMCIA_SOCKETS) {
        return -1;
    }
    
    log_debug("CardBus power state change: socket %d, state %d", 
             nic_ctx->socket, power_state);
    
    switch (power_state) {
        case 0:  /* D0 - Full power */
            return enable_socket(nic_ctx->socket);
            
        case 3:  /* D3 - Power off */
            return disable_socket(nic_ctx->socket);
            
        default:
            /* Intermediate power states not implemented */
            return 0;
    }
}

/**
 * @brief Register packet interface with core driver
 */
static int register_packet_interface(uint8_t socket, nic_context_t *nic_ctx) {
    /* This would integrate with the core packet driver interface */
    /* For now, just log the registration */
    
    log_info("Registering packet interface for socket %d (type %d)", 
             socket, nic_ctx->type);
    
    /* In real implementation, this would:
     * 1. Allocate packet driver handle
     * 2. Register with DOS packet driver API
     * 3. Set up interrupt handling
     * 4. Initialize packet queues
     */
    
    return 0;
}

/**
 * @brief Unregister packet interface
 */
static void unregister_packet_interface(uint8_t socket) {
    log_info("Unregistering packet interface for socket %d", socket);
    
    /* In real implementation, this would:
     * 1. Stop packet processing
     * 2. Flush packet queues  
     * 3. Unregister from DOS packet driver API
     * 4. Release packet driver handle
     */
}

/**
 * @brief Get integration statistics
 */
void get_integration_statistics(integration_stats_t *stats) {
    int i;
    
    if (!stats) {
        return;
    }
    
    memset(stats, 0, sizeof(integration_stats_t));
    
    /* Count active contexts */
    for (i = 0; i < MAX_PCMCIA_SOCKETS; i++) {
        if (ptask_pcmcia_contexts[i].active) {
            stats->active_ptask_contexts++;
        }
        if (boomtex_cardbus_contexts[i].active) {
            stats->active_boomtex_contexts++;
        }
    }
    
    stats->total_integrations = stats->active_ptask_contexts + 
                               stats->active_boomtex_contexts;
}

/**
 * @brief Check if socket has active NIC integration
 */
bool is_socket_integrated(uint8_t socket) {
    if (socket >= MAX_PCMCIA_SOCKETS) {
        return false;
    }
    
    return ptask_pcmcia_contexts[socket].active || 
           boomtex_cardbus_contexts[socket].active;
}

/**
 * @brief Get NIC context for socket
 */
nic_context_t* get_socket_nic_context(uint8_t socket) {
    if (socket >= MAX_PCMCIA_SOCKETS) {
        return NULL;
    }
    
    if (ptask_pcmcia_contexts[socket].active) {
        return &ptask_pcmcia_contexts[socket].nic_context;
    }
    
    if (boomtex_cardbus_contexts[socket].active) {
        return &boomtex_cardbus_contexts[socket].nic_context;
    }
    
    return NULL;
}