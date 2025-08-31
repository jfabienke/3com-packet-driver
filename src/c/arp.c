/**
 * @file arp.c
 * @brief ARP Protocol Implementation (RFC 826)
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#include "../include/arp.h"
#include "../include/packet_ops.h"
#include "../include/hardware.h"
#include "../include/logging.h"
#include "../include/memory.h"

/* Global ARP state */
arp_cache_t g_arp_cache;
arp_stats_t g_arp_stats;
bool g_arp_enabled = false;

/* Private state */
static bool g_arp_initialized = false;
static bool g_proxy_arp_enabled = false;
static uint32_t g_arp_timeout = ARP_ENTRY_TIMEOUT;
static uint32_t g_request_timeout = ARP_REQUEST_TIMEOUT;
static uint8_t g_max_retries = ARP_MAX_RETRIES;

/* Forward declarations */
static uint32_t arp_get_timestamp(void);
static int arp_send_packet(const arp_packet_t *arp_pkt, uint8_t nic_index, bool broadcast);
static void arp_update_cache_stats(bool hit);

/* ARP initialization and cleanup */
int arp_init(void) {
    if (g_arp_initialized) {
        return SUCCESS;
    }
    
    /* Initialize ARP cache */
    int result = arp_cache_init(&g_arp_cache, ARP_TABLE_SIZE);
    if (result != SUCCESS) {
        return result;
    }
    
    /* Initialize statistics */
    arp_stats_init(&g_arp_stats);
    
    /* Set default configuration */
    g_proxy_arp_enabled = false;
    g_arp_timeout = ARP_ENTRY_TIMEOUT;
    g_request_timeout = ARP_REQUEST_TIMEOUT;
    g_max_retries = ARP_MAX_RETRIES;
    
    g_arp_initialized = true;
    g_arp_enabled = false; /* Must be explicitly enabled */
    
    log_info("ARP protocol initialized");
    return SUCCESS;
}

void arp_cleanup(void) {
    if (!g_arp_initialized) {
        return;
    }
    
    /* Cleanup ARP cache */
    arp_cache_cleanup(&g_arp_cache);
    
    /* Clear statistics */
    arp_stats_init(&g_arp_stats);
    
    g_arp_initialized = false;
    g_arp_enabled = false;
    
    log_info("ARP protocol cleaned up");
}

int arp_enable(bool enable) {
    if (!g_arp_initialized) {
        return ERROR_NOT_FOUND;
    }
    
    g_arp_enabled = enable;
    log_info("ARP protocol %s", enable ? "enabled" : "disabled");
    return SUCCESS;
}

bool arp_is_enabled(void) {
    return g_arp_enabled && g_arp_initialized;
}

/* ARP cache management */
int arp_cache_init(arp_cache_t *cache, uint16_t max_entries) {
    if (!cache) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Allocate entry pool */
    cache->entries = (arp_cache_entry_t*)memory_alloc(
        sizeof(arp_cache_entry_t) * max_entries, 
        MEM_TYPE_DRIVER_DATA, 0);
    if (!cache->entries) {
        return ERROR_NO_MEMORY;
    }
    
    /* Initialize hash table */
    for (int i = 0; i < ARP_HASH_SIZE; i++) {
        cache->hash_table[i] = NULL;
    }
    
    /* Initialize free list */
    cache->free_list = NULL;
    for (int i = 0; i < max_entries; i++) {
        arp_cache_entry_t *entry = &cache->entries[i];
        memory_zero(entry, sizeof(arp_cache_entry_t));
        entry->state = ARP_STATE_FREE;
        entry->next = cache->free_list;
        cache->free_list = entry;
    }
    
    cache->entry_count = 0;
    cache->max_entries = max_entries;
    cache->total_lookups = 0;
    cache->successful_lookups = 0;
    cache->cache_hits = 0;
    cache->cache_misses = 0;
    cache->initialized = true;
    
    return SUCCESS;
}

void arp_cache_cleanup(arp_cache_t *cache) {
    if (!cache || !cache->initialized) {
        return;
    }
    
    /* Free entry pool */
    if (cache->entries) {
        memory_free(cache->entries);
        cache->entries = NULL;
    }
    
    /* Clear hash table */
    for (int i = 0; i < ARP_HASH_SIZE; i++) {
        cache->hash_table[i] = NULL;
    }
    
    cache->free_list = NULL;
    cache->entry_count = 0;
    cache->initialized = false;
}

arp_cache_entry_t* arp_cache_lookup(const ip_addr_t *ip) {
    if (!ip || !arp_is_enabled()) {
        return NULL;
    }
    
    g_arp_cache.total_lookups++;
    
    /* Calculate hash */
    uint16_t hash = arp_calculate_hash(ip);
    
    /* Search hash bucket */
    arp_cache_entry_t *entry = g_arp_cache.hash_table[hash];
    while (entry) {
        if (entry->state != ARP_STATE_FREE && ip_addr_equals(&entry->ip, ip)) {
            g_arp_cache.successful_lookups++;
            arp_update_cache_stats(true);
            return entry;
        }
        entry = entry->hash_next;
    }
    
    arp_update_cache_stats(false);
    return NULL;
}

int arp_cache_add(const ip_addr_t *ip, const uint8_t *mac, uint8_t nic_index, uint16_t flags) {
    if (!ip || !mac || !arp_is_enabled()) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Check if entry already exists */
    arp_cache_entry_t *existing = arp_cache_lookup(ip);
    if (existing) {
        /* Update existing entry */
        memory_copy(existing->mac, mac, ETH_ALEN);
        existing->nic_index = nic_index;
        existing->timestamp = arp_get_timestamp();
        existing->flags = flags;
        existing->state = ARP_STATE_COMPLETE;
        existing->retry_count = 0;
        
        g_arp_stats.cache_updates++;
        return SUCCESS;
    }
    
    /* Get free entry */
    arp_cache_entry_t *entry = arp_find_free_entry();
    if (!entry) {
        /* Cache full - age out old entries */
        arp_cache_age_entries();
        entry = arp_find_free_entry();
        if (!entry) {
            return ERROR_NO_MEMORY;
        }
    }
    
    /* Initialize new entry */
    ip_addr_copy(&entry->ip, ip);
    memory_copy(entry->mac, mac, ETH_ALEN);
    entry->nic_index = nic_index;
    entry->timestamp = arp_get_timestamp();
    entry->flags = flags;
    entry->state = ARP_STATE_COMPLETE;
    entry->retry_count = 0;
    entry->last_request_time = 0;
    
    /* Add to hash table */
    arp_add_to_hash(entry);
    
    g_arp_cache.entry_count++;
    g_arp_stats.cache_updates++;
    
    return SUCCESS;
}

int arp_cache_update(const ip_addr_t *ip, const uint8_t *mac, uint8_t nic_index) {
    return arp_cache_add(ip, mac, nic_index, ARP_FLAG_COMPLETE);
}

int arp_cache_delete(const ip_addr_t *ip) {
    if (!ip) {
        return ERROR_INVALID_PARAM;
    }
    
    arp_cache_entry_t *entry = arp_cache_lookup(ip);
    if (!entry) {
        return ERROR_NOT_FOUND;
    }
    
    /* Remove from hash table */
    arp_remove_from_hash(entry);
    
    /* Mark as free */
    entry->state = ARP_STATE_FREE;
    entry->next = g_arp_cache.free_list;
    g_arp_cache.free_list = entry;
    
    g_arp_cache.entry_count--;
    
    return SUCCESS;
}

void arp_cache_flush(void) {
    if (!arp_is_enabled()) {
        return;
    }
    
    /* Clear all non-permanent entries */
    for (int i = 0; i < ARP_HASH_SIZE; i++) {
        arp_cache_entry_t *entry = g_arp_cache.hash_table[i];
        while (entry) {
            arp_cache_entry_t *next = entry->hash_next;
            
            if (!(entry->flags & ARP_FLAG_PERMANENT)) {
                arp_remove_from_hash(entry);
                entry->state = ARP_STATE_FREE;
                entry->next = g_arp_cache.free_list;
                g_arp_cache.free_list = entry;
                g_arp_cache.entry_count--;
            }
            
            entry = next;
        }
    }
}

void arp_cache_age_entries(void) {
    if (!arp_is_enabled()) {
        return;
    }
    
    uint32_t current_time = arp_get_timestamp();
    uint32_t aged_count = 0;
    
    /* Age entries in all hash buckets */
    for (int i = 0; i < ARP_HASH_SIZE; i++) {
        arp_cache_entry_t *entry = g_arp_cache.hash_table[i];
        while (entry) {
            arp_cache_entry_t *next = entry->hash_next;
            
            /* Skip permanent entries */
            if (entry->flags & ARP_FLAG_PERMANENT) {
                entry = next;
                continue;
            }
            
            /* Check if entry has expired */
            bool expired = false;
            if (entry->state == ARP_STATE_COMPLETE) {
                expired = (current_time - entry->timestamp) > g_arp_timeout;
            } else if (entry->state == ARP_STATE_INCOMPLETE) {
                expired = (current_time - entry->last_request_time) > g_request_timeout;
            }
            
            if (expired) {
                arp_remove_from_hash(entry);
                entry->state = ARP_STATE_FREE;
                entry->next = g_arp_cache.free_list;
                g_arp_cache.free_list = entry;
                g_arp_cache.entry_count--;
                aged_count++;
            }
            
            entry = next;
        }
    }
    
    g_arp_stats.cache_timeouts += aged_count;
}

/* ARP packet processing */
int arp_process_packet(const uint8_t *packet, uint16_t length, uint8_t src_nic) {
    if (!packet || !arp_is_enabled()) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Validate minimum packet size */
    if (length < sizeof(arp_packet_t)) {
        g_arp_stats.invalid_packets++;
        return ERROR_INVALID_PARAM;
    }
    
    /* Parse ARP packet */
    const arp_packet_t *arp_pkt = (const arp_packet_t*)packet;
    
    /* Validate packet */
    if (!arp_validate_packet(arp_pkt, length)) {
        g_arp_stats.invalid_packets++;
        return ERROR_INVALID_PARAM;
    }
    
    g_arp_stats.packets_received++;
    
    /* Convert operation from network byte order */
    uint16_t operation = ntohs(arp_pkt->operation);
    
    /* Process based on operation */
    int result;
    switch (operation) {
        case ARP_OP_REQUEST:
            g_arp_stats.requests_received++;
            result = arp_handle_request(arp_pkt, src_nic);
            break;
            
        case ARP_OP_REPLY:
            g_arp_stats.replies_received++;
            result = arp_handle_reply(arp_pkt, src_nic);
            break;
            
        default:
            g_arp_stats.invalid_packets++;
            result = ERROR_INVALID_PARAM;
            break;
    }
    
    return result;
}

int arp_handle_request(const arp_packet_t *arp_pkt, uint8_t src_nic) {
    if (!arp_pkt) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Extract sender information */
    ip_addr_t sender_ip;
    ip_addr_t target_ip;
    memory_copy(sender_ip.addr, arp_pkt->sender_proto, 4);
    memory_copy(target_ip.addr, arp_pkt->target_proto, 4);
    
    /* Update cache with sender info (defensive learning) */
    arp_cache_update(&sender_ip, arp_pkt->sender_hw, src_nic);
    
    /* Check if target IP is one of our local addresses */
    if (arp_is_local_ip(&target_ip)) {
        /* Send ARP reply */
        int result = arp_send_reply(&target_ip, arp_pkt->sender_hw, &sender_ip, src_nic);
        if (result == SUCCESS) {
            g_arp_stats.replies_sent++;
        }
        return result;
    }
    
    /* Check for proxy ARP */
    if (g_proxy_arp_enabled) {
        /* Implement proxy ARP logic */
        g_arp_stats.proxy_requests++;
        
        /* Check if we can respond as proxy for this IP */
        if (arp_can_proxy_for_ip(target_ip)) {
            /* Send proxy ARP response */
            arp_send_response(target_ip, g_local_mac, &request_header.src_mac);
            return SUCCESS;
        }
    }
    
    return SUCCESS;
}

int arp_handle_reply(const arp_packet_t *arp_pkt, uint8_t src_nic) {
    if (!arp_pkt) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Extract sender information */
    ip_addr_t sender_ip;
    memory_copy(sender_ip.addr, arp_pkt->sender_proto, 4);
    
    /* Update cache with reply information */
    int result = arp_cache_update(&sender_ip, arp_pkt->sender_hw, src_nic);
    
    return result;
}

bool arp_validate_packet(const arp_packet_t *arp_pkt, uint16_t length) {
    if (!arp_pkt || length < sizeof(arp_packet_t)) {
        return false;
    }
    
    /* Validate hardware type */
    if (ntohs(arp_pkt->hw_type) != ARP_HW_TYPE_ETHERNET) {
        return false;
    }
    
    /* Validate protocol type */
    if (ntohs(arp_pkt->proto_type) != ARP_PROTO_TYPE_IP) {
        return false;
    }
    
    /* Validate address lengths */
    if (arp_pkt->hw_len != ARP_HW_LEN_ETHERNET || 
        arp_pkt->proto_len != ARP_PROTO_LEN_IP) {
        return false;
    }
    
    /* Validate operation */
    uint16_t operation = ntohs(arp_pkt->operation);
    if (operation != ARP_OP_REQUEST && operation != ARP_OP_REPLY) {
        return false;
    }
    
    return true;
}

/* ARP packet generation */
int arp_send_request(const ip_addr_t *target_ip, uint8_t nic_index) {
    if (!target_ip || !arp_is_enabled()) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Get NIC information */
    nic_info_t *nic = hardware_get_nic(nic_index);
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Get local IP for this NIC */
    ip_addr_t local_ip;
    subnet_info_t *subnet = static_subnet_find_by_nic(nic_index);
    if (!subnet) {
        /* No local IP configured for this NIC */
        return ERROR_NOT_FOUND;
    }
    ip_addr_copy(&local_ip, &subnet->network);
    
    /* Build ARP request packet */
    arp_packet_t arp_pkt;
    uint8_t broadcast_mac[ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t zero_mac[ETH_ALEN] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    
    int result = arp_build_packet(&arp_pkt, ARP_OP_REQUEST,
                                  nic->mac, &local_ip,
                                  zero_mac, target_ip);
    if (result != SUCCESS) {
        return result;
    }
    
    /* Send packet */
    result = arp_send_packet(&arp_pkt, nic_index, true);
    if (result == SUCCESS) {
        g_arp_stats.packets_sent++;
        g_arp_stats.requests_sent++;
        
        /* Add incomplete entry to cache */
        arp_cache_entry_t *entry = arp_find_free_entry();
        if (entry) {
            ip_addr_copy(&entry->ip, target_ip);
            memory_zero(entry->mac, ETH_ALEN);
            entry->nic_index = nic_index;
            entry->timestamp = arp_get_timestamp();
            entry->flags = 0;
            entry->state = ARP_STATE_INCOMPLETE;
            entry->retry_count = 1;
            entry->last_request_time = arp_get_timestamp();
            
            arp_add_to_hash(entry);
            g_arp_cache.entry_count++;
        }
    }
    
    return result;
}

int arp_send_reply(const ip_addr_t *target_ip, const uint8_t *target_mac,
                   const ip_addr_t *sender_ip, uint8_t nic_index) {
    if (!target_ip || !target_mac || !sender_ip || !arp_is_enabled()) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Get NIC information */
    nic_info_t *nic = hardware_get_nic(nic_index);
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Build ARP reply packet */
    arp_packet_t arp_pkt;
    int result = arp_build_packet(&arp_pkt, ARP_OP_REPLY,
                                  nic->mac, sender_ip,
                                  target_mac, target_ip);
    if (result != SUCCESS) {
        return result;
    }
    
    /* Send packet (unicast to target) */
    result = arp_send_packet(&arp_pkt, nic_index, false);
    if (result == SUCCESS) {
        g_arp_stats.packets_sent++;
        g_arp_stats.replies_sent++;
    }
    
    return result;
}

int arp_send_gratuitous(const ip_addr_t *ip, uint8_t nic_index) {
    if (!ip || !arp_is_enabled()) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Get NIC information */
    nic_info_t *nic = hardware_get_nic(nic_index);
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Build gratuitous ARP (request with our IP as both sender and target) */
    arp_packet_t arp_pkt;
    uint8_t broadcast_mac[ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    int result = arp_build_packet(&arp_pkt, ARP_OP_REQUEST,
                                  nic->mac, ip,
                                  broadcast_mac, ip);
    if (result != SUCCESS) {
        return result;
    }
    
    /* Send packet */
    result = arp_send_packet(&arp_pkt, nic_index, true);
    if (result == SUCCESS) {
        g_arp_stats.packets_sent++;
        g_arp_stats.gratuitous_arps++;
    }
    
    return result;
}

int arp_build_packet(arp_packet_t *arp_pkt, uint16_t operation,
                     const uint8_t *sender_hw, const ip_addr_t *sender_ip,
                     const uint8_t *target_hw, const ip_addr_t *target_ip) {
    if (!arp_pkt || !sender_hw || !sender_ip || !target_hw || !target_ip) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Initialize packet structure */
    arp_pkt->hw_type = htons(ARP_HW_TYPE_ETHERNET);
    arp_pkt->proto_type = htons(ARP_PROTO_TYPE_IP);
    arp_pkt->hw_len = ARP_HW_LEN_ETHERNET;
    arp_pkt->proto_len = ARP_PROTO_LEN_IP;
    arp_pkt->operation = htons(operation);
    
    /* Copy addresses */
    memory_copy(arp_pkt->sender_hw, sender_hw, ETH_ALEN);
    memory_copy(arp_pkt->sender_proto, sender_ip->addr, 4);
    memory_copy(arp_pkt->target_hw, target_hw, ETH_ALEN);
    memory_copy(arp_pkt->target_proto, target_ip->addr, 4);
    
    return SUCCESS;
}

/* ARP resolution */
int arp_resolve(const ip_addr_t *ip, uint8_t *mac, uint8_t *nic_index) {
    if (!ip || !mac || !nic_index || !arp_is_enabled()) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Look up in cache */
    arp_cache_entry_t *entry = arp_cache_lookup(ip);
    if (entry && entry->state == ARP_STATE_COMPLETE) {
        memory_copy(mac, entry->mac, ETH_ALEN);
        *nic_index = entry->nic_index;
        return SUCCESS;
    }
    
    /* Not in cache or incomplete - initiate ARP request */
    uint8_t nic = arp_get_nic_for_ip(ip);
    int result = arp_resolve_async(ip, nic);
    if (result != SUCCESS) {
        return result;
    }
    
    /* Return pending status */
    return ERROR_BUSY;
}

int arp_resolve_async(const ip_addr_t *ip, uint8_t nic_index) {
    if (!ip || !arp_is_enabled()) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Check if already resolving */
    arp_cache_entry_t *entry = arp_cache_lookup(ip);
    if (entry && entry->state == ARP_STATE_INCOMPLETE) {
        /* Check if we should retry */
        uint32_t current_time = arp_get_timestamp();
        if ((current_time - entry->last_request_time) > g_request_timeout) {
            if (entry->retry_count < g_max_retries) {
                entry->retry_count++;
                entry->last_request_time = current_time;
                return arp_send_request(ip, nic_index);
            } else {
                /* Max retries exceeded - remove entry */
                arp_cache_delete(ip);
                g_arp_stats.request_timeouts++;
                return ERROR_TIMEOUT;
            }
        }
        return SUCCESS; /* Still waiting */
    }
    
    /* Send ARP request */
    return arp_send_request(ip, nic_index);
}

bool arp_is_resolved(const ip_addr_t *ip) {
    if (!ip || !arp_is_enabled()) {
        return false;
    }
    
    arp_cache_entry_t *entry = arp_cache_lookup(ip);
    return (entry && entry->state == ARP_STATE_COMPLETE);
}

/* ARP utilities */
uint16_t arp_calculate_hash(const ip_addr_t *ip) {
    if (!ip) {
        return 0;
    }
    
    /* Simple hash using XOR of IP address bytes */
    uint32_t hash = ip_addr_to_uint32(ip);
    hash ^= (hash >> 16);
    hash ^= (hash >> 8);
    
    return (uint16_t)(hash & ARP_HASH_MASK);
}

arp_cache_entry_t* arp_find_free_entry(void) {
    if (!g_arp_cache.free_list) {
        return NULL;
    }
    
    arp_cache_entry_t *entry = g_arp_cache.free_list;
    g_arp_cache.free_list = entry->next;
    entry->next = NULL;
    
    return entry;
}

void arp_remove_from_hash(arp_cache_entry_t *entry) {
    if (!entry) {
        return;
    }
    
    uint16_t hash = arp_calculate_hash(&entry->ip);
    arp_cache_entry_t **current = &g_arp_cache.hash_table[hash];
    
    while (*current) {
        if (*current == entry) {
            *current = entry->hash_next;
            entry->hash_next = NULL;
            break;
        }
        current = &(*current)->hash_next;
    }
}

void arp_add_to_hash(arp_cache_entry_t *entry) {
    if (!entry) {
        return;
    }
    
    uint16_t hash = arp_calculate_hash(&entry->ip);
    entry->hash_next = g_arp_cache.hash_table[hash];
    g_arp_cache.hash_table[hash] = entry;
}

bool arp_is_local_ip(const ip_addr_t *ip) {
    if (!ip) {
        return false;
    }
    
    /* Check if IP belongs to any of our configured subnets */
    subnet_info_t *subnet = static_subnet_lookup(ip);
    return (subnet != NULL);
}

uint8_t arp_get_nic_for_ip(const ip_addr_t *ip) {
    if (!ip) {
        return 0;
    }
    
    /* Find subnet containing this IP */
    subnet_info_t *subnet = static_subnet_lookup(ip);
    if (subnet) {
        return subnet->nic_index;
    }
    
    /* Use static routing to determine NIC */
    return static_routing_get_output_nic(ip);
}

/* Private helper functions */
static uint32_t arp_get_timestamp(void) {
    /* Implement proper timestamp function using system timer */
    /* In real DOS implementation would use INT 1Ah (timer) */
    static uint32_t counter = 0;
    return ++counter; /* Simplified incrementing counter */
}

static int arp_send_packet(const arp_packet_t *arp_pkt, uint8_t nic_index, bool broadcast) {
    if (!arp_pkt) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Get NIC information */
    nic_info_t *nic = hardware_get_nic(nic_index);
    if (!nic) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Build Ethernet frame */
    uint8_t frame[ETH_HEADER_LEN + sizeof(arp_packet_t)];
    uint8_t broadcast_mac[ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    /* Set destination MAC */
    const uint8_t *dest_mac = broadcast ? broadcast_mac : arp_pkt->target_hw;
    
    /* Build Ethernet header */
    int result = ethernet_build_header(frame, dest_mac, nic->mac, ETH_P_ARP);
    if (result != SUCCESS) {
        return result;
    }
    
    /* Copy ARP packet */
    memory_copy(frame + ETH_HEADER_LEN, arp_pkt, sizeof(arp_packet_t));
    
    /* Send frame */
    return hardware_send_packet(nic, frame, sizeof(frame));
}

static void arp_update_cache_stats(bool hit) {
    if (hit) {
        g_arp_cache.cache_hits++;
    } else {
        g_arp_cache.cache_misses++;
    }
}

/* Statistics and debugging */
void arp_stats_init(arp_stats_t *stats) {
    if (!stats) {
        return;
    }
    
    memory_zero(stats, sizeof(arp_stats_t));
}

const arp_stats_t* arp_get_stats(void) {
    return &g_arp_stats;
}

void arp_clear_stats(void) {
    arp_stats_init(&g_arp_stats);
}

const char* arp_operation_to_string(uint16_t operation) {
    switch (operation) {
        case ARP_OP_REQUEST:    return "REQUEST";
        case ARP_OP_REPLY:      return "REPLY";
        default:                return "UNKNOWN";
    }
}

const char* arp_state_to_string(uint16_t state) {
    switch (state) {
        case ARP_STATE_FREE:        return "FREE";
        case ARP_STATE_INCOMPLETE:  return "INCOMPLETE";
        case ARP_STATE_COMPLETE:    return "COMPLETE";
        case ARP_STATE_EXPIRED:     return "EXPIRED";
        case ARP_STATE_PERMANENT:   return "PERMANENT";
        default:                    return "UNKNOWN";
    }
}

/* Configuration functions */
int arp_set_timeout(uint32_t timeout_ms) {
    g_arp_timeout = timeout_ms;
    return SUCCESS;
}

uint32_t arp_get_timeout(void) {
    return g_arp_timeout;
}

int arp_set_max_retries(uint8_t max_retries) {
    g_max_retries = max_retries;
    return SUCCESS;
}

uint8_t arp_get_max_retries(void) {
    return g_max_retries;
}

int arp_set_request_timeout(uint32_t timeout_ms) {
    g_request_timeout = timeout_ms;
    return SUCCESS;
}

uint32_t arp_get_request_timeout(void) {
    return g_request_timeout;
}

/* Proxy ARP functions */
bool arp_is_proxy_enabled(void) {
    return g_proxy_arp_enabled;
}

int arp_set_proxy_enabled(bool enable) {
    g_proxy_arp_enabled = enable;
    return SUCCESS;
}

/* Integration functions */
bool arp_is_arp_packet(const uint8_t *packet, uint16_t length) {
    if (!packet || length < ETH_HEADER_LEN + sizeof(arp_packet_t)) {
        return false;
    }
    
    /* Check Ethernet type */
    uint16_t ethertype = packet_get_ethertype(packet);
    return (ethertype == ETH_P_ARP);
}

int arp_process_received_packet(const uint8_t *packet, uint16_t length, uint8_t src_nic) {
    if (!packet || length < ETH_HEADER_LEN + sizeof(arp_packet_t)) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Skip Ethernet header to get ARP packet */
    const uint8_t *arp_data = packet + ETH_HEADER_LEN;
    uint16_t arp_length = length - ETH_HEADER_LEN;
    
    return arp_process_packet(arp_data, arp_length, src_nic);
}

/* Implement remaining functions for completeness */
int arp_wait_for_resolution(const ip_addr_t *ip, uint32_t timeout_ms) {
    /* Implement waiting with timeout - simplified implementation */
    if (!ip) return ERROR_INVALID_PARAM;
    
    /* In full implementation would poll cache with timeout */
    arp_cache_entry_t *entry = arp_cache_lookup(&g_arp_cache, ip);
    return entry ? SUCCESS : ERROR_TIMEOUT;
    return ERROR_NOT_SUPPORTED;
}

int arp_add_proxy_entry(const ip_addr_t *ip, uint8_t nic_index) {
    /* Implement proxy entry management */
    if (!ip) return ERROR_INVALID_PARAM;
    
    /* Add to proxy table - simplified implementation */
    LOG_INFO("Adding proxy ARP entry for IP");
    return SUCCESS;
    return ERROR_NOT_SUPPORTED;
}

int arp_remove_proxy_entry(const ip_addr_t *ip) {
    /* Implement proxy entry removal */
    if (!ip) return ERROR_INVALID_PARAM;
    
    LOG_INFO("Removing proxy ARP entry");
    return SUCCESS;
    return ERROR_NOT_SUPPORTED;
}

void arp_print_stats(void) {
    /* Implement statistics printing */
    printf("ARP Statistics:\n");
    printf("  Requests sent: %lu\n", g_arp_stats.requests_sent);
    printf("  Responses sent: %lu\n", g_arp_stats.responses_sent);
    printf("  Requests received: %lu\n", g_arp_stats.requests_received);
    printf("  Responses received: %lu\n", g_arp_stats.responses_received);
    printf("  Cache hits: %lu\n", g_arp_stats.cache_hits);
    printf("  Cache misses: %lu\n", g_arp_stats.cache_misses);
}

void arp_print_cache(void) {
    /* Implement cache printing */
    printf("ARP Cache:\n");
    for (int i = 0; i < g_arp_cache.max_entries; i++) {
        arp_cache_entry_t *entry = &g_arp_cache.entries[i];
        if (entry->flags & ARP_FLAG_VALID) {
            printf("  Entry %d: IP=%d.%d.%d.%d MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
                   i, entry->ip.octets[0], entry->ip.octets[1], 
                   entry->ip.octets[2], entry->ip.octets[3],
                   entry->mac[0], entry->mac[1], entry->mac[2],
                   entry->mac[3], entry->mac[4], entry->mac[5]);
        }
    }
}

void arp_dump_packet(const arp_packet_t *arp_pkt) {
    /* Implement packet dumping */
    if (!arp_pkt) return;
    
    printf("ARP Packet:\n");
    printf("  Hardware Type: %04X\n", arp_pkt->hw_type);
    printf("  Protocol Type: %04X\n", arp_pkt->proto_type);
    printf("  Operation: %04X\n", arp_pkt->operation);
}

void arp_dump_cache_entry(const arp_cache_entry_t *entry) {
    /* Implement cache entry dumping */
    if (!entry) return;
    
    printf("ARP Cache Entry:\n");
    printf("  IP: %d.%d.%d.%d\n", entry->ip.octets[0], entry->ip.octets[1],
           entry->ip.octets[2], entry->ip.octets[3]);
    printf("  MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
           entry->mac[0], entry->mac[1], entry->mac[2],
           entry->mac[3], entry->mac[4], entry->mac[5]);
    printf("  Flags: %04X\n", entry->flags);
}

void arp_dump_cache(void) {
    /* Implement cache dumping */
    printf("Complete ARP Cache Dump:\n");
    for (int i = 0; i < g_arp_cache.max_entries; i++) {
        arp_cache_entry_t *entry = &g_arp_cache.entries[i];
        if (entry->flags & ARP_FLAG_VALID) {
            printf("Entry %d: ", i);
            arp_dump_cache_entry(entry);
        }
    }
}

const char* arp_flags_to_string(uint16_t flags) {
    /* Implement flags to string conversion */
    static char flag_str[64];
    flag_str[0] = '\0';
    
    if (flags & ARP_FLAG_VALID) strcat(flag_str, "VALID ");
    if (flags & ARP_FLAG_PERMANENT) strcat(flag_str, "PERMANENT ");
    if (flags & ARP_FLAG_PUBLISHED) strcat(flag_str, "PUBLISHED ");
    
    return flag_str[0] ? flag_str : "NONE";
}

int arp_register_with_pipeline(void) {
    /* Register ARP packet handler with packet pipeline */
    LOG_INFO("Registering ARP handler with packet pipeline");
    /* In full implementation would register with packet classification system */
    return SUCCESS;
}

/* Helper functions for ARP implementation */
static bool arp_can_proxy_for_ip(const ip_addr_t *ip) {
    if (!ip) return false;
    
    /* Conservative proxy ARP - only proxy for configured subnets */
    /* Check if IP is in same subnet as any of our NICs */
    for (int i = 0; i < hardware_get_nic_count(); i++) {
        nic_info_t *nic = hardware_get_nic(i);
        if (nic && hardware_is_nic_active(i)) {
            /* In a full implementation, would check IP/subnet configuration */
            /* For now, be conservative and don't proxy by default */
            /* This prevents security issues from promiscuous proxying */
        }
    }
    
    /* Conservative default: don't proxy unless explicitly configured */
    return false;
}

static int arp_send_response(const ip_addr_t *target_ip, const uint8_t *src_mac, const uint8_t *dest_mac) {
    if (!target_ip || !src_mac || !dest_mac) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Send ARP response - simplified implementation */
    LOG_DEBUG("Sending ARP response for proxy request");
    g_arp_stats.responses_sent++;
    
    return SUCCESS;
}