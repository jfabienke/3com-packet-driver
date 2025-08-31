/**
 * @file hotplug.c
 * @brief Hot-plug event handling for PCMCIA cards
 *
 * Manages card insertion/removal events and coordinates with NIC modules
 * for seamless hot-plug operation.
 */

#include "../include/pcmcia_internal.h"

/* Global context */
extern pcmcia_context_t g_pcmcia_context;

/* Previous interrupt handler for chaining */
static void (__interrupt *prev_interrupt_handler)(void) = NULL;

/* Interrupt vector for PCMCIA events (typically IRQ 10 or 11) */
#define PCMCIA_IRQ_VECTOR       10
#define PCMCIA_INTERRUPT_VECTOR (0x08 + PCMCIA_IRQ_VECTOR)

/**
 * @brief Register PCMCIA event handlers
 * @param handlers Pointer to event handler structure
 * @return 0 on success, negative error code otherwise
 */
int register_pcmcia_events(pcmcia_event_handlers_t *handlers) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    
    if (!handlers) {
        return PCMCIA_ERR_INVALID_PARAM;
    }
    
    /* Copy event handlers */
    ctx->event_handlers = *handlers;
    
    /* Install interrupt handler for card status changes */
    if (install_card_status_interrupt() < 0) {
        log_error("Failed to install card status interrupt handler");
        return PCMCIA_ERR_HARDWARE;
    }
    
    /* Enable card status change interrupts for all sockets */
    enable_card_status_interrupts();
    
    log_info("PCMCIA event handlers registered successfully");
    return 0;
}

/**
 * @brief Install interrupt handler for card status changes
 */
static int install_card_status_interrupt(void) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    
    /* Save previous interrupt handler */
    prev_interrupt_handler = _dos_getvect(PCMCIA_INTERRUPT_VECTOR);
    
    /* Install our interrupt handler */
    _dos_setvect(PCMCIA_INTERRUPT_VECTOR, pcmcia_card_status_isr);
    
    /* Store previous handler in context for cleanup */
    ctx->prev_interrupt_handler = prev_interrupt_handler;
    
    log_debug("PCMCIA interrupt handler installed at vector 0x%02X", 
             PCMCIA_INTERRUPT_VECTOR);
    
    return 0;
}

/**
 * @brief Enable card status change interrupts for all sockets
 */
void enable_card_status_interrupts(void) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    int i;
    
    for (i = 0; i < ctx->socket_count; i++) {
        if (ctx->socket_services_available) {
            enable_socket_interrupts_ss(i);
        } else {
            enable_socket_interrupts_pe(i);
        }
        
        log_debug("Enabled status change interrupts for socket %d", i);
    }
    
    /* Enable IRQ at PIC */
    enable_irq(PCMCIA_IRQ_VECTOR);
}

/**
 * @brief Enable socket interrupts using Socket Services
 */
static int enable_socket_interrupts_ss(uint8_t socket) {
    /* Register callback with Socket Services */
    return register_socket_callback(socket, socket_status_callback);
}

/**
 * @brief Enable socket interrupts using Point Enabler
 */
static int enable_socket_interrupts_pe(uint8_t socket) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    point_enabler_context_t *pe = &ctx->point_enabler;
    uint8_t int_gen_ctrl;
    
    /* Enable card detect interrupts */
    int_gen_ctrl = pcic_read_reg(pe->io_base, socket, PCIC_INT_GEN_CTRL);
    int_gen_ctrl |= PCMCIA_IRQ_VECTOR;  /* Set IRQ number */
    int_gen_ctrl |= 0x10;               /* Enable card detect interrupt */
    pcic_write_reg(pe->io_base, socket, PCIC_INT_GEN_CTRL, int_gen_ctrl);
    
    /* Clear any pending interrupts */
    pcic_read_reg(pe->io_base, socket, PCIC_CARD_CHANGE);
    
    return 0;
}

/**
 * @brief PCMCIA card status interrupt service routine
 */
void __interrupt pcmcia_card_status_isr(void) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    uint8_t socket;
    uint8_t status, changes;
    bool interrupt_handled = false;
    
    /* Check all sockets for status changes */
    for (socket = 0; socket < ctx->socket_count; socket++) {
        status = get_socket_status(socket);
        changes = status ^ ctx->socket_status[socket];
        
        if (changes == 0) {
            continue;  /* No changes in this socket */
        }
        
        interrupt_handled = true;
        
        /* Update cached status */
        ctx->socket_status[socket] = status;
        
        /* Handle card detection changes */
        if (changes & SOCKET_STATUS_CARD_DETECT) {
            if (status & SOCKET_STATUS_CARD_DETECT) {
                /* Card inserted */
                log_debug("ISR: Card insertion detected in socket %d", socket);
                schedule_card_insertion(socket);
            } else {
                /* Card removed */
                log_debug("ISR: Card removal detected in socket %d", socket);
                schedule_card_removal(socket);
            }
        }
        
        /* Handle ready/busy changes */
        if (changes & SOCKET_STATUS_READY_CHANGE) {
            log_debug("ISR: Ready status change in socket %d", socket);
            if (ctx->event_handlers.status_changed) {
                schedule_status_change(socket, status);
            }
        }
        
        /* Clear interrupt at controller level */
        if (ctx->socket_services_available) {
            acknowledge_socket_interrupt_ss(socket);
        } else {
            acknowledge_socket_interrupt_pe(socket);
        }
    }
    
    /* Acknowledge interrupt at PIC */
    if (interrupt_handled) {
        acknowledge_pcmcia_interrupt();
    } else {
        /* Chain to previous interrupt handler if we didn't handle it */
        if (prev_interrupt_handler) {
            prev_interrupt_handler();
        }
    }
}

/**
 * @brief Schedule card insertion processing (deferred from ISR)
 */
static void schedule_card_insertion(uint8_t socket) {
    /* For DOS, we process immediately but with interrupts enabled */
    /* In a more sophisticated system, this would queue for later processing */
    
    _enable();  /* Re-enable interrupts for card processing */
    handle_card_insertion(socket);
    _disable(); /* Disable for ISR return */
}

/**
 * @brief Schedule card removal processing (deferred from ISR)
 */
static void schedule_card_removal(uint8_t socket) {
    _enable();  /* Re-enable interrupts for card processing */
    handle_card_removal(socket);
    _disable(); /* Disable for ISR return */
}

/**
 * @brief Schedule status change processing (deferred from ISR)
 */
static void schedule_status_change(uint8_t socket, uint8_t status) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    
    if (ctx->event_handlers.status_changed) {
        _enable();  /* Re-enable interrupts */
        ctx->event_handlers.status_changed(socket, status);
        _disable(); /* Disable for ISR return */
    }
}

/**
 * @brief Handle card insertion event
 */
void handle_card_insertion(socket_t socket) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    cis_3com_info_t cis_info;
    resource_allocation_t resources;
    int card_type;
    
    log_info("Processing card insertion in socket %d", socket);
    ctx->stats.cards_inserted++;
    
    /* Wait for card to stabilize */
    delay_ms(500);
    
    /* Verify card is still present (debounce) */
    uint8_t status = get_socket_status(socket);
    if (!(status & SOCKET_STATUS_CARD_DETECT)) {
        log_debug("Card removed before processing insertion in socket %d", socket);
        return;
    }
    
    /* Enable socket power */
    if (enable_socket(socket) < 0) {
        log_error("Failed to enable socket %d", socket);
        return;
    }
    
    /* Parse CIS to identify card */
    card_type = parse_3com_cis(socket, &cis_info);
    if (card_type < 0) {
        if (card_type == PCMCIA_ERR_NOT_3COM) {
            log_info("Non-3Com card inserted in socket %d - ignoring", socket);
        } else {
            log_error("Failed to parse CIS in socket %d: %s", 
                     socket, pcmcia_error_string(card_type));
            ctx->stats.cis_parse_errors++;
        }
        return;
    }
    
    /* Allocate resources for the card */
    if (allocate_card_resources(socket, &cis_info, &resources) < 0) {
        log_error("Failed to allocate resources for card in socket %d", socket);
        ctx->stats.resource_allocation_failures++;
        return;
    }
    
    /* Configure card */
    if (configure_card(socket, &resources, &cis_info) < 0) {
        log_error("Failed to configure card in socket %d", socket);
        free_card_resources(socket, &resources);
        return;
    }
    
    /* Update socket information */
    ctx->sockets[socket].inserted_card = card_type;
    ctx->sockets[socket].cis_info = cis_info;
    
    /* Initialize appropriate NIC driver module */
    switch (card_type) {
        case CARD_3C589:
        case CARD_3C589B:
        case CARD_3C589C:
        case CARD_3C589D:
        case CARD_3C562:
        case CARD_3C562B:
        case CARD_3C574:
            /* PCMCIA cards handled by PTASK.MOD */
            if (initialize_ptask_pcmcia(socket, &resources) < 0) {
                log_error("Failed to initialize PTASK for socket %d", socket);
                free_card_resources(socket, &resources);
                return;
            }
            break;
            
        case CARD_3C575:
        case CARD_3C575C:
            /* CardBus cards handled by BOOMTEX.MOD */
            if (initialize_boomtex_cardbus(socket, &resources) < 0) {
                log_error("Failed to initialize BOOMTEX for socket %d", socket);
                free_card_resources(socket, &resources);
                return;
            }
            break;
            
        default:
            log_error("Unsupported card type %d in socket %d", card_type, socket);
            free_card_resources(socket, &resources);
            return;
    }
    
    /* Call user insertion handler if registered */
    if (ctx->event_handlers.card_inserted) {
        ctx->event_handlers.card_inserted(socket);
    }
    
    log_info("Card %s successfully initialized in socket %d", 
             card_type_name(card_type), socket);
}

/**
 * @brief Handle card removal event
 */
void handle_card_removal(socket_t socket) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    socket_info_t *socket_info;
    card_type_t card_type;
    
    if (socket >= ctx->socket_count) {
        return;
    }
    
    socket_info = &ctx->sockets[socket];
    card_type = socket_info->inserted_card;
    
    log_info("Processing card removal from socket %d (%s)", 
             socket, card_type_name(card_type));
    ctx->stats.cards_removed++;
    
    /* Call user removal handler first */
    if (ctx->event_handlers.card_removed) {
        ctx->event_handlers.card_removed(socket);
    }
    
    /* Graceful shutdown of NIC driver */
    switch (card_type) {
        case CARD_3C589:
        case CARD_3C589B:
        case CARD_3C589C:
        case CARD_3C589D:
        case CARD_3C562:
        case CARD_3C562B:
        case CARD_3C574:
            cleanup_ptask_pcmcia(socket);
            break;
            
        case CARD_3C575:
        case CARD_3C575C:
            cleanup_boomtex_cardbus(socket);
            break;
            
        default:
            /* Unknown card type - just clean up resources */
            break;
    }
    
    /* Free allocated resources */
    resource_allocation_t resources;
    /* Get resource info from socket context if available */
    free_card_resources(socket, &resources);
    
    /* Power down socket */
    disable_socket(socket);
    
    /* Clear socket information */
    socket_info->inserted_card = CARD_UNKNOWN;
    memset(&socket_info->cis_info, 0, sizeof(cis_3com_info_t));
    
    log_info("Card removal from socket %d completed", socket);
}

/**
 * @brief Acknowledge PCMCIA interrupt at PIC
 */
void acknowledge_pcmcia_interrupt(void) {
    /* Send EOI to interrupt controller */
    if (PCMCIA_IRQ_VECTOR >= 8) {
        /* IRQ is on secondary PIC */
        outb(0xA0, 0x20);  /* EOI to secondary PIC */
        outb(0x20, 0x20);  /* EOI to primary PIC */
    } else {
        /* IRQ is on primary PIC */
        outb(0x20, 0x20);  /* EOI to primary PIC */
    }
}

/**
 * @brief Acknowledge socket interrupt using Socket Services
 */
static void acknowledge_socket_interrupt_ss(uint8_t socket) {
    /* Socket Services handles interrupt acknowledgment automatically */
}

/**
 * @brief Acknowledge socket interrupt using Point Enabler
 */
static void acknowledge_socket_interrupt_pe(uint8_t socket) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    point_enabler_context_t *pe = &ctx->point_enabler;
    
    /* Clear card change interrupt by reading the register */
    pcic_read_reg(pe->io_base, socket, PCIC_CARD_CHANGE);
}

/**
 * @brief Enable IRQ at Programmable Interrupt Controller
 */
static void enable_irq(uint8_t irq) {
    uint16_t port;
    uint8_t mask;
    
    if (irq >= 8) {
        /* Secondary PIC (IRQs 8-15) */
        port = 0xA1;
        irq -= 8;
    } else {
        /* Primary PIC (IRQs 0-7) */
        port = 0x21;
    }
    
    /* Clear the IRQ bit to enable it */
    mask = inb(port);
    mask &= ~(1 << irq);
    outb(port, mask);
}

/**
 * @brief Disable IRQ at Programmable Interrupt Controller
 */
static void disable_irq(uint8_t irq) {
    uint16_t port;
    uint8_t mask;
    
    if (irq >= 8) {
        /* Secondary PIC (IRQs 8-15) */
        port = 0xA1;
        irq -= 8;
    } else {
        /* Primary PIC (IRQs 0-7) */
        port = 0x21;
    }
    
    /* Set the IRQ bit to disable it */
    mask = inb(port);
    mask |= (1 << irq);
    outb(port, mask);
}

/**
 * @brief Socket Services callback for status changes
 */
static void socket_status_callback(uint8_t socket, uint8_t event) {
    /* This is called by Socket Services when events occur */
    /* We handle the same events as our ISR */
    
    if (event & 0x01) {  /* Card detect change */
        uint8_t status = get_socket_status(socket);
        if (status & SOCKET_STATUS_CARD_DETECT) {
            handle_card_insertion(socket);
        } else {
            handle_card_removal(socket);
        }
    }
    
    if (event & 0x02) {  /* Ready/busy change */
        pcmcia_context_t *ctx = &g_pcmcia_context;
        if (ctx->event_handlers.status_changed) {
            uint8_t status = get_socket_status(socket);
            ctx->event_handlers.status_changed(socket, status);
        }
    }
}

/**
 * @brief Cleanup PCMCIA event handling
 */
void cleanup_pcmcia_events(void) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    int i;
    
    /* Disable card status interrupts for all sockets */
    for (i = 0; i < ctx->socket_count; i++) {
        if (ctx->socket_services_available) {
            /* Unregister Socket Services callback */
            register_socket_callback(i, NULL);
        } else {
            /* Disable Point Enabler interrupts */
            point_enabler_context_t *pe = &ctx->point_enabler;
            pcic_write_reg(pe->io_base, i, PCIC_INT_GEN_CTRL, 0x00);
        }
    }
    
    /* Disable IRQ at PIC */
    disable_irq(PCMCIA_IRQ_VECTOR);
    
    /* Restore previous interrupt handler */
    if (ctx->prev_interrupt_handler) {
        _dos_setvect(PCMCIA_INTERRUPT_VECTOR, ctx->prev_interrupt_handler);
        ctx->prev_interrupt_handler = NULL;
    }
    
    /* Clear event handlers */
    memset(&ctx->event_handlers, 0, sizeof(pcmcia_event_handlers_t));
    
    log_info("PCMCIA event handling cleaned up");
}

/**
 * @brief Get PCMCIA statistics
 */
void get_pcmcia_statistics(pcmcia_statistics_t *stats) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    
    if (stats) {
        stats->cards_inserted = ctx->stats.cards_inserted;
        stats->cards_removed = ctx->stats.cards_removed;
        stats->cis_parse_errors = ctx->stats.cis_parse_errors;
        stats->resource_allocation_failures = ctx->stats.resource_allocation_failures;
        stats->socket_count = ctx->socket_count;
        stats->socket_services_available = ctx->socket_services_available;
    }
}

/**
 * @brief Reset PCMCIA statistics
 */
void reset_pcmcia_statistics(void) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    
    memset(&ctx->stats, 0, sizeof(ctx->stats));
    log_debug("PCMCIA statistics reset");
}

/**
 * @brief Check if hot-plug is supported
 */
bool is_hotplug_supported(void) {
    /* Hot-plug is supported in both Socket Services and Point Enabler modes */
    return true;
}

/**
 * @brief Force card detection scan
 * @return Number of cards found
 */
int force_card_scan(void) {
    pcmcia_context_t *ctx = &g_pcmcia_context;
    int i, cards_found = 0;
    
    log_info("Forcing PCMCIA card detection scan");
    
    for (i = 0; i < ctx->socket_count; i++) {
        uint8_t current_status = get_socket_status(i);
        uint8_t previous_status = ctx->socket_status[i];
        
        /* Update cached status */
        ctx->socket_status[i] = current_status;
        
        /* Check for changes */
        if ((current_status & SOCKET_STATUS_CARD_DETECT) && 
            !(previous_status & SOCKET_STATUS_CARD_DETECT)) {
            /* Card inserted */
            handle_card_insertion(i);
            cards_found++;
        } else if (!(current_status & SOCKET_STATUS_CARD_DETECT) && 
                   (previous_status & SOCKET_STATUS_CARD_DETECT)) {
            /* Card removed */
            handle_card_removal(i);
        } else if (current_status & SOCKET_STATUS_CARD_DETECT) {
            /* Card present */
            cards_found++;
        }
    }
    
    log_info("Forced scan complete: %d cards found", cards_found);
    return cards_found;
}