/**
 * @file static_routing.c
 * @brief Static subnet-based routing
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#include "statrt.h"
#include "arp.h"
#include "logging.h"
#include "common.h"

/* Global static routing state */
static_routing_table_t g_static_routing_table;
arp_table_t g_arp_table;
static_routing_stats_t g_static_routing_stats;
bool g_static_routing_enabled = false;

/* Private state */
static bool g_static_routing_initialized = false;

/* Internal helper functions */
static static_route_t* static_route_find_best_match(const ip_addr_t *dest_ip);
static subnet_info_t* static_subnet_find_containing(const ip_addr_t *ip);
static void static_routing_update_stats_lookup(bool hit);
static uint32_t static_routing_get_timestamp(void);

/* Static routing initialization and cleanup */
int static_routing_init(void) {
    if (g_static_routing_initialized) {
        return SUCCESS;
    }
    
    /* Initialize routing table */
    int result = static_routing_table_init(&g_static_routing_table, 128, 32);
    if (result != SUCCESS) {
        return result;
    }
    
    /* Initialize ARP table */
    result = arp_table_init(&g_arp_table, 256);
    if (result != SUCCESS) {
        static_routing_table_cleanup(&g_static_routing_table);
        return result;
    }
    
    /* Initialize statistics */
    static_routing_stats_init(&g_static_routing_stats);
    
    /* Set default gateway to zero (no default gateway) */
    ip_addr_set(&g_static_routing_table.default_gateway, 0, 0, 0, 0);
    g_static_routing_table.default_nic = 0;
    
    g_static_routing_initialized = true;
    g_static_routing_enabled = false; /* Must be explicitly enabled */
    
    return SUCCESS;
}

void static_routing_cleanup(void) {
    if (!g_static_routing_initialized) {
        return;
    }
    
    /* Cleanup tables */
    static_routing_table_cleanup(&g_static_routing_table);
    arp_table_cleanup(&g_arp_table);
    
    /* Clear statistics */
    static_routing_stats_init(&g_static_routing_stats);
    
    g_static_routing_initialized = false;
    g_static_routing_enabled = false;
}

int static_routing_enable(bool enable) {
    if (!g_static_routing_initialized) {
        return ERROR_NOT_FOUND;
    }
    
    g_static_routing_enabled = enable;
    return SUCCESS;
}

bool static_routing_is_enabled(void) {
    return g_static_routing_enabled && g_static_routing_initialized;
}

/* Static routing table management */
int static_routing_table_init(static_routing_table_t *table, uint16_t max_routes, uint16_t max_subnets) {
    if (!table) {
        return ERROR_INVALID_PARAM;
    }
    
    table->routes = NULL;
    table->subnets = NULL;
    table->route_count = 0;
    table->max_routes = max_routes;
    table->subnet_count = 0;
    table->max_subnets = max_subnets;
    ip_addr_set(&table->default_gateway, 0, 0, 0, 0);
    table->default_nic = 0;
    table->initialized = true;
    
    return SUCCESS;
}

void static_routing_table_cleanup(static_routing_table_t *table) {
    if (!table || !table->initialized) {
        return;
    }
    
    /* Free all routes */
    static_route_t *route = table->routes;
    while (route) {
        static_route_t *next = route->next;
        memory_free(route);
        route = next;
    }
    
    /* Free all subnets */
    subnet_info_t *subnet = table->subnets;
    while (subnet) {
        subnet_info_t *next = subnet->next;
        memory_free(subnet);
        subnet = next;
    }
    
    table->routes = NULL;
    table->subnets = NULL;
    table->route_count = 0;
    table->subnet_count = 0;
    table->initialized = false;
}

/* Route management */
int static_route_add(const ip_addr_t *dest_network, const ip_addr_t *netmask,
                    const ip_addr_t *gateway, uint8_t nic_index, uint8_t metric) {
    if (!dest_network || !netmask || !static_routing_is_enabled()) {
        return ERROR_INVALID_PARAM;
    }
    
    if (g_static_routing_table.route_count >= g_static_routing_table.max_routes) {
        return ERROR_NO_MEMORY;
    }
    
    /* Check if route already exists */
    static_route_t *existing = static_route_find_exact(dest_network, netmask);
    if (existing) {
        /* Update existing route */
        if (gateway) {
            ip_addr_copy(&existing->gateway, gateway);
            existing->flags |= STATIC_ROUTE_FLAG_GATEWAY;
        } else {
            ip_addr_set(&existing->gateway, 0, 0, 0, 0);
            existing->flags &= ~STATIC_ROUTE_FLAG_GATEWAY;
        }
        existing->dest_nic = nic_index;
        existing->metric = metric;
        existing->flags |= STATIC_ROUTE_FLAG_MODIFIED;
        existing->age = static_routing_get_timestamp();
        return SUCCESS;
    }
    
    /* Create new route */
    static_route_t *route = (static_route_t*)memory_alloc(sizeof(static_route_t),
                                                        MEM_TYPE_DRIVER_DATA, 0);
    if (!route) {
        return ERROR_NO_MEMORY;
    }
    
    /* Initialize route */
    ip_addr_copy(&route->dest_network, dest_network);
    ip_addr_copy(&route->netmask, netmask);
    route->dest_nic = nic_index;
    route->metric = metric;
    route->flags = STATIC_ROUTE_FLAG_UP;
    route->age = static_routing_get_timestamp();
    
    if (gateway && !ip_addr_is_zero(gateway)) {
        ip_addr_copy(&route->gateway, gateway);
        route->flags |= STATIC_ROUTE_FLAG_GATEWAY;
    } else {
        ip_addr_set(&route->gateway, 0, 0, 0, 0);
    }
    
    /* Add to route list (sorted by metric - lower is better) */
    static_route_t **current = &g_static_routing_table.routes;
    while (*current && (*current)->metric <= metric) {
        current = &(*current)->next;
    }
    
    route->next = *current;
    *current = route;
    g_static_routing_table.route_count++;
    
    g_static_routing_stats.routes_added++;
    
    return SUCCESS;
}

int static_route_delete(const ip_addr_t *dest_network, const ip_addr_t *netmask) {
    if (!dest_network || !netmask) {
        return ERROR_INVALID_PARAM;
    }
    
    static_route_t **current = &g_static_routing_table.routes;
    while (*current) {
        if (ip_addr_equals(&(*current)->dest_network, dest_network) &&
            ip_addr_equals(&(*current)->netmask, netmask)) {
            
            static_route_t *to_delete = *current;
            *current = (*current)->next;
            memory_free(to_delete);
            g_static_routing_table.route_count--;
            g_static_routing_stats.routes_deleted++;
            return SUCCESS;
        }
        current = &(*current)->next;
    }
    
    return ERROR_NOT_FOUND;
}

static_route_t* static_route_lookup(const ip_addr_t *dest_ip) {
    if (!dest_ip || !static_routing_is_enabled()) {
        return NULL;
    }
    
    g_static_routing_stats.route_lookups++;
    
    static_route_t *best_match = static_route_find_best_match(dest_ip);
    
    if (best_match) {
        g_static_routing_stats.route_hits++;
    } else {
        g_static_routing_stats.route_misses++;
    }
    
    return best_match;
}

static_route_t* static_route_find_exact(const ip_addr_t *dest_network, const ip_addr_t *netmask) {
    if (!dest_network || !netmask) {
        return NULL;
    }
    
    static_route_t *route = g_static_routing_table.routes;
    while (route) {
        if (ip_addr_equals(&route->dest_network, dest_network) &&
            ip_addr_equals(&route->netmask, netmask)) {
            return route;
        }
        route = route->next;
    }
    
    return NULL;
}

void static_route_clear_all(void) {
    static_routing_table_cleanup(&g_static_routing_table);
    static_routing_table_init(&g_static_routing_table, 
                             g_static_routing_table.max_routes,
                             g_static_routing_table.max_subnets);
}

/* Subnet management */
int static_subnet_add(const ip_addr_t *network, const ip_addr_t *netmask, uint8_t nic_index) {
    if (!network || !netmask || nic_index >= MAX_NICS) {
        return ERROR_INVALID_PARAM;
    }
    
    if (g_static_routing_table.subnet_count >= g_static_routing_table.max_subnets) {
        return ERROR_NO_MEMORY;
    }
    
    /* Create new subnet info */
    subnet_info_t *subnet = (subnet_info_t*)memory_alloc(sizeof(subnet_info_t),
                                                       MEM_TYPE_DRIVER_DATA, 0);
    if (!subnet) {
        return ERROR_NO_MEMORY;
    }
    
    /* Initialize subnet */
    ip_addr_copy(&subnet->network, network);
    ip_addr_copy(&subnet->netmask, netmask);
    subnet->prefix_len = subnet_mask_to_prefix_len(netmask);
    subnet->nic_index = nic_index;
    subnet->flags = SUBNET_FLAG_ACTIVE | SUBNET_FLAG_STATIC;
    
    /* Add to subnet list */
    subnet->next = g_static_routing_table.subnets;
    g_static_routing_table.subnets = subnet;
    g_static_routing_table.subnet_count++;
    
    return SUCCESS;
}

subnet_info_t* static_subnet_lookup(const ip_addr_t *ip) {
    if (!ip) {
        return NULL;
    }
    
    return static_subnet_find_containing(ip);
}

subnet_info_t* static_subnet_find_by_nic(uint8_t nic_index) {
    subnet_info_t *subnet = g_static_routing_table.subnets;
    while (subnet) {
        if (subnet->nic_index == nic_index && (subnet->flags & SUBNET_FLAG_ACTIVE)) {
            return subnet;
        }
        subnet = subnet->next;
    }
    
    return NULL;
}

bool static_subnet_contains_ip(const subnet_info_t *subnet, const ip_addr_t *ip) {
    if (!subnet || !ip) {
        return false;
    }
    
    return subnet_contains_ip(&subnet->network, &subnet->netmask, ip);
}

/* ARP table management */
int arp_table_init(arp_table_t *table, uint16_t max_entries) {
    if (!table) {
        return ERROR_INVALID_PARAM;
    }
    
    table->entries = NULL;
    table->entry_count = 0;
    table->max_entries = max_entries;
    table->aging_time = 300000; /* 5 minutes */
    
    return SUCCESS;
}

void arp_table_cleanup(arp_table_t *table) {
    if (!table) {
        return;
    }
    
    /* Free all ARP entries */
    arp_entry_t *entry = table->entries;
    while (entry) {
        arp_entry_t *next = entry->next;
        memory_free(entry);
        entry = next;
    }
    
    table->entries = NULL;
    table->entry_count = 0;
}

int arp_entry_add(const ip_addr_t *ip, const uint8_t *mac, uint8_t nic_index) {
    if (!ip || !mac) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Check if entry already exists */
    arp_entry_t *existing = arp_entry_lookup(ip);
    if (existing) {
        /* Update existing entry */
        memory_copy(existing->mac, mac, ETH_ALEN);
        existing->nic_index = nic_index;
        existing->timestamp = static_routing_get_timestamp();
        existing->flags |= ARP_FLAG_COMPLETE;
        return SUCCESS;
    }
    
    /* Check table capacity */
    if (g_arp_table.entry_count >= g_arp_table.max_entries) {
        /* Remove oldest entry to make room (LRU eviction) */
        arp_entry_t *oldest = NULL;
        arp_entry_t *current = g_arp_table.entries;
        uint32_t oldest_timestamp = 0xFFFFFFFF;
        
        /* Find oldest entry that's not permanent */
        while (current) {
            if (!(current->flags & ARP_FLAG_PERMANENT) && current->timestamp < oldest_timestamp) {
                oldest_timestamp = current->timestamp;
                oldest = current;
            }
            current = current->next;
        }
        
        if (oldest) {
            /* Remove the oldest entry */
            arp_entry_delete(&oldest->ip);
        } else {
            /* All entries are permanent - cannot make room */
            return ERROR_NO_MEMORY;
        }
    }
    
    /* Create new entry */
    arp_entry_t *entry = (arp_entry_t*)memory_alloc(sizeof(arp_entry_t),
                                                  MEM_TYPE_DRIVER_DATA, 0);
    if (!entry) {
        return ERROR_NO_MEMORY;
    }
    
    /* Initialize entry */
    ip_addr_copy(&entry->ip, ip);
    memory_copy(entry->mac, mac, ETH_ALEN);
    entry->nic_index = nic_index;
    entry->timestamp = static_routing_get_timestamp();
    entry->flags = ARP_FLAG_COMPLETE;
    
    /* Add to ARP table */
    entry->next = g_arp_table.entries;
    g_arp_table.entries = entry;
    g_arp_table.entry_count++;
    
    return SUCCESS;
}

arp_entry_t* arp_entry_lookup(const ip_addr_t *ip) {
    if (!ip) {
        return NULL;
    }
    
    arp_entry_t *entry = g_arp_table.entries;
    while (entry) {
        if (ip_addr_equals(&entry->ip, ip)) {
            return entry;
        }
        entry = entry->next;
    }
    
    return NULL;
}

/* Default gateway management */
int static_routing_set_default_gateway(const ip_addr_t *gateway, uint8_t nic_index) {
    if (!gateway || nic_index >= MAX_NICS) {
        return ERROR_INVALID_PARAM;
    }
    
    ip_addr_copy(&g_static_routing_table.default_gateway, gateway);
    g_static_routing_table.default_nic = nic_index;
    
    return SUCCESS;
}

int static_routing_get_default_gateway(ip_addr_t *gateway, uint8_t *nic_index) {
    if (!gateway || !nic_index) {
        return ERROR_INVALID_PARAM;
    }
    
    if (ip_addr_is_zero(&g_static_routing_table.default_gateway)) {
        return ERROR_NOT_FOUND;
    }
    
    ip_addr_copy(gateway, &g_static_routing_table.default_gateway);
    *nic_index = g_static_routing_table.default_nic;
    
    return SUCCESS;
}

/* Routing decisions for IP packets */
uint8_t static_routing_get_output_nic(const ip_addr_t *dest_ip) {
    if (!dest_ip || !static_routing_is_enabled()) {
        return 0; /* Default to first NIC */
    }
    
    /* Check if destination is in a local subnet */
    subnet_info_t *local_subnet = static_subnet_lookup(dest_ip);
    if (local_subnet) {
        return local_subnet->nic_index;
    }
    
    /* Look for static route */
    static_route_t *route = static_route_lookup(dest_ip);
    if (route) {
        g_static_routing_stats.packets_routed++;
        return route->dest_nic;
    }
    
    /* Use default gateway if available */
    if (!ip_addr_is_zero(&g_static_routing_table.default_gateway)) {
        g_static_routing_stats.packets_to_gateway++;
        return g_static_routing_table.default_nic;
    }
    
    /* No route found */
    return 0;
}

int static_routing_get_next_hop(const ip_addr_t *dest_ip, ip_addr_t *next_hop, uint8_t *nic_index) {
    if (!dest_ip || !next_hop || !nic_index) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Check if destination is in local subnet */
    subnet_info_t *local_subnet = static_subnet_lookup(dest_ip);
    if (local_subnet) {
        /* Direct delivery - next hop is the destination itself */
        ip_addr_copy(next_hop, dest_ip);
        *nic_index = local_subnet->nic_index;
        return SUCCESS;
    }
    
    /* Look for static route */
    static_route_t *route = static_route_lookup(dest_ip);
    if (route) {
        if (route->flags & STATIC_ROUTE_FLAG_GATEWAY) {
            ip_addr_copy(next_hop, &route->gateway);
        } else {
            ip_addr_copy(next_hop, dest_ip);
        }
        *nic_index = route->dest_nic;
        return SUCCESS;
    }
    
    /* Use default gateway */
    if (!ip_addr_is_zero(&g_static_routing_table.default_gateway)) {
        ip_addr_copy(next_hop, &g_static_routing_table.default_gateway);
        *nic_index = g_static_routing_table.default_nic;
        return SUCCESS;
    }
    
    return ERROR_NOT_FOUND;
}

bool static_routing_is_local_subnet(const ip_addr_t *ip) {
    return static_subnet_lookup(ip) != NULL;
}

/* IP address utilities */
void ip_addr_set(ip_addr_t *addr, uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    if (!addr) return;
    addr->addr[0] = a;
    addr->addr[1] = b;
    addr->addr[2] = c;
    addr->addr[3] = d;
}

void ip_addr_copy(ip_addr_t *dest, const ip_addr_t *src) {
    if (dest && src) {
        memory_copy(dest->addr, src->addr, 4);
    }
}

bool ip_addr_equals(const ip_addr_t *addr1, const ip_addr_t *addr2) {
    if (!addr1 || !addr2) {
        return false;
    }
    
    return memory_compare(addr1->addr, addr2->addr, 4) == 0;
}

bool ip_addr_is_zero(const ip_addr_t *addr) {
    if (!addr) return true;
    return (addr->addr[0] | addr->addr[1] | addr->addr[2] | addr->addr[3]) == 0;
}

bool ip_addr_is_broadcast(const ip_addr_t *addr) {
    if (!addr) return false;
    return (addr->addr[0] == 255 && addr->addr[1] == 255 && 
            addr->addr[2] == 255 && addr->addr[3] == 255);
}

bool ip_addr_is_multicast(const ip_addr_t *addr) {
    if (!addr) return false;
    return (addr->addr[0] >= 224 && addr->addr[0] <= 239);
}

bool ip_addr_is_loopback(const ip_addr_t *addr) {
    if (!addr) return false;
    return (addr->addr[0] == 127);
}

uint32_t ip_addr_to_uint32(const ip_addr_t *addr) {
    if (!addr) return 0;
    return (uint32_t)addr->addr[0] << 24 | 
           (uint32_t)addr->addr[1] << 16 |
           (uint32_t)addr->addr[2] << 8 | 
           (uint32_t)addr->addr[3];
}

void ip_addr_from_uint32(ip_addr_t *addr, uint32_t value) {
    if (!addr) return;
    addr->addr[0] = (value >> 24) & 0xFF;
    addr->addr[1] = (value >> 16) & 0xFF;
    addr->addr[2] = (value >> 8) & 0xFF;
    addr->addr[3] = value & 0xFF;
}

/* Subnet utilities */
void subnet_apply_mask(ip_addr_t *result, const ip_addr_t *ip, const ip_addr_t *mask) {
    int i;
    if (!result || !ip || !mask) return;

    for (i = 0; i < 4; i++) {
        result->addr[i] = ip->addr[i] & mask->addr[i];
    }
}

bool subnet_contains_ip(const ip_addr_t *network, const ip_addr_t *mask, const ip_addr_t *ip) {
    if (!network || !mask || !ip) {
        return false;
    }
    
    ip_addr_t masked_ip;
    subnet_apply_mask(&masked_ip, ip, mask);
    
    return ip_addr_equals(network, &masked_ip);
}

uint8_t subnet_mask_to_prefix_len(const ip_addr_t *mask) {
    if (!mask) return 0;
    
    uint32_t mask_val = ip_addr_to_uint32(mask);
    uint8_t prefix_len = 0;
    
    while (mask_val & 0x80000000) {
        prefix_len++;
        mask_val <<= 1;
    }
    
    return prefix_len;
}

void subnet_prefix_len_to_mask(ip_addr_t *mask, uint8_t prefix_len) {
    if (!mask) return;
    
    if (prefix_len > 32) prefix_len = 32;
    
    uint32_t mask_val = 0;
    if (prefix_len > 0) {
        mask_val = ~((1UL << (32 - prefix_len)) - 1);
    }
    
    ip_addr_from_uint32(mask, mask_val);
}

/* Statistics */
void static_routing_stats_init(static_routing_stats_t *stats) {
    if (!stats) return;
    memory_zero(stats, sizeof(static_routing_stats_t));
}

const static_routing_stats_t* static_routing_get_stats(void) {
    return &g_static_routing_stats;
}

void static_routing_clear_stats(void) {
    static_routing_stats_init(&g_static_routing_stats);
}

/* Integration with main routing system */
route_decision_t static_routing_decide(const packet_buffer_t *packet, uint8_t src_nic, uint8_t *dest_nic) {
    if (!packet || !packet->data || !dest_nic || !static_routing_is_enabled()) {
        return ROUTE_DECISION_DROP;
    }
    
    /* Parse IP header from packet - already implemented below */
    if (packet->length < ETH_HLEN + sizeof(ip_header_t)) {
        return ROUTE_DECISION_DROP;
    }
    
    /* Skip Ethernet header to get to IP header */
    const uint8_t *ip_data = packet->data + ETH_HLEN;
    ip_header_t ip_header;
    
    if (!static_routing_parse_ip_header(ip_data, packet->length - ETH_HLEN, &ip_header)) {
        return ROUTE_DECISION_DROP;
    }
    
    /* Get output NIC for destination IP */
    uint8_t output_nic = static_routing_get_output_nic(&ip_header.dest_ip);
    
    /* Avoid routing back to source NIC */
    if (output_nic == src_nic) {
        return ROUTE_DECISION_DROP;
    }
    
    *dest_nic = output_nic;
    return ROUTE_DECISION_FORWARD;
}

/* IP header parsing */
bool static_routing_parse_ip_header(const uint8_t *packet, uint16_t length, ip_header_t *header) {
    if (!packet || !header || length < sizeof(ip_header_t)) {
        return false;
    }
    
    /* Copy header data */
    memory_copy(header, packet, sizeof(ip_header_t));
    
    /* Convert network byte order to host byte order */
    header->total_length = ntohs(header->total_length);
    header->identification = ntohs(header->identification);
    header->flags_fragment = ntohs(header->flags_fragment);
    header->checksum = ntohs(header->checksum);
    
    /* Basic validation */
    return static_routing_validate_ip_header(header) == SUCCESS;
}

int static_routing_validate_ip_header(const ip_header_t *header) {
    if (!header) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Check version (should be 4) */
    if ((header->version_ihl >> 4) != 4) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Check header length */
    uint8_t ihl = (header->version_ihl & 0x0F) * 4;
    if (ihl < sizeof(ip_header_t)) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Verify IP header checksum */
    uint16_t calculated_checksum = static_routing_calculate_ip_checksum(header);
    if (calculated_checksum != header->checksum) {
        return ERROR_INVALID_PARAM; /* Checksum mismatch */
    }
    
    return SUCCESS;
}

/* Private helper function implementations */
static static_route_t* static_route_find_best_match(const ip_addr_t *dest_ip) {
    if (!dest_ip) {
        return NULL;
    }
    
    static_route_t *best_match = NULL;
    uint8_t best_prefix_len = 0;
    
    static_route_t *route = g_static_routing_table.routes;
    while (route) {
        if (!(route->flags & STATIC_ROUTE_FLAG_UP)) {
            route = route->next;
            continue;
        }
        
        /* Check if destination IP is in this route's network */
        if (subnet_contains_ip(&route->dest_network, &route->netmask, dest_ip)) {
            uint8_t prefix_len = subnet_mask_to_prefix_len(&route->netmask);
            
            /* Longest prefix match */
            if (prefix_len > best_prefix_len) {
                best_match = route;
                best_prefix_len = prefix_len;
            }
        }
        
        route = route->next;
    }
    
    return best_match;
}

static subnet_info_t* static_subnet_find_containing(const ip_addr_t *ip) {
    if (!ip) {
        return NULL;
    }
    
    subnet_info_t *subnet = g_static_routing_table.subnets;
    while (subnet) {
        if ((subnet->flags & SUBNET_FLAG_ACTIVE) && 
            static_subnet_contains_ip(subnet, ip)) {
            return subnet;
        }
        subnet = subnet->next;
    }
    
    return NULL;
}

static uint32_t static_routing_get_timestamp(void) {
    return get_system_timestamp_ms();
}

/* Enhanced static routing functions */
int static_subnet_delete(const ip_addr_t *network, const ip_addr_t *netmask) {
    if (!network || !netmask) {
        return ERROR_INVALID_PARAM;
    }
    
    subnet_info_t **current = &g_static_routing_table.subnets;
    while (*current) {
        if (ip_addr_equals(&(*current)->network, network) &&
            ip_addr_equals(&(*current)->netmask, netmask)) {
            
            subnet_info_t *to_delete = *current;
            *current = (*current)->next;
            memory_free(to_delete);
            g_static_routing_table.subnet_count--;
            return SUCCESS;
        }
        current = &(*current)->next;
    }
    
    return ERROR_NOT_FOUND;
}

int arp_entry_delete(const ip_addr_t *ip) {
    if (!ip) {
        return ERROR_INVALID_PARAM;
    }
    
    arp_entry_t **current = &g_arp_table.entries;
    while (*current) {
        if (ip_addr_equals(&(*current)->ip, ip)) {
            arp_entry_t *to_delete = *current;
            *current = (*current)->next;
            memory_free(to_delete);
            g_arp_table.entry_count--;
            return SUCCESS;
        }
        current = &(*current)->next;
    }
    
    return ERROR_NOT_FOUND;
}

void arp_table_age_entries(void) {
    if (!g_static_routing_initialized) {
        return;
    }
    
    uint32_t current_time = static_routing_get_timestamp();
    uint32_t aged_count = 0;
    
    arp_entry_t **current = &g_arp_table.entries;
    while (*current) {
        arp_entry_t *entry = *current;
        
        /* Skip permanent entries */
        if (entry->flags & ARP_FLAG_PERMANENT) {
            current = &(*current)->next;
            continue;
        }
        
        /* Check if entry has expired */
        if ((current_time - entry->timestamp) > g_arp_table.aging_time) {
            /* Remove expired entry */
            *current = entry->next;
            memory_free(entry);
            g_arp_table.entry_count--;
            aged_count++;
        } else {
            current = &(*current)->next;
        }
    }
    
    if (aged_count > 0) {
        g_static_routing_stats.arp_timeouts += aged_count;
    }
}

void arp_table_flush(void) {
    arp_table_cleanup(&g_arp_table);
    arp_table_init(&g_arp_table, g_arp_table.max_entries);
}

int static_routing_delete_default_gateway(void) {
    ip_addr_set(&g_static_routing_table.default_gateway, 0, 0, 0, 0);
    g_static_routing_table.default_nic = 0;
    return SUCCESS;
}

bool static_routing_is_local_ip(const ip_addr_t *ip) {
    if (!ip) {
        return false;
    }
    
    /* Check if IP matches any of our configured subnet network addresses */
    subnet_info_t *subnet = g_static_routing_table.subnets;
    while (subnet) {
        if (subnet->flags & SUBNET_FLAG_ACTIVE) {
            /* Check if this IP is the network address of this subnet */
            if (ip_addr_equals(&subnet->network, ip)) {
                return true;
            }
            
            /* Check if this IP is within our subnet range */
            /* For now, we consider the network address + 1 as local */
            ip_addr_t local_ip = subnet->network;
            if (local_ip.addr[3] < 255) {
                local_ip.addr[3]++;
                if (ip_addr_equals(&local_ip, ip)) {
                    return true;
                }
            }
        }
        subnet = subnet->next;
    }
    
    return false;
}

int static_routing_process_ip_packet(const uint8_t *packet, uint16_t length,
                                    uint8_t src_nic, uint8_t *dest_nic) {
    if (!packet || !dest_nic || !static_routing_is_enabled()) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Parse IP header */
    if (length < sizeof(ip_header_t)) {
        return ERROR_INVALID_PARAM;
    }
    
    ip_header_t ip_header;
    if (!static_routing_parse_ip_header(packet, length, &ip_header)) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Get output NIC for destination IP */
    uint8_t output_nic = static_routing_get_output_nic(&ip_header.dest_ip);
    
    /* Avoid routing back to source NIC */
    if (output_nic == src_nic) {
        return ERROR_INVALID_PARAM;
    }
    
    *dest_nic = output_nic;
    g_static_routing_stats.packets_routed++;
    
    return SUCCESS;
}

int static_routing_resolve_mac(const ip_addr_t *ip, uint8_t *mac, uint8_t *nic_index) {
    if (!ip || !mac || !nic_index) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Look up in ARP table */
    arp_entry_t *arp_entry = arp_entry_lookup(ip);
    if (arp_entry && (arp_entry->flags & ARP_FLAG_COMPLETE)) {
        memory_copy(mac, arp_entry->mac, ETH_ALEN);
        *nic_index = arp_entry->nic_index;
        return SUCCESS;
    }
    
    /* ARP entry not found or incomplete */
    return ERROR_NOT_FOUND;
}

void static_routing_print_table(void) {
    if (!static_routing_is_enabled()) {
        return;
    }
    
    log_info("=== Static Routing Table ===");
    log_info("Routes: %d/%d", g_static_routing_table.route_count, g_static_routing_table.max_routes);
    
    static_route_t *route = g_static_routing_table.routes;
    while (route) {
        log_info("Route: %d.%d.%d.%d/%d.%d.%d.%d -> NIC %d (metric %d)",
                route->dest_network.addr[0], route->dest_network.addr[1],
                route->dest_network.addr[2], route->dest_network.addr[3],
                route->netmask.addr[0], route->netmask.addr[1],
                route->netmask.addr[2], route->netmask.addr[3],
                route->dest_nic, route->metric);
        
        if (route->flags & STATIC_ROUTE_FLAG_GATEWAY) {
            log_info("  Gateway: %d.%d.%d.%d",
                    route->gateway.addr[0], route->gateway.addr[1],
                    route->gateway.addr[2], route->gateway.addr[3]);
        }
        
        route = route->next;
    }
    
    /* Print default gateway */
    if (!ip_addr_is_zero(&g_static_routing_table.default_gateway)) {
        log_info("Default Gateway: %d.%d.%d.%d via NIC %d",
                g_static_routing_table.default_gateway.addr[0],
                g_static_routing_table.default_gateway.addr[1],
                g_static_routing_table.default_gateway.addr[2],
                g_static_routing_table.default_gateway.addr[3],
                g_static_routing_table.default_nic);
    }
}

void static_routing_print_subnets(void) {
    if (!static_routing_is_enabled()) {
        return;
    }
    
    log_info("=== Configured Subnets ===");
    log_info("Subnets: %d/%d", g_static_routing_table.subnet_count, g_static_routing_table.max_subnets);
    
    subnet_info_t *subnet = g_static_routing_table.subnets;
    while (subnet) {
        log_info("Subnet: %d.%d.%d.%d/%d on NIC %d (flags: 0x%04X)",
                subnet->network.addr[0], subnet->network.addr[1],
                subnet->network.addr[2], subnet->network.addr[3],
                subnet->prefix_len, subnet->nic_index, subnet->flags);
        subnet = subnet->next;
    }
}

void static_routing_print_arp_table(void) {
    if (!g_static_routing_initialized) {
        return;
    }
    
    log_info("=== ARP Table ===");
    log_info("Entries: %d/%d", g_arp_table.entry_count, g_arp_table.max_entries);
    
    arp_entry_t *entry = g_arp_table.entries;
    while (entry) {
        log_info("ARP: %d.%d.%d.%d -> %02X:%02X:%02X:%02X:%02X:%02X (NIC %d, flags: 0x%04X)",
                entry->ip.addr[0], entry->ip.addr[1], entry->ip.addr[2], entry->ip.addr[3],
                entry->mac[0], entry->mac[1], entry->mac[2],
                entry->mac[3], entry->mac[4], entry->mac[5],
                entry->nic_index, entry->flags);
        entry = entry->next;
    }
}

const char* static_route_flags_to_string(uint32_t flags) {
    static char buffer[64];
    buffer[0] = '\0';
    
    if (flags & STATIC_ROUTE_FLAG_UP) {
        strcat(buffer, "UP ");
    }
    if (flags & STATIC_ROUTE_FLAG_GATEWAY) {
        strcat(buffer, "GATEWAY ");
    }
    if (flags & STATIC_ROUTE_FLAG_HOST) {
        strcat(buffer, "HOST ");
    }
    if (flags & STATIC_ROUTE_FLAG_DYNAMIC) {
        strcat(buffer, "DYNAMIC ");
    }
    if (flags & STATIC_ROUTE_FLAG_MODIFIED) {
        strcat(buffer, "MODIFIED ");
    }
    
    if (buffer[0] == '\0') {
        strcpy(buffer, "NONE");
    }
    
    return buffer;
}

uint16_t static_routing_calculate_ip_checksum(const ip_header_t *header) {
    if (!header) {
        return 0;
    }
    
    /* Calculate standard Internet checksum over IP header */
    const uint16_t *data = (const uint16_t*)header;
    uint32_t sum = 0;
    uint8_t header_len = (header->version_ihl & 0x0F) * 4;
    int i;

    /* Sum all 16-bit words in header (excluding checksum field) */
    for (i = 0; i < header_len / 2; i++) {
        if (i == 5) { /* Skip checksum field */
            continue;
        }
        sum += ntohs(data[i]);
    }
    
    /* Add carry bits */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    /* One's complement */
    return (uint16_t)(~sum);
}

bool subnet_is_valid_mask(const ip_addr_t *mask) {
    if (!mask) {
        return false;
    }
    
    /* Convert to 32-bit value */
    uint32_t mask_val = ip_addr_to_uint32(mask);
    
    /* Check if mask has contiguous 1 bits followed by contiguous 0 bits */
    uint32_t inverted = ~mask_val;
    
    /* Add 1 to inverted mask - should be power of 2 if valid */
    uint32_t test = inverted + 1;
    
    /* Check if result is power of 2 (has only one bit set) */
    return (test != 0) && ((test & (test - 1)) == 0);
}

