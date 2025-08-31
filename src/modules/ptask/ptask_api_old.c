/**
 * @file ptask_api.c
 * @brief PTASK.MOD API Implementation
 * 
 * Team A Implementation - API functions for module interface
 * Implements all PTASK API functions with proper parameter validation
 */

#include "ptask_internal.h"

/* External context reference */
extern ptask_context_t g_ptask_context;
extern memory_services_t *g_memory_services;

/**
 * @brief API function to detect hardware
 * 
 * @param params Detection parameters
 * @return SUCCESS on success, negative error code on failure
 */
int ptask_api_detect_hardware(ptask_detect_params_t far *params) {
    int hardware_detected = 0;
    
    if (!params) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("PTASK API: Hardware detection requested");
    
    /* Check what hardware types are requested */
    if (params->hardware_types & (1 << PTASK_HARDWARE_3C509B)) {
        int result = ptask_detect_3c509b();
        if (result > 0) {
            hardware_detected |= (1 << PTASK_HARDWARE_3C509B);
            LOG_INFO("PTASK API: 3C509B detected");
        }
    }
    
    if (params->hardware_types & (1 << PTASK_HARDWARE_3C589)) {
        int result = ptask_detect_3c589();
        if (result > 0) {
            hardware_detected |= (1 << PTASK_HARDWARE_3C589);
            LOG_INFO("PTASK API: 3C589 detected");
        }
    }
    
    if (params->hardware_types & (1 << PTASK_HARDWARE_NE2000_COMPAT)) {
        int result = ptask_detect_ne2000();
        if (result > 0) {
            hardware_detected |= (1 << PTASK_HARDWARE_NE2000_COMPAT);
            LOG_INFO("PTASK API: NE2000 compatibility detected");
        }
    }
    
    return hardware_detected;
}

/**
 * @brief API function to initialize NIC
 * 
 * @param params Initialization parameters  
 * @return SUCCESS on success, negative error code on failure
 */
int ptask_api_initialize_nic(ptask_init_params_t far *params) {
    nic_info_t nic;
    int result;
    
    if (!params) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("PTASK API: NIC initialization requested for type %d", params->hardware_type);
    
    /* Initialize NIC structure */
    memset(&nic, 0, sizeof(nic_info_t));
    
    /* Initialize based on hardware type */
    switch (params->hardware_type) {
        case PTASK_HARDWARE_3C509B:
            result = ptask_init_3c509b_hardware(&nic);
            break;
            
        case PTASK_HARDWARE_3C589:
            result = ptask_init_3c589_hardware(&nic);
            break;
            
        case PTASK_HARDWARE_NE2000_COMPAT:
            {
                ne2000_config_t ne_config;
                ne_config.io_base = 0x300;
                ne_config.interrupt_line = 3;
                result = ne2000_init_hardware(&ne_config);
                if (result == SUCCESS) {
                    nic.io_base = ne_config.io_base;
                    nic.irq = ne_config.interrupt_line;
                    memcpy(nic.mac, ne_config.mac_address, 6);
                }
            }
            break;
            
        default:
            LOG_ERROR("PTASK API: Unsupported hardware type: %d", params->hardware_type);
            return ERROR_UNSUPPORTED_HARDWARE;
    }
    
    if (result < 0) {
        LOG_ERROR("PTASK API: Hardware initialization failed: %d", result);
        return result;
    }
    
    /* Return configuration to caller */
    params->io_base = nic.io_base;
    params->irq = nic.irq;
    memcpy(params->mac_address, nic.mac, 6);
    params->capabilities = 0;  /* Basic capabilities for Week 1 */
    
    /* Update global context */
    g_ptask_context.hardware_type = params->hardware_type;
    g_ptask_context.io_base = nic.io_base;
    g_ptask_context.irq = nic.irq;
    memcpy(g_ptask_context.mac_address, nic.mac, 6);
    g_ptask_context.hardware_initialized = true;
    
    LOG_INFO("PTASK API: NIC initialized successfully - I/O: 0x%X, IRQ: %d",
             nic.io_base, nic.irq);
    
    return SUCCESS;
}

/**
 * @brief API function to send packet
 * 
 * @param params Send parameters
 * @return SUCCESS on success, negative error code on failure
 */
int ptask_api_send_packet(ptask_send_params_t far *params) {
    timing_context_t timing;
    uint16_t send_time_us;
    int result;
    
    if (!params || !params->packet_data || params->packet_length == 0) {
        return ERROR_INVALID_PARAM;
    }
    
    if (!g_ptask_context.hardware_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    /* Start timing measurement */
    TIMING_START(timing);
    
    /* Dispatch to appropriate hardware handler */
    switch (g_ptask_context.hardware_type) {
        case PTASK_HARDWARE_3C509B:
            result = ptask_send_3c509b_packet(params->packet_data, params->packet_length);
            break;
            
        case PTASK_HARDWARE_3C589:
            result = ptask_send_3c589_packet(params->packet_data, params->packet_length);
            break;
            
        case PTASK_HARDWARE_NE2000_COMPAT:
            result = ne2000_send_packet(params->packet_data, params->packet_length);
            break;
            
        default:
            result = ERROR_UNSUPPORTED_HARDWARE;
            break;
    }
    
    TIMING_END(timing);
    send_time_us = TIMING_GET_MICROSECONDS(timing);
    
    if (result == SUCCESS) {
        /* Update statistics */
        g_ptask_context.packets_sent++;
        g_ptask_context.bytes_sent += params->packet_length;
        
        LOG_TRACE("PTASK API: Sent packet of %d bytes in %d μs",
                  params->packet_length, send_time_us);
    } else {
        g_ptask_context.send_errors++;
        LOG_DEBUG("PTASK API: Send failed: %d", result);
    }
    
    return result;
}

/**
 * @brief API function to receive packet
 * 
 * @param params Receive parameters
 * @return SUCCESS on success, negative error code on failure
 */
int ptask_api_receive_packet(ptask_recv_params_t far *params) {
    timing_context_t timing;
    uint16_t recv_time_us;
    uint16_t received_length = 0;
    int result;
    
    if (!params || !params->buffer || !params->received_length || params->buffer_size == 0) {
        return ERROR_INVALID_PARAM;
    }
    
    if (!g_ptask_context.hardware_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    /* Start timing measurement */
    TIMING_START(timing);
    
    /* Dispatch to appropriate hardware handler */
    switch (g_ptask_context.hardware_type) {
        case PTASK_HARDWARE_3C509B:
            result = ptask_receive_3c509b_packet(params->buffer, params->buffer_size, &received_length);
            break;
            
        case PTASK_HARDWARE_3C589:
            result = ptask_receive_3c589_packet(params->buffer, params->buffer_size, &received_length);
            break;
            
        case PTASK_HARDWARE_NE2000_COMPAT:
            {
                uint16_t length = params->buffer_size;
                result = ne2000_receive_packet(params->buffer, &length);
                received_length = length;
            }
            break;
            
        default:
            result = ERROR_UNSUPPORTED_HARDWARE;
            break;
    }
    
    TIMING_END(timing);
    recv_time_us = TIMING_GET_MICROSECONDS(timing);
    
    /* Return received length */
    *(params->received_length) = received_length;
    
    if (result == SUCCESS) {
        /* Update statistics */
        g_ptask_context.packets_received++;
        g_ptask_context.bytes_received += received_length;
        
        LOG_TRACE("PTASK API: Received packet of %d bytes in %d μs",
                  received_length, recv_time_us);
    } else if (result != ERROR_NO_DATA) {
        g_ptask_context.receive_errors++;
        LOG_DEBUG("PTASK API: Receive failed: %d", result);
    }
    
    return result;
}

/**
 * @brief API function to get statistics
 * 
 * @param params Statistics parameters
 * @return SUCCESS on success, negative error code on failure
 */
int ptask_api_get_statistics(ptask_stats_params_t far *params) {
    if (!params) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Copy current statistics */
    params->packets_sent = g_ptask_context.packets_sent;
    params->packets_received = g_ptask_context.packets_received;
    params->bytes_sent = g_ptask_context.bytes_sent;
    params->bytes_received = g_ptask_context.bytes_received;
    params->send_errors = g_ptask_context.send_errors;
    params->receive_errors = g_ptask_context.receive_errors;
    params->avg_isr_time_us = g_ptask_context.avg_isr_time_us;
    params->max_isr_time_us = g_ptask_context.max_isr_time_us;
    
    LOG_DEBUG("PTASK API: Statistics retrieved - TX: %lu, RX: %lu",
              g_ptask_context.packets_sent, g_ptask_context.packets_received);
    
    return SUCCESS;
}

/**
 * @brief API function to configure module
 * 
 * @param params Configuration parameters
 * @return SUCCESS on success, negative error code on failure
 */
int ptask_api_configure(ptask_config_params_t far *params) {
    if (!params) {
        return ERROR_INVALID_PARAM;
    }
    
    LOG_DEBUG("PTASK API: Configuration requested - type: %d, flags: 0x%X",
              params->config_type, params->config_flags);
    
    /* Handle different configuration types */
    switch (params->config_type) {
        case 1:  /* IRQ configuration */
            if (params->config_data && params->config_length >= 1) {
                uint8_t far *irq_data = (uint8_t far *)params->config_data;
                uint8_t new_irq = *irq_data;
                
                if (new_irq >= 3 && new_irq <= 15) {
                    g_ptask_context.irq = new_irq;
                    LOG_INFO("PTASK API: IRQ configured to %d", new_irq);
                    return SUCCESS;
                }
            }
            return ERROR_INVALID_PARAM;
            
        case 2:  /* I/O base configuration */
            if (params->config_data && params->config_length >= 2) {
                uint16_t far *io_data = (uint16_t far *)params->config_data;
                uint16_t new_io_base = *io_data;
                
                if (new_io_base >= 0x200 && new_io_base <= 0x3E0) {
                    g_ptask_context.io_base = new_io_base;
                    LOG_INFO("PTASK API: I/O base configured to 0x%X", new_io_base);
                    return SUCCESS;
                }
            }
            return ERROR_INVALID_PARAM;
            
        default:
            LOG_WARNING("PTASK API: Unsupported configuration type: %d", params->config_type);
            return ERROR_UNSUPPORTED_FUNCTION;
    }
}

/**
 * @brief Update module performance statistics
 * 
 * @param packets_sent Number of packets sent
 * @param packets_received Number of packets received  
 * @param bytes_sent Number of bytes sent
 * @param bytes_received Number of bytes received
 */
void ptask_update_statistics(uint32_t packets_sent, uint32_t packets_received,
                            uint32_t bytes_sent, uint32_t bytes_received) {
    g_ptask_context.packets_sent += packets_sent;
    g_ptask_context.packets_received += packets_received;
    g_ptask_context.bytes_sent += bytes_sent;
    g_ptask_context.bytes_received += bytes_received;
}

/**
 * @brief Update timing statistics
 * 
 * @param isr_time_us ISR execution time in microseconds
 * @param cli_time_us CLI section time in microseconds
 */
void ptask_update_timing_stats(uint16_t isr_time_us, uint16_t cli_time_us) {
    /* Update ISR timing (simple moving average) */
    if (g_ptask_context.avg_isr_time_us == 0) {
        g_ptask_context.avg_isr_time_us = isr_time_us;
    } else {
        g_ptask_context.avg_isr_time_us = (g_ptask_context.avg_isr_time_us * 7 + isr_time_us) / 8;
    }
    
    /* Update maximum ISR time */
    if (isr_time_us > g_ptask_context.max_isr_time_us) {
        g_ptask_context.max_isr_time_us = isr_time_us;
    }
    
    /* Update CLI timing */
    if (g_ptask_context.avg_cli_time_us == 0) {
        g_ptask_context.avg_cli_time_us = cli_time_us;
    } else {
        g_ptask_context.avg_cli_time_us = (g_ptask_context.avg_cli_time_us * 7 + cli_time_us) / 8;
    }
    
    /* Update maximum CLI time */
    if (cli_time_us > g_ptask_context.max_cli_time_us) {
        g_ptask_context.max_cli_time_us = cli_time_us;
    }
}

/**
 * @brief Get module context pointer
 * 
 * @return Pointer to module context
 */
ptask_context_t* ptask_get_context(void) {
    return &g_ptask_context;
}

/**
 * @brief Log module information
 */
void ptask_log_module_info(void) {
    LOG_INFO("PTASK.MOD Status Report:");
    LOG_INFO("  Hardware Type: %d", g_ptask_context.hardware_type);
    LOG_INFO("  I/O Base: 0x%X", g_ptask_context.io_base);
    LOG_INFO("  IRQ: %d", g_ptask_context.irq);
    LOG_INFO("  MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             g_ptask_context.mac_address[0], g_ptask_context.mac_address[1],
             g_ptask_context.mac_address[2], g_ptask_context.mac_address[3],
             g_ptask_context.mac_address[4], g_ptask_context.mac_address[5]);
    LOG_INFO("  Packets TX: %lu, RX: %lu", 
             g_ptask_context.packets_sent, g_ptask_context.packets_received);
    LOG_INFO("  ISR Time: avg=%d μs, max=%d μs",
             g_ptask_context.avg_isr_time_us, g_ptask_context.max_isr_time_us);
}

/**
 * @brief Validate API parameters
 * 
 * @param params Parameter pointer
 * @param param_size Expected parameter size
 * @return SUCCESS if valid, negative error code if invalid
 */
int ptask_validate_parameters(void far *params, uint16_t param_size) {
    if (!params) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Basic pointer validation - check if pointer is reasonable */
    if ((uint32_t)params < 0x1000 || (uint32_t)params > 0xFFFFF) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Size validation */
    if (param_size == 0 || param_size > 1024) {
        return ERROR_INVALID_PARAM;
    }
    
    return SUCCESS;
}