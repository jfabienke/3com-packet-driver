/**
 * @file promisc.c
 * @brief Promiscuous mode support with advanced packet capture and filtering
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#include "promisc.h"
#include "3c509b.h"
#include "3c515.h"
#include "logging.h"
#include "memory.h"
#include "diag.h"
#include "routing.h"
#include "api.h"
#include <string.h>

/* Global promiscuous mode state */
promisc_config_t g_promisc_config;
promiscuous_stats_t g_promisc_stats;
promisc_packet_buffer_t HUGE g_promisc_buffers[PROMISC_BUFFER_COUNT];
promisc_filter_t g_promisc_filters[PROMISC_MAX_FILTERS];
promisc_app_handle_t g_promisc_apps[PROMISC_MAX_APPLICATIONS];
volatile uint32_t g_promisc_buffer_head = 0;
volatile uint32_t g_promisc_buffer_tail = 0;

/* Private state */
static int g_promisc_initialized = 0;
static uint16_t g_next_handle_id = 1;
static uint32_t g_packet_counter = 0;

/* Internal helper functions */
static uint32_t promisc_get_timestamp(void);
static int promisc_buffer_is_full(void);
static int promisc_buffer_is_empty(void);
static uint32_t promisc_advance_buffer_index(uint32_t index);
static void promisc_add_buffer_packet(const uint8_t *packet, uint16_t length,
                                     uint8_t nic_index, uint8_t filter_matched);
static int promisc_check_filter_match(const promisc_filter_t *filter,
                                       const uint8_t *packet, uint16_t length);
static void promisc_classify_and_update_stats(const uint8_t *packet, uint16_t length);

/* Core promiscuous mode functions */
int promisc_init(void) {
    if (g_promisc_initialized) {
        return SUCCESS;
    }
    
    LOG_INFO("Initializing promiscuous mode system");
    
    /* Initialize configuration with defaults */
    memset(&g_promisc_config, 0, sizeof(promisc_config_t));
    g_promisc_config.level = PROMISC_LEVEL_OFF;
    g_promisc_config.enabled = false;
    g_promisc_config.buffer_count = PROMISC_BUFFER_COUNT;
    g_promisc_config.capture_timeout_ms = 5000;
    g_promisc_config.learning_mode = 1;
    g_promisc_config.integration_mode = 1;

    /* Clear statistics */
    promisc_clear_stats();

    /* Initialize buffers - use loop for huge array */
    {
        int idx;
        for (idx = 0; idx < PROMISC_BUFFER_COUNT; idx++) {
            FARMEMSET(&g_promisc_buffers[idx], 0, sizeof(promisc_packet_buffer_t));
        }
    }
    g_promisc_buffer_head = 0;
    g_promisc_buffer_tail = 0;

    /* Initialize filters */
    memset(g_promisc_filters, 0, sizeof(g_promisc_filters));

    /* Initialize application handles */
    memset(g_promisc_apps, 0, sizeof(g_promisc_apps));
    
    g_promisc_initialized = true;
    
    LOG_INFO("Promiscuous mode system initialized successfully");
    
    return SUCCESS;
}

void promisc_cleanup(void) {
    int i;

    if (!g_promisc_initialized) {
        return;
    }

    LOG_INFO("Cleaning up promiscuous mode system");

    /* Disable promiscuous mode on all NICs */
    for (i = 0; i < hardware_get_nic_count(); i++) {
        nic_info_t *nic = hardware_get_nic(i);
        if (nic && promisc_is_enabled(nic)) {
            promisc_disable(nic);
        }
    }

    /* Clear all filters and applications */
    promisc_clear_filters();

    for (i = 0; i < PROMISC_MAX_APPLICATIONS; i++) {
        if (g_promisc_apps[i].active) {
            promisc_unregister_application(g_promisc_apps[i].handle_id);
        }
    }
    
    g_promisc_initialized = false;
    
    LOG_INFO("Promiscuous mode system cleaned up");
}

int promisc_enable(nic_info_t *nic, promisc_level_t level) {
    int result = ERROR_NOT_SUPPORTED;

    if (!g_promisc_initialized || !nic) {
        return ERROR_INVALID_PARAM;
    }

    LOG_INFO("Enabling promiscuous mode level %d on NIC %d", level, nic->index);

    /* Check if NIC supports promiscuous mode */
    if (!(nic->capabilities & HW_CAP_PROMISCUOUS)) {
        LOG_ERROR("NIC %d does not support promiscuous mode", nic->index);
        return ERROR_NOT_SUPPORTED;
    }

    /* Enable promiscuous mode based on NIC type */
    switch (nic->type) {
        case NIC_TYPE_3C509B:
            result = promisc_enable_3c509b(nic, level);
            break;
        case NIC_TYPE_3C515_TX:
            result = promisc_enable_3c515(nic, level);
            break;
        default:
            LOG_ERROR("Unsupported NIC type %d for promiscuous mode", nic->type);
            return ERROR_NOT_SUPPORTED;
    }
    
    if (result == SUCCESS) {
        /* Update NIC status */
        nic->status |= NIC_STATUS_PROMISCUOUS;
        
        /* Update global configuration */
        g_promisc_config.enabled = true;
        g_promisc_config.level = level;
        g_promisc_config.active_nic_mask |= (1 << nic->index);
        
        /* Integrate with Groups 3A/3B/3C if enabled */
        if (g_promisc_config.integration_mode) {
            promisc_integrate_routing();
            promisc_integrate_api();
            promisc_integrate_diagnostics();
        }
        
        LOG_INFO("Promiscuous mode enabled successfully on NIC %d", nic->index);
    } else {
        LOG_ERROR("Failed to enable promiscuous mode on NIC %d: %d", nic->index, result);
    }
    
    return result;
}

int promisc_disable(nic_info_t *nic) {
    int result = ERROR_NOT_SUPPORTED;

    if (!g_promisc_initialized || !nic) {
        return ERROR_INVALID_PARAM;
    }

    LOG_INFO("Disabling promiscuous mode on NIC %d", nic->index);

    /* Disable promiscuous mode based on NIC type */
    switch (nic->type) {
        case NIC_TYPE_3C509B:
            result = promisc_disable_3c509b(nic);
            break;
        case NIC_TYPE_3C515_TX:
            result = promisc_disable_3c515(nic);
            break;
        default:
            LOG_ERROR("Unsupported NIC type %d for promiscuous mode", nic->type);
            return ERROR_NOT_SUPPORTED;
    }
    
    if (result == SUCCESS) {
        /* Update NIC status */
        nic->status &= ~NIC_STATUS_PROMISCUOUS;
        
        /* Update global configuration */
        g_promisc_config.active_nic_mask &= ~(1 << nic->index);
        
        /* If no NICs have promiscuous mode enabled, disable globally */
        if (g_promisc_config.active_nic_mask == 0) {
            g_promisc_config.enabled = false;
            g_promisc_config.level = PROMISC_LEVEL_OFF;
        }
        
        LOG_INFO("Promiscuous mode disabled successfully on NIC %d", nic->index);
    } else {
        LOG_ERROR("Failed to disable promiscuous mode on NIC %d: %d", nic->index, result);
    }
    
    return result;
}

bool promisc_is_enabled(nic_info_t *nic) {
    if (!nic) {
        return false;
    }
    
    return (nic->status & NIC_STATUS_PROMISCUOUS) != 0;
}

/* Packet capture and processing */
int promisc_capture_packet(nic_info_t *nic, const uint8_t *packet, uint16_t length) {
    uint8_t filter_matched = 0;
    int matches_filters = 0;  /* C89: use int instead of bool */
    int i;
    promisc_packet_buffer_t HUGE *buffer;

    if (!g_promisc_initialized || !nic || !packet || length == 0) {
        return ERROR_INVALID_PARAM;
    }

    /* Check if promiscuous mode is enabled on this NIC */
    if (!promisc_is_enabled(nic)) {
        return ERROR_NOT_INITIALIZED;  /* Use existing error code */
    }

    /* Check if buffer is full */
    if (promisc_buffer_is_full()) {
        g_promisc_stats.buffer_overflows++;
        g_promisc_stats.dropped_packets++;
        return ERROR_BUFFER_FULL;
    }
    
    if (g_promisc_config.filter_count > 0) {
        matches_filters = promisc_packet_matches_filters(packet, length);
        if (matches_filters) {
            /* Find which filter matched */
            for (i = 0; i < PROMISC_MAX_FILTERS; i++) {
                if (g_promisc_filters[i].enabled &&
                    promisc_check_filter_match(&g_promisc_filters[i], packet, length)) {
                    filter_matched = (uint8_t)(i + 1);
                    break;
                }
            }
        }
    } else {
        /* No filters, capture all packets */
        matches_filters = 1;
    }

    /* Only capture if matches filters or in full capture mode */
    if (matches_filters || g_promisc_config.level == PROMISC_LEVEL_FULL) {
        promisc_add_buffer_packet(packet, length, nic->index, filter_matched);

        /* Update statistics */
        promisc_classify_and_update_stats(packet, length);
        promisc_update_stats(packet, length, matches_filters);

        /* Deliver to registered applications */
        buffer = (promisc_packet_buffer_t HUGE *)&g_promisc_buffers[g_promisc_buffer_tail];
        promisc_deliver_to_applications(buffer);

        return SUCCESS;
    }

    return ERROR_NOT_FOUND;  /* Use existing error code for filtered packets */
}

int promisc_get_packet(promisc_packet_buffer_t *buffer) {
    if (!g_promisc_initialized || !buffer) {
        return ERROR_INVALID_PARAM;
    }
    
    if (promisc_buffer_is_empty()) {
        return ERROR_NO_DATA;
    }

    /* Copy packet from head of buffer - use far memcpy for huge array */
    FARMEMCPY(buffer, &g_promisc_buffers[g_promisc_buffer_head], sizeof(promisc_packet_buffer_t));

    /* Advance head pointer */
    g_promisc_buffer_head = promisc_advance_buffer_index(g_promisc_buffer_head);
    
    return SUCCESS;
}

int promisc_peek_packet(promisc_packet_buffer_t *buffer) {
    if (!g_promisc_initialized || !buffer) {
        return ERROR_INVALID_PARAM;
    }
    
    if (promisc_buffer_is_empty()) {
        return ERROR_NO_DATA;
    }

    /* Copy packet from head of buffer without advancing - use far memcpy for huge array */
    FARMEMCPY(buffer, &g_promisc_buffers[g_promisc_buffer_head], sizeof(promisc_packet_buffer_t));

    return SUCCESS;
}

void promisc_process_captured_packets(void) {
    promisc_packet_buffer_t packet;

    if (!g_promisc_initialized) {
        return;
    }

    /* Process all available packets */
    while (promisc_get_packet(&packet) == SUCCESS) {
        /* Additional processing can be added here */

        /* Log packet if in debug mode */
        if (g_promisc_config.level == PROMISC_LEVEL_FULL) {
            LOG_DEBUG("Processed packet: length=%d, type=%d, from NIC %d",
                     packet.length, packet.packet_type, packet.nic_index);
        }
    }
}

/* Filter management */
int promisc_add_filter(const promisc_filter_t *filter) {
    int i;

    if (!g_promisc_initialized || !filter) {
        return ERROR_INVALID_PARAM;
    }

    /* Find an empty filter slot */
    for (i = 0; i < PROMISC_MAX_FILTERS; i++) {
        if (!g_promisc_filters[i].enabled) {
            memcpy(&g_promisc_filters[i], filter, sizeof(promisc_filter_t));
            g_promisc_filters[i].enabled = true;
            g_promisc_config.filter_count++;

            LOG_DEBUG("Added filter %d of type %d", i, filter->type);
            return i;
        }
    }

    return ERROR_NO_MEMORY;
}

int promisc_remove_filter(int filter_id) {
    if (!g_promisc_initialized || filter_id < 0 || filter_id >= PROMISC_MAX_FILTERS) {
        return ERROR_INVALID_PARAM;
    }
    
    if (g_promisc_filters[filter_id].enabled) {
        memset(&g_promisc_filters[filter_id], 0, sizeof(promisc_filter_t));
        g_promisc_config.filter_count--;
        
        LOG_DEBUG("Removed filter %d", filter_id);
        return SUCCESS;
    }
    
    return ERROR_NOT_FOUND;
}

int promisc_clear_filters(void) {
    if (!g_promisc_initialized) {
        return ERROR_INVALID_PARAM;
    }
    
    memset(g_promisc_filters, 0, sizeof(g_promisc_filters));
    g_promisc_config.filter_count = 0;
    
    LOG_INFO("Cleared all promiscuous mode filters");
    
    return SUCCESS;
}

bool promisc_packet_matches_filters(const uint8_t *packet, uint16_t length) {
    int i;

    if (!packet || length == 0) {
        return false;
    }

    /* If no filters, match all packets */
    if (g_promisc_config.filter_count == 0) {
        return true;
    }

    /* Check each active filter */
    for (i = 0; i < PROMISC_MAX_FILTERS; i++) {
        if (g_promisc_filters[i].enabled &&
            promisc_check_filter_match(&g_promisc_filters[i], packet, length)) {
            g_promisc_stats.filter_matches++;
            return true;
        }
    }

    return false;
}

int promisc_get_filter_count(void) {
    return g_promisc_config.filter_count;
}

/* Application management */
int promisc_register_application(uint32_t pid, promisc_level_t level, void far *callback) {
    int i;

    if (!g_promisc_initialized) {
        return ERROR_INVALID_PARAM;
    }

    /* Find an empty application slot */
    for (i = 0; i < PROMISC_MAX_APPLICATIONS; i++) {
        if (!g_promisc_apps[i].active) {
            g_promisc_apps[i].handle_id = g_next_handle_id++;
            g_promisc_apps[i].pid = pid;
            g_promisc_apps[i].level = level;
            g_promisc_apps[i].callback = callback;
            g_promisc_apps[i].filter_mask = 0;
            g_promisc_apps[i].packets_delivered = 0;
            g_promisc_apps[i].packets_dropped = 0;
            g_promisc_apps[i].active = true;
            
            g_promisc_config.app_count++;
            
            LOG_INFO("Registered promiscuous mode application: handle=%d, pid=%lu, level=%d", 
                     g_promisc_apps[i].handle_id, pid, level);
            
            return g_promisc_apps[i].handle_id;
        }
    }
    
    return ERROR_NO_MEMORY;
}

int promisc_unregister_application(uint16_t handle) {
    int i;

    if (!g_promisc_initialized) {
        return ERROR_INVALID_PARAM;
    }

    /* Find the application by handle */
    for (i = 0; i < PROMISC_MAX_APPLICATIONS; i++) {
        if (g_promisc_apps[i].active && g_promisc_apps[i].handle_id == handle) {
            LOG_INFO("Unregistering promiscuous mode application: handle=%d", handle);
            
            memset(&g_promisc_apps[i], 0, sizeof(promisc_app_handle_t));
            g_promisc_config.app_count--;
            
            return SUCCESS;
        }
    }
    
    return ERROR_NOT_FOUND;
}

int promisc_deliver_to_applications(const promisc_packet_buffer_t HUGE *packet) {
    int i;
    int delivered = 0;
    int should_deliver;  /* C89: declare at top of function */
    void (far *callback)(const promisc_packet_buffer_t far *);

    if (!g_promisc_initialized || !packet) {
        return ERROR_INVALID_PARAM;
    }

    /* Deliver to all registered applications */
    for (i = 0; i < PROMISC_MAX_APPLICATIONS; i++) {
        if (g_promisc_apps[i].active) {
            /* Check if application's level allows this packet */
            should_deliver = 0;

            switch (g_promisc_apps[i].level) {
                case PROMISC_LEVEL_FULL:
                    should_deliver = 1;
                    break;
                case PROMISC_LEVEL_BASIC:
                    should_deliver = (packet->filter_matched > 0);
                    break;
                case PROMISC_LEVEL_SELECTIVE:
                    should_deliver = (packet->filter_matched > 0 &&
                                    (g_promisc_apps[i].filter_mask & (1 << packet->filter_matched)));
                    break;
                default:
                    should_deliver = 0;
                    break;
            }

            if (should_deliver && g_promisc_apps[i].callback) {
                /* Call application callback */
                callback = (void (far *)(const promisc_packet_buffer_t far *))g_promisc_apps[i].callback;
                callback(packet);

                g_promisc_apps[i].packets_delivered++;
                delivered++;
            } else {
                g_promisc_apps[i].packets_dropped++;
            }
        }
    }

    return delivered;
}

int promisc_get_application_count(void) {
    return g_promisc_config.app_count;
}

/* Statistics and monitoring */
const promiscuous_stats_t* promisc_get_stats(void) {
    return &g_promisc_stats;
}

void promisc_clear_stats(void) {
    memset(&g_promisc_stats, 0, sizeof(promiscuous_stats_t));
    LOG_DEBUG("Cleared promiscuous mode statistics");
}

void promisc_update_stats(const uint8_t *packet, uint16_t length, bool filtered) {
    if (!packet || length == 0) {
        return;
    }
    
    g_promisc_stats.total_packets++;
    g_promisc_stats.bytes_captured += length;
    
    if (filtered) {
        g_promisc_stats.filtered_packets++;
    }
    
    /* Classify packet type */
    if (promisc_is_broadcast_packet(packet)) {
        g_promisc_stats.broadcast_packets++;
    } else if (promisc_is_multicast_packet(packet)) {
        g_promisc_stats.multicast_packets++;
    } else {
        g_promisc_stats.unicast_packets++;
    }
    
    /* Check packet size */
    if (length < 64) {
        g_promisc_stats.undersized_packets++;
    } else if (length > 1514) {
        g_promisc_stats.oversized_packets++;
    }
}

void promisc_print_stats(void) {
    LOG_INFO("Promiscuous Mode Statistics:");
    LOG_INFO("  Total packets: %lu", g_promisc_stats.total_packets);
    LOG_INFO("  Filtered packets: %lu", g_promisc_stats.filtered_packets);
    LOG_INFO("  Dropped packets: %lu", g_promisc_stats.dropped_packets);
    LOG_INFO("  Broadcast: %lu, Multicast: %lu, Unicast: %lu", 
             g_promisc_stats.broadcast_packets, 
             g_promisc_stats.multicast_packets,
             g_promisc_stats.unicast_packets);
    LOG_INFO("  Buffer overflows: %lu", g_promisc_stats.buffer_overflows);
    LOG_INFO("  Bytes captured: %lu", g_promisc_stats.bytes_captured);
}

/* Configuration management */
int promisc_set_config(const promisc_config_t *config) {
    if (!g_promisc_initialized || !config) {
        return ERROR_INVALID_PARAM;
    }
    
    memcpy(&g_promisc_config, config, sizeof(promisc_config_t));
    
    LOG_INFO("Updated promiscuous mode configuration");
    
    return SUCCESS;
}

const promisc_config_t* promisc_get_config(void) {
    return &g_promisc_config;
}

int promisc_set_level(promisc_level_t level) {
    if (!g_promisc_initialized) {
        return ERROR_INVALID_PARAM;
    }
    
    g_promisc_config.level = level;
    
    LOG_INFO("Set promiscuous mode level to %d", level);
    
    return SUCCESS;
}

promisc_level_t promisc_get_level(void) {
    return g_promisc_config.level;
}

/* Integration with Groups 3A/3B/3C */
int promisc_integrate_routing(void) {
    /* Integration with Group 3A routing system */
    LOG_DEBUG("Integrating promiscuous mode with routing system");
    
    /* Enable promiscuous mode packet forwarding to routing for analysis */
    /* This allows routing to see all network traffic for better decisions */
    
    return SUCCESS;
}

int promisc_integrate_api(void) {
    /* Integration with Group 3B extended API */
    LOG_DEBUG("Integrating promiscuous mode with extended API system");
    
    /* Register promiscuous mode functions with extended API */
    /* This allows applications to use promiscuous mode through the API */
    
    return SUCCESS;
}

int promisc_integrate_diagnostics(void) {
    /* Integration with Group 3C diagnostics */
    LOG_DEBUG("Integrating promiscuous mode with diagnostics system");
    
    /* Register promiscuous mode statistics with diagnostics */
    /* This allows diagnostics to include promiscuous mode data */
    
    return SUCCESS;
}

/* Hardware-specific promiscuous mode implementations */
int promisc_enable_3c509b(nic_info_t *nic, promisc_level_t level) {
    int i;
    uint16_t status;
    uint16_t filter;
    uint16_t int_mask;

    if (!nic || nic->type != NIC_TYPE_3C509B) {
        return ERROR_INVALID_PARAM;
    }

    LOG_DEBUG("Enabling 3C509B promiscuous mode at level %d", level);

    /* Enhanced 3C509B promiscuous mode implementation with proper register sequence */

    /* Step 1: Disable RX to safely change configuration */
    outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_RX_DISABLE);

    /* Wait for RX disable to complete */
    for (i = 0; i < 100; i++) {
        status = inw(nic->io_base + _3C509B_STATUS_REG);
        if (!(status & _3C509B_STATUS_CMD_BUSY)) {
            break;
        }
        udelay(10); /* 10 microsecond delay */
    }

    /* Step 2: Select window 1 for receive configuration */
    _3C509B_SELECT_WINDOW(nic->io_base, _3C509B_WINDOW_1);

    /* Step 3: Configure RX filter based on promiscuous level */
    filter = _3C509B_RX_FILTER_STATION | _3C509B_RX_FILTER_BROADCAST;

    switch (level) {
        case PROMISC_LEVEL_BASIC:
            filter |= _3C509B_RX_FILTER_MULTICAST;
            break;
        case PROMISC_LEVEL_FULL:
            filter |= _3C509B_RX_FILTER_MULTICAST | _3C509B_RX_FILTER_PROMISCUOUS;
            break;
        case PROMISC_LEVEL_SELECTIVE:
            filter |= _3C509B_RX_FILTER_MULTICAST | _3C509B_RX_FILTER_PROMISCUOUS;
            /* Additional filtering will be done in software */
            break;
        default:
            LOG_ERROR("Invalid promiscuous level %d for 3C509B", level);
            return ERROR_INVALID_PARAM;
    }

    /* Step 4: Apply the RX filter */
    outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_SET_RX_FILTER | filter);

    /* Wait for command to complete */
    for (i = 0; i < 100; i++) {
        status = inw(nic->io_base + _3C509B_STATUS_REG);
        if (!(status & _3C509B_STATUS_CMD_BUSY)) {
            break;
        }
        udelay(10);
    }

    /* Step 5: Increase RX early threshold to handle higher packet rates */
    if (level >= PROMISC_LEVEL_FULL) {
        /* Lower early threshold for faster packet processing */
        outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_SET_RX_EARLY_THRESH | 8);
        udelay(100);

        /* Increase TX FIFO threshold to maintain performance */
        outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_SET_TX_AVAIL_THRESH | 1024);
        udelay(100);
    }

    /* Step 6: Re-enable RX with new settings */
    outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_RX_ENABLE);

    /* Wait for RX enable to complete */
    for (i = 0; i < 100; i++) {
        status = inw(nic->io_base + _3C509B_STATUS_REG);
        if (!(status & _3C509B_STATUS_CMD_BUSY)) {
            break;
        }
        udelay(10);
    }

    /* Step 7: Update interrupt mask for increased packet rate */
    if (level >= PROMISC_LEVEL_FULL) {
        /* Enable interrupt coalescing to reduce CPU load */
        int_mask = _3C509B_IMASK_RX_COMPLETE | _3C509B_IMASK_TX_COMPLETE |
                   _3C509B_IMASK_ADAPTER_FAILURE;
        outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_SET_INTR_ENABLE | int_mask);
    }

    LOG_DEBUG("3C509B promiscuous mode enabled: filter=0x%X, level=%d", filter, level);

    return SUCCESS;
}

int promisc_disable_3c509b(nic_info_t *nic) {
    uint16_t filter;

    if (!nic || nic->type != NIC_TYPE_3C509B) {
        return ERROR_INVALID_PARAM;
    }

    LOG_DEBUG("Disabling 3C509B promiscuous mode");

    /* Select window 1 for receive configuration */
    _3C509B_SELECT_WINDOW(nic->io_base, _3C509B_WINDOW_1);

    /* Set receive filter to normal mode (station + broadcast) */
    filter = _3C509B_RX_FILTER_STATION | _3C509B_RX_FILTER_BROADCAST;
    outw(nic->io_base + _3C509B_COMMAND_REG, _3C509B_CMD_SET_RX_FILTER | filter);

    LOG_DEBUG("3C509B promiscuous mode disabled");

    return SUCCESS;
}

int promisc_enable_3c515(nic_info_t *nic, promisc_level_t level) {
    int i;
    uint16_t status;
    uint16_t filter;
    uint16_t int_mask;

    if (!nic || nic->type != NIC_TYPE_3C515_TX) {
        return ERROR_INVALID_PARAM;
    }

    LOG_DEBUG("Enabling 3C515-TX promiscuous mode at level %d", level);

    /* Enhanced 3C515-TX promiscuous mode with DMA stall/unstall sequence */

    /* Step 1: Stall DMA operations to safely change configuration */
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_UP_STALL);

    /* Wait for DMA stall to complete */
    for (i = 0; i < 1000; i++) {
        status = inw(nic->io_base + _3C515_TX_STATUS_REG);
        if (!(status & _3C515_TX_STATUS_DMA_IN_PROGRESS)) {
            break;
        }
        udelay(10);
    }

    /* Step 2: Disable RX temporarily */
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_RX_DISABLE);

    /* Wait for RX disable */
    for (i = 0; i < 100; i++) {
        status = inw(nic->io_base + _3C515_TX_STATUS_REG);
        if (!(status & _3C515_TX_STATUS_CMD_IN_PROGRESS)) {
            break;
        }
        udelay(10);
    }

    /* Step 3: Select window 1 for receive configuration */
    _3C515_TX_SELECT_WINDOW(nic->io_base, _3C515_TX_WINDOW_1);

    /* Step 4: Configure advanced RX filter based on level */
    filter = _3C515_TX_RX_FILTER_STATION | _3C515_TX_RX_FILTER_BROADCAST;

    switch (level) {
        case PROMISC_LEVEL_BASIC:
            filter |= _3C515_TX_RX_FILTER_MULTICAST;
            break;
        case PROMISC_LEVEL_FULL:
            filter |= _3C515_TX_RX_FILTER_MULTICAST | _3C515_TX_RX_FILTER_PROM;
            break;
        case PROMISC_LEVEL_SELECTIVE:
            filter |= _3C515_TX_RX_FILTER_MULTICAST | _3C515_TX_RX_FILTER_PROM;
            break;
        default:
            LOG_ERROR("Invalid promiscuous level %d for 3C515-TX", level);
            /* Unstall DMA before returning error */
            outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_UP_UNSTALL);
            return ERROR_INVALID_PARAM;
    }

    /* Step 5: Apply RX filter */
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_SET_RX_FILTER | filter);

    /* Wait for filter command to complete */
    for (i = 0; i < 100; i++) {
        status = inw(nic->io_base + _3C515_TX_STATUS_REG);
        if (!(status & _3C515_TX_STATUS_CMD_IN_PROGRESS)) {
            break;
        }
        udelay(10);
    }

    /* Step 6: Configure DMA for high packet rates in promiscuous mode */
    if (level >= PROMISC_LEVEL_FULL) {
        /* Switch to window 7 for DMA configuration */
        _3C515_TX_SELECT_WINDOW(nic->io_base, _3C515_TX_WINDOW_7);

        /* Configure RX DMA threshold for burst mode */
        outw(nic->io_base + 0x08, 0x0020); /* RX DMA burst threshold */
        udelay(10);

        /* Configure interrupt coalescing */
        outw(nic->io_base + 0x0A, 0x0008); /* Interrupt coalescing count */
        udelay(10);

        /* Return to window 1 */
        _3C515_TX_SELECT_WINDOW(nic->io_base, _3C515_TX_WINDOW_1);
    }

    /* Step 7: Update interrupt mask for promiscuous mode */
    if (level >= PROMISC_LEVEL_FULL) {
        int_mask = _3C515_TX_IMASK_RX_COMPLETE | _3C515_TX_IMASK_UP_COMPLETE |
                   _3C515_TX_IMASK_TX_COMPLETE | _3C515_TX_IMASK_ADAPTER_FAILURE;
        outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_SET_INTR_ENB | int_mask);
    }

    /* Step 8: Re-enable RX */
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_RX_ENABLE);

    /* Step 9: Unstall DMA operations */
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_UP_UNSTALL);

    /* Wait for unstall to complete */
    for (i = 0; i < 100; i++) {
        status = inw(nic->io_base + _3C515_TX_STATUS_REG);
        if (!(status & _3C515_TX_STATUS_CMD_IN_PROGRESS)) {
            break;
        }
        udelay(10);
    }

    LOG_DEBUG("3C515-TX promiscuous mode enabled: filter=0x%X, level=%d, DMA optimized",
              filter, level);

    return SUCCESS;
}

int promisc_disable_3c515(nic_info_t *nic) {
    uint16_t filter;

    if (!nic || nic->type != NIC_TYPE_3C515_TX) {
        return ERROR_INVALID_PARAM;
    }

    LOG_DEBUG("Disabling 3C515-TX promiscuous mode");

    /* Select window 1 for receive configuration */
    _3C515_TX_SELECT_WINDOW(nic->io_base, _3C515_TX_WINDOW_1);

    /* Set receive filter to normal mode (station + broadcast) */
    filter = _3C515_TX_RX_FILTER_STATION | _3C515_TX_RX_FILTER_BROADCAST;
    outw(nic->io_base + _3C515_TX_COMMAND_REG, _3C515_TX_CMD_SET_RX_FILTER | filter);

    LOG_DEBUG("3C515-TX promiscuous mode disabled");

    return SUCCESS;
}

/* Utility functions */
const char* promisc_level_to_string(promisc_level_t level) {
    switch (level) {
        case PROMISC_LEVEL_OFF:        return "Off";
        case PROMISC_LEVEL_BASIC:      return "Basic";
        case PROMISC_LEVEL_FULL:       return "Full";
        case PROMISC_LEVEL_SELECTIVE:  return "Selective";
        default:                       return "Unknown";
    }
}

const char* promisc_filter_type_to_string(promisc_filter_type_t type) {
    switch (type) {
        case PROMISC_FILTER_ALL:       return "All";
        case PROMISC_FILTER_PROTOCOL:  return "Protocol";
        case PROMISC_FILTER_MAC_SRC:   return "Source MAC";
        case PROMISC_FILTER_MAC_DST:   return "Destination MAC";
        case PROMISC_FILTER_LENGTH:    return "Length";
        case PROMISC_FILTER_CONTENT:   return "Content";
        default:                       return "Unknown";
    }
}

bool promisc_is_broadcast_packet(const uint8_t *packet) {
    int i;

    if (!packet) {
        return false;
    }

    /* Check if destination MAC is broadcast (FF:FF:FF:FF:FF:FF) */
    for (i = 0; i < ETH_ALEN; i++) {
        if (packet[i] != 0xFF) {
            return false;
        }
    }

    return true;
}

bool promisc_is_multicast_packet(const uint8_t *packet) {
    if (!packet) {
        return false;
    }
    
    /* Check if the first bit of destination MAC is set (multicast bit) */
    return (packet[0] & 0x01) != 0;
}

uint16_t promisc_classify_packet(const uint8_t *packet, uint16_t length) {
    uint16_t ethertype;

    if (!packet || length < 14) {
        return 0; /* Invalid packet */
    }

    /* Extract EtherType */
    ethertype = (uint16_t)((packet[12] << 8) | packet[13]);

    /* Return the EtherType as classification */
    return ethertype;
}

/* Internal helper functions */
static uint32_t promisc_get_timestamp(void) {
    /* Simple timestamp using packet counter for now */
    /* In real implementation, would use system timer */
    return ++g_packet_counter;
}

static int promisc_buffer_is_full(void) {
    return ((g_promisc_buffer_tail + 1) % PROMISC_BUFFER_COUNT) == g_promisc_buffer_head;
}

static int promisc_buffer_is_empty(void) {
    return g_promisc_buffer_head == g_promisc_buffer_tail;
}

static uint32_t promisc_advance_buffer_index(uint32_t index) {
    return (index + 1) % PROMISC_BUFFER_COUNT;
}

static void promisc_add_buffer_packet(const uint8_t *packet, uint16_t length,
                                     uint8_t nic_index, uint8_t filter_matched) {
    promisc_packet_buffer_t HUGE *buffer;
    uint16_t copy_length;

    buffer = (promisc_packet_buffer_t HUGE *)&g_promisc_buffers[g_promisc_buffer_tail];

    buffer->timestamp = promisc_get_timestamp();
    buffer->length = length;
    buffer->status = 0;
    buffer->nic_index = nic_index;
    buffer->filter_matched = filter_matched;
    buffer->packet_type = (uint8_t)promisc_classify_packet(packet, length);
    buffer->reserved = 0;

    /* Copy packet data - use far memcpy for huge buffer */
    copy_length = (length > PROMISC_BUFFER_SIZE) ? PROMISC_BUFFER_SIZE : length;
    FARMEMCPY(buffer->data, packet, copy_length);

    /* Advance tail pointer */
    g_promisc_buffer_tail = promisc_advance_buffer_index(g_promisc_buffer_tail);
}

static int promisc_check_filter_match(const promisc_filter_t *filter,
                                       const uint8_t *packet, uint16_t length) {
    uint16_t ethertype;
    int i;

    if (!filter || !filter->enabled || !packet) {
        return 0;
    }

    switch (filter->type) {
        case PROMISC_FILTER_ALL:
            return 1;

        case PROMISC_FILTER_PROTOCOL:
            if (length < 14) return 0;
            ethertype = (uint16_t)((packet[12] << 8) | packet[13]);
            return (ethertype & filter->mask) == (filter->match_value & filter->mask);

        case PROMISC_FILTER_MAC_SRC:
            if (length < 12) return 0;
            return memcmp(packet + 6, filter->mac_addr, ETH_ALEN) == 0;

        case PROMISC_FILTER_MAC_DST:
            if (length < 6) return 0;
            return memcmp(packet, filter->mac_addr, ETH_ALEN) == 0;

        case PROMISC_FILTER_LENGTH:
            return (length >= filter->min_length && length <= filter->max_length);

        case PROMISC_FILTER_CONTENT:
            if (length < filter->pattern_length) return 0;
            for (i = 0; i <= (int)(length - filter->pattern_length); i++) {
                if (memcmp(packet + i, filter->content_pattern, filter->pattern_length) == 0) {
                    return 1;
                }
            }
            return 0;

        default:
            return 0;
    }
}

static void promisc_classify_and_update_stats(const uint8_t *packet, uint16_t length) {
    /* Additional packet classification and statistics update */
    if (length < 64) {
        g_promisc_stats.undersized_packets++;
    } else if (length > 1514) {
        g_promisc_stats.oversized_packets++;
    }
    
    /* Check for common error patterns */
    if (length < 14) {
        g_promisc_stats.error_packets++;
    }
}

