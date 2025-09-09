/**
 * @file packet_ops_capabilities.c
 * @brief Capability-aware packet operations
 *
 * This file provides capability-driven packet transmission and reception
 * operations that adapt to the specific features of each NIC model.
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include "../include/packet_ops.h"
#include "../include/nic_capabilities.h"
#include "../include/hardware.h"
#include "../include/logging.h"
#include "../include/buffer_alloc.h"
#include "../include/stats.h"
#include <string.h>

/* ========================================================================== */
/* CAPABILITY-AWARE PACKET TRANSMISSION                                      */
/* ========================================================================== */

/**
 * @brief Send packet using capability-optimized path
 * @param interface_num NIC interface number
 * @param packet_data Packet data
 * @param length Packet length
 * @param dest_addr Destination MAC address
 * @param handle Sender handle
 * @return 0 on success, negative on error
 */
int packet_send_with_capabilities(uint8_t interface_num, const uint8_t *packet_data, 
                                 uint16_t length, const uint8_t *dest_addr, uint16_t handle) {
    nic_context_t *ctx;
    buffer_desc_t *buffer;
    uint8_t *frame_buffer;
    int result;
    uint16_t frame_length;
    
    if (!packet_data || length == 0 || !dest_addr) {
        LOG_ERROR("packet_send_with_capabilities: Invalid parameters");
        return PACKET_ERR_INVALID_PARAM;
    }
    
    LOG_DEBUG("Capability-aware packet send: interface=%d, length=%d, handle=%04X", 
             interface_num, length, handle);
    
    /* Get NIC context for capability-aware operations */
    ctx = hardware_get_nic_context(interface_num);
    if (!ctx) {
        LOG_ERROR("Invalid interface number or no context: %d", interface_num);
        return PACKET_ERR_INVALID_NIC;
    }
    
    /* Validate packet size against NIC capabilities */
    if (length < ctx->info->min_packet_size || length > ctx->info->max_packet_size) {
        LOG_ERROR("Invalid packet size %d for %s (min=%d, max=%d)", 
                 length, ctx->info->name, ctx->info->min_packet_size, ctx->info->max_packet_size);
        return PACKET_ERR_INVALID_SIZE;
    }
    
    /* Calculate frame size */
    frame_length = ETH_HEADER_LEN + length;
    if (frame_length < ETH_MIN_FRAME) {
        frame_length = ETH_MIN_FRAME;
    }
    
    /* Use capability-specific buffer allocation */
    if (nic_has_capability(ctx, NIC_CAP_BUSMASTER)) {
        /* Allocate DMA-capable buffer with proper alignment */
        buffer = buffer_alloc_dma_aligned(frame_length, ctx->info->buffer_alignment);
        LOG_DEBUG("Using DMA-aligned buffer for bus mastering NIC");
    } else {
        /* Standard buffer allocation for PIO NICs */
        buffer = buffer_alloc_ethernet_frame(frame_length, BUFFER_TYPE_TX);
        LOG_DEBUG("Using standard buffer for PIO NIC");
    }
    
    if (!buffer) {
        LOG_ERROR("Failed to allocate transmit buffer");
        return PACKET_ERR_NO_BUFFERS;
    }
    
    frame_buffer = (uint8_t*)buffer_get_data_ptr(buffer);
    if (!frame_buffer) {
        buffer_free_any(buffer);
        return PACKET_ERR_NO_BUFFERS;
    }
    
    /* Build Ethernet frame with capability-aware optimizations */
    if (nic_has_capability(ctx, NIC_CAP_DIRECT_PIO)) {
        /* Use optimized frame building for direct PIO NICs */
        result = packet_build_frame_direct_pio(frame_buffer, frame_length, 
                                             dest_addr, ctx->mac,
                                             0x0800, packet_data, length);
        LOG_DEBUG("Using direct PIO frame building optimization");
    } else {
        /* Standard frame building */
        result = packet_build_ethernet_frame_optimized(frame_buffer, frame_length,
                                                     dest_addr, ctx->mac,
                                                     0x0800, packet_data, length);
        LOG_DEBUG("Using standard frame building");
    }
    
    if (result < 0) {
        LOG_ERROR("Failed to build Ethernet frame: %d", result);
        buffer_free_any(buffer);
        return result;
    }
    
    /* Send using capability-appropriate method */
    result = packet_transmit_with_capabilities(ctx, frame_buffer, frame_length);
    
    if (result == SUCCESS) {
        LOG_DEBUG("Packet sent successfully via %s (frame_size=%d)", 
                 ctx->info->name, frame_length);
    } else {
        LOG_ERROR("Packet transmission failed: %d", result);
    }
    
    /* Free buffer */
    buffer_free_any(buffer);
    
    return result;
}

/**
 * @brief Transmit packet using capability-specific method
 * @param ctx NIC context
 * @param frame_data Frame data to transmit
 * @param frame_length Frame length
 * @return 0 on success, negative on error
 */
static int packet_transmit_with_capabilities(nic_context_t *ctx, const uint8_t *frame_data, uint16_t frame_length) {
    int result;
    
    /* Use capability-specific transmission path */
    if (nic_has_capability(ctx, NIC_CAP_BUSMASTER)) {
        /* DMA-based transmission for bus mastering NICs */
        result = packet_transmit_dma(ctx, frame_data, frame_length);
        LOG_DEBUG("Used DMA transmission path");
    } else if (nic_has_capability(ctx, NIC_CAP_DIRECT_PIO)) {
        /* Optimized PIO transmission for 3C509B */
        result = packet_transmit_direct_pio(ctx, frame_data, frame_length);
        LOG_DEBUG("Used direct PIO transmission path");
    } else {
        /* Standard PIO transmission */
        result = packet_transmit_standard_pio(ctx, frame_data, frame_length);
        LOG_DEBUG("Used standard PIO transmission path");
    }
    
    /* Apply any post-transmission optimizations */
    if (result == SUCCESS) {
        if (nic_has_capability(ctx, NIC_CAP_INTERRUPT_MIT)) {
            /* Adjust interrupt mitigation based on traffic */
            packet_adjust_interrupt_mitigation(ctx);
        }
        
        /* Update capability-specific statistics */
        packet_update_capability_stats(ctx, true, true);
    } else {
        packet_update_capability_stats(ctx, true, false);
    }
    
    return result;
}

/**
 * @brief DMA-based packet transmission
 * @param ctx NIC context
 * @param frame_data Frame data
 * @param frame_length Frame length
 * @return 0 on success, negative on error
 */
static int packet_transmit_dma(nic_context_t *ctx, const uint8_t *frame_data, uint16_t frame_length) {
    /* This would implement DMA-specific transmission for 3C515-TX */
    
    LOG_DEBUG("DMA transmission: %d bytes", frame_length);
    
    /* Ensure DMA is configured */
    if (ctx->info->vtable && ctx->info->vtable->configure_busmaster) {
        int dma_result = ctx->info->vtable->configure_busmaster(ctx, true);
        if (dma_result != NIC_CAP_SUCCESS) {
            LOG_WARNING("DMA configuration failed, falling back to PIO");
            return packet_transmit_standard_pio(ctx, frame_data, frame_length);
        }
    }
    
    /* Use vtable for actual transmission */
    if (ctx->info->vtable && ctx->info->vtable->send_packet) {
        return ctx->info->vtable->send_packet(ctx, frame_data, frame_length);
    }
    
    return ERROR_NOT_SUPPORTED;
}

/**
 * @brief Direct PIO packet transmission with optimizations
 * @param ctx NIC context
 * @param frame_data Frame data
 * @param frame_length Frame length
 * @return 0 on success, negative on error
 */
static int packet_transmit_direct_pio(nic_context_t *ctx, const uint8_t *frame_data, uint16_t frame_length) {
    /* This would implement optimized PIO transmission for 3C509B */
    
    LOG_DEBUG("Direct PIO transmission: %d bytes", frame_length);
    
    /* Apply direct PIO optimizations */
    /* - Use wider data transfers where possible
     * - Minimize I/O port accesses
     * - Use block transfers for larger packets
     */
    
    /* Use vtable for actual transmission */
    if (ctx->info->vtable && ctx->info->vtable->send_packet) {
        return ctx->info->vtable->send_packet(ctx, frame_data, frame_length);
    }
    
    return ERROR_NOT_SUPPORTED;
}

/**
 * @brief Standard PIO packet transmission
 * @param ctx NIC context
 * @param frame_data Frame data
 * @param frame_length Frame length
 * @return 0 on success, negative on error
 */
static int packet_transmit_standard_pio(nic_context_t *ctx, const uint8_t *frame_data, uint16_t frame_length) {
    LOG_DEBUG("Standard PIO transmission: %d bytes", frame_length);
    
    /* Use vtable for actual transmission */
    if (ctx->info->vtable && ctx->info->vtable->send_packet) {
        return ctx->info->vtable->send_packet(ctx, frame_data, frame_length);
    }
    
    return ERROR_NOT_SUPPORTED;
}

/* ========================================================================== */
/* CAPABILITY-AWARE PACKET RECEPTION                                         */
/* ========================================================================== */

/**
 * @brief Receive packet using capability-optimized path
 * @param interface_num NIC interface number
 * @param buffer Buffer for received packet
 * @param buffer_size Size of buffer
 * @param received_length Actual packet length received
 * @param src_addr Source MAC address
 * @return 0 on success, negative on error
 */
int packet_receive_with_capabilities(uint8_t interface_num, uint8_t *buffer, uint16_t buffer_size,
                                    uint16_t *received_length, uint8_t *src_addr) {
    nic_context_t *ctx;
    uint8_t *packet_ptr;
    uint16_t packet_length;
    int result;
    
    if (!buffer || !received_length || buffer_size == 0) {
        LOG_ERROR("packet_receive_with_capabilities: Invalid parameters");
        return PACKET_ERR_INVALID_PARAM;
    }
    
    /* Get NIC context */
    ctx = hardware_get_nic_context(interface_num);
    if (!ctx) {
        LOG_ERROR("Invalid interface number or no context: %d", interface_num);
        return PACKET_ERR_INVALID_NIC;
    }
    
    /* Use capability-specific reception method */
    result = packet_receive_with_capability_optimization(ctx, &packet_ptr, &packet_length);
    if (result != SUCCESS) {
        return result;
    }
    
    /* Apply copybreak optimization if supported and beneficial */
    if (nic_has_capability(ctx, NIC_CAP_RX_COPYBREAK) && 
        packet_length <= ctx->copybreak_threshold) {
        
        result = packet_apply_copybreak_optimization(ctx, packet_ptr, packet_length,
                                                   buffer, buffer_size, received_length);
        LOG_DEBUG("Applied RX copybreak optimization for %d byte packet", packet_length);
    } else {
        /* Standard copy */
        result = packet_copy_received_data(packet_ptr, packet_length,
                                         buffer, buffer_size, received_length);
        LOG_DEBUG("Used standard packet copy for %d byte packet", packet_length);
    }
    
    /* Extract source address if requested */
    if (src_addr && packet_length >= ETH_HEADER_LEN) {
        memcpy(src_addr, packet_ptr + 6, 6);  /* Source MAC is at offset 6 */
    }
    
    /* Update capability-specific statistics */
    if (result == SUCCESS) {
        packet_update_capability_stats(ctx, false, true);
    } else {
        packet_update_capability_stats(ctx, false, false);
    }
    
    return result;
}

/**
 * @brief Receive packet using capability-optimized method
 * @param ctx NIC context
 * @param packet Pointer to received packet data
 * @param length Received packet length
 * @return 0 on success, negative on error
 */
static int packet_receive_with_capability_optimization(nic_context_t *ctx, uint8_t **packet, uint16_t *length) {
    int result;
    
    /* Use capability-specific reception path */
    if (nic_has_capability(ctx, NIC_CAP_BUSMASTER)) {
        /* DMA-based reception for bus mastering NICs */
        result = packet_receive_dma(ctx, packet, length);
        LOG_DEBUG("Used DMA reception path");
    } else if (nic_has_capability(ctx, NIC_CAP_DIRECT_PIO)) {
        /* Optimized PIO reception for 3C509B */
        result = packet_receive_direct_pio(ctx, packet, length);
        LOG_DEBUG("Used direct PIO reception path");
    } else {
        /* Standard PIO reception */
        result = packet_receive_standard_pio(ctx, packet, length);
        LOG_DEBUG("Used standard PIO reception path");
    }
    
    /* Apply post-reception optimizations */
    if (result == SUCCESS && nic_has_capability(ctx, NIC_CAP_INTERRUPT_MIT)) {
        /* Adjust interrupt mitigation based on reception rate */
        packet_adjust_interrupt_mitigation(ctx);
    }
    
    return result;
}

/**
 * @brief Apply RX copybreak optimization
 * @param ctx NIC context
 * @param packet_data Source packet data
 * @param packet_length Source packet length
 * @param buffer Destination buffer
 * @param buffer_size Destination buffer size
 * @param copied_length Actual copied length
 * @return 0 on success, negative on error
 */
static int packet_apply_copybreak_optimization(nic_context_t *ctx, const uint8_t *packet_data, 
                                             uint16_t packet_length, uint8_t *buffer, 
                                             uint16_t buffer_size, uint16_t *copied_length) {
    if (packet_length > buffer_size) {
        LOG_WARNING("Packet too large for buffer: %d > %d", packet_length, buffer_size);
        return PACKET_ERR_BUFFER_TOO_SMALL;
    }
    
    /* Use optimized copy for small packets */
    if (packet_length <= ctx->copybreak_threshold) {
        /* Fast copy optimization - could use assembly or optimized routines */
        packet_fast_copy_small(buffer, packet_data, packet_length);
        LOG_DEBUG("Used fast copy for %d byte packet (threshold=%d)", 
                 packet_length, ctx->copybreak_threshold);
    } else {
        /* Standard copy for larger packets */
        memcpy(buffer, packet_data, packet_length);
        LOG_DEBUG("Used standard copy for %d byte packet", packet_length);
    }
    
    *copied_length = packet_length;
    return SUCCESS;
}

/* ========================================================================== */
/* CAPABILITY-SPECIFIC OPTIMIZATIONS                                         */
/* ========================================================================== */

/**
 * @brief Adjust interrupt mitigation based on traffic patterns
 * @param ctx NIC context
 */
static void packet_adjust_interrupt_mitigation(nic_context_t *ctx) {
    static uint32_t last_adjust_time = 0;
    static uint32_t last_packet_count = 0;
    uint32_t current_time = stats_get_timestamp();
    uint32_t current_packets = ctx->packets_sent + ctx->packets_received;
    
    /* Only adjust every 100ms */
    if (current_time - last_adjust_time < 100) {
        return;
    }
    
    /* Calculate packet rate */
    uint32_t packet_rate = current_packets - last_packet_count;
    
    /* Adjust mitigation based on packet rate */
    if (packet_rate > 1000) {
        /* High traffic - increase mitigation */
        ctx->interrupt_mitigation = MIN(ctx->interrupt_mitigation + 10, 500);
    } else if (packet_rate < 100) {
        /* Low traffic - decrease mitigation for latency */
        ctx->interrupt_mitigation = MAX(ctx->interrupt_mitigation - 10, 50);
    }
    
    LOG_DEBUG("Adjusted interrupt mitigation to %d µs (packet rate: %u/100ms)",
             ctx->interrupt_mitigation, packet_rate);
    
    last_adjust_time = current_time;
    last_packet_count = current_packets;
}

/**
 * @brief Update capability-specific statistics
 * @param ctx NIC context
 * @param is_transmit True for transmit, false for receive
 * @param success True if operation was successful
 */
static void packet_update_capability_stats(nic_context_t *ctx, bool is_transmit, bool success) {
    if (success) {
        if (is_transmit) {
            ctx->packets_sent++;
        } else {
            ctx->packets_received++;
        }
    } else {
        ctx->errors++;
    }
    
    /* Update capability-specific counters */
    /* This would be expanded to track specific capability usage */
}

/**
 * @brief Fast copy optimization for small packets
 * @param dest Destination buffer
 * @param src Source buffer
 * @param length Length to copy
 */
static void packet_fast_copy_small(uint8_t *dest, const uint8_t *src, uint16_t length) {
    /* This could be implemented with optimized assembly or SIMD instructions */
    /* For now, use standard memcpy */
    memcpy(dest, src, length);
}

/**
 * @brief Build Ethernet frame with direct PIO optimizations
 * @param frame_buffer Destination frame buffer
 * @param frame_length Total frame length
 * @param dest_mac Destination MAC address
 * @param src_mac Source MAC address
 * @param ethertype Ethernet type
 * @param payload Payload data
 * @param payload_length Payload length
 * @return Frame length on success, negative on error
 */
static int packet_build_frame_direct_pio(uint8_t *frame_buffer, uint16_t frame_length,
                                        const uint8_t *dest_mac, const uint8_t *src_mac,
                                        uint16_t ethertype, const uint8_t *payload, 
                                        uint16_t payload_length) {
    if (!frame_buffer || !dest_mac || !src_mac || !payload) {
        return PACKET_ERR_INVALID_PARAM;
    }
    
    if (frame_length < ETH_HEADER_LEN + payload_length) {
        return PACKET_ERR_BUFFER_TOO_SMALL;
    }
    
    /* Build Ethernet header with optimizations */
    /* Use wider writes where possible for better performance */
    uint16_t *frame16 = (uint16_t*)frame_buffer;
    const uint16_t *dest16 = (const uint16_t*)dest_mac;
    const uint16_t *src16 = (const uint16_t*)src_mac;
    
    /* Copy destination MAC (6 bytes = 3 16-bit writes) */
    frame16[0] = dest16[0];
    frame16[1] = dest16[1];
    frame16[2] = dest16[2];
    
    /* Copy source MAC (6 bytes = 3 16-bit writes) */
    frame16[3] = src16[0];
    frame16[4] = src16[1];
    frame16[5] = src16[2];
    
    /* Set ethertype */
    frame16[6] = htons(ethertype);
    
    /* Copy payload */
    memcpy(frame_buffer + ETH_HEADER_LEN, payload, payload_length);
    
    /* Pad to minimum frame size if necessary */
    uint16_t total_length = ETH_HEADER_LEN + payload_length;
    if (total_length < ETH_MIN_FRAME) {
        memset(frame_buffer + total_length, 0, ETH_MIN_FRAME - total_length);
        total_length = ETH_MIN_FRAME;
    }
    
    return total_length;
}

/* ========================================================================== */
/* COMPATIBILITY WRAPPERS                                                    */
/* ========================================================================== */

/**
 * @brief Enhanced packet send that automatically uses capabilities
 * @param interface_num NIC interface number
 * @param packet_data Packet data
 * @param length Packet length
 * @param dest_addr Destination MAC address
 * @param handle Sender handle
 * @return 0 on success, negative on error
 */
int packet_send_enhanced_caps(uint8_t interface_num, const uint8_t *packet_data, 
                             uint16_t length, const uint8_t *dest_addr, uint16_t handle) {
    /* Check if capability system is available */
    nic_context_t *ctx = hardware_get_nic_context(interface_num);
    if (ctx) {
        /* Use capability-aware transmission */
        return packet_send_with_capabilities(interface_num, packet_data, length, dest_addr, handle);
    } else {
        /* Fall back to legacy transmission */
        LOG_DEBUG("Falling back to legacy packet transmission");
        return packet_send_enhanced(interface_num, packet_data, length, dest_addr, handle);
    }
}

/**
 * @brief Enhanced packet receive that automatically uses capabilities
 * @param interface_num NIC interface number
 * @param buffer Buffer for received packet
 * @param buffer_size Size of buffer
 * @param received_length Actual packet length received
 * @param src_addr Source MAC address
 * @return 0 on success, negative on error
 */
int packet_receive_enhanced_caps(uint8_t interface_num, uint8_t *buffer, uint16_t buffer_size,
                                uint16_t *received_length, uint8_t *src_addr) {
    /* Check if capability system is available */
    nic_context_t *ctx = hardware_get_nic_context(interface_num);
    if (ctx) {
        /* Use capability-aware reception */
        return packet_receive_with_capabilities(interface_num, buffer, buffer_size, 
                                              received_length, src_addr);
    } else {
        /* Fall back to legacy reception */
        LOG_DEBUG("Falling back to legacy packet reception");
        /* This would call the existing legacy function */
        return ERROR_NOT_SUPPORTED;  /* Placeholder */
    }
}