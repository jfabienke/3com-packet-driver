/**
 * @file network_logging.c
 * @brief Network Logging Implementation
 * 
 * Simple UDP-based remote logging system for DOS packet driver debugging.
 */

#include "network_logging.h"
#include "timer_services.h"
#include "../include/logging.h"
#include <string.h>
#include <stdio.h>

/* Network logging state */
static netlog_config_t g_netlog_config = {0};
static uint16_t g_netlog_sequence = 0;
static uint16_t g_packets_sent = 0;
static uint16_t g_send_errors = 0;
static uint8_t g_netlog_initialized = 0;

/**
 * @brief Parse IP address from string
 */
static uint32_t parse_ip_address(const char *ip_str)
{
    uint32_t ip = 0;
    int a, b, c, d;
    
    if (sscanf(ip_str, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
        if (a >= 0 && a <= 255 && b >= 0 && b <= 255 &&
            c >= 0 && c <= 255 && d >= 0 && d <= 255) {
            /* Network byte order */
            ip = ((uint32_t)a << 24) | ((uint32_t)b << 16) | 
                 ((uint32_t)c << 8) | (uint32_t)d;
        }
    }
    
    return ip;
}

/**
 * @brief Simple checksum calculation
 */
static uint16_t calculate_checksum(const void *data, uint16_t length)
{
    const uint8_t *ptr = (const uint8_t *)data;
    uint32_t sum = 0;
    uint16_t i;
    
    for (i = 0; i < length; i++) {
        sum += ptr[i];
    }
    
    return (uint16_t)(sum & 0xFFFF);
}

/**
 * @brief Initialize network logging system
 */
int netlog_init(uint32_t dest_ip, uint16_t dest_port)
{
    if (g_netlog_initialized) {
        return NETLOG_SUCCESS;
    }
    
    memset(&g_netlog_config, 0, sizeof(g_netlog_config));
    
    g_netlog_config.dest_ip = dest_ip;
    g_netlog_config.dest_port = dest_port;
    g_netlog_config.source_port = NETLOG_DEFAULT_PORT + 1000; /* Use different source port */
    g_netlog_config.enabled = (dest_ip != 0) ? 1 : 0;
    g_netlog_config.hw_available = 0; /* Will be set by hardware layer */
    
    g_netlog_sequence = 0;
    g_packets_sent = 0;
    g_send_errors = 0;
    
    g_netlog_initialized = 1;
    
    if (g_netlog_config.enabled) {
        LOG_INFO("Network logging initialized to %08lX:%u", dest_ip, dest_port);
    } else {
        LOG_DEBUG("Network logging disabled");
    }
    
    return NETLOG_SUCCESS;
}

/**
 * @brief Configure network logging from string
 */
int netlog_configure(const char *config_str)
{
    uint32_t ip;
    uint16_t port;
    char ip_str[16];
    
    if (!config_str) {
        return NETLOG_ERROR_INVALID;
    }
    
    if (strcmp(config_str, "off") == 0 || strcmp(config_str, "OFF") == 0) {
        return netlog_init(0, 0);  /* Disable */
    }
    
    /* Parse "IP:PORT" format */
    if (sscanf(config_str, "%15[^:]:%u", ip_str, &port) == 2) {
        ip = parse_ip_address(ip_str);
        if (ip != 0 && port > 0 && port < 65536) {
            return netlog_init(ip, port);
        }
    }
    
    return NETLOG_ERROR_CONFIG;
}

/**
 * @brief Create network log packet
 */
static void create_log_packet(netlog_packet_t *packet, uint8_t level, 
                             uint8_t category, const char *message)
{
    memset(packet, 0, sizeof(netlog_packet_t));
    
    packet->magic = NETLOG_MAGIC;
    packet->timestamp = get_millisecond_timestamp();
    packet->sequence = ++g_netlog_sequence;
    packet->level = level;
    packet->category = category;
    
    /* Copy message with truncation */
    if (message) {
        uint16_t msg_len = strlen(message);
        if (msg_len >= sizeof(packet->message)) {
            msg_len = sizeof(packet->message) - 1;
        }
        memcpy(packet->message, message, msg_len);
        packet->message[msg_len] = '\0';
        packet->length = msg_len;
    } else {
        packet->length = 0;
    }
}

/**
 * @brief Send raw packet via network hardware (stub)
 * 
 * In a real implementation, this would interface with the packet driver
 * to send UDP packets. For now, it's a stub that simulates sending.
 */
static int send_udp_packet(const netlog_packet_t *packet)
{
    /* This is a simplified stub implementation */
    /* Real implementation would:
     * 1. Build Ethernet header
     * 2. Build IP header with dest_ip
     * 3. Build UDP header with dest_port/source_port
     * 4. Encapsulate packet data
     * 5. Send via NIC hardware
     */
    
    if (!g_netlog_config.hw_available) {
        g_send_errors++;
        return NETLOG_ERROR_NETWORK;
    }
    
    /* Simulate network send delay and potential errors */
    if ((g_netlog_sequence % 50) == 0) {
        /* Simulate 2% packet loss */
        g_send_errors++;
        return NETLOG_ERROR_NETWORK;
    }
    
    g_packets_sent++;
    return NETLOG_SUCCESS;
}

/**
 * @brief Send log message via UDP
 */
int netlog_send_message(uint8_t level, uint8_t category, const char *message)
{
    netlog_packet_t packet;
    
    if (!g_netlog_initialized || !g_netlog_config.enabled || !message) {
        return NETLOG_ERROR_INVALID;
    }
    
    /* Create log packet */
    create_log_packet(&packet, level, category, message);
    
    /* Send packet */
    int result = send_udp_packet(&packet);
    
    if (result != NETLOG_SUCCESS) {
        /* Don't log network errors to avoid recursion */
        return result;
    }
    
    return NETLOG_SUCCESS;
}

/**
 * @brief Check if network logging is available
 */
int netlog_is_available(void)
{
    return (g_netlog_initialized && g_netlog_config.enabled && g_netlog_config.hw_available);
}

/**
 * @brief Enable/disable hardware availability
 */
void netlog_set_hw_available(int available)
{
    g_netlog_config.hw_available = available ? 1 : 0;
    
    if (available && g_netlog_config.enabled) {
        LOG_DEBUG("Network logging hardware available");
    }
}

/**
 * @brief Get network logging statistics
 */
void netlog_get_stats(uint16_t *packets_sent, uint16_t *send_errors, uint16_t *sequence)
{
    if (packets_sent) *packets_sent = g_packets_sent;
    if (send_errors) *send_errors = g_send_errors;
    if (sequence) *sequence = g_netlog_sequence;
}

/**
 * @brief Cleanup network logging
 */
void netlog_cleanup(void)
{
    if (!g_netlog_initialized) {
        return;
    }
    
    /* Send final shutdown message */
    if (g_netlog_config.enabled && g_netlog_config.hw_available) {
        netlog_send_message(2, 0x80, "Network logging shutdown");
    }
    
    memset(&g_netlog_config, 0, sizeof(g_netlog_config));
    g_netlog_initialized = 0;
    
    LOG_DEBUG("Network logging cleanup complete (sent=%u, errors=%u)", 
              g_packets_sent, g_send_errors);
}