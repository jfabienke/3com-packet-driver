/**
 * @file routing.c
 * @brief Packet routing between multiple NICs
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#include "routing.h"
#include "logging.h"
#include "statrt.h"
#include "common.h"
#include "cpudet.h"
#include "hardware.h"
#include "mii.h"
#include "3c515.h"
#include "atomtime.h"
#include "arp.h"

/* Global routing state */
routing_table_t g_routing_table;
bridge_table_t g_bridge_table;
routing_stats_t g_routing_stats;
bool g_routing_enabled = false;

/* Private global state */
static bool g_routing_initialized = false;
static bool g_learning_enabled = true;
static uint32_t g_aging_time_ms = 300000; /* 5 minutes default */

/* Rate limiting structures */
typedef struct rate_limit_info {
    uint32_t packets_per_sec;
    uint32_t current_count;
    uint32_t last_reset_time;
} rate_limit_info_t;

static rate_limit_info_t g_rate_limits[MAX_NICS];

/* Internal helper functions */
static route_entry_t* routing_find_entry(route_rule_type_t rule_type, const void *rule_data);
static void routing_remove_entry(route_entry_t *entry);
static bridge_entry_t* bridge_find_entry(const uint8_t *mac);
static void bridge_remove_entry(bridge_entry_t *entry);
static void bridge_add_entry(const uint8_t *mac, uint8_t nic_index);
static bridge_entry_t* bridge_find_oldest_entry(void);
static uint32_t routing_get_timestamp(void);
static bool routing_check_rate_limit_internal(uint8_t nic_index);

/* Routing initialization and cleanup */
int routing_init(void) {
    int result;

    if (g_routing_initialized) {
        return SUCCESS;
    }

    /* Initialize routing table */
    result = routing_table_init(&g_routing_table, 256);
    if (result != SUCCESS) {
        return result;
    }
    
    /* Initialize bridge table */
    result = bridge_table_init(&g_bridge_table, 512);
    if (result != SUCCESS) {
        routing_table_cleanup(&g_routing_table);
        return result;
    }
    
    /* Initialize statistics */
    routing_stats_init(&g_routing_stats);
    
    /* Initialize rate limiting */
    {
        int i;
        for (i = 0; i < MAX_NICS; i++) {
            g_rate_limits[i].packets_per_sec = 0; /* Unlimited by default */
            g_rate_limits[i].current_count = 0;
            g_rate_limits[i].last_reset_time = 0;
        }
    }
    
    /* Set default routing configuration */
    g_routing_table.default_decision = ROUTE_DECISION_FORWARD;
    g_routing_table.default_nic = 0;
    g_routing_table.learning_enabled = g_learning_enabled;
    g_routing_table.learning_timeout = g_aging_time_ms;
    
    g_routing_initialized = true;
    g_routing_enabled = false; /* Must be explicitly enabled */
    
    return SUCCESS;
}

void routing_cleanup(void) {
    if (!g_routing_initialized) {
        return;
    }
    
    /* Cleanup routing and bridge tables */
    routing_table_cleanup(&g_routing_table);
    bridge_table_cleanup(&g_bridge_table);
    
    /* Clear statistics */
    routing_stats_init(&g_routing_stats);
    
    g_routing_initialized = false;
    g_routing_enabled = false;
}

int routing_enable(bool enable) {
    if (!g_routing_initialized) {
        return ERROR_NOT_FOUND;
    }
    
    g_routing_enabled = enable;
    return SUCCESS;
}

bool routing_is_enabled(void) {
    return g_routing_enabled && g_routing_initialized;
}

/* Routing table management */
int routing_table_init(routing_table_t *table, uint16_t max_entries) {
    if (!table) {
        return ERROR_INVALID_PARAM;
    }
    
    table->entries = NULL;
    table->entry_count = 0;
    table->max_entries = max_entries;
    table->default_decision = ROUTE_DECISION_DROP;
    table->default_nic = 0;
    table->learning_enabled = true;
    table->learning_timeout = 300000; /* 5 minutes */
    
    return SUCCESS;
}

void routing_table_cleanup(routing_table_t *table) {
    if (!table) {
        return;
    }
    
    /* Free all route entries with proper linked list traversal */
    route_entry_t *current = table->entries;
    while (current) {
        route_entry_t *next = current->next;
        
        /* Zero out the entry before freeing for security */
        memory_zero(current, sizeof(route_entry_t));
        memory_free(current);
        
        current = next;
    }
    
    /* Clear table state */
    table->entries = NULL;
    table->entry_count = 0;
}

int routing_add_rule(route_rule_type_t rule_type, const void *rule_data,
                    uint8_t src_nic, uint8_t dest_nic, route_decision_t decision) {
    if (!rule_data || !routing_validate_nic(src_nic) || !routing_validate_nic(dest_nic)) {
        return ERROR_INVALID_PARAM;
    }
    
    if (g_routing_table.entry_count >= g_routing_table.max_entries) {
        return ERROR_NO_MEMORY;
    }
    
    /* Check if rule already exists to prevent duplicates */
    route_entry_t *existing = routing_find_entry(rule_type, rule_data);
    if (existing) {
        /* Update existing rule */
        existing->src_nic = src_nic;
        existing->dest_nic = dest_nic;
        existing->decision = decision;
        return SUCCESS;
    }
    
    /* Create new rule entry */
    route_entry_t *entry = (route_entry_t*)memory_alloc(sizeof(route_entry_t), 
                                                       MEM_TYPE_DRIVER_DATA, 0);
    if (!entry) {
        return ERROR_NO_MEMORY;
    }
    
    /* Initialize entry */
    entry->rule_type = rule_type;
    entry->src_nic = src_nic;
    entry->dest_nic = dest_nic;
    entry->decision = decision;
    entry->priority = 100; /* Default priority */
    entry->flags = 0;
    entry->packet_count = 0;
    entry->byte_count = 0;
    entry->next = NULL;
    
    /* Copy rule-specific data */
    switch (rule_type) {
        case ROUTE_RULE_MAC_ADDRESS:
            memory_copy(entry->dest_mac, (const uint8_t*)rule_data, ETH_ALEN);
            memory_set(entry->mask, 0xFF, ETH_ALEN); /* Full match by default */
            break;
        case ROUTE_RULE_ETHERTYPE:
            entry->ethertype = *(const uint16_t*)rule_data;
            break;
        default:
            memory_free(entry);
            return ERROR_INVALID_PARAM;
    }
    
    /* Add to routing table */
    entry->next = g_routing_table.entries;
    g_routing_table.entries = entry;
    g_routing_table.entry_count++;
    
    return SUCCESS;
}

/* Bridge learning functions */
int bridge_table_init(bridge_table_t *table, uint16_t max_entries) {
    if (!table) {
        return ERROR_INVALID_PARAM;
    }
    
    table->entries = NULL;
    table->entry_count = 0;
    table->max_entries = max_entries;
    table->aging_time = 300000; /* 5 minutes */
    table->total_lookups = 0;
    table->successful_lookups = 0;
    
    return SUCCESS;
}

void bridge_table_cleanup(bridge_table_t *table) {
    if (!table) {
        return;
    }
    
    /* Free all bridge entries with proper linked list traversal */
    bridge_entry_t *current = table->entries;
    while (current) {
        bridge_entry_t *next = current->next;
        
        /* Zero out the entry before freeing for security */
        memory_zero(current, sizeof(bridge_entry_t));
        memory_free(current);
        
        current = next;
    }
    
    /* Clear table state and reset statistics */
    table->entries = NULL;
    table->entry_count = 0;
    table->total_lookups = 0;
    table->successful_lookups = 0;
}

int bridge_learn_mac(const uint8_t *mac, uint8_t nic_index) {
    if (!mac || !routing_validate_nic(nic_index) || !g_learning_enabled) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Check if MAC already exists */
    bridge_entry_t *existing = bridge_find_entry(mac);
    if (existing) {
        /* Update existing entry */
        existing->nic_index = nic_index;
        existing->timestamp = routing_get_timestamp();
        existing->packet_count++;
        return SUCCESS;
    }
    
    /* Check table capacity */
    if (g_bridge_table.entry_count >= g_bridge_table.max_entries) {
        /* Remove oldest entry to make room (LRU eviction) */
        bridge_entry_t *oldest = bridge_find_oldest_entry();
        if (oldest) {
            bridge_remove_entry(oldest);
            memory_zero(oldest, sizeof(bridge_entry_t));
            memory_free(oldest);
        } else {
            /* Should not happen, but handle gracefully */
            return ERROR_NO_MEMORY;
        }
    }
    
    /* Create new entry */
    bridge_add_entry(mac, nic_index);
    
    return SUCCESS;
}

bridge_entry_t* bridge_lookup_mac(const uint8_t *mac) {
    if (!mac) {
        return NULL;
    }
    
    g_bridge_table.total_lookups++;
    
    bridge_entry_t *entry = bridge_find_entry(mac);
    if (entry) {
        g_bridge_table.successful_lookups++;
        return entry;
    }
    
    return NULL;
}

/* Packet routing decisions */
route_decision_t routing_decide(const packet_buffer_t *packet, uint8_t src_nic,
                               uint8_t *dest_nic) {
    const uint8_t *eth_header;
    const uint8_t *dest_mac;
    const uint8_t *src_mac;
    uint16_t ethertype;
    route_decision_t decision;
    bridge_entry_t *bridge_entry;

    if (!packet || !packet->data || !dest_nic || !routing_is_enabled()) {
        return ROUTE_DECISION_DROP;
    }

    /* Extract and validate Ethernet header */
    if (packet->length < ETH_HLEN) {
        g_routing_stats.packets_dropped++;
        return ROUTE_DECISION_DROP;
    }

    eth_header = packet->data;
    dest_mac = eth_header;
    src_mac = eth_header + ETH_ALEN;
    ethertype = ntohs(*(uint16_t*)(eth_header + 2 * ETH_ALEN));

    /* Learn source MAC if learning is enabled */
    if (g_learning_enabled) {
        bridge_learn_mac(src_mac, src_nic);
    }

    /* Check for broadcast */
    if (is_broadcast_mac(dest_mac)) {
        g_routing_stats.packets_broadcast++;
        return ROUTE_DECISION_BROADCAST;
    }

    /* Check for multicast */
    if (is_multicast_mac(dest_mac)) {
        g_routing_stats.packets_multicast++;
        return ROUTE_DECISION_MULTICAST;
    }

    /* Try MAC-based routing first */
    decision = routing_lookup_mac(dest_mac, src_nic, dest_nic);
    if (decision != ROUTE_DECISION_DROP) {
        return decision;
    }

    /* Try Ethertype-based routing */
    decision = routing_lookup_ethertype(ethertype, src_nic, dest_nic);
    if (decision != ROUTE_DECISION_DROP) {
        return decision;
    }

    /* Check bridge learning table */
    bridge_entry = bridge_lookup_mac(dest_mac);
    if (bridge_entry) {
        *dest_nic = bridge_entry->nic_index;
        
        /* Avoid routing back to source NIC */
        if (*dest_nic == src_nic) {
            return ROUTE_DECISION_DROP;
        }
        
        g_routing_stats.packets_forwarded++;
        return ROUTE_DECISION_FORWARD;
    }
    
    /* Use default routing decision */
    *dest_nic = g_routing_table.default_nic;
    
    switch (g_routing_table.default_decision) {
        case ROUTE_DECISION_FORWARD:
            g_routing_stats.packets_forwarded++;
            break;
        case ROUTE_DECISION_BROADCAST:
            g_routing_stats.packets_broadcast++;
            break;
        case ROUTE_DECISION_DROP:
        default:
            g_routing_stats.packets_dropped++;
            break;
    }
    
    return g_routing_table.default_decision;
}

route_decision_t routing_lookup_mac(const uint8_t *dest_mac, uint8_t src_nic,
                                   uint8_t *dest_nic) {
    if (!dest_mac || !dest_nic) {
        return ROUTE_DECISION_DROP;
    }
    
    /* Search routing table for MAC-based rules */
    route_entry_t *entry = g_routing_table.entries;
    while (entry) {
        if (entry->rule_type == ROUTE_RULE_MAC_ADDRESS) {
            /* Check if MAC matches with mask */
            if (routing_mac_match_mask(dest_mac, entry->dest_mac, entry->mask)) {
                *dest_nic = entry->dest_nic;
                entry->packet_count++;
                g_routing_stats.table_lookups++;
                return entry->decision;
            }
        }
        entry = entry->next;
    }
    
    g_routing_stats.table_lookups++;
    return ROUTE_DECISION_DROP;
}

route_decision_t routing_lookup_ethertype(uint16_t ethertype, uint8_t src_nic,
                                         uint8_t *dest_nic) {
    if (!dest_nic) {
        return ROUTE_DECISION_DROP;
    }
    
    /* Search routing table for Ethertype-based rules */
    route_entry_t *entry = g_routing_table.entries;
    while (entry) {
        if (entry->rule_type == ROUTE_RULE_ETHERTYPE) {
            if (entry->ethertype == ethertype) {
                *dest_nic = entry->dest_nic;
                entry->packet_count++;
                g_routing_stats.table_lookups++;
                return entry->decision;
            }
        }
        entry = entry->next;
    }
    
    g_routing_stats.table_lookups++;
    return ROUTE_DECISION_DROP;
}

/* Packet processing */
int route_packet(packet_buffer_t *packet, uint8_t src_nic) {
    uint8_t dest_nic;
    route_decision_t decision;

    if (!packet || !routing_is_enabled()) {
        return ERROR_INVALID_PARAM;
    }

    /* Check rate limiting */
    if (!routing_check_rate_limit_internal(src_nic)) {
        g_routing_stats.packets_dropped++;
        return ERROR_BUSY;
    }

    decision = routing_decide(packet, src_nic, &dest_nic);
    
    switch (decision) {
        case ROUTE_DECISION_FORWARD:
            return forward_packet(packet, src_nic, dest_nic);
            
        case ROUTE_DECISION_BROADCAST:
            return broadcast_packet(packet, src_nic);
            
        case ROUTE_DECISION_MULTICAST:
            /* Extract destination MAC for multicast routing */
            {
                const uint8_t *dest_mac = packet->data;
                return multicast_packet(packet, src_nic, dest_mac);
            }
            
        case ROUTE_DECISION_LOOPBACK:
            /* Implement loopback - packet stays on same interface */
            LOG_DEBUG("Loopback packet on NIC %d", src_nic);
            return SUCCESS;
            
        case ROUTE_DECISION_DROP:
        default:
            return SUCCESS; /* Silently drop */
    }
}

int forward_packet(packet_buffer_t *packet, uint8_t src_nic, uint8_t dest_nic) {
    nic_info_t *nic;
    int result;

    if (!packet || !routing_validate_nic(dest_nic)) {
        return ERROR_INVALID_PARAM;
    }

    /* Avoid forwarding back to source */
    if (src_nic == dest_nic) {
        return ERROR_INVALID_PARAM;
    }

    /* Get destination NIC and send packet */
    nic = hardware_get_nic(dest_nic);
    if (!nic || !nic->ops) {
        return ERROR_NOT_FOUND;
    }

    /* Use hardware abstraction to send packet */
    result = hardware_send_packet(nic, packet->data, packet->length);
    if (result == SUCCESS) {
        g_routing_stats.packets_forwarded++;
    } else {
        g_routing_stats.routing_errors++;
    }

    return result;
}

int broadcast_packet(packet_buffer_t *packet, uint8_t src_nic) {
    int errors = 0;
    int sent = 0;
    int i;
    nic_info_t *nic;
    int result;

    if (!packet) {
        return ERROR_INVALID_PARAM;
    }

    /* Send to all NICs except source */
    for (i = 0; i < hardware_get_nic_count(); i++) {
        if (i == src_nic) {
            continue; /* Skip source NIC */
        }

        nic = hardware_get_nic(i);
        if (!nic || !nic->ops) {
            continue;
        }

        result = hardware_send_packet(nic, packet->data, packet->length);
        if (result == SUCCESS) {
            sent++;
        } else {
            errors++;
        }
    }
    
    if (sent > 0) {
        g_routing_stats.packets_broadcast++;
        return SUCCESS;
    } else {
        g_routing_stats.routing_errors++;
        return ERROR_IO;
    }
}

/* Validation functions */
bool routing_validate_nic(uint8_t nic_index) {
    return (nic_index < MAX_NICS) && hardware_is_nic_present(nic_index);
}

/* MAC address utilities */
bool routing_mac_equals(const uint8_t *mac1, const uint8_t *mac2) {
    if (!mac1 || !mac2) {
        return false;
    }
    
    return memory_compare(mac1, mac2, ETH_ALEN) == 0;
}

bool routing_mac_match_mask(const uint8_t *mac, const uint8_t *pattern,
                           const uint8_t *mask) {
    int i;
    if (!mac || !pattern || !mask) {
        return false;
    }

    for (i = 0; i < ETH_ALEN; i++) {
        if ((mac[i] & mask[i]) != (pattern[i] & mask[i])) {
            return false;
        }
    }

    return true;
}

void routing_mac_copy(uint8_t *dest, const uint8_t *src) {
    if (dest && src) {
        memory_copy(dest, src, ETH_ALEN);
    }
}

/* Statistics and monitoring */
void routing_stats_init(routing_stats_t *stats) {
    if (!stats) {
        return;
    }
    
    memory_zero(stats, sizeof(routing_stats_t));
}

const routing_stats_t* routing_get_stats(void) {
    return &g_routing_stats;
}

void routing_clear_stats(void) {
    routing_stats_init(&g_routing_stats);
}

/* Configuration */
int routing_set_learning_enabled(bool enable) {
    g_learning_enabled = enable;
    g_routing_table.learning_enabled = enable;
    return SUCCESS;
}

bool routing_get_learning_enabled(void) {
    return g_learning_enabled;
}

int routing_set_aging_time(uint32_t aging_time_ms) {
    g_aging_time_ms = aging_time_ms;
    g_routing_table.learning_timeout = aging_time_ms;
    g_bridge_table.aging_time = aging_time_ms;
    return SUCCESS;
}

uint32_t routing_get_aging_time(void) {
    return g_aging_time_ms;
}

/* Rate limiting */
int routing_set_rate_limit(uint8_t nic_index, uint32_t packets_per_sec) {
    if (nic_index >= MAX_NICS) {
        return ERROR_INVALID_PARAM;
    }
    
    g_rate_limits[nic_index].packets_per_sec = packets_per_sec;
    g_rate_limits[nic_index].current_count = 0;
    g_rate_limits[nic_index].last_reset_time = routing_get_timestamp();
    
    return SUCCESS;
}

int routing_check_rate_limit(uint8_t nic_index) {
    return routing_check_rate_limit_internal(nic_index) ? SUCCESS : ERROR_BUSY;
}

void routing_update_rate_counters(void) {
    uint32_t current_time;
    int i;
    rate_limit_info_t *limit;

    current_time = routing_get_timestamp();
    for (i = 0; i < MAX_NICS; i++) {
        limit = &g_rate_limits[i];

        /* Reset counter every second */
        if (current_time - limit->last_reset_time >= 1000) {
            limit->current_count = 0;
            limit->last_reset_time = current_time;
        }
    }
}

/* Debug and utility functions */
const char* routing_decision_to_string(route_decision_t decision) {
    switch (decision) {
        case ROUTE_DECISION_DROP:       return "DROP";
        case ROUTE_DECISION_FORWARD:    return "FORWARD";
        case ROUTE_DECISION_BROADCAST:  return "BROADCAST";
        case ROUTE_DECISION_LOOPBACK:   return "LOOPBACK";
        case ROUTE_DECISION_MULTICAST:  return "MULTICAST";
        default:                        return "UNKNOWN";
    }
}

const char* routing_rule_type_to_string(route_rule_type_t rule_type) {
    switch (rule_type) {
        case ROUTE_RULE_NONE:           return "NONE";
        case ROUTE_RULE_MAC_ADDRESS:    return "MAC_ADDRESS";
        case ROUTE_RULE_ETHERTYPE:      return "ETHERTYPE";
        case ROUTE_RULE_PORT:           return "PORT";
        case ROUTE_RULE_VLAN:           return "VLAN";
        case ROUTE_RULE_PRIORITY:       return "PRIORITY";
        default:                        return "UNKNOWN";
    }
}

/* Private helper function implementations */
static route_entry_t* routing_find_entry(route_rule_type_t rule_type, const void *rule_data) {
    if (!rule_data) {
        return NULL;
    }
    
    route_entry_t *entry = g_routing_table.entries;
    while (entry) {
        if (entry->rule_type == rule_type) {
            switch (rule_type) {
                case ROUTE_RULE_MAC_ADDRESS:
                    if (routing_mac_equals(entry->dest_mac, (const uint8_t*)rule_data)) {
                        return entry;
                    }
                    break;
                case ROUTE_RULE_ETHERTYPE:
                    if (entry->ethertype == *(const uint16_t*)rule_data) {
                        return entry;
                    }
                    break;
                default:
                    break;
            }
        }
        entry = entry->next;
    }
    
    return NULL;
}

static bridge_entry_t* bridge_find_entry(const uint8_t *mac) {
    if (!mac) {
        return NULL;
    }
    
    bridge_entry_t *entry = g_bridge_table.entries;
    while (entry) {
        if (routing_mac_equals(entry->mac, mac)) {
            return entry;
        }
        entry = entry->next;
    }
    
    return NULL;
}

static void bridge_add_entry(const uint8_t *mac, uint8_t nic_index) {
    bridge_entry_t *entry = (bridge_entry_t*)memory_alloc(sizeof(bridge_entry_t),
                                                         MEM_TYPE_DRIVER_DATA, 0);
    if (!entry) {
        return;
    }
    
    memory_copy(entry->mac, mac, ETH_ALEN);
    entry->nic_index = nic_index;
    entry->timestamp = routing_get_timestamp();
    entry->packet_count = 1;
    entry->next = g_bridge_table.entries;
    
    g_bridge_table.entries = entry;
    g_bridge_table.entry_count++;
}

static uint32_t routing_get_timestamp(void) {
    return get_system_timestamp_ms();
}

static bool routing_check_rate_limit_internal(uint8_t nic_index) {
    if (nic_index >= MAX_NICS) {
        return false;
    }
    
    rate_limit_info_t *limit = &g_rate_limits[nic_index];
    
    /* No rate limiting if packets_per_sec is 0 */
    if (limit->packets_per_sec == 0) {
        return true;
    }
    
    /* Check if within rate limit */
    if (limit->current_count < limit->packets_per_sec) {
        limit->current_count++;
        return true;
    }
    
    return false;
}

static void bridge_remove_entry(bridge_entry_t *entry) {
    if (!entry) {
        return;
    }
    
    /* Remove from singly-linked list */
    bridge_entry_t **current = &g_bridge_table.entries;
    while (*current) {
        if (*current == entry) {
            *current = entry->next;
            break;
        }
        current = &(*current)->next;
    }
    
    /* Update entry count */
    if (g_bridge_table.entry_count > 0) {
        g_bridge_table.entry_count--;
    }
}

static bridge_entry_t* bridge_find_oldest_entry(void) {
    bridge_entry_t *current = g_bridge_table.entries;
    bridge_entry_t *oldest = NULL;
    uint32_t oldest_timestamp = 0xFFFFFFFF;
    
    /* Find entry with oldest (smallest) timestamp */
    while (current) {
        if (current->timestamp < oldest_timestamp) {
            oldest_timestamp = current->timestamp;
            oldest = current;
        }
        current = current->next;
    }
    
    return oldest;
}

/* Missing bridge_entry_t structure needs prev pointer for doubly linked list */
static void bridge_add_entry_impl(const uint8_t *mac, uint8_t nic_index) {
    bridge_entry_t *entry = (bridge_entry_t*)memory_alloc(sizeof(bridge_entry_t),
                                                         MEM_TYPE_DRIVER_DATA, 0);
    if (!entry) {
        return;
    }
    
    memory_copy(entry->mac, mac, ETH_ALEN);
    entry->nic_index = nic_index;
    entry->timestamp = routing_get_timestamp();
    entry->packet_count = 1;
    entry->next = g_bridge_table.entries;
    
    g_bridge_table.entries = entry;
    g_bridge_table.entry_count++;
}

/* Implementation of remaining functions declared in header file */
int routing_remove_rule(route_rule_type_t rule_type, const void *rule_data) {
    route_entry_t **current;
    route_entry_t *entry;
    bool should_remove;

    if (!rule_data || !routing_is_enabled()) {
        return ERROR_INVALID_PARAM;
    }

    current = &g_routing_table.entries;
    while (*current) {
        entry = *current;
        should_remove = false;
        
        if (entry->rule_type == rule_type) {
            switch (rule_type) {
                case ROUTE_RULE_MAC_ADDRESS:
                    if (routing_mac_equals(entry->dest_mac, (const uint8_t*)rule_data)) {
                        should_remove = true;
                    }
                    break;
                case ROUTE_RULE_ETHERTYPE:
                    if (entry->ethertype == *(const uint16_t*)rule_data) {
                        should_remove = true;
                    }
                    break;
                default:
                    break;
            }
        }
        
        if (should_remove) {
            *current = entry->next;
            memory_zero(entry, sizeof(route_entry_t));
            memory_free(entry);
            g_routing_table.entry_count--;
            return SUCCESS;
        }
        
        current = &entry->next;
    }
    
    return ERROR_NOT_FOUND;
}

route_entry_t* routing_find_rule(route_rule_type_t rule_type, const void *rule_data) {
    return routing_find_entry(rule_type, rule_data);
}

void routing_clear_table(void) {
    routing_table_cleanup(&g_routing_table);
    routing_table_init(&g_routing_table, g_routing_table.max_entries);
}

int routing_set_default_route(uint8_t nic_index, route_decision_t decision) {
    if (!routing_validate_nic(nic_index)) {
        return ERROR_INVALID_PARAM;
    }
    
    g_routing_table.default_nic = nic_index;
    g_routing_table.default_decision = decision;
    
    return SUCCESS;
}

void bridge_age_entries(void) {
    uint32_t current_time;
    uint32_t aged_count;
    bridge_entry_t **current;
    bridge_entry_t *entry;

    if (!g_routing_initialized || !g_learning_enabled) {
        return;
    }

    current_time = routing_get_timestamp();
    aged_count = 0;

    current = &g_bridge_table.entries;
    while (*current) {
        entry = *current;
        
        /* Check if entry has expired */
        if ((current_time - entry->timestamp) > g_bridge_table.aging_time) {
            /* Remove expired entry */
            *current = entry->next;
            
            memory_zero(entry, sizeof(bridge_entry_t));
            memory_free(entry);
            g_bridge_table.entry_count--;
            aged_count++;
        } else {
            current = &entry->next;
        }
    }
    
    g_routing_stats.cache_misses += aged_count; /* Reuse cache_misses for aged entries */
}

void bridge_flush_table(void) {
    bridge_table_cleanup(&g_bridge_table);
    bridge_table_init(&g_bridge_table, g_bridge_table.max_entries);
}

int bridge_remove_mac(const uint8_t *mac) {
    bridge_entry_t *entry;

    if (!mac) {
        return ERROR_INVALID_PARAM;
    }

    entry = bridge_find_entry(mac);
    if (entry) {
        bridge_remove_entry(entry);
        memory_zero(entry, sizeof(bridge_entry_t));
        memory_free(entry);
        return SUCCESS;
    }

    return ERROR_NOT_FOUND;
}

int multicast_packet(packet_buffer_t *packet, uint8_t src_nic,
                    const uint8_t *dest_mac) {
    const uint8_t *ip_header;
    uint8_t protocol;
    uint8_t dest_nic;
    route_decision_t decision;

    if (!packet || !dest_mac) {
        return ERROR_INVALID_PARAM;
    }

    /* For now, implement basic IGMP snooping */
    /* Check if this is an IGMP packet (IP protocol 2) */
    if (packet->length >= ETH_HLEN + 20) { /* Ethernet + IP headers */
        ip_header = packet->data + ETH_HLEN;
        protocol = ip_header[9];

        if (protocol == 2) { /* IGMP */
            /* Forward IGMP to all NICs for multicast management */
            return broadcast_packet(packet, src_nic);
        }
    }

    /* For other multicast packets, use MAC-based forwarding if possible */
    decision = routing_lookup_mac(dest_mac, src_nic, &dest_nic);
    
    if (decision == ROUTE_DECISION_FORWARD) {
        return forward_packet(packet, src_nic, dest_nic);
    }
    
    /* Default to controlled flooding - send to all NICs except source */
    return broadcast_packet(packet, src_nic);
}

/* Additional utility functions */
static uint16_t mac_hash_16bit(const uint8_t *mac) {
    uint16_t hash;

    if (!mac) return 0;

    hash = ((uint16_t)mac[0] << 8) | mac[1];
    hash ^= ((uint16_t)mac[2] << 8) | mac[3];
    hash ^= ((uint16_t)mac[4] << 8) | mac[5];
    hash = (hash << 5) - hash; /* Multiply by 31 */
    return hash & 0x01FF; /* Modulo 512 */
}

bool routing_is_local_mac(const uint8_t *mac) {
    int i;
    if (!mac) {
        return false;
    }

    /* Check against each NIC's MAC address */
    for (i = 0; i < hardware_get_nic_count(); i++) {
        nic_info_t *nic = hardware_get_nic(i);
        if (nic && routing_mac_equals(mac, nic->mac_addr)) {
            return true;
        }
    }

    return false;
}

void routing_stats_update(routing_stats_t *stats, route_decision_t decision) {
    if (!stats) {
        return;
    }
    
    stats->packets_routed++;
    
    switch (decision) {
        case ROUTE_DECISION_FORWARD:
            stats->packets_forwarded++;
            break;
        case ROUTE_DECISION_BROADCAST:
            stats->packets_broadcast++;
            break;
        case ROUTE_DECISION_MULTICAST:
            stats->packets_multicast++;
            break;
        case ROUTE_DECISION_LOOPBACK:
            stats->packets_looped++;
            break;
        case ROUTE_DECISION_DROP:
        default:
            stats->packets_dropped++;
            break;
    }
}

void routing_print_stats(void) {
    const routing_stats_t *stats = &g_routing_stats;
    
    log_info("=== Routing Statistics ===");
    log_info("Packets Routed:    %lu", stats->packets_routed);
    log_info("Packets Forwarded: %lu", stats->packets_forwarded);
    log_info("Packets Broadcast: %lu", stats->packets_broadcast);
    log_info("Packets Multicast: %lu", stats->packets_multicast);
    log_info("Packets Looped:    %lu", stats->packets_looped);
    log_info("Packets Dropped:   %lu", stats->packets_dropped);
    log_info("Routing Errors:    %lu", stats->routing_errors);
    log_info("Table Lookups:     %lu", stats->table_lookups);
    log_info("Cache Hits:        %lu", stats->cache_hits);
    log_info("Cache Misses:      %lu", stats->cache_misses);
}

void routing_print_table(void) {
    route_entry_t *entry;
    int count;

    if (!routing_is_enabled()) {
        log_info("Routing is not enabled");
        return;
    }

    log_info("=== Routing Table ===");
    log_info("Entries: %d/%d", g_routing_table.entry_count, g_routing_table.max_entries);

    entry = g_routing_table.entries;
    count = 0;
    while (entry && count < 20) { /* Limit output for readability */
        log_info("Rule %d: Type=%s, SRC=%d, DST=%d, Decision=%s, Priority=%d",
                count + 1,
                routing_rule_type_to_string(entry->rule_type),
                entry->src_nic,
                entry->dest_nic,
                routing_decision_to_string(entry->decision),
                entry->priority);
        
        if (entry->rule_type == ROUTE_RULE_MAC_ADDRESS) {
            log_info("  MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                    entry->dest_mac[0], entry->dest_mac[1], entry->dest_mac[2],
                    entry->dest_mac[3], entry->dest_mac[4], entry->dest_mac[5]);
        } else if (entry->rule_type == ROUTE_RULE_ETHERTYPE) {
            log_info("  EtherType: 0x%04X", entry->ethertype);
        }
        
        entry = entry->next;
        count++;
    }
    
    if (entry) {
        log_info("... (%d more entries not shown)", g_routing_table.entry_count - count);
    }
}

void routing_print_bridge_table(void) {
    bridge_entry_t *entry;
    int count;

    if (!g_routing_initialized) {
        log_info("Bridge table not initialized");
        return;
    }

    log_info("=== Bridge Learning Table ===");
    log_info("Entries: %d/%d", g_bridge_table.entry_count, g_bridge_table.max_entries);
    log_info("Lookups: %lu total, %lu successful (%.1f%% hit rate)",
            g_bridge_table.total_lookups,
            g_bridge_table.successful_lookups,
            g_bridge_table.total_lookups > 0 ?
                (100.0f * g_bridge_table.successful_lookups) / g_bridge_table.total_lookups : 0.0f);

    entry = g_bridge_table.entries;
    count = 0;
    while (entry && count < 20) { /* Limit output */
        log_info("Bridge %d: %02X:%02X:%02X:%02X:%02X:%02X -> NIC %d (packets: %lu)",
                count + 1,
                entry->mac[0], entry->mac[1], entry->mac[2],
                entry->mac[3], entry->mac[4], entry->mac[5],
                entry->nic_index,
                entry->packet_count);
        
        entry = entry->next;
        count++;
    }
    
    if (entry) {
        log_info("... (%d more entries not shown)", g_bridge_table.entry_count - count);
    }
}

bool routing_should_forward(const packet_buffer_t *packet, uint8_t src_nic, uint8_t dest_nic) {
    if (!packet || src_nic == dest_nic) {
        return false;
    }
    
    if (!routing_validate_nic(src_nic) || !routing_validate_nic(dest_nic)) {
        return false;
    }
    
    /* Don't forward if destination NIC is not active */
    nic_info_t *dest_nic_info = hardware_get_nic(dest_nic);
    if (!dest_nic_info || !dest_nic_info->enabled) {
        return false;
    }
    
    return true;
}

bool routing_is_loop(const packet_buffer_t *packet, uint8_t src_nic, uint8_t dest_nic) {
    /* Simple loop detection - forwarding back to source NIC */
    if (src_nic == dest_nic) {
        return true;
    }
    
    /* Additional loop detection could be implemented here */
    /* For example, checking for broadcast loops, TTL, etc. */
    
    return false;
}

int routing_set_table_size(uint16_t max_entries) {
    if (g_routing_table.entry_count > 0) {
        return ERROR_BUSY; /* Cannot resize active table */
    }
    
    g_routing_table.max_entries = max_entries;
    return SUCCESS;
}

void routing_dump_table(void) {
    routing_print_table();
}

void routing_dump_bridge_table(void) {
    routing_print_bridge_table();
}

void routing_dump_packet_route(const packet_buffer_t *packet, uint8_t src_nic) {
    const uint8_t *eth_header;
    const uint8_t *dest_mac;
    const uint8_t *src_mac;
    uint16_t ethertype;
    uint8_t dest_nic;
    route_decision_t decision;

    if (!packet || !packet->data) {
        log_info("Invalid packet for route dump");
        return;
    }

    log_info("=== Packet Route Analysis ===");
    log_info("Source NIC: %d", src_nic);
    log_info("Packet Length: %d bytes", packet->length);

    if (packet->length >= ETH_HLEN) {
        eth_header = packet->data;
        dest_mac = eth_header;
        src_mac = eth_header + ETH_ALEN;
        ethertype = ntohs(*(uint16_t*)(eth_header + 2 * ETH_ALEN));

        log_info("Destination MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                dest_mac[0], dest_mac[1], dest_mac[2],
                dest_mac[3], dest_mac[4], dest_mac[5]);
        log_info("Source MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                src_mac[0], src_mac[1], src_mac[2],
                src_mac[3], src_mac[4], src_mac[5]);
        log_info("EtherType: 0x%04X", ethertype);

        decision = routing_decide(packet, src_nic, &dest_nic);

        log_info("Routing Decision: %s", routing_decision_to_string(decision));
        if (decision == ROUTE_DECISION_FORWARD) {
            log_info("Destination NIC: %d", dest_nic);
        }
    }
}

/* Self-test and validation functions */
int routing_self_test(void) {
    log_info("Running routing self-test...");
    
    /* Test basic routing functions */
    if (!routing_is_enabled()) {
        log_info("Routing is not enabled - enabling for test");
        if (routing_enable(true) != SUCCESS) {
            log_error("Failed to enable routing");
            return ERROR_FAILED;
        }
    }
    
    /* Test MAC utilities */
    uint8_t mac1[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t mac2[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t mac3[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    
    if (!routing_mac_equals(mac1, mac2)) {
        log_error("MAC comparison failed - identical MACs not equal");
        return ERROR_FAILED;
    }
    
    if (routing_mac_equals(mac1, mac3)) {
        log_error("MAC comparison failed - different MACs are equal");
        return ERROR_FAILED;
    }
    
    /* Test hash function */
    uint16_t hash1 = mac_hash_16bit(mac1);
    uint16_t hash2 = mac_hash_16bit(mac2);
    uint16_t hash3 = mac_hash_16bit(mac3);
    
    if (hash1 != hash2) {
        log_error("Hash function failed - identical MACs have different hashes");
        return ERROR_FAILED;
    }
    
    /* Different MACs should generally have different hashes (not guaranteed, but likely) */
    log_info("MAC hash test: %04X vs %04X", hash1, hash3);
    
    log_info("Routing self-test completed successfully");
    return SUCCESS;
}

int routing_validate_configuration(void) {
    route_entry_t *entry;
    int count;
    bridge_entry_t *bridge_entry;

    if (!g_routing_initialized) {
        log_error("Routing not initialized");
        return ERROR_NOT_FOUND;
    }

    /* Validate routing table integrity */
    entry = g_routing_table.entries;
    count = 0;

    while (entry) {
        count++;

        /* Validate NIC indices */
        if (!routing_validate_nic(entry->src_nic) || !routing_validate_nic(entry->dest_nic)) {
            log_error("Invalid NIC index in routing entry");
            return ERROR_INVALID_PARAM;
        }

        /* Check for circular references */
        if (count > g_routing_table.max_entries) {
            log_error("Circular reference detected in routing table");
            return ERROR_FAILED;
        }

        entry = entry->next;
    }

    if (count != g_routing_table.entry_count) {
        log_error("Routing table count mismatch: counted %d, expected %d",
                 count, g_routing_table.entry_count);
        return ERROR_FAILED;
    }

    /* Validate bridge table integrity */
    bridge_entry = g_bridge_table.entries;
    count = 0;
    
    while (bridge_entry) {
        count++;
        
        if (!routing_validate_nic(bridge_entry->nic_index)) {
            log_error("Invalid NIC index in bridge entry");
            return ERROR_INVALID_PARAM;
        }
        
        if (count > g_bridge_table.max_entries) {
            log_error("Circular reference detected in bridge table");
            return ERROR_FAILED;
        }
        
        bridge_entry = bridge_entry->next;
    }
    
    if (count != g_bridge_table.entry_count) {
        log_error("Bridge table count mismatch: counted %d, expected %d",
                 count, g_bridge_table.entry_count);
        return ERROR_FAILED;
    }
    
    log_info("Routing configuration validation successful");
    return SUCCESS;
}

int routing_test_forwarding(uint8_t src_nic, uint8_t dest_nic) {
    if (!routing_validate_nic(src_nic) || !routing_validate_nic(dest_nic)) {
        return ERROR_INVALID_PARAM;
    }
    
    if (src_nic == dest_nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Check if both NICs are available and operational */
    nic_info_t *src_nic_info = hardware_get_nic(src_nic);
    nic_info_t *dest_nic_info = hardware_get_nic(dest_nic);
    
    if (!src_nic_info || !dest_nic_info) {
        return ERROR_NOT_FOUND;
    }
    
    if (!src_nic_info->enabled || !dest_nic_info->enabled) {
        return ERROR_NOT_READY;
    }
    
    log_info("Forwarding test: NIC %d -> NIC %d: OK", src_nic, dest_nic);
    return SUCCESS;
}

/* ============================================================================
 * Multi-NIC Failover Support
 * ============================================================================ */

/* Failover state structure - explicit fields for portability */
static volatile struct {
    volatile uint8_t primary_nic;           /* Primary NIC index (0-7) */
    volatile uint8_t secondary_nic;         /* Secondary NIC index (0-7) */
    volatile uint8_t active_nic;            /* Currently active NIC */
    volatile uint8_t failover_active;       /* Failover in progress (0/1) */
    volatile uint8_t storm_prevention;      /* Storm prevention active (0/1) */
    volatile uint8_t degraded_mode;         /* Both NICs down (0/1) */
    uint8_t pad[2];                          /* Padding for alignment */
} g_failover_state = {0, 1, 0, 0, 0, 0, {0, 0}};

/* Failover statistics - accessed from ISR and main context */
static volatile struct {
    volatile uint32_t failover_count;       /* Total failovers */
    volatile uint32_t failback_count;       /* Total failbacks */
    volatile uint32_t link_loss_events;     /* Link loss detections */
    volatile uint32_t storm_prevented;      /* Storm prevention activations */
    volatile uint32_t last_failover_time;   /* Timestamp of last failover */
    volatile uint32_t last_link_check;      /* Timestamp of last link check */
} g_failover_stats = {0, 0, 0, 0, 0, 0};

/* Failover configuration - configurable thresholds */
static struct {
    uint32_t link_check_interval_ms;   /* Check link interval (default 1000ms) */
    uint32_t link_loss_threshold;      /* Consecutive failures before failover (default 3) */
    uint32_t storm_prevention_ms;      /* Minimum time between failovers (default 5000ms) */
    uint32_t failback_delay_ms;        /* Wait before failing back to primary (default 10000ms) */
    uint32_t link_stable_ms;           /* Link must be stable this long before use (default 2000ms) */
} g_failover_config = {1000, 3, 5000, 10000, 2000};

/* Link monitoring state - accessed from timer ISR */
static volatile uint8_t g_link_loss_count[MAX_NICS] = {0};
static volatile uint32_t g_last_link_up_time[MAX_NICS] = {0};

/**
 * @brief Configure multi-NIC failover
 * @param primary_nic Primary NIC index
 * @param secondary_nic Secondary NIC index
 * @return SUCCESS or error code
 */
int routing_configure_failover(uint8_t primary_nic, uint8_t secondary_nic) {
    if (primary_nic >= MAX_NICS || secondary_nic >= MAX_NICS) {
        return ERROR_INVALID_PARAM;
    }
    
    if (primary_nic == secondary_nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Verify both NICs exist and are operational */
    nic_info_t *primary = hardware_get_nic(primary_nic);
    nic_info_t *secondary = hardware_get_nic(secondary_nic);
    
    if (!primary || !secondary) {
        return ERROR_NOT_FOUND;
    }
    
    if (!(primary->status & NIC_STATUS_PRESENT) || 
        !(secondary->status & NIC_STATUS_PRESENT)) {
        return ERROR_NOT_READY;
    }
    
    /* Configure failover state */
    g_failover_state.primary_nic = primary_nic;
    g_failover_state.secondary_nic = secondary_nic;
    g_failover_state.active_nic = primary_nic;
    g_failover_state.failover_active = 0;
    
    /* Reset counters */
    g_link_loss_count[primary_nic] = 0;
    g_link_loss_count[secondary_nic] = 0;
    
    LOG_INFO("Failover configured: Primary=NIC%d, Secondary=NIC%d", 
             primary_nic, secondary_nic);
    
    return SUCCESS;
}

/**
 * @brief Configure failover thresholds
 * @param link_check_ms Link check interval in milliseconds (0=use default)
 * @param loss_threshold Consecutive link losses before failover (0=use default)
 * @param storm_ms Storm prevention timeout in milliseconds (0=use default)
 * @param failback_ms Failback delay in milliseconds (0=use default)
 * @param link_stable_ms Link stability requirement in milliseconds (0=use default)
 * @return SUCCESS or error code
 */
int routing_set_failover_thresholds(uint32_t link_check_ms, uint32_t loss_threshold,
                                    uint32_t storm_ms, uint32_t failback_ms,
                                    uint32_t link_stable_ms) {
    /* Apply new thresholds (0 means keep existing value) */
    if (link_check_ms > 0) {
        if (link_check_ms < 100 || link_check_ms > 60000) {
            return ERROR_INVALID_PARAM;  /* Range: 100ms to 60s */
        }
        g_failover_config.link_check_interval_ms = link_check_ms;
    }
    
    if (loss_threshold > 0) {
        if (loss_threshold < 1 || loss_threshold > 10) {
            return ERROR_INVALID_PARAM;  /* Range: 1 to 10 failures */
        }
        g_failover_config.link_loss_threshold = loss_threshold;
    }
    
    if (storm_ms > 0) {
        if (storm_ms < 1000 || storm_ms > 300000) {
            return ERROR_INVALID_PARAM;  /* Range: 1s to 5min */
        }
        g_failover_config.storm_prevention_ms = storm_ms;
    }
    
    if (failback_ms > 0) {
        if (failback_ms < 1000 || failback_ms > 600000) {
            return ERROR_INVALID_PARAM;  /* Range: 1s to 10min */
        }
        g_failover_config.failback_delay_ms = failback_ms;
    }
    
    if (link_stable_ms > 0) {
        if (link_stable_ms < 100 || link_stable_ms > 30000) {
            return ERROR_INVALID_PARAM;  /* Range: 100ms to 30s */
        }
        g_failover_config.link_stable_ms = link_stable_ms;
    }
    
    LOG_INFO("Failover thresholds: check=%lums, loss=%lu, storm=%lums, failback=%lums, stable=%lums",
             g_failover_config.link_check_interval_ms,
             g_failover_config.link_loss_threshold,
             g_failover_config.storm_prevention_ms,
             g_failover_config.failback_delay_ms,
             g_failover_config.link_stable_ms);
    
    return SUCCESS;
}

/**
 * @brief Check NIC link status
 * @param nic_index NIC to check
 * @return true if link is up, false otherwise
 */
static bool check_nic_link_status(uint8_t nic_index) {
    nic_info_t *nic = hardware_get_nic(nic_index);
    if (!nic) return false;
    
    /* Check if NIC reports link up */
    if (nic->link_status == NIC_LINK_UP) {
        return true;
    }
    
    /* For MII-capable NICs, check PHY status */
    if (nic->mii_capable && nic->phy_address != PHY_ADDR_INVALID) {
        uint16_t bmsr;
        uint16_t flags;
        int timeout;
        int i;

        /* Select Window 4 for MII access */
        _3C515_TX_SELECT_WINDOW(nic->io_base, 4);

        /* Double-read BMSR to clear latched bits */
        for (i = 0; i < 2; i++) {
            timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
            
            /* Wait for MII ready */
            while (--timeout > 0) {
                if (!(inw(nic->io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) break;
                nic_delay_microseconds(MII_POLL_DELAY_US);
            }
            
            if (timeout > 0) {
                /* Issue read command with brief interrupt disable */
                _asm {
                    pushf
                    pop ax
                    mov flags, ax
                    cli
                }
                outw(nic->io_base + _3C515_MII_CMD,
                     MII_CMD_READ | (nic->phy_address << MII_CMD_PHY_SHIFT) | 
                     (MII_BMSR << MII_CMD_REG_SHIFT));
                _asm {
                    mov ax, flags
                    push ax
                    popf
                }
                
                /* Wait for completion */
                timeout = MII_POLL_TIMEOUT_US / MII_POLL_DELAY_US;
                while (--timeout > 0) {
                    if (!(inw(nic->io_base + _3C515_MII_CMD) & MII_CMD_BUSY)) break;
                    nic_delay_microseconds(MII_POLL_DELAY_US);
                }
                
                if (timeout > 0) {
                    bmsr = inw(nic->io_base + _3C515_MII_DATA);
                    /* On second read, check link status */
                    if (i == 1) {
                        nic->link_status = (bmsr & BMSR_LSTATUS) ? NIC_LINK_UP : NIC_LINK_DOWN;
                        return (bmsr & BMSR_LSTATUS) ? true : false;
                    }
                }
            }
        }
    }
    
    return false;
}

/**
 * @brief Perform NIC failover
 * @param from_nic NIC failing over from
 * @param to_nic NIC failing over to
 * @return SUCCESS or error code
 */
static int perform_failover(uint8_t from_nic, uint8_t to_nic) {
    uint32_t current_time;
    uint32_t link_up_duration;
    nic_info_t *from_nic_info;
    nic_info_t *to_nic_info;
    int result;
    bridge_entry_t *entry;

    current_time = routing_get_timestamp();

    /* Storm prevention check */
    if (g_failover_state.storm_prevention) {
        if ((current_time - g_failover_stats.last_failover_time) < g_failover_config.storm_prevention_ms) {
            g_failover_stats.storm_prevented++;
            LOG_WARNING("Failover storm prevention active - skipping failover");
            return ERROR_BUSY;
        }
    }

    /* Verify target NIC has stable link */
    if (!check_nic_link_status(to_nic)) {
        LOG_ERROR("Cannot failover to NIC%d - no link", to_nic);
        return ERROR_NOT_READY;
    }

    /* Check link stability - must be up for configured period */
    if (g_last_link_up_time[to_nic] > 0) {
        link_up_duration = current_time - g_last_link_up_time[to_nic];
        if (link_up_duration < g_failover_config.link_stable_ms) {
            LOG_WARNING("NIC%d link not stable yet (%lums < %lums required)",
                       to_nic, link_up_duration, g_failover_config.link_stable_ms);
            return ERROR_NOT_READY;
        }
    }

    /* Get NIC handles */
    from_nic_info = hardware_get_nic(from_nic);
    to_nic_info = hardware_get_nic(to_nic);

    if (!from_nic_info || !to_nic_info) {
        LOG_ERROR("Invalid NIC handles during failover");
        return ERROR_INVALID_PARAM;
    }

    /* Stop the failing NIC to ensure clean state */
    LOG_INFO("Stopping NIC%d before failover", from_nic);
    if (from_nic_info->ops && from_nic_info->ops->stop) {
        from_nic_info->ops->stop(from_nic_info);
    }

    /* Ensure target NIC is started and ready */
    LOG_INFO("Starting NIC%d for failover", to_nic);
    if (to_nic_info->ops && to_nic_info->ops->start) {
        result = to_nic_info->ops->start(to_nic_info);
        if (result != SUCCESS) {
            LOG_ERROR("Failed to start NIC%d: %d", to_nic, result);
            /* Try to restart the original NIC */
            if (from_nic_info->ops && from_nic_info->ops->start) {
                from_nic_info->ops->start(from_nic_info);
            }
            return result;
        }
    }
    
    /* Perform failover with atomic state update */
    {
        uint16_t flags;
        
        /* Disable interrupts for atomic state transition */
        _asm {
            pushf
            pop ax
            mov flags, ax
            cli
        }
        
        /* Update all state atomically */
        g_failover_state.active_nic = to_nic;
        g_failover_state.failover_active = 1;
        g_failover_stats.failover_count++;
        g_failover_stats.last_failover_time = current_time;
        g_routing_table.default_nic = to_nic;
        
        /* Restore interrupts */
        _asm {
            mov ax, flags
            push ax
            popf
        }
    }
    
    /* Clear bridge table to force relearning */
    entry = g_bridge_table.entries;
    while (entry) {
        bridge_entry_t *next = entry->next;
        if (entry->nic_index == from_nic) {
            bridge_remove_entry(entry);
        }
        entry = next;
    }

    LOG_INFO("FAILOVER: NIC%d -> NIC%d (link loss on primary)", from_nic, to_nic);

    /* Send gratuitous ARPs to update neighbor caches with new NIC's MAC */
    if (to_nic_info && to_nic_info->ip_configured) {
        ip_addr_t local_ip;
        int arp_result;

        local_ip.addr = to_nic_info->ip_addr;

        /* Send burst of 3 gratuitous ARPs with 100ms spacing for reliability */
        arp_result = arp_send_gratuitous_burst(&local_ip, to_nic, 3, 100);
        if (arp_result != SUCCESS) {
            LOG_WARNING("Failed to send gratuitous ARP burst: %d", arp_result);
        } else {
            LOG_DEBUG("Sent gratuitous ARP burst for IP %08lX on NIC%d",
                     local_ip.addr, to_nic);
        }
    }
    
    /* Enable storm prevention for next interval */
    g_failover_state.storm_prevention = 1;
    
    return SUCCESS;
}

/**
 * @brief Monitor link status and handle failover
 * @return SUCCESS or error code
 * 
 * This should be called periodically (e.g., from timer interrupt or main loop)
 */
int routing_monitor_failover(void) {
    uint32_t current_time;
    uint32_t last_check;
    uint32_t last_failover;
    uint8_t active;
    uint8_t primary;
    uint8_t secondary;
    bool active_link_up;
    bool primary_link_up;
    bool secondary_link_up;
    uint8_t target_nic;
    int result;

    current_time = routing_get_timestamp();
    last_check = atomic_time_read(&g_failover_stats.last_link_check);

    /* Rate limit link checks */
    if ((current_time - last_check) < g_failover_config.link_check_interval_ms) {
        return SUCCESS;
    }

    /* UPDATE the last check time atomically */
    atomic_time_write(&g_failover_stats.last_link_check, current_time);

    /* Clear storm prevention after timeout */
    if (g_failover_state.storm_prevention) {
        last_failover = atomic_time_read(&g_failover_stats.last_failover_time);
        if ((current_time - last_failover) >= g_failover_config.storm_prevention_ms) {
            g_failover_state.storm_prevention = 0;
        }
    }

    /* Check BOTH NICs link status for proper failback timing */
    active = g_failover_state.active_nic;
    primary = g_failover_state.primary_nic;
    secondary = g_failover_state.secondary_nic;

    /* Update link status for both NICs */
    active_link_up = check_nic_link_status(active);
    primary_link_up = check_nic_link_status(primary);
    secondary_link_up = check_nic_link_status(secondary);
    
    /* Check for degraded state (both NICs down) */
    if (!primary_link_up && !secondary_link_up) {
        if (!g_failover_state.degraded_mode) {
            uint16_t flags;
            
            /* Atomic transition to degraded mode */
            _asm {
                pushf
                pop ax
                mov flags, ax
                cli
            }
            
            g_failover_state.degraded_mode = 1;
            g_routing_table.default_decision = ROUTE_DECISION_DROP;
            
            _asm {
                mov ax, flags
                push ax
                popf
            }
            
            LOG_ERROR("DEGRADED MODE: Both primary and secondary NICs have no link!");
        }
        return SUCCESS;  /* Nothing more we can do */
    } else if (g_failover_state.degraded_mode) {
        uint16_t flags;
        uint8_t selected_nic = primary_link_up ? primary : secondary;
        
        /* Atomic recovery from degraded mode */
        _asm {
            pushf
            pop ax
            mov flags, ax
            cli
        }
        
        g_failover_state.degraded_mode = 0;
        g_failover_state.active_nic = selected_nic;
        g_routing_table.default_decision = ROUTE_DECISION_FORWARD;
        g_routing_table.default_nic = selected_nic;
        
        _asm {
            mov ax, flags
            push ax
            popf
        }
        
        LOG_INFO("RECOVERY: Exiting degraded mode - using %s NIC%d", 
                 primary_link_up ? "primary" : "secondary", selected_nic);
    }
    
    /* Track up-time for both NICs regardless of which is active */
    if (primary_link_up) {
        if (g_link_loss_count[primary] > 0) {
            g_last_link_up_time[primary] = current_time;
            g_link_loss_count[primary] = 0;
        }
    } else {
        g_link_loss_count[primary]++;
    }
    
    if (active_link_up) {
        /* Active link is up - reset loss counter */
        g_link_loss_count[active] = 0;
        
        /* Check if we should fail back to primary */
        if (g_failover_state.failover_active &&
            active == g_failover_state.secondary_nic) {

            /* Check if primary has been up long enough (already checked above) */
            if (primary_link_up) {
                uint32_t primary_up_time = current_time -
                    g_last_link_up_time[primary];

                if (primary_up_time >= g_failover_config.failback_delay_ms) {
                    /* Fail back to primary */
                    g_failover_state.active_nic = g_failover_state.primary_nic;
                    g_failover_state.failover_active = 0;
                    g_routing_table.default_nic = g_failover_state.primary_nic;
                    g_failover_stats.failback_count++;

                    LOG_INFO("FAILBACK: NIC%d -> NIC%d (primary restored)",
                             g_failover_state.secondary_nic,
                             g_failover_state.primary_nic);
                }
            }
        }
    } else {
        /* Link is down - increment loss counter */
        g_link_loss_count[active]++;
        g_failover_stats.link_loss_events++;

        /* Check if we should failover */
        if (g_link_loss_count[active] >= g_failover_config.link_loss_threshold) {
            /* Determine failover target */
            if (active == g_failover_state.primary_nic) {
                target_nic = g_failover_state.secondary_nic;
            } else {
                target_nic = g_failover_state.primary_nic;
            }

            /* Attempt failover */
            result = perform_failover(active, target_nic);

            /* Only reset loss counter if failover succeeded */
            if (result == SUCCESS) {
                g_link_loss_count[active] = 0;
            } else {
                /* Failover failed - try again next interval with backoff */
                LOG_WARNING("Failover failed: %d", result);
            }
        }
    }
    
    return SUCCESS;
}

/**
 * @brief Get failover status
 * @param primary Output: primary NIC index
 * @param secondary Output: secondary NIC index
 * @param active Output: active NIC index
 * @return true if failover is configured
 */
bool routing_get_failover_status(uint8_t *primary, uint8_t *secondary, uint8_t *active) {
    if (primary) *primary = g_failover_state.primary_nic;
    if (secondary) *secondary = g_failover_state.secondary_nic;
    if (active) *active = g_failover_state.active_nic;
    
    return (g_failover_state.primary_nic != g_failover_state.secondary_nic);
}

/**
 * @brief Get failover statistics
 */
void routing_get_failover_stats(uint32_t *failovers, uint32_t *failbacks, 
                                uint32_t *link_losses, uint32_t *storms_prevented) {
    if (failovers) *failovers = g_failover_stats.failover_count;
    if (failbacks) *failbacks = g_failover_stats.failback_count;
    if (link_losses) *link_losses = g_failover_stats.link_loss_events;
    if (storms_prevented) *storms_prevented = g_failover_stats.storm_prevented;
}

/**
 * @brief Check if system is in degraded mode
 * @return true if both NICs are down
 */
bool routing_is_degraded(void) {
    return g_failover_state.degraded_mode != 0;
}

/* Additional MAC address utility functions */
bool is_broadcast_mac(const uint8_t *mac) {
    if (!mac) return false;
    return (mac[0] == 0xFF && mac[1] == 0xFF && mac[2] == 0xFF &&
            mac[3] == 0xFF && mac[4] == 0xFF && mac[5] == 0xFF);
}

bool is_multicast_mac(const uint8_t *mac) {
    if (!mac) return false;
    return (mac[0] & 0x01) != 0;
}

bool is_unicast_mac(const uint8_t *mac) {
    if (!mac) return false;
    return (mac[0] & 0x01) == 0;
}

/* Network byte order conversion utilities */
uint16_t ntohs(uint16_t netshort) {
    return ((netshort & 0xFF) << 8) | ((netshort & 0xFF00) >> 8);
}

uint16_t htons(uint16_t hostshort) {
    return ntohs(hostshort); /* Same operation for 16-bit values */
}

uint32_t ntohl(uint32_t netlong) {
    /* Use BSWAP instruction on 486+ for optimal performance */
    extern cpu_info_t g_cpu_info;  /* From cpu_detect module */
    
    if (g_cpu_info.features & FEATURE_BSWAP) {
        /* Use inline assembly for BSWAP on 486+ */
        uint32_t result = netlong;
        _asm {
            mov eax, result
            bswap eax
            mov result, eax
        }
        return result;
    }
    
    /* Fallback for 286/386 */
    return ((netlong & 0xFF) << 24) |
           ((netlong & 0xFF00) << 8) |
           ((netlong & 0xFF0000) >> 8) |
           ((netlong & 0xFF000000) >> 24);
}

uint32_t htonl(uint32_t hostlong) {
    return ntohl(hostlong); /* Same operation */
}

