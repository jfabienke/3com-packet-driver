/**
 * @file hardware_capabilities.c
 * @brief Hardware abstraction layer with capability-driven operations
 *
 * This file provides a bridge between the existing hardware abstraction layer
 * and the new capability-driven system, allowing gradual migration while
 * maintaining backward compatibility.
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include "../include/hardware.h"
#include "../include/nic_capabilities.h"
#include "../include/logging.h"
#include "../include/memory.h"
#include "../include/error_handling.h"
#include <string.h>

/* ========================================================================== */
/* CAPABILITY-AWARE HARDWARE STATE                                           */
/* ========================================================================== */

/* Enhanced NIC context array for capability management */
static nic_context_t g_nic_contexts[MAX_NICS];
static bool g_capability_system_initialized = false;

/* Legacy compatibility mappings */
static nic_info_t g_legacy_nic_infos[MAX_NICS];
static int g_legacy_nic_count = 0;

/* ========================================================================== */
/* CAPABILITY SYSTEM INITIALIZATION                                          */
/* ========================================================================== */

/**
 * @brief Initialize the capability-driven hardware system
 * @return 0 on success, negative on error
 */
int hardware_capabilities_init(void) {
    if (g_capability_system_initialized) {
        return SUCCESS;
    }
    
    LOG_INFO("Initializing capability-driven hardware system");
    
    /* Clear context array */
    memset(g_nic_contexts, 0, sizeof(g_nic_contexts));
    memset(g_legacy_nic_infos, 0, sizeof(g_legacy_nic_infos));
    g_legacy_nic_count = 0;
    
    g_capability_system_initialized = true;
    
    LOG_INFO("Capability-driven hardware system initialized");
    return SUCCESS;
}

/**
 * @brief Cleanup the capability-driven hardware system
 */
void hardware_capabilities_cleanup(void) {
    if (!g_capability_system_initialized) {
        return;
    }
    
    LOG_INFO("Cleaning up capability-driven hardware system");
    
    /* Cleanup all active contexts */
    for (int i = 0; i < MAX_NICS; i++) {
        if (g_nic_contexts[i].info != NULL) {
            nic_context_cleanup(&g_nic_contexts[i]);
        }
    }
    
    g_capability_system_initialized = false;
    g_legacy_nic_count = 0;
}

/* ========================================================================== */
/* NIC DETECTION AND REGISTRATION                                            */
/* ========================================================================== */

/**
 * @brief Detect and register a NIC using capability system
 * @param nic_type Type of NIC to detect
 * @param io_base I/O base address
 * @param irq IRQ number
 * @return Index of registered NIC, or negative on error
 */
int hardware_register_nic_with_capabilities(nic_type_t nic_type, uint16_t io_base, uint8_t irq) {
    if (!g_capability_system_initialized) {
        int result = hardware_capabilities_init();
        if (result != SUCCESS) {
            return result;
        }
    }
    
    if (g_legacy_nic_count >= MAX_NICS) {
        LOG_ERROR("Maximum number of NICs (%d) already registered", MAX_NICS);
        return ERROR_BUFFER_FULL;
    }
    
    /* Get NIC information from database */
    const nic_info_entry_t *info_entry = nic_get_info_entry(nic_type);
    if (!info_entry) {
        LOG_ERROR("Unknown NIC type: %d", nic_type);
        return ERROR_INVALID_PARAM;
    }
    
    /* Find free context slot */
    int nic_index = -1;
    for (int i = 0; i < MAX_NICS; i++) {
        if (g_nic_contexts[i].info == NULL) {
            nic_index = i;
            break;
        }
    }
    
    if (nic_index == -1) {
        LOG_ERROR("No free context slots available");
        return ERROR_BUFFER_FULL;
    }
    
    /* Initialize NIC context */
    nic_context_t *ctx = &g_nic_contexts[nic_index];
    int result = nic_context_init(ctx, info_entry, io_base, irq);
    if (result != NIC_CAP_SUCCESS) {
        LOG_ERROR("Failed to initialize NIC context: %d", result);
        return result;
    }
    
    /* Detect runtime capabilities */
    result = nic_detect_runtime_capabilities(ctx);
    if (result != NIC_CAP_SUCCESS) {
        LOG_WARNING("Runtime capability detection failed: %d", result);
        /* Continue anyway with static capabilities */
    }
    
    /* Create legacy compatibility structure */
    nic_info_t *legacy_nic = &g_legacy_nic_infos[g_legacy_nic_count];
    result = nic_context_to_info(ctx, legacy_nic);
    if (result != NIC_CAP_SUCCESS) {
        LOG_ERROR("Failed to create legacy NIC info: %d", result);
        nic_context_cleanup(ctx);
        return result;
    }
    
    /* Set legacy compatibility fields */
    legacy_nic->index = nic_index;
    legacy_nic->ops = NULL;  /* Will be set by compatibility layer */
    legacy_nic->status = NIC_STATUS_PRESENT | NIC_STATUS_INITIALIZED;
    
    /* Log capability information */
    char cap_string[256];
    nic_get_capability_string(nic_get_capabilities(ctx), cap_string, sizeof(cap_string));
    LOG_INFO("Registered %s at I/O 0x%04X IRQ %d with capabilities: %s",
             info_entry->name, io_base, irq, cap_string);
    
    g_legacy_nic_count++;
    return nic_index;
}

/* ========================================================================== */
/* CAPABILITY-DRIVEN PACKET OPERATIONS                                       */
/* ========================================================================== */

/**
 * @brief Send packet using capability-appropriate method
 * @param nic_index Index of NIC to use
 * @param packet Packet data
 * @param length Packet length
 * @return 0 on success, negative on error
 */
int hardware_send_packet_caps(int nic_index, const uint8_t *packet, uint16_t length) {
    if (nic_index < 0 || nic_index >= MAX_NICS) {
        return ERROR_INVALID_PARAM;
    }
    
    nic_context_t *ctx = &g_nic_contexts[nic_index];
    if (ctx->info == NULL) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Use capability-driven packet sending */
    int result = nic_send_packet_caps(ctx, packet, length);
    
    /* Update legacy statistics for compatibility */
    if (nic_index < g_legacy_nic_count) {
        nic_info_t *legacy_nic = &g_legacy_nic_infos[nic_index];
        if (result == NIC_CAP_SUCCESS) {
            legacy_nic->tx_packets++;
            legacy_nic->tx_bytes += length;
        } else {
            legacy_nic->tx_errors++;
        }
    }
    
    return (result == NIC_CAP_SUCCESS) ? SUCCESS : ERROR_HARDWARE;
}

/**
 * @brief Receive packet using capability-appropriate method
 * @param nic_index Index of NIC to use
 * @param buffer Buffer for packet data
 * @param length Buffer size on input, packet length on output
 * @return 0 on success, negative on error
 */
int hardware_receive_packet_caps(int nic_index, uint8_t *buffer, uint16_t *length) {
    if (nic_index < 0 || nic_index >= MAX_NICS || !buffer || !length) {
        return ERROR_INVALID_PARAM;
    }
    
    nic_context_t *ctx = &g_nic_contexts[nic_index];
    if (ctx->info == NULL) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Use capability-driven packet receiving */
    int result = nic_receive_packet_caps(ctx, buffer, length);
    
    /* Update legacy statistics for compatibility */
    if (nic_index < g_legacy_nic_count && result == NIC_CAP_SUCCESS) {
        nic_info_t *legacy_nic = &g_legacy_nic_infos[nic_index];
        legacy_nic->rx_packets++;
        legacy_nic->rx_bytes += *length;
    } else if (result != NIC_CAP_SUCCESS && nic_index < g_legacy_nic_count) {
        g_legacy_nic_infos[nic_index].rx_errors++;
    }
    
    return (result == NIC_CAP_SUCCESS) ? SUCCESS : ERROR_HARDWARE;
}

/* ========================================================================== */
/* CAPABILITY-AWARE CONFIGURATION                                            */
/* ========================================================================== */

/**
 * @brief Configure NIC using capability-driven approach
 * @param nic_index Index of NIC to configure
 * @param config Configuration parameters
 * @return 0 on success, negative on error
 */
int hardware_configure_nic_caps(int nic_index, const nic_config_t *config) {
    if (nic_index < 0 || nic_index >= MAX_NICS || !config) {
        return ERROR_INVALID_PARAM;
    }
    
    nic_context_t *ctx = &g_nic_contexts[nic_index];
    if (ctx->info == NULL) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Use capability-driven configuration */
    int result = nic_configure_caps(ctx, config);
    
    /* Update legacy structure for compatibility */
    if (nic_index < g_legacy_nic_count) {
        nic_info_t *legacy_nic = &g_legacy_nic_infos[nic_index];
        legacy_nic->io_base = ctx->io_base;
        legacy_nic->irq = ctx->irq;
        memcpy(legacy_nic->mac, ctx->mac, 6);
        legacy_nic->link_up = ctx->link_up;
        legacy_nic->speed = ctx->speed;
        legacy_nic->full_duplex = ctx->full_duplex;
    }
    
    return (result == NIC_CAP_SUCCESS) ? SUCCESS : ERROR_HARDWARE;
}

/**
 * @brief Set promiscuous mode using capabilities
 * @param nic_index Index of NIC
 * @param enable True to enable, false to disable
 * @return 0 on success, negative on error
 */
int hardware_set_promiscuous_caps(int nic_index, bool enable) {
    if (nic_index < 0 || nic_index >= MAX_NICS) {
        return ERROR_INVALID_PARAM;
    }
    
    nic_context_t *ctx = &g_nic_contexts[nic_index];
    if (ctx->info == NULL) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Check if promiscuous mode is supported */
    if (!nic_has_capability(ctx, NIC_CAP_MULTICAST)) {
        LOG_WARNING("NIC does not support promiscuous mode");
        return ERROR_NOT_SUPPORTED;
    }
    
    /* Use vtable function if available */
    if (ctx->info->vtable && ctx->info->vtable->set_promiscuous) {
        int result = ctx->info->vtable->set_promiscuous(ctx, enable);
        
        /* Update legacy status */
        if (result == NIC_CAP_SUCCESS && nic_index < g_legacy_nic_count) {
            if (enable) {
                g_legacy_nic_infos[nic_index].status |= NIC_STATUS_PROMISCUOUS;
            } else {
                g_legacy_nic_infos[nic_index].status &= ~NIC_STATUS_PROMISCUOUS;
            }
        }
        
        return (result == NIC_CAP_SUCCESS) ? SUCCESS : ERROR_HARDWARE;
    }
    
    return ERROR_NOT_SUPPORTED;
}

/* ========================================================================== */
/* PERFORMANCE OPTIMIZATION                                                  */
/* ========================================================================== */

/**
 * @brief Optimize NIC performance based on capabilities
 * @param nic_index Index of NIC to optimize
 * @param optimization_flags Optimization flags
 * @return 0 on success, negative on error
 */
int hardware_optimize_performance_caps(int nic_index, uint32_t optimization_flags) {
    if (nic_index < 0 || nic_index >= MAX_NICS) {
        return ERROR_INVALID_PARAM;
    }
    
    nic_context_t *ctx = &g_nic_contexts[nic_index];
    if (ctx->info == NULL) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_INFO("Optimizing performance for %s with flags 0x%08X",
             ctx->info->name, optimization_flags);
    
    /* Apply capability-specific optimizations */
    
    /* Latency optimization */
    if (optimization_flags & NIC_OPT_LATENCY) {
        if (nic_has_capability(ctx, NIC_CAP_INTERRUPT_MIT)) {
            ctx->interrupt_mitigation = 50;  /* Reduce mitigation for low latency */
        }
        if (nic_has_capability(ctx, NIC_CAP_RX_COPYBREAK)) {
            ctx->copybreak_threshold = 128;  /* Lower threshold for latency */
        }
    }
    
    /* Throughput optimization */
    if (optimization_flags & NIC_OPT_THROUGHPUT) {
        if (nic_has_capability(ctx, NIC_CAP_INTERRUPT_MIT)) {
            ctx->interrupt_mitigation = 200;  /* Higher mitigation for throughput */
        }
        if (nic_has_capability(ctx, NIC_CAP_RX_COPYBREAK)) {
            ctx->copybreak_threshold = 512;  /* Higher threshold for throughput */
        }
        if (nic_has_capability(ctx, NIC_CAP_RING_BUFFER)) {
            ctx->tx_ring_size = ctx->info->default_tx_ring_size * 2;
            ctx->rx_ring_size = ctx->info->default_rx_ring_size * 2;
        }
    }
    
    /* Power optimization */
    if (optimization_flags & NIC_OPT_POWER) {
        if (nic_has_capability(ctx, NIC_CAP_WAKEUP)) {
            /* Configure wake-on-LAN for power savings */
            LOG_DEBUG("Configuring wake-on-LAN for power optimization");
        }
    }
    
    /* Compatibility optimization */
    if (optimization_flags & NIC_OPT_COMPATIBILITY) {
        /* Use most conservative settings */
        ctx->interrupt_mitigation = 100;  /* Standard mitigation */
        ctx->copybreak_threshold = 256;   /* Standard threshold */
        ctx->tx_ring_size = ctx->info->default_tx_ring_size;
        ctx->rx_ring_size = ctx->info->default_rx_ring_size;
    }
    
    LOG_INFO("Performance optimization complete for %s", ctx->info->name);
    return SUCCESS;
}

/* ========================================================================== */
/* CAPABILITY QUERY AND STATUS                                               */
/* ========================================================================== */

/**
 * @brief Get NIC capabilities
 * @param nic_index Index of NIC
 * @return Capability flags, or 0 if invalid
 */
nic_capability_flags_t hardware_get_nic_capabilities(int nic_index) {
    if (nic_index < 0 || nic_index >= MAX_NICS) {
        return NIC_CAP_NONE;
    }
    
    nic_context_t *ctx = &g_nic_contexts[nic_index];
    if (ctx->info == NULL) {
        return NIC_CAP_NONE;
    }
    
    return nic_get_capabilities(ctx);
}

/**
 * @brief Check if NIC has specific capability
 * @param nic_index Index of NIC
 * @param capability Capability to check
 * @return true if supported, false otherwise
 */
bool hardware_nic_has_capability(int nic_index, nic_capability_flags_t capability) {
    if (nic_index < 0 || nic_index >= MAX_NICS) {
        return false;
    }
    
    nic_context_t *ctx = &g_nic_contexts[nic_index];
    if (ctx->info == NULL) {
        return false;
    }
    
    return nic_has_capability(ctx, capability);
}

/**
 * @brief Get capability-aware statistics
 * @param nic_index Index of NIC
 * @param stats Output statistics structure
 * @return 0 on success, negative on error
 */
int hardware_get_nic_stats_caps(int nic_index, nic_stats_t *stats) {
    if (nic_index < 0 || nic_index >= MAX_NICS || !stats) {
        return ERROR_INVALID_PARAM;
    }
    
    nic_context_t *ctx = &g_nic_contexts[nic_index];
    if (ctx->info == NULL) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Use vtable function if available */
    if (ctx->info->vtable && ctx->info->vtable->get_stats) {
        int result = ctx->info->vtable->get_stats(ctx, stats);
        return (result == NIC_CAP_SUCCESS) ? SUCCESS : ERROR_HARDWARE;
    }
    
    /* Fall back to basic statistics */
    memset(stats, 0, sizeof(nic_stats_t));
    stats->tx_packets = ctx->packets_sent;
    stats->rx_packets = ctx->packets_received;
    stats->tx_errors = ctx->errors;
    stats->rx_errors = ctx->errors;
    
    return SUCCESS;
}

/* ========================================================================== */
/* COMPATIBILITY FUNCTIONS                                                   */
/* ========================================================================== */

/**
 * @brief Get legacy NIC info structure
 * @param nic_index Index of NIC
 * @return Pointer to legacy structure, or NULL if invalid
 */
nic_info_t* hardware_get_legacy_nic_info(int nic_index) {
    if (nic_index < 0 || nic_index >= g_legacy_nic_count) {
        return NULL;
    }
    
    return &g_legacy_nic_infos[nic_index];
}

/**
 * @brief Get NIC context for advanced operations
 * @param nic_index Index of NIC
 * @return Pointer to NIC context, or NULL if invalid
 */
nic_context_t* hardware_get_nic_context(int nic_index) {
    if (nic_index < 0 || nic_index >= MAX_NICS) {
        return NULL;
    }
    
    nic_context_t *ctx = &g_nic_contexts[nic_index];
    return (ctx->info != NULL) ? ctx : NULL;
}

/**
 * @brief Update legacy NIC info from context
 * @param nic_index Index of NIC
 * @return 0 on success, negative on error
 */
int hardware_sync_legacy_info(int nic_index) {
    if (nic_index < 0 || nic_index >= MAX_NICS || nic_index >= g_legacy_nic_count) {
        return ERROR_INVALID_PARAM;
    }
    
    nic_context_t *ctx = &g_nic_contexts[nic_index];
    if (ctx->info == NULL) {
        return ERROR_INVALID_PARAM;
    }
    
    nic_info_t *legacy_nic = &g_legacy_nic_infos[nic_index];
    return nic_context_to_info(ctx, legacy_nic);
}

/* ========================================================================== */
/* DEBUG AND DIAGNOSTICS                                                     */
/* ========================================================================== */

/**
 * @brief Print capability information for all NICs
 */
void hardware_print_capability_info(void) {
    if (!g_capability_system_initialized) {
        printf("Capability system not initialized\n");
        return;
    }
    
    printf("=== NIC Capability Information ===\n");
    
    for (int i = 0; i < MAX_NICS; i++) {
        nic_context_t *ctx = &g_nic_contexts[i];
        if (ctx->info == NULL) {
            continue;
        }
        
        char cap_string[512];
        nic_get_capability_string(nic_get_capabilities(ctx), cap_string, sizeof(cap_string));
        
        printf("NIC %d: %s\n", i, ctx->info->name);
        printf("  I/O Base: 0x%04X, IRQ: %d\n", ctx->io_base, ctx->irq);
        printf("  MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
               ctx->mac[0], ctx->mac[1], ctx->mac[2],
               ctx->mac[3], ctx->mac[4], ctx->mac[5]);
        printf("  Link: %s, Speed: %d Mbps, Duplex: %s\n",
               ctx->link_up ? "Up" : "Down", ctx->speed,
               ctx->full_duplex ? "Full" : "Half");
        printf("  Capabilities: %s\n", cap_string);
        printf("  TX Ring: %d, RX Ring: %d\n", ctx->tx_ring_size, ctx->rx_ring_size);
        printf("  Copybreak: %d bytes, Int. Mitigation: %d µs\n",
               ctx->copybreak_threshold, ctx->interrupt_mitigation);
        printf("  Packets Sent: %u, Received: %u, Errors: %u\n",
               ctx->packets_sent, ctx->packets_received, ctx->errors);
        printf("\n");
    }
}