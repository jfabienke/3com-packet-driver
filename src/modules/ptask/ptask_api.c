/**
 * @file ptask_api.c
 * @brief PTASK.MOD API Implementation (Simplified Wrapper)
 * 
 * REFACTORED IMPLEMENTATION - Simple wrapper around existing driver
 * 
 * This file provides PTASK-specific API parameter structures and
 * validation, then delegates to the module bridge infrastructure
 * which connects to the existing 3C509B driver.
 */

#include "../common/module_bridge.h"
#include "ptask_internal.h"

/* External references */
extern module_bridge_t g_ptask_bridge;

/**
 * @brief PTASK-specific hardware detection parameters
 */
typedef struct {
    uint8_t nic_index;                     /* NIC index (0 for PTASK) */
    uint16_t detected_hardware;            /* Hardware type detected */
    uint32_t io_base;                      /* I/O base address */
    uint8_t irq;                          /* IRQ line */
    uint8_t mac_address[6];               /* MAC address */
} __attribute__((packed)) ptask_detect_params_t;

/**
 * @brief PTASK-specific send parameters
 */
typedef struct {
    uint8_t nic_index;                     /* Always 0 for PTASK */
    void far *packet_data;                 /* Packet data pointer */
    uint16_t packet_length;                /* Packet length */
    uint16_t packet_type;                  /* Ethernet type */
} __attribute__((packed)) ptask_send_params_t;

/**
 * @brief PTASK-specific receive parameters
 */
typedef struct {
    uint8_t nic_index;                     /* Always 0 for PTASK */
    void far *buffer;                      /* Receive buffer */
    uint16_t buffer_size;                  /* Buffer size */
    uint16_t bytes_received;               /* Actual bytes received */
    uint16_t packet_type;                  /* Ethernet type */
} __attribute__((packed)) ptask_recv_params_t;

/**
 * @brief PTASK-specific statistics parameters
 */
typedef struct {
    uint8_t nic_index;                     /* Always 0 for PTASK */
    uint32_t packets_sent;                 /* Packets transmitted */
    uint32_t packets_received;             /* Packets received */
    uint32_t tx_errors;                    /* Transmission errors */
    uint32_t rx_errors;                    /* Reception errors */
    uint32_t interrupts;                   /* Interrupt count */
    uint32_t isr_avg_timing_us;            /* Average ISR time */
} __attribute__((packed)) ptask_stats_params_t;

/**
 * @brief API function: Hardware detection
 * 
 * PTASK supports only one NIC (3C509B), so this function returns
 * the detection results from module initialization.
 * 
 * @param params Detection parameters structure
 * @return SUCCESS on success, negative error code on failure
 */
int ptask_api_detect_hardware(ptask_detect_params_t far *params) {
    if (!params || params->nic_index != 0) {
        return ERROR_INVALID_PARAM;
    }
    
    if (g_ptask_bridge.module_state != MODULE_STATE_ACTIVE) {
        return ERROR_MODULE_NOT_READY;
    }
    
    /* Return cached detection results */
    module_init_context_t *ctx = g_ptask_bridge.init_context;
    
    params->detected_hardware = PTASK_HARDWARE_3C509B;
    params->io_base = ctx->detected_io_base;
    params->irq = ctx->detected_irq;
    memcpy(params->mac_address, ctx->mac_address, 6);
    
    LOG_DEBUG("PTASK API: Hardware detection - 3C509B at I/O 0x%X, IRQ %d",
              params->io_base, params->irq);
    
    return SUCCESS;
}

/**
 * @brief API function: Send packet
 * 
 * Validates PTASK-specific parameters and delegates to bridge.
 * 
 * @param params Send parameters structure
 * @return SUCCESS on success, negative error code on failure
 */
int ptask_api_send_packet(ptask_send_params_t far *params) {
    if (!params || params->nic_index != 0 || !params->packet_data) {
        return ERROR_INVALID_PARAM;
    }
    
    if (params->packet_length == 0 || params->packet_length > 1514) {
        return ERROR_INVALID_PACKET_SIZE;
    }
    
    /* Delegate to bridge infrastructure */
    return module_bridge_send_packet(&g_ptask_bridge,
                                   params->packet_data,
                                   params->packet_length);
}

/**
 * @brief API function: Receive packet
 * 
 * Validates PTASK-specific parameters and delegates to bridge.
 * 
 * @param params Receive parameters structure
 * @return SUCCESS on success, negative error code on failure
 */
int ptask_api_receive_packet(ptask_recv_params_t far *params) {
    if (!params || params->nic_index != 0 || !params->buffer) {
        return ERROR_INVALID_PARAM;
    }
    
    if (params->buffer_size == 0) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Delegate to bridge infrastructure */
    return module_bridge_receive_packet(&g_ptask_bridge,
                                      params->buffer,
                                      params->buffer_size,
                                      &params->bytes_received);
}

/**
 * @brief API function: Get statistics
 * 
 * Returns PTASK-specific statistics from the bridge.
 * 
 * @param params Statistics parameters structure
 * @return SUCCESS on success, negative error code on failure
 */
int ptask_api_get_statistics(ptask_stats_params_t far *params) {
    if (!params || params->nic_index != 0) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Get statistics from bridge */
    return module_bridge_get_statistics(&g_ptask_bridge, params);
}

/**
 * @brief API function: Configure NIC
 * 
 * PTASK configuration is handled during initialization.
 * This function validates parameters but doesn't change settings.
 * 
 * @param params Configuration parameters
 * @return SUCCESS (configuration fixed at init)
 */
int ptask_api_configure(void far *params) {
    /* PTASK has fixed configuration set during initialization */
    /* All settings come from existing 3C509B driver configuration */
    
    LOG_DEBUG("PTASK API: Configuration request - using existing driver settings");
    
    return SUCCESS;
}