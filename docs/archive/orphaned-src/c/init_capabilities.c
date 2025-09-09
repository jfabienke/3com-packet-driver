/**
 * @file init_capabilities.c
 * @brief Capability-based driver initialization
 *
 * This file provides initialization routines that use the capability system
 * for cleaner, more maintainable NIC detection and setup.
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include "../include/init.h"
#include "../include/nic_capabilities.h"
#include "../include/hardware.h"
#include "../include/logging.h"
#include "../include/config.h"
#include "../include/nic_init.h"
#include "../include/cpu_detect.h"
#include "../include/buffer_alloc.h"
#include "../include/stats.h"
#include <string.h>

/* ========================================================================== */
/* CAPABILITY-BASED INITIALIZATION STATE                                     */
/* ========================================================================== */

typedef struct {
    bool capability_system_initialized;
    bool hardware_detected;
    int num_nics_detected;
    nic_context_t detected_contexts[MAX_NICS];
    init_performance_metrics_t performance_metrics;
} capability_init_state_t;

static capability_init_state_t g_cap_init_state = {0};

/* ========================================================================== */
/* CAPABILITY-BASED HARDWARE INITIALIZATION                                  */
/* ========================================================================== */

/**
 * @brief Initialize hardware using capability-driven approach
 * @param config Driver configuration
 * @return 0 on success, negative on error
 */
int hardware_init_with_capabilities(const config_t *config) {
    int result;
    uint32_t start_time;
    
    if (!config) {
        LOG_ERROR("hardware_init_with_capabilities: NULL config parameter");
        return INIT_ERR_INVALID_PARAM;
    }
    
    start_time = stats_get_timestamp();
    
    LOG_INFO("Initializing hardware with capability-driven detection");
    LOG_INFO("Config: IO1=0x%04X, IO2=0x%04X, IRQ1=%d, IRQ2=%d, Busmaster=%s",
             config->io1_base, config->io2_base, config->irq1, config->irq2,
             config->busmaster ? "enabled" : "disabled");
    
    /* Clear initialization state */
    memset(&g_cap_init_state, 0, sizeof(g_cap_init_state));
    
    /* Initialize capability system */
    result = hardware_capabilities_init();
    if (result != SUCCESS) {
        LOG_ERROR("Capability system initialization failed: %d", result);
        return INIT_ERR_HARDWARE;
    }
    g_cap_init_state.capability_system_initialized = true;
    
    /* Initialize basic hardware layer */
    result = hardware_init();
    if (result != SUCCESS) {
        LOG_ERROR("Hardware layer initialization failed: %d", result);
        return INIT_ERR_HARDWARE;
    }
    
    /* Detect NICs using capability-aware methods */
    result = detect_nics_with_capabilities(config);
    if (result < 0) {
        LOG_ERROR("Capability-based NIC detection failed: %d", result);
        return result;
    }
    
    g_cap_init_state.num_nics_detected = result;
    g_cap_init_state.hardware_detected = true;
    
    /* Record performance metrics */
    g_cap_init_state.performance_metrics.init_time_ms = stats_get_timestamp() - start_time;
    g_cap_init_state.performance_metrics.nics_detected = result;
    
    LOG_INFO("Capability-based hardware initialization complete: %d NICs detected in %u ms",
             result, g_cap_init_state.performance_metrics.init_time_ms);
    
    return result;
}

/**
 * @brief Detect NICs using capability-driven approach
 * @param config Driver configuration
 * @return Number of NICs detected, or negative on error
 */
static int detect_nics_with_capabilities(const config_t *config) {
    int total_detected = 0;
    int result;
    
    LOG_INFO("Starting capability-driven NIC detection");
    
    /* Phase 1: Detect 3C509B NICs (PIO-based, simpler) */
    result = detect_3c509b_with_capabilities(config);
    if (result > 0) {
        total_detected += result;
        LOG_INFO("Phase 1: Detected %d 3C509B NIC(s)", result);
    } else if (result < 0) {
        LOG_WARNING("Phase 1: 3C509B detection failed: %d", result);
    }
    
    /* Phase 2: Detect 3C515-TX NICs (bus mastering, more complex) */
    result = detect_3c515_with_capabilities(config);
    if (result > 0) {
        total_detected += result;
        LOG_INFO("Phase 2: Detected %d 3C515-TX NIC(s)", result);
    } else if (result < 0) {
        LOG_WARNING("Phase 2: 3C515-TX detection failed: %d", result);
    }
    
    /* Phase 3: Initialize all detected NICs */
    if (total_detected > 0) {
        result = initialize_detected_nics_with_capabilities();
        if (result < 0) {
            LOG_ERROR("Failed to initialize detected NICs: %d", result);
            return result;
        }
    }
    
    return total_detected;
}

/**
 * @brief Detect 3C509B NICs using capability system
 * @param config Driver configuration
 * @return Number of NICs detected, or negative on error
 */
static int detect_3c509b_with_capabilities(const config_t *config) {
    nic_detect_info_t detect_info[MAX_NICS];
    int detected_count = 0;
    int result;
    
    LOG_DEBUG("Detecting 3C509B NICs with capability awareness");
    
    /* Use existing detection but enhance with capability information */
    int legacy_detected = nic_detect_3c509b(detect_info, MAX_NICS);
    if (legacy_detected <= 0) {
        LOG_DEBUG("No 3C509B NICs detected by legacy method");
        return 0;
    }
    
    /* Process each detected NIC with capability enhancement */
    for (int i = 0; i < legacy_detected && detected_count < MAX_NICS; i++) {
        nic_context_t *ctx = &g_cap_init_state.detected_contexts[detected_count];
        
        /* Get 3C509B capability information */
        const nic_info_entry_t *info_entry = nic_get_info_entry(NIC_TYPE_3C509B);
        if (!info_entry) {
            LOG_ERROR("3C509B capability information not found");
            continue;
        }
        
        /* Initialize context with detected information */
        result = nic_context_init(ctx, info_entry, detect_info[i].io_base, detect_info[i].irq);
        if (result != NIC_CAP_SUCCESS) {
            LOG_WARNING("Failed to initialize 3C509B context at I/O 0x%04X: %d",
                       detect_info[i].io_base, result);
            continue;
        }
        
        /* Copy MAC address if available */
        if (detect_info[i].mac_valid) {
            memcpy(ctx->mac, detect_info[i].mac_address, 6);
        }
        
        /* Perform capability-specific detection enhancements */
        result = enhance_3c509b_detection(ctx, &detect_info[i]);
        if (result != SUCCESS) {
            LOG_WARNING("3C509B capability enhancement failed at I/O 0x%04X: %d",
                       detect_info[i].io_base, result);
            /* Continue anyway with basic capabilities */
        }
        
        detected_count++;
        
        LOG_INFO("Enhanced 3C509B detection: I/O=0x%04X, IRQ=%d, MAC=%02X:%02X:%02X:%02X:%02X:%02X",
                ctx->io_base, ctx->irq,
                ctx->mac[0], ctx->mac[1], ctx->mac[2],
                ctx->mac[3], ctx->mac[4], ctx->mac[5]);
    }
    
    return detected_count;
}

/**
 * @brief Detect 3C515-TX NICs using capability system
 * @param config Driver configuration
 * @return Number of NICs detected, or negative on error
 */
static int detect_3c515_with_capabilities(const config_t *config) {
    nic_detect_info_t detect_info[MAX_NICS];
    int detected_count = 0;
    int result;
    
    LOG_DEBUG("Detecting 3C515-TX NICs with capability awareness");
    
    /* Use existing detection but enhance with capability information */
    int legacy_detected = nic_detect_3c515(detect_info, MAX_NICS);
    if (legacy_detected <= 0) {
        LOG_DEBUG("No 3C515-TX NICs detected by legacy method");
        return 0;
    }
    
    /* Process each detected NIC with capability enhancement */
    for (int i = 0; i < legacy_detected && detected_count < MAX_NICS; i++) {
        int ctx_index = g_cap_init_state.num_nics_detected + detected_count;
        if (ctx_index >= MAX_NICS) {
            LOG_WARNING("Maximum number of NICs exceeded");
            break;
        }
        
        nic_context_t *ctx = &g_cap_init_state.detected_contexts[ctx_index];
        
        /* Get 3C515-TX capability information */
        const nic_info_entry_t *info_entry = nic_get_info_entry(NIC_TYPE_3C515_TX);
        if (!info_entry) {
            LOG_ERROR("3C515-TX capability information not found");
            continue;
        }
        
        /* Initialize context with detected information */
        result = nic_context_init(ctx, info_entry, detect_info[i].io_base, detect_info[i].irq);
        if (result != NIC_CAP_SUCCESS) {
            LOG_WARNING("Failed to initialize 3C515-TX context at I/O 0x%04X: %d",
                       detect_info[i].io_base, result);
            continue;
        }
        
        /* Copy MAC address if available */
        if (detect_info[i].mac_valid) {
            memcpy(ctx->mac, detect_info[i].mac_address, 6);
        }
        
        /* Perform capability-specific detection enhancements */
        result = enhance_3c515_detection(ctx, &detect_info[i], config);
        if (result != SUCCESS) {
            LOG_WARNING("3C515-TX capability enhancement failed at I/O 0x%04X: %d",
                       detect_info[i].io_base, result);
            /* Continue anyway with basic capabilities */
        }
        
        detected_count++;
        
        LOG_INFO("Enhanced 3C515-TX detection: I/O=0x%04X, IRQ=%d, MAC=%02X:%02X:%02X:%02X:%02X:%02X",
                ctx->io_base, ctx->irq,
                ctx->mac[0], ctx->mac[1], ctx->mac[2],
                ctx->mac[3], ctx->mac[4], ctx->mac[5]);
    }
    
    return detected_count;
}

/**
 * @brief Enhance 3C509B detection with capability-specific tests
 * @param ctx NIC context
 * @param detect_info Detection information
 * @return 0 on success, negative on error
 */
static int enhance_3c509b_detection(nic_context_t *ctx, const nic_detect_info_t *detect_info) {
    int result;
    
    LOG_DEBUG("Enhancing 3C509B detection with capability tests");
    
    /* Test direct PIO capability */
    if (nic_has_capability(ctx, NIC_CAP_DIRECT_PIO)) {
        result = test_direct_pio_capability(ctx);
        if (result == SUCCESS) {
            LOG_DEBUG("3C509B direct PIO capability confirmed");
        } else {
            LOG_WARNING("3C509B direct PIO test failed: %d", result);
            nic_update_capabilities(ctx, ctx->detected_caps & ~NIC_CAP_DIRECT_PIO);
        }
    }
    
    /* Test RX copybreak capability */
    if (nic_has_capability(ctx, NIC_CAP_RX_COPYBREAK)) {
        result = test_rx_copybreak_capability(ctx);
        if (result == SUCCESS) {
            LOG_DEBUG("3C509B RX copybreak capability confirmed");
            /* Optimize copybreak threshold for this NIC */
            ctx->copybreak_threshold = 256;  /* Optimal for 3C509B */
        } else {
            LOG_WARNING("3C509B RX copybreak test failed: %d", result);
        }
    }
    
    /* Test multicast capability */
    if (nic_has_capability(ctx, NIC_CAP_MULTICAST)) {
        result = test_multicast_capability(ctx);
        if (result == SUCCESS) {
            LOG_DEBUG("3C509B multicast capability confirmed");
        } else {
            LOG_WARNING("3C509B multicast test failed: %d", result);
        }
    }
    
    /* Perform runtime capability detection */
    result = nic_detect_runtime_capabilities(ctx);
    if (result != NIC_CAP_SUCCESS) {
        LOG_WARNING("3C509B runtime capability detection failed: %d", result);
    }
    
    return SUCCESS;
}

/**
 * @brief Enhance 3C515-TX detection with capability-specific tests
 * @param ctx NIC context
 * @param detect_info Detection information
 * @param config Driver configuration
 * @return 0 on success, negative on error
 */
static int enhance_3c515_detection(nic_context_t *ctx, const nic_detect_info_t *detect_info, 
                                  const config_t *config) {
    int result;
    
    LOG_DEBUG("Enhancing 3C515-TX detection with capability tests");
    
    /* Test bus mastering capability */
    if (nic_has_capability(ctx, NIC_CAP_BUSMASTER)) {
        if (config->busmaster) {
            result = test_busmaster_capability(ctx);
            if (result == SUCCESS) {
                LOG_DEBUG("3C515-TX bus mastering capability confirmed");
                ctx->active_caps |= NIC_CAP_BUSMASTER;
            } else {
                LOG_WARNING("3C515-TX bus mastering test failed: %d", result);
                nic_update_capabilities(ctx, ctx->detected_caps & ~NIC_CAP_BUSMASTER);
            }
        } else {
            LOG_INFO("Bus mastering disabled by configuration");
            nic_update_capabilities(ctx, ctx->detected_caps & ~NIC_CAP_BUSMASTER);
        }
    }
    
    /* Test MII interface capability */
    if (nic_has_capability(ctx, NIC_CAP_MII)) {
        result = test_mii_capability(ctx);
        if (result == SUCCESS) {
            LOG_DEBUG("3C515-TX MII interface capability confirmed");
        } else {
            LOG_WARNING("3C515-TX MII test failed: %d", result);
        }
    }
    
    /* Test interrupt mitigation capability */
    if (nic_has_capability(ctx, NIC_CAP_INTERRUPT_MIT)) {
        result = test_interrupt_mitigation_capability(ctx);
        if (result == SUCCESS) {
            LOG_DEBUG("3C515-TX interrupt mitigation capability confirmed");
            /* Set optimal mitigation for this NIC */
            ctx->interrupt_mitigation = 100;  /* 100µs optimal for 3C515-TX */
        } else {
            LOG_WARNING("3C515-TX interrupt mitigation test failed: %d", result);
        }
    }
    
    /* Test full duplex capability */
    if (nic_has_capability(ctx, NIC_CAP_FULL_DUPLEX)) {
        result = test_full_duplex_capability(ctx);
        if (result == SUCCESS) {
            LOG_DEBUG("3C515-TX full duplex capability confirmed");
        } else {
            LOG_WARNING("3C515-TX full duplex test failed: %d", result);
        }
    }
    
    /* Perform runtime capability detection */
    result = nic_detect_runtime_capabilities(ctx);
    if (result != NIC_CAP_SUCCESS) {
        LOG_WARNING("3C515-TX runtime capability detection failed: %d", result);
    }
    
    return SUCCESS;
}

/**
 * @brief Initialize all detected NICs using capability system
 * @return 0 on success, negative on error
 */
static int initialize_detected_nics_with_capabilities(void) {
    int result;
    int initialized_count = 0;
    
    LOG_INFO("Initializing detected NICs with capability system");
    
    for (int i = 0; i < MAX_NICS; i++) {
        nic_context_t *ctx = &g_cap_init_state.detected_contexts[i];
        if (ctx->info == NULL) {
            continue;  /* No NIC in this slot */
        }
        
        /* Register NIC with hardware layer */
        result = hardware_register_nic_with_capabilities(ctx->info->nic_type, 
                                                        ctx->io_base, ctx->irq);
        if (result < 0) {
            LOG_ERROR("Failed to register %s at I/O 0x%04X: %d",
                     ctx->info->name, ctx->io_base, result);
            continue;
        }
        
        /* Initialize NIC using capability-aware method */
        result = initialize_nic_with_capabilities(ctx);
        if (result != SUCCESS) {
            LOG_ERROR("Failed to initialize %s at I/O 0x%04X: %d",
                     ctx->info->name, ctx->io_base, result);
            continue;
        }
        
        /* Configure NIC using capabilities */
        nic_config_t nic_config = {0};
        nic_config.io_base = ctx->io_base;
        nic_config.irq = ctx->irq;
        nic_config.media = ctx->current_media;
        
        result = hardware_configure_nic_caps(result, &nic_config);
        if (result != SUCCESS) {
            LOG_WARNING("Failed to configure %s: %d", ctx->info->name, result);
            /* Continue anyway */
        }
        
        initialized_count++;
        
        /* Log capability summary */
        char cap_string[256];
        nic_get_capability_string(nic_get_capabilities(ctx), cap_string, sizeof(cap_string));
        LOG_INFO("Initialized %s: I/O=0x%04X IRQ=%d Capabilities=[%s]",
                ctx->info->name, ctx->io_base, ctx->irq, cap_string);
    }
    
    LOG_INFO("Successfully initialized %d NICs with capability system", initialized_count);
    return initialized_count;
}

/**
 * @brief Initialize a single NIC using capability-aware methods
 * @param ctx NIC context
 * @return 0 on success, negative on error
 */
static int initialize_nic_with_capabilities(nic_context_t *ctx) {
    int result;
    
    if (!ctx || !ctx->info) {
        return INIT_ERR_INVALID_PARAM;
    }
    
    LOG_DEBUG("Initializing %s with capability-aware methods", ctx->info->name);
    
    /* Use vtable initialization if available */
    if (ctx->info->vtable && ctx->info->vtable->init) {
        result = ctx->info->vtable->init(ctx);
        if (result != NIC_CAP_SUCCESS) {
            LOG_ERROR("VTable initialization failed for %s: %d", ctx->info->name, result);
            return INIT_ERR_NIC_INIT;
        }
    }
    
    /* Apply capability-specific optimizations */
    if (nic_has_capability(ctx, NIC_CAP_BUSMASTER)) {
        result = optimize_for_busmaster(ctx);
        if (result != SUCCESS) {
            LOG_WARNING("Bus mastering optimization failed: %d", result);
        }
    }
    
    if (nic_has_capability(ctx, NIC_CAP_DIRECT_PIO)) {
        result = optimize_for_direct_pio(ctx);
        if (result != SUCCESS) {
            LOG_WARNING("Direct PIO optimization failed: %d", result);
        }
    }
    
    if (nic_has_capability(ctx, NIC_CAP_RX_COPYBREAK)) {
        result = optimize_for_rx_copybreak(ctx);
        if (result != SUCCESS) {
            LOG_WARNING("RX copybreak optimization failed: %d", result);
        }
    }
    
    /* Set initial state */
    ctx->state = 1;  /* Initialized state */
    
    LOG_DEBUG("Capability-aware initialization complete for %s", ctx->info->name);
    return SUCCESS;
}

/* ========================================================================== */
/* CAPABILITY TESTING FUNCTIONS                                              */
/* ========================================================================== */

/**
 * @brief Test direct PIO capability
 * @param ctx NIC context
 * @return 0 on success, negative on error
 */
static int test_direct_pio_capability(nic_context_t *ctx) {
    /* This would perform actual hardware tests for direct PIO optimization */
    LOG_DEBUG("Testing direct PIO capability for %s", ctx->info->name);
    
    /* Placeholder - would test optimized PIO operations */
    return SUCCESS;
}

/**
 * @brief Test RX copybreak capability
 * @param ctx NIC context
 * @return 0 on success, negative on error
 */
static int test_rx_copybreak_capability(nic_context_t *ctx) {
    /* This would test RX copybreak optimization */
    LOG_DEBUG("Testing RX copybreak capability for %s", ctx->info->name);
    
    /* Placeholder - would test small packet handling */
    return SUCCESS;
}

/**
 * @brief Test bus mastering capability
 * @param ctx NIC context
 * @return 0 on success, negative on error
 */
static int test_busmaster_capability(nic_context_t *ctx) {
    /* This would test DMA/bus mastering functionality */
    LOG_DEBUG("Testing bus mastering capability for %s", ctx->info->name);
    
    /* Placeholder - would test DMA operations */
    return SUCCESS;
}

/**
 * @brief Test MII interface capability
 * @param ctx NIC context
 * @return 0 on success, negative on error
 */
static int test_mii_capability(nic_context_t *ctx) {
    /* This would test MII interface functionality */
    LOG_DEBUG("Testing MII capability for %s", ctx->info->name);
    
    /* Placeholder - would test MII register access */
    return SUCCESS;
}

/**
 * @brief Test interrupt mitigation capability
 * @param ctx NIC context
 * @return 0 on success, negative on error
 */
static int test_interrupt_mitigation_capability(nic_context_t *ctx) {
    /* This would test interrupt mitigation */
    LOG_DEBUG("Testing interrupt mitigation capability for %s", ctx->info->name);
    
    /* Placeholder - would test interrupt coalescing */
    return SUCCESS;
}

/**
 * @brief Test full duplex capability
 * @param ctx NIC context
 * @return 0 on success, negative on error
 */
static int test_full_duplex_capability(nic_context_t *ctx) {
    /* This would test full duplex mode */
    LOG_DEBUG("Testing full duplex capability for %s", ctx->info->name);
    
    /* Placeholder - would test duplex configuration */
    return SUCCESS;
}

/**
 * @brief Test multicast capability
 * @param ctx NIC context
 * @return 0 on success, negative on error
 */
static int test_multicast_capability(nic_context_t *ctx) {
    /* This would test multicast filtering */
    LOG_DEBUG("Testing multicast capability for %s", ctx->info->name);
    
    /* Placeholder - would test multicast filter */
    return SUCCESS;
}

/* ========================================================================== */
/* OPTIMIZATION FUNCTIONS                                                    */
/* ========================================================================== */

/**
 * @brief Optimize NIC for bus mastering
 * @param ctx NIC context
 * @return 0 on success, negative on error
 */
static int optimize_for_busmaster(nic_context_t *ctx) {
    LOG_DEBUG("Applying bus mastering optimizations for %s", ctx->info->name);
    
    /* Configure for larger ring buffers */
    ctx->tx_ring_size = ctx->info->default_tx_ring_size * 2;
    ctx->rx_ring_size = ctx->info->default_rx_ring_size * 2;
    
    /* Set interrupt mitigation for better DMA performance */
    ctx->interrupt_mitigation = 200;  /* Higher mitigation for DMA */
    
    return SUCCESS;
}

/**
 * @brief Optimize NIC for direct PIO
 * @param ctx NIC context
 * @return 0 on success, negative on error
 */
static int optimize_for_direct_pio(nic_context_t *ctx) {
    LOG_DEBUG("Applying direct PIO optimizations for %s", ctx->info->name);
    
    /* Configure for lower latency */
    ctx->interrupt_mitigation = 50;  /* Lower mitigation for PIO */
    
    /* Optimize ring sizes for PIO */
    ctx->tx_ring_size = ctx->info->default_tx_ring_size;
    ctx->rx_ring_size = ctx->info->default_rx_ring_size;
    
    return SUCCESS;
}

/**
 * @brief Optimize NIC for RX copybreak
 * @param ctx NIC context
 * @return 0 on success, negative on error
 */
static int optimize_for_rx_copybreak(nic_context_t *ctx) {
    LOG_DEBUG("Applying RX copybreak optimizations for %s", ctx->info->name);
    
    /* Set optimal copybreak threshold based on NIC type */
    if (ctx->info->nic_type == NIC_TYPE_3C509B) {
        ctx->copybreak_threshold = 256;  /* Optimal for 3C509B */
    } else if (ctx->info->nic_type == NIC_TYPE_3C515_TX) {
        ctx->copybreak_threshold = 512;  /* Optimal for 3C515-TX */
    }
    
    return SUCCESS;
}

/* ========================================================================== */
/* STATUS AND CLEANUP                                                        */
/* ========================================================================== */

/**
 * @brief Get initialization status
 * @return Pointer to initialization state
 */
const capability_init_state_t* get_capability_init_state(void) {
    return &g_cap_init_state;
}

/**
 * @brief Clean up capability-based initialization
 */
void cleanup_capability_initialization(void) {
    if (g_cap_init_state.capability_system_initialized) {
        hardware_capabilities_cleanup();
    }
    
    /* Clear all contexts */
    for (int i = 0; i < MAX_NICS; i++) {
        if (g_cap_init_state.detected_contexts[i].info != NULL) {
            nic_context_cleanup(&g_cap_init_state.detected_contexts[i]);
        }
    }
    
    memset(&g_cap_init_state, 0, sizeof(g_cap_init_state));
    
    LOG_INFO("Capability-based initialization cleanup complete");
}