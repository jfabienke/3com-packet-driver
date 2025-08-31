/**
 * @file module_init_helper.c
 * @brief Module Initialization Helper Implementation
 * 
 * Provides common initialization logic for all modules that wrap
 * existing drivers. This eliminates code duplication between
 * PTASK, CORKSCRW, and BOOMTEX modules.
 */

#include "module_bridge.h"
#include "../../include/nic_init.h"
#include "../../include/memory.h"
#include "../../include/logging.h"
#include "../../include/error_codes.h"
#include "../../include/driver_version.h"
#include "../../c/3c509b.h"
#include "../../c/3c515.h"
#include "../../loader/centralized_detection.h"
#include "../../loader/device_registry.h"

/**
 * @brief Initialize a module bridge structure
 */
int module_bridge_init(module_bridge_t *bridge, 
                      module_header_t *header,
                      module_init_context_t *init_context) {
    if (!bridge || !header || !init_context) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("Module Bridge: Initializing bridge for module %s", header->module_name);
    
    /* Initialize bridge structure */
    memset(bridge, 0, sizeof(module_bridge_t));
    
    /* Store references */
    bridge->header = header;
    bridge->module_id = header->module_id;
    bridge->init_context = init_context;
    bridge->module_state = MODULE_STATE_INITIALIZING;
    bridge->device_registry_id = -1; /* No device claimed yet */
    
    /* Allocate NIC context */
    bridge->nic_context = malloc(sizeof(nic_info_t));
    if (!bridge->nic_context) {
        LOG_ERROR("Module Bridge: Failed to allocate NIC context");
        return ERROR_MEMORY_ALLOC;
    }
    
    memset(bridge->nic_context, 0, sizeof(nic_info_t));
    
    /* Allocate versioned driver operations */
    bridge->versioned_ops = malloc(sizeof(versioned_driver_ops_t));
    if (!bridge->versioned_ops) {
        LOG_ERROR("Module Bridge: Failed to allocate versioned driver ops");
        free(bridge->nic_context);
        return ERROR_MEMORY_ALLOC;
    }
    
    memset(bridge->versioned_ops, 0, sizeof(versioned_driver_ops_t));
    
    LOG_INFO("Module Bridge: Bridge initialized for module ID 0x%04X", 
             header->module_id);
    
    return SUCCESS;
}

/**
 * @brief Connect bridge to existing NIC driver
 */
int module_bridge_connect_driver(module_bridge_t *bridge, uint8_t nic_type) {
    if (!bridge || !bridge->nic_context || !bridge->init_context) {
        return ERROR_INVALID_PARAM;
    }
    
    nic_info_t *nic = bridge->nic_context;
    module_init_context_t *ctx = bridge->init_context;
    int result;
    
    LOG_DEBUG("Module Bridge: Connecting to driver for NIC type %d", nic_type);
    
    /* Find and claim device from registry */
    uint8_t bus_type = (nic_type == NIC_TYPE_3C509B) ? BUS_TYPE_ISA : 
                      (nic_type == NIC_TYPE_3C515_TX) ? BUS_TYPE_ISA : BUS_TYPE_PCI;
    
    int registry_id = device_registry_find_by_location(bus_type, ctx->detected_io_base, 
                                                       ctx->pci_bus, ctx->pci_device, 
                                                       ctx->pci_function);
    
    if (registry_id < 0) {
        LOG_ERROR("Module Bridge: Device not found in registry - I/O 0x%X, Bus type %d", 
                  ctx->detected_io_base, bus_type);
        return ERROR_DEVICE_NOT_FOUND;
    }
    
    /* Attempt to claim device atomically */
    result = device_registry_claim(registry_id, bridge->module_id);
    if (result != SUCCESS) {
        if (result == ERROR_DEVICE_BUSY) {
            LOG_ERROR("Module Bridge: Device already claimed by another module - registry ID %d", 
                      registry_id);
        } else {
            LOG_ERROR("Module Bridge: Failed to claim device %d: %d", registry_id, result);
        }
        return result;
    }
    
    bridge->device_registry_id = registry_id;
    LOG_INFO("Module Bridge: Successfully claimed device %d", registry_id);
    
    /* Create configuration for existing driver */
    nic_init_config_t config;
    memset(&config, 0, sizeof(config));
    
    config.nic_type = nic_type;
    config.io_base = ctx->detected_io_base;
    config.irq = ctx->detected_irq;
    config.force_pio = ctx->force_pio_mode;
    config.enable_bus_mastering = ctx->enable_bus_mastering;
    config.enable_checksums = ctx->enable_checksums;
    
    /* Copy MAC address if available */
    if (ctx->mac_address[0] != 0 || ctx->mac_address[1] != 0 || 
        ctx->mac_address[2] != 0 || ctx->mac_address[3] != 0 ||
        ctx->mac_address[4] != 0 || ctx->mac_address[5] != 0) {
        memcpy(nic->mac, ctx->mac_address, 6);
        memcpy(nic->perm_mac, ctx->mac_address, 6);
    }
    
    /* Initialize using existing driver */
    switch (nic_type) {
        case NIC_TYPE_3C509B:
            LOG_INFO("Module Bridge: Connecting to 3C509B driver");
            result = nic_init_3c509b(nic, &config);
            if (result == SUCCESS) {
                bridge->nic_ops = get_3c509b_ops();
                LOG_INFO("Module Bridge: 3C509B driver connected successfully");
            }
            break;
            
        case NIC_TYPE_3C515_TX:
            LOG_INFO("Module Bridge: Connecting to 3C515 driver");
            result = nic_init_3c515(nic, &config);
            if (result == SUCCESS) {
                bridge->nic_ops = get_3c515_ops();
                LOG_INFO("Module Bridge: 3C515 driver connected successfully");
            }
            break;
            
        default:
            LOG_ERROR("Module Bridge: Unsupported NIC type %d", nic_type);
            return ERROR_UNSUPPORTED_FUNCTION;
    }
    
    if (result != SUCCESS) {
        LOG_ERROR("Module Bridge: Driver initialization failed: %d", result);
        return result;
    }
    
    /* Create versioned driver interface wrapper */
    const char *driver_name = (nic_type == NIC_TYPE_3C509B) ? "3C509B" : 
                             (nic_type == NIC_TYPE_3C515_TX) ? "3C515" : "Unknown";
    
    result = driver_create_versioned_ops(bridge->nic_ops, driver_name, "3Com", bridge->versioned_ops);
    if (result != SUCCESS) {
        LOG_ERROR("Module Bridge: Failed to create versioned driver ops: %d", result);
        return result;
    }
    
    /* Validate version compatibility */
    driver_compatibility_t compat = driver_check_compatibility(
        bridge->versioned_ops, 
        CURRENT_DRIVER_VERSION, 
        DRIVER_FEATURE_BASIC
    );
    
    if (compat < 0) {
        LOG_ERROR("Module Bridge: Driver compatibility check failed: %s", 
                  driver_compatibility_string(compat));
        return ERROR_INVALID_PARAMETER;
    }
    
    if (compat > 0) {
        LOG_WARNING("Module Bridge: Driver compatibility warning: %s", 
                    driver_compatibility_string(compat));
        /* Continue - warnings are non-fatal */
    }
    
    LOG_INFO("Module Bridge: Versioned driver interface created - %s by %s", 
             bridge->versioned_ops->driver_name, bridge->versioned_ops->vendor_name);
    
    if (!bridge->nic_ops) {
        LOG_ERROR("Module Bridge: Failed to get driver operations table");
        return ERROR_NOT_FOUND;
    }
    
    /* Update bridge state */
    bridge->module_state = MODULE_STATE_ACTIVE;
    
    /* Mark device as verified by driver */
    result = device_registry_verify(bridge->device_registry_id, bridge->module_id);
    if (result != SUCCESS) {
        LOG_WARNING("Module Bridge: Failed to verify device in registry: %d", result);
        /* Continue - not fatal */
    } else {
        LOG_DEBUG("Module Bridge: Device %d verified by driver", bridge->device_registry_id);
    }
    
    /* Set hardware-specific flags */
    if (nic->bus_master_capable) {
        bridge->module_flags |= MODULE_BRIDGE_FLAG_BUS_MASTER;
    }
    
    if (nic->dma_capable) {
        bridge->module_flags |= MODULE_BRIDGE_FLAG_DMA_ACTIVE;
    }
    
    /* Validate ISR safety */
    result = module_bridge_validate_isr_safety(bridge);
    if (result != SUCCESS) {
        LOG_ERROR("Module Bridge: ISR safety validation failed: %d", result);
        return result;
    }
    
    LOG_INFO("Module Bridge: Driver connection complete - I/O 0x%X, IRQ %d, %s mode",
             nic->io_base, nic->irq, 
             (nic->dma_capable) ? "DMA" : "PIO");
    
    return SUCCESS;
}

/**
 * @brief Generic API dispatcher for bridged modules
 */
int module_bridge_api_dispatch(module_bridge_t *bridge, 
                              uint16_t function, 
                              void far *params) {
    if (!bridge || !bridge->nic_ops || bridge->module_state != MODULE_STATE_ACTIVE) {
        return ERROR_MODULE_NOT_READY;
    }
    
    /* Route based on common API functions */
    switch (function) {
        case 0x01: /* Hardware detection */
            /* Detection already done by loader - return cached results */
            return SUCCESS;
            
        case 0x02: /* Initialize NIC */
            /* Already initialized - return success */
            return SUCCESS;
            
        case 0x03: /* Send packet */
            return module_bridge_send_packet(bridge, 
                ((struct {uint8_t unused; const void far *data; uint16_t length;} far *)params)->data,
                ((struct {uint8_t unused; const void far *data; uint16_t length;} far *)params)->length);
            
        case 0x04: /* Receive packet */
            return module_bridge_receive_packet(bridge,
                ((struct {uint8_t unused; void far *buffer; uint16_t size; uint16_t *received;} far *)params)->buffer,
                ((struct {uint8_t unused; void far *buffer; uint16_t size; uint16_t *received;} far *)params)->size,
                ((struct {uint8_t unused; void far *buffer; uint16_t size; uint16_t *received;} far *)params)->received);
            
        case 0x05: /* Get statistics */
            return module_bridge_get_statistics(bridge, params);
            
        default:
            LOG_WARNING("Module Bridge: Unknown API function: 0x%04X", function);
            return ERROR_UNSUPPORTED_FUNCTION;
    }
}

/**
 * @brief Generic packet send wrapper
 */
int module_bridge_send_packet(module_bridge_t *bridge,
                             const void far *packet_data,
                             uint16_t packet_length) {
    if (!bridge || !bridge->nic_ops || !bridge->nic_ops->send_packet) {
        return ERROR_INVALID_PARAM;
    }
    
    int result = bridge->nic_ops->send_packet(bridge->nic_context, 
                                             (const uint8_t *)packet_data, 
                                             packet_length);
    
    if (result == SUCCESS) {
        bridge->packets_sent++;
    }
    
    return result;
}

/**
 * @brief Generic packet receive wrapper
 */
int module_bridge_receive_packet(module_bridge_t *bridge,
                                void far *buffer,
                                uint16_t buffer_size,
                                uint16_t *bytes_received) {
    if (!bridge || !bridge->nic_ops || !bridge->nic_ops->receive_packet) {
        return ERROR_INVALID_PARAM;
    }
    
    size_t received = 0;
    int result = bridge->nic_ops->receive_packet(bridge->nic_context,
                                                (uint8_t *)buffer,
                                                &received);
    
    if (result == SUCCESS && bytes_received) {
        *bytes_received = (uint16_t)received;
        bridge->packets_received++;
    }
    
    return result;
}

/**
 * @brief Generic interrupt handler wrapper
 */
void far module_bridge_handle_interrupt(module_bridge_t *bridge) {
    uint32_t start_time_us;
    int enter_result, exit_result;
    
    if (!bridge || !bridge->nic_ops || bridge->module_state != MODULE_STATE_ACTIVE) {
        return;
    }
    
    /* Get timestamp for ISR timing */
    start_time_us = get_system_timestamp_ms() * 1000; /* Convert ms to us */
    
    /* ISR safety validation entry */
    enter_result = module_bridge_isr_enter(bridge);
    if (enter_result != SUCCESS) {
        LOG_ERROR("Module Bridge: ISR entry validation failed: %d", enter_result);
        if (enter_result == ERROR_ISR_REENTRANT) {
            /* Allow controlled reentrancy but log it */
            LOG_WARNING("Module Bridge: Allowing ISR reentrancy - level %d", 
                        bridge->isr_nesting_level);
        } else {
            /* Unsafe to proceed */
            return;
        }
    }
    
    /* Call actual ISR using versioned interface */
    if (bridge->versioned_ops && bridge->versioned_ops->handle_interrupt_v1 && bridge->nic_context) {
        bridge->versioned_ops->handle_interrupt_v1(bridge->nic_context);
    } else if (bridge->nic_ops && bridge->nic_ops->handle_interrupt && bridge->nic_context) {
        /* Fallback to legacy interface */
        LOG_WARNING("Module Bridge: Using legacy ISR interface");
        bridge->nic_ops->handle_interrupt(bridge->nic_context);
    }
    
    /* ISR safety validation exit */
    exit_result = module_bridge_isr_exit(bridge, start_time_us);
    if (exit_result == WARNING_ISR_SLOW) {
        LOG_WARNING("Module Bridge: ISR %lu us exceeded real-time threshold", 
                    bridge->last_isr_time_us);
    }
}

/**
 * @brief Get module statistics
 */
int module_bridge_get_statistics(module_bridge_t *bridge, void far *stats) {
    if (!bridge || !stats) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Generic statistics structure */
    struct {
        uint32_t packets_sent;
        uint32_t packets_received;
        uint32_t tx_errors;
        uint32_t rx_errors;
        uint32_t interrupts;
        uint32_t last_isr_time_us;
    } far *module_stats = stats;
    
    module_stats->packets_sent = bridge->packets_sent;
    module_stats->packets_received = bridge->packets_received;
    module_stats->last_isr_time_us = bridge->last_isr_time_us;
    
    /* Get hardware-specific statistics if available */
    if (bridge->nic_ops && bridge->nic_ops->get_statistics) {
        bridge->nic_ops->get_statistics(bridge->nic_context, stats);
    }
    
    return SUCCESS;
}

/**
 * @brief Cleanup bridge and associated resources
 */
int module_bridge_cleanup(module_bridge_t *bridge) {
    if (!bridge) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("Module Bridge: Cleaning up bridge for module ID 0x%04X", 
              bridge->module_id);
    
    bridge->module_state = MODULE_STATE_UNLOADING;
    
    /* Release device from registry */
    if (bridge->device_registry_id >= 0) {
        int result = device_registry_release(bridge->device_registry_id, bridge->module_id);
        if (result == SUCCESS) {
            LOG_DEBUG("Module Bridge: Released device %d from registry", bridge->device_registry_id);
        } else {
            LOG_WARNING("Module Bridge: Failed to release device %d: %d", 
                        bridge->device_registry_id, result);
        }
        bridge->device_registry_id = -1;
    }
    
    /* Cleanup existing driver */
    if (bridge->nic_ops && bridge->nic_ops->cleanup && bridge->nic_context) {
        bridge->nic_ops->cleanup(bridge->nic_context);
    }
    
    /* Free allocated memory */
    if (bridge->nic_context) {
        free(bridge->nic_context);
        bridge->nic_context = NULL;
    }
    
    if (bridge->versioned_ops) {
        free(bridge->versioned_ops);
        bridge->versioned_ops = NULL;
    }
    
    if (bridge->module_private) {
        free(bridge->module_private);
        bridge->module_private = NULL;
    }
    
    /* Clear bridge structure */
    memset(bridge, 0, sizeof(module_bridge_t));
    
    LOG_INFO("Module Bridge: Cleanup completed");
    
    return SUCCESS;
}

/**
 * @brief Get module context from centralized detection
 * 
 * This is the preferred method for modules to get their initialization
 * context. It uses the centralized detection service results.
 * 
 * @param module_id Module ID requesting context
 * @param nic_type NIC type (NIC_TYPE_3C509B, NIC_TYPE_3C515_TX)
 * @return Pointer to context or NULL if not available
 */
module_init_context_t* module_get_context_from_detection(uint16_t module_id, uint8_t nic_type) {
    LOG_DEBUG("Module Bridge: Getting context from centralized detection for module 0x%04X, type %d", 
              module_id, nic_type);
    
    /* Check if centralized detection is ready */
    if (!centralized_detection_is_ready()) {
        LOG_WARNING("Module Bridge: Centralized detection not ready - initializing");
        
        int result = centralized_detection_initialize();
        if (result < 0) {
            LOG_ERROR("Module Bridge: Centralized detection initialization failed: %d", result);
            return NULL;
        }
    }
    
    /* Get context from centralized detection */
    module_init_context_t *context = centralized_detection_get_context(module_id, nic_type);
    if (!context) {
        LOG_WARNING("Module Bridge: No hardware context available for module 0x%04X, type %d", 
                   module_id, nic_type);
        return NULL;
    }
    
    LOG_INFO("Module Bridge: Retrieved context from centralized detection - I/O 0x%X, IRQ %d",
             context->detected_io_base, context->detected_irq);
    
    return context;
}

/**
 * @brief Helper to create standardized module init context (legacy)
 * 
 * This function is kept for backwards compatibility but modules should
 * prefer using module_get_context_from_detection().
 * 
 * @param context Context structure to fill
 * @param io_base Detected I/O base address
 * @param irq Detected IRQ
 * @param mac_addr MAC address (6 bytes)
 * @param device_id Hardware device ID
 * @return SUCCESS on success, negative error code on failure
 */
int module_create_init_context(module_init_context_t *context,
                              uint16_t io_base,
                              uint8_t irq,
                              const uint8_t *mac_addr,
                              uint16_t device_id) {
    if (!context) {
        return ERROR_INVALID_PARAM;
    }
    
    memset(context, 0, sizeof(module_init_context_t));
    
    context->detected_io_base = io_base;
    context->detected_irq = irq;
    context->device_id = device_id;
    context->vendor_id = 0x10B7;  /* 3Com vendor ID */
    
    if (mac_addr) {
        memcpy(context->mac_address, mac_addr, 6);
    }
    
    /* Use global CPU detection results */
    extern cpu_info_t g_cpu_info;
    context->cpu_info = &g_cpu_info;
    
    /* Set reasonable defaults */
    context->enable_bus_mastering = 1;  /* Enable if supported */
    context->enable_checksums = 1;      /* Enable if supported */
    context->force_pio_mode = 0;        /* Use DMA if available */
    
    return SUCCESS;
}

/* ISR Safety Validation Implementation */

/**
 * @brief Validate ISR safety for bridge
 */
int module_bridge_validate_isr_safety(module_bridge_t *bridge) {
    if (!bridge) {
        return ERROR_INVALID_PARAMETER;
    }
    
    LOG_DEBUG("Module Bridge: Validating ISR safety for module 0x%04X", bridge->module_id);
    
    /* Check basic ISR state */
    if (bridge->isr_nesting_level > 3) {
        LOG_ERROR("Module Bridge: ISR nesting level too deep: %d", bridge->isr_nesting_level);
        return ERROR_ISR_REENTRANT;
    }
    
    /* Check maximum ISR duration (should be < 100us for real-time systems) */
    if (bridge->isr_max_duration_us > 100) {
        LOG_WARNING("Module Bridge: ISR duration exceeds real-time limit: %lu us", 
                    bridge->isr_max_duration_us);
        /* Continue - not fatal but log warning */
    }
    
    /* Validate driver ISR function exists */
    if (!bridge->nic_ops || !bridge->nic_ops->handle_interrupt) {
        LOG_ERROR("Module Bridge: No ISR function registered");
        return ERROR_ISR_UNSAFE;
    }
    
    /* Check stack guard if available (DOS doesn't provide stack protection) */
    if (bridge->isr_stack_guard) {
        /* Basic stack canary check */
        uint16_t far *canary = (uint16_t far *)bridge->isr_stack_guard;
        if (*canary != 0xDEAD) {
            LOG_ERROR("Module Bridge: Stack corruption detected in ISR");
            return ERROR_ISR_STACK_OVERFLOW;
        }
    }
    
    /* Mark as validated */
    bridge->module_flags |= MODULE_BRIDGE_FLAG_ISR_SAFE;
    
    LOG_INFO("Module Bridge: ISR safety validation passed - %lu invocations, max %lu us",
             bridge->isr_entry_count, bridge->isr_max_duration_us);
    
    return SUCCESS;
}

/**
 * @brief ISR entry point with safety validation
 */
int module_bridge_isr_enter(module_bridge_t *bridge) {
    if (!bridge) {
        return ERROR_INVALID_PARAMETER;
    }
    
    /* Check if already in ISR (reentrancy) */
    if (bridge->module_flags & MODULE_BRIDGE_FLAG_ISR_LOCKED) {
        bridge->isr_nesting_level++;
        if (bridge->isr_nesting_level > 3) {
            LOG_ERROR("Module Bridge: ISR reentrancy limit exceeded");
            return ERROR_ISR_REENTRANT;
        }
        LOG_WARNING("Module Bridge: ISR reentrancy detected - level %d", 
                    bridge->isr_nesting_level);
    }
    
    /* Set ISR lock */
    bridge->module_flags |= MODULE_BRIDGE_FLAG_ISR_LOCKED;
    bridge->isr_entry_count++;
    
    /* Set stack guard if not already set */
    if (!bridge->isr_stack_guard) {
        /* Place canary on stack */
        uint16_t stack_canary = 0xDEAD;
        bridge->isr_stack_guard = (void far *)&stack_canary;
    }
    
    return SUCCESS;
}

/**
 * @brief ISR exit point with metrics update
 */
int module_bridge_isr_exit(module_bridge_t *bridge, uint32_t start_time_us) {
    uint32_t current_time_us;
    uint32_t duration_us;
    int result = SUCCESS;
    
    if (!bridge) {
        return ERROR_INVALID_PARAMETER;
    }
    
    /* Calculate ISR duration */
    current_time_us = get_system_timestamp_ms() * 1000; /* Convert ms to us */
    if (current_time_us >= start_time_us) {
        duration_us = current_time_us - start_time_us;
    } else {
        /* Handle timer rollover */
        duration_us = (UINT32_MAX - start_time_us) + current_time_us;
    }
    
    /* Update metrics */
    bridge->last_isr_time_us = duration_us;
    if (duration_us > bridge->isr_max_duration_us) {
        bridge->isr_max_duration_us = duration_us;
    }
    
    /* Check for excessive ISR duration */
    if (duration_us > 100) {  /* 100us threshold for real-time systems */
        LOG_WARNING("Module Bridge: ISR execution time excessive: %lu us", duration_us);
        result = WARNING_ISR_SLOW;
    }
    
    /* Handle reentrancy */
    if (bridge->isr_nesting_level > 0) {
        bridge->isr_nesting_level--;
        LOG_DEBUG("Module Bridge: ISR nesting decreased to %d", bridge->isr_nesting_level);
    } else {
        /* Clear ISR lock */
        bridge->module_flags &= ~MODULE_BRIDGE_FLAG_ISR_LOCKED;
    }
    
    return result;
}

/**
 * @brief Check if ISR is currently executing
 */
int module_bridge_isr_is_active(module_bridge_t *bridge) {
    if (!bridge) {
        return 0;
    }
    
    return (bridge->module_flags & MODULE_BRIDGE_FLAG_ISR_LOCKED) ? 1 : 0;
}