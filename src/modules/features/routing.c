/**
 * @file routing.c
 * @brief ROUTING.MOD - Multi-NIC Routing Feature Module
 * 
 * Phase 3A: Dynamic Module Loading - Stream 3 Feature Implementation
 * 
 * This module provides advanced routing capabilities for multi-NIC systems:
 * - Static route management
 * - Flow-aware packet routing
 * - Load balancing across NICs
 * - Route priority handling
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#include "module_api.h"
#include <stdio.h>
#include <string.h>

/* Routing Constants */
#define MAX_STATIC_ROUTES       32
#define MAX_ROUTE_METRICS       16
#define DEFAULT_ROUTE_METRIC    10
#define INFINITE_METRIC         255

/* Route Types */
#define ROUTE_TYPE_DIRECT       0x01    /* Direct connection */
#define ROUTE_TYPE_STATIC       0x02    /* Static route */
#define ROUTE_TYPE_DEFAULT      0x04    /* Default route */

/* Load Balancing Algorithms */
#define LB_ALGORITHM_ROUND_ROBIN    0
#define LB_ALGORITHM_WEIGHTED       1
#define LB_ALGORITHM_LEAST_USED     2
#define LB_ALGORITHM_HASH_BASED     3

/* Route Entry Structure */
typedef struct {
    uint8_t  dest_net[4];       /* Destination network */
    uint8_t  dest_mask[4];      /* Network mask */
    uint8_t  gateway[4];        /* Gateway address */
    uint8_t  nic_id;            /* Output NIC ID */
    uint8_t  metric;            /* Route metric */
    uint8_t  route_type;        /* Route type flags */
    uint8_t  flags;             /* Route flags */
    uint32_t timestamp;         /* Creation timestamp */
    uint32_t use_count;         /* Usage counter */
    uint32_t last_used;         /* Last use timestamp */
} route_entry_t;

/* Load Balancing Context */
typedef struct {
    uint8_t  algorithm;         /* Load balancing algorithm */
    uint8_t  nic_count;         /* Number of available NICs */
    uint8_t  nic_list[MAX_NICS_SUPPORTED]; /* Available NIC list */
    uint16_t nic_weights[MAX_NICS_SUPPORTED]; /* NIC weights */
    uint32_t nic_usage[MAX_NICS_SUPPORTED];   /* Usage counters */
    uint16_t round_robin_index; /* Round-robin index */
} load_balance_context_t;

/* Routing Statistics */
typedef struct {
    uint32_t packets_routed;    /* Total packets routed */
    uint32_t routes_matched;    /* Route matches */
    uint32_t routes_missed;     /* Route misses */
    uint32_t lb_decisions;      /* Load balancing decisions */
    uint32_t route_updates;     /* Route table updates */
    uint32_t route_failures;    /* Routing failures */
} routing_stats_t;

/* Module Context */
typedef struct {
    route_entry_t routes[MAX_STATIC_ROUTES];    /* Route table */
    uint16_t route_count;                       /* Number of routes */
    load_balance_context_t lb_context;          /* Load balancing */
    routing_stats_t stats;                      /* Statistics */
    bool enabled;                               /* Module enabled */
    core_services_t* core_services;             /* Core services */
} routing_context_t;

/* Global module context */
static routing_context_t routing_ctx;

/* Forward declarations */
static bool routing_route_packet(const packet_t* packet, uint8_t* output_nic);
static bool routing_add_route(const uint8_t* dest_net, const uint8_t* dest_mask,
                             const uint8_t* gateway, uint8_t nic_id, uint8_t metric);
static bool routing_delete_route(const uint8_t* dest_net, const uint8_t* dest_mask);
static route_entry_t* routing_find_route(const uint8_t* dest_addr);
static bool routing_update_route_usage(route_entry_t* route);

/* Load balancing functions */
static uint8_t lb_round_robin(void);
static uint8_t lb_weighted_round_robin(void);
static uint8_t lb_least_used(void);
static uint8_t lb_hash_based(const packet_t* packet);

/* Utility functions */
static bool ip_addr_match(const uint8_t* addr, const uint8_t* net, const uint8_t* mask);
static void ip_addr_copy(uint8_t* dest, const uint8_t* src);
static uint16_t ip_checksum(const uint8_t* data, uint16_t length);
static uint8_t* get_dest_ip_from_packet(const packet_t* packet);

/* API functions for other modules */
static bool routing_api_add_route(const char* dest_net, const char* gateway, uint8_t nic_id);
static bool routing_api_delete_route(const char* dest_net);
static bool routing_api_get_stats(routing_stats_t* stats);
static bool routing_api_set_lb_algorithm(uint8_t algorithm);

/* Module API registration */
static const api_registration_t routing_apis[] = {
    {"add_route", routing_api_add_route},
    {"delete_route", routing_api_delete_route},
    {"get_stats", routing_api_get_stats},
    {"set_lb_algorithm", routing_api_set_lb_algorithm},
    {NULL, NULL}  /* Terminator */
};

/* ============================================================================
 * Module Header and Initialization
 * ============================================================================ */

/* Module header - must be first in the file */
const module_header_t module_header = {
    .magic = MODULE_MAGIC,
    .version = 0x0100,  /* Version 1.0 */
    .header_size = sizeof(module_header_t),
    .module_size = 0,   /* Filled by linker */
    .module_class = MODULE_CLASS_FEATURE,
    .family_id = FAMILY_UNKNOWN,
    .feature_flags = FEATURE_ROUTING,
    .api_version = MODULE_API_VERSION,
    .init_offset = (uint16_t)routing_init,
    .vtable_offset = 0,  /* No vtable for feature modules */
    .cleanup_offset = (uint16_t)routing_cleanup,
    .info_offset = 0,
    .deps_count = 0,
    .deps_offset = 0,
    .min_dos_version = 0x0300,  /* DOS 3.0+ */
    .min_cpu_family = 2,        /* 286+ */
    .name = "ROUTING",
    .description = "Multi-NIC Routing Engine",
    .author = "3Com/Phase3A",
    .build_timestamp = 0,       /* Filled by build system */
    .checksum = 0,              /* Calculated by build system */
    .reserved = {0}
};

/**
 * @brief Feature module initialization function
 */
bool routing_init(core_services_t* core, const module_config_t* config)
{
    if (!core) {
        return false;
    }
    
    /* Initialize module context */
    memset(&routing_ctx, 0, sizeof(routing_context_t));
    routing_ctx.core_services = core;
    routing_ctx.enabled = true;
    
    /* Initialize load balancing context */
    routing_ctx.lb_context.algorithm = LB_ALGORITHM_ROUND_ROBIN;
    routing_ctx.lb_context.nic_count = 0;
    routing_ctx.lb_context.round_robin_index = 0;
    
    /* Set default weights */
    for (int i = 0; i < MAX_NICS_SUPPORTED; i++) {
        routing_ctx.lb_context.nic_weights[i] = 100;  /* Equal weight */
    }
    
    /* Register packet handler for routing */
    if (!core->register_packet_handler(0x0800, routing_packet_handler)) {  /* IP packets */
        core->log_message(LOG_LEVEL_ERROR, "ROUTING",
            "Failed to register IP packet handler");
        return false;
    }
    
    /* Register APIs */
    if (!core->register_apis("ROUTING", routing_apis)) {
        core->log_message(LOG_LEVEL_ERROR, "ROUTING",
            "Failed to register routing APIs");
        return false;
    }
    
    /* Add default routes if configured */
    routing_add_default_routes();
    
    core->log_message(LOG_LEVEL_INFO, "ROUTING",
        "Multi-NIC routing engine initialized");
    
    return true;
}

/**
 * @brief Module cleanup function
 */
void routing_cleanup(void)
{
    if (routing_ctx.core_services) {
        /* Unregister packet handler */
        routing_ctx.core_services->unregister_packet_handler(0x0800);
        
        /* Unregister APIs */
        routing_ctx.core_services->unregister_apis("ROUTING");
        
        routing_ctx.core_services->log_message(LOG_LEVEL_INFO, "ROUTING",
            "Multi-NIC routing engine cleanup complete");
    }
    
    /* Clear context */
    memset(&routing_ctx, 0, sizeof(routing_context_t));
}

/* ============================================================================
 * Core Routing Functions
 * ============================================================================ */

/**
 * @brief Packet handler for routing decisions
 */
void routing_packet_handler(packet_t* packet)
{
    uint8_t output_nic;
    bool route_found;
    
    if (!packet || !routing_ctx.enabled) {
        return;
    }
    
    /* Attempt to route the packet */
    route_found = routing_route_packet(packet, &output_nic);
    
    if (route_found && output_nic != packet->nic_id) {
        /* Forward packet to different NIC */
        if (routing_ctx.core_services->send_packet(output_nic, packet)) {
            routing_ctx.stats.packets_routed++;
        } else {
            routing_ctx.stats.route_failures++;
        }
    }
}

/**
 * @brief Route a packet to appropriate NIC
 */
static bool routing_route_packet(const packet_t* packet, uint8_t* output_nic)
{
    route_entry_t* route;
    uint8_t* dest_ip;
    
    if (!packet || !output_nic) {
        return false;
    }
    
    /* Extract destination IP from packet */
    dest_ip = get_dest_ip_from_packet(packet);
    if (!dest_ip) {
        routing_ctx.stats.route_failures++;
        return false;
    }
    
    /* Find matching route */
    route = routing_find_route(dest_ip);
    if (route) {
        *output_nic = route->nic_id;
        routing_update_route_usage(route);
        routing_ctx.stats.routes_matched++;
        return true;
    }
    
    /* No specific route found - use load balancing */
    if (routing_ctx.lb_context.nic_count > 1) {
        *output_nic = routing_load_balance_decision(packet);
        routing_ctx.stats.lb_decisions++;
        return true;
    }
    
    /* No routing decision possible */
    routing_ctx.stats.routes_missed++;
    return false;
}

/**
 * @brief Make load balancing decision
 */
uint8_t routing_load_balance_decision(const packet_t* packet)
{
    switch (routing_ctx.lb_context.algorithm) {
        case LB_ALGORITHM_ROUND_ROBIN:
            return lb_round_robin();
            
        case LB_ALGORITHM_WEIGHTED:
            return lb_weighted_round_robin();
            
        case LB_ALGORITHM_LEAST_USED:
            return lb_least_used();
            
        case LB_ALGORITHM_HASH_BASED:
            return lb_hash_based(packet);
            
        default:
            return routing_ctx.lb_context.nic_list[0];  /* Default to first NIC */
    }
}

/* ============================================================================
 * Route Management Functions
 * ============================================================================ */

/**
 * @brief Add a static route
 */
static bool routing_add_route(const uint8_t* dest_net, const uint8_t* dest_mask,
                             const uint8_t* gateway, uint8_t nic_id, uint8_t metric)
{
    route_entry_t* route;
    
    if (!dest_net || !dest_mask || routing_ctx.route_count >= MAX_STATIC_ROUTES) {
        return false;
    }
    
    /* Find free route slot */
    route = &routing_ctx.routes[routing_ctx.route_count];
    
    /* Set up route entry */
    ip_addr_copy(route->dest_net, dest_net);
    ip_addr_copy(route->dest_mask, dest_mask);
    if (gateway) {
        ip_addr_copy(route->gateway, gateway);
    } else {
        memset(route->gateway, 0, 4);  /* Direct route */
    }
    
    route->nic_id = nic_id;
    route->metric = metric;
    route->route_type = gateway ? ROUTE_TYPE_STATIC : ROUTE_TYPE_DIRECT;
    route->flags = 0;
    route->timestamp = routing_ctx.core_services->timing.get_ticks();
    route->use_count = 0;
    route->last_used = 0;
    
    routing_ctx.route_count++;
    routing_ctx.stats.route_updates++;
    
    routing_ctx.core_services->log_message(LOG_LEVEL_INFO, "ROUTING",
        "Added route to %d.%d.%d.%d/%d.%d.%d.%d via NIC %d",
        dest_net[0], dest_net[1], dest_net[2], dest_net[3],
        dest_mask[0], dest_mask[1], dest_mask[2], dest_mask[3],
        nic_id);
    
    return true;
}

/**
 * @brief Delete a static route
 */
static bool routing_delete_route(const uint8_t* dest_net, const uint8_t* dest_mask)
{
    if (!dest_net || !dest_mask) {
        return false;
    }
    
    /* Find and remove route */
    for (int i = 0; i < routing_ctx.route_count; i++) {
        route_entry_t* route = &routing_ctx.routes[i];
        
        if (ip_addr_match(route->dest_net, dest_net, dest_mask) &&
            memcmp(route->dest_mask, dest_mask, 4) == 0) {
            
            /* Remove route by shifting remaining entries */
            memmove(&routing_ctx.routes[i], &routing_ctx.routes[i + 1],
                   (routing_ctx.route_count - i - 1) * sizeof(route_entry_t));
            
            routing_ctx.route_count--;
            routing_ctx.stats.route_updates++;
            
            routing_ctx.core_services->log_message(LOG_LEVEL_INFO, "ROUTING",
                "Deleted route to %d.%d.%d.%d/%d.%d.%d.%d",
                dest_net[0], dest_net[1], dest_net[2], dest_net[3],
                dest_mask[0], dest_mask[1], dest_mask[2], dest_mask[3]);
            
            return true;
        }
    }
    
    return false;  /* Route not found */
}

/**
 * @brief Find route for destination address
 */
static route_entry_t* routing_find_route(const uint8_t* dest_addr)
{
    route_entry_t* best_route = NULL;
    uint8_t best_prefix_len = 0;
    
    if (!dest_addr) {
        return NULL;
    }
    
    /* Find longest prefix match */
    for (int i = 0; i < routing_ctx.route_count; i++) {
        route_entry_t* route = &routing_ctx.routes[i];
        
        if (ip_addr_match(dest_addr, route->dest_net, route->dest_mask)) {
            /* Calculate prefix length */
            uint8_t prefix_len = 0;
            for (int j = 0; j < 4; j++) {
                uint8_t mask_byte = route->dest_mask[j];
                while (mask_byte & 0x80) {
                    prefix_len++;
                    mask_byte <<= 1;
                }
            }
            
            /* Use longest prefix match */
            if (prefix_len > best_prefix_len) {
                best_prefix_len = prefix_len;
                best_route = route;
            }
        }
    }
    
    return best_route;
}

/**
 * @brief Update route usage statistics
 */
static bool routing_update_route_usage(route_entry_t* route)
{
    if (!route) {
        return false;
    }
    
    route->use_count++;
    route->last_used = routing_ctx.core_services->timing.get_ticks();
    
    /* Update load balancing usage */
    if (route->nic_id < MAX_NICS_SUPPORTED) {
        routing_ctx.lb_context.nic_usage[route->nic_id]++;
    }
    
    return true;
}

/* ============================================================================
 * Load Balancing Algorithms
 * ============================================================================ */

/**
 * @brief Round-robin load balancing
 */
static uint8_t lb_round_robin(void)
{
    load_balance_context_t* lb = &routing_ctx.lb_context;
    uint8_t nic_id;
    
    if (lb->nic_count == 0) {
        return 0;  /* No NICs available */
    }
    
    nic_id = lb->nic_list[lb->round_robin_index];
    lb->round_robin_index = (lb->round_robin_index + 1) % lb->nic_count;
    
    return nic_id;
}

/**
 * @brief Weighted round-robin load balancing
 */
static uint8_t lb_weighted_round_robin(void)
{
    load_balance_context_t* lb = &routing_ctx.lb_context;
    static uint16_t weight_counters[MAX_NICS_SUPPORTED];
    uint8_t selected_nic = 0;
    uint16_t max_weight = 0;
    
    /* Find NIC with highest remaining weight */
    for (int i = 0; i < lb->nic_count; i++) {
        uint8_t nic_id = lb->nic_list[i];
        
        if (weight_counters[nic_id] == 0) {
            weight_counters[nic_id] = lb->nic_weights[nic_id];
        }
        
        if (weight_counters[nic_id] > max_weight) {
            max_weight = weight_counters[nic_id];
            selected_nic = nic_id;
        }
    }
    
    /* Decrement weight counter */
    if (max_weight > 0) {
        weight_counters[selected_nic]--;
    }
    
    return selected_nic;
}

/**
 * @brief Least-used load balancing
 */
static uint8_t lb_least_used(void)
{
    load_balance_context_t* lb = &routing_ctx.lb_context;
    uint8_t selected_nic = 0;
    uint32_t min_usage = 0xFFFFFFFF;
    
    /* Find NIC with least usage */
    for (int i = 0; i < lb->nic_count; i++) {
        uint8_t nic_id = lb->nic_list[i];
        
        if (lb->nic_usage[nic_id] < min_usage) {
            min_usage = lb->nic_usage[nic_id];
            selected_nic = nic_id;
        }
    }
    
    return selected_nic;
}

/**
 * @brief Hash-based load balancing
 */
static uint8_t lb_hash_based(const packet_t* packet)
{
    load_balance_context_t* lb = &routing_ctx.lb_context;
    uint32_t hash = 0;
    uint8_t* data = packet->data;
    
    if (!packet || !data || lb->nic_count == 0) {
        return 0;
    }
    
    /* Simple hash based on packet content */
    for (int i = 0; i < min(packet->length, 32); i++) {
        hash = (hash * 31) + data[i];
    }
    
    return lb->nic_list[hash % lb->nic_count];
}

/* ============================================================================
 * API Functions for External Access
 * ============================================================================ */

/**
 * @brief API function to add route (string format)
 */
static bool routing_api_add_route(const char* dest_net, const char* gateway, uint8_t nic_id)
{
    uint8_t dest_ip[4], mask_ip[4], gw_ip[4];
    
    /* Parse destination network (simplified parser) */
    if (sscanf(dest_net, "%d.%d.%d.%d/%d.%d.%d.%d",
               &dest_ip[0], &dest_ip[1], &dest_ip[2], &dest_ip[3],
               &mask_ip[0], &mask_ip[1], &mask_ip[2], &mask_ip[3]) != 8) {
        return false;
    }
    
    /* Parse gateway if provided */
    if (gateway && strlen(gateway) > 0) {
        if (sscanf(gateway, "%d.%d.%d.%d",
                   &gw_ip[0], &gw_ip[1], &gw_ip[2], &gw_ip[3]) != 4) {
            return false;
        }
        return routing_add_route(dest_ip, mask_ip, gw_ip, nic_id, DEFAULT_ROUTE_METRIC);
    } else {
        return routing_add_route(dest_ip, mask_ip, NULL, nic_id, DEFAULT_ROUTE_METRIC);
    }
}

/**
 * @brief API function to delete route (string format)
 */
static bool routing_api_delete_route(const char* dest_net)
{
    uint8_t dest_ip[4], mask_ip[4];
    
    /* Parse destination network */
    if (sscanf(dest_net, "%d.%d.%d.%d/%d.%d.%d.%d",
               &dest_ip[0], &dest_ip[1], &dest_ip[2], &dest_ip[3],
               &mask_ip[0], &mask_ip[1], &mask_ip[2], &mask_ip[3]) != 8) {
        return false;
    }
    
    return routing_delete_route(dest_ip, mask_ip);
}

/**
 * @brief API function to get routing statistics
 */
static bool routing_api_get_stats(routing_stats_t* stats)
{
    if (!stats) {
        return false;
    }
    
    *stats = routing_ctx.stats;
    return true;
}

/**
 * @brief API function to set load balancing algorithm
 */
static bool routing_api_set_lb_algorithm(uint8_t algorithm)
{
    if (algorithm > LB_ALGORITHM_HASH_BASED) {
        return false;
    }
    
    routing_ctx.lb_context.algorithm = algorithm;
    
    routing_ctx.core_services->log_message(LOG_LEVEL_INFO, "ROUTING",
        "Load balancing algorithm changed to %d", algorithm);
    
    return true;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Check if IP address matches network/mask
 */
static bool ip_addr_match(const uint8_t* addr, const uint8_t* net, const uint8_t* mask)
{
    for (int i = 0; i < 4; i++) {
        if ((addr[i] & mask[i]) != (net[i] & mask[i])) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Copy IP address
 */
static void ip_addr_copy(uint8_t* dest, const uint8_t* src)
{
    memcpy(dest, src, 4);
}

/**
 * @brief Calculate IP checksum
 */
static uint16_t ip_checksum(const uint8_t* data, uint16_t length)
{
    uint32_t sum = 0;
    
    /* Sum 16-bit words */
    while (length > 1) {
        sum += (data[0] << 8) | data[1];
        data += 2;
        length -= 2;
    }
    
    /* Add odd byte if present */
    if (length > 0) {
        sum += data[0] << 8;
    }
    
    /* Add carry */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

/**
 * @brief Extract destination IP from packet
 */
static uint8_t* get_dest_ip_from_packet(const packet_t* packet)
{
    if (!packet || packet->length < 34) {  /* Min Ethernet + IP header */
        return NULL;
    }
    
    /* Skip Ethernet header (14 bytes) and get IP destination (offset 16-19) */
    return packet->data + 30;  /* Ethernet(14) + IP dest offset(16) */
}

/**
 * @brief Add default routes during initialization
 */
void routing_add_default_routes(void)
{
    uint8_t default_net[4] = {0, 0, 0, 0};
    uint8_t default_mask[4] = {0, 0, 0, 0};
    
    /* Add default route to first available NIC */
    routing_add_route(default_net, default_mask, NULL, 0, 255);
    
    routing_ctx.core_services->log_message(LOG_LEVEL_INFO, "ROUTING",
        "Default routes initialized");
}