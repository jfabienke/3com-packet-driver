/**
 * @file routing.h
 * @brief Routing function prototypes
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef _ROUTING_H_
#define _ROUTING_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include "common.h"
#include "hardware.h"
#include "packet_ops.h"

/* Routing decision types */
typedef enum {
    ROUTE_DECISION_DROP = 0,                /* Drop the packet */
    ROUTE_DECISION_FORWARD,                 /* Forward the packet */
    ROUTE_DECISION_BROADCAST,               /* Broadcast the packet */
    ROUTE_DECISION_LOOPBACK,                /* Loop back to sender */
    ROUTE_DECISION_MULTICAST                /* Send to multicast group */
} route_decision_t;

/* Routing rule types */
typedef enum {
    ROUTE_RULE_NONE = 0,                    /* No specific rule */
    ROUTE_RULE_MAC_ADDRESS,                 /* Route by MAC address */
    ROUTE_RULE_ETHERTYPE,                   /* Route by Ethernet type */
    ROUTE_RULE_PORT,                        /* Route by port */
    ROUTE_RULE_VLAN,                        /* Route by VLAN (if supported) */
    ROUTE_RULE_PRIORITY                     /* Route by priority */
} route_rule_type_t;

/* Routing table entry */
typedef struct route_entry {
    route_rule_type_t rule_type;            /* Type of routing rule */
    uint8_t dest_mac[ETH_ALEN];             /* Destination MAC address */
    uint8_t mask[ETH_ALEN];                 /* MAC address mask */
    uint16_t ethertype;                     /* Ethernet type filter */
    uint8_t src_nic;                        /* Source NIC index */
    uint8_t dest_nic;                       /* Destination NIC index */
    route_decision_t decision;              /* Routing decision */
    uint8_t priority;                       /* Rule priority */
    uint32_t flags;                         /* Additional flags */
    uint32_t packet_count;                  /* Packets matched */
    uint32_t byte_count;                    /* Bytes matched */
    struct route_entry *next;               /* Next entry in table */
} route_entry_t;

/* Routing table structure */
typedef struct routing_table {
    route_entry_t *entries;                 /* Table entries */
    uint16_t entry_count;                   /* Number of entries */
    uint16_t max_entries;                   /* Maximum entries */
    route_decision_t default_decision;      /* Default routing decision */
    uint8_t default_nic;                    /* Default output NIC */
    bool learning_enabled;                  /* MAC learning enabled */
    uint32_t learning_timeout;              /* Learning timeout (ms) */
} routing_table_t;

/* Bridge learning table entry */
typedef struct bridge_entry {
    uint8_t mac[ETH_ALEN];                  /* MAC address */
    uint8_t nic_index;                      /* Associated NIC */
    uint32_t timestamp;                     /* Last seen timestamp */
    uint32_t packet_count;                  /* Packets from this MAC */
    struct bridge_entry *next;              /* Next entry */
} bridge_entry_t;

/* Bridge learning table */
typedef struct bridge_table {
    bridge_entry_t *entries;                /* Table entries */
    uint16_t entry_count;                   /* Number of entries */
    uint16_t max_entries;                   /* Maximum entries */
    uint32_t aging_time;                    /* Entry aging time (ms) */
    uint32_t total_lookups;                 /* Total lookup count */
    uint32_t successful_lookups;            /* Successful lookups */
} bridge_table_t;

/* Routing statistics */
typedef struct routing_stats {
    uint32_t packets_routed;                /* Total packets routed */
    uint32_t packets_dropped;               /* Packets dropped */
    uint32_t packets_broadcast;             /* Broadcast packets */
    uint32_t packets_multicast;             /* Multicast packets */
    uint32_t packets_forwarded;             /* Forwarded packets */
    uint32_t packets_looped;                /* Looped back packets */
    uint32_t routing_errors;                /* Routing errors */
    uint32_t table_lookups;                 /* Table lookup count */
    uint32_t cache_hits;                    /* Cache hit count */
    uint32_t cache_misses;                  /* Cache miss count */
} routing_stats_t;

/* Global routing state */
extern routing_table_t g_routing_table;
extern bridge_table_t g_bridge_table;
extern routing_stats_t g_routing_stats;
extern bool g_routing_enabled;

/* Routing initialization and cleanup */
int routing_init(void);
void routing_cleanup(void);
int routing_enable(bool enable);
bool routing_is_enabled(void);

/* Routing table management */
int routing_table_init(routing_table_t *table, uint16_t max_entries);
void routing_table_cleanup(routing_table_t *table);
int routing_add_rule(route_rule_type_t rule_type, const void *rule_data,
                    uint8_t src_nic, uint8_t dest_nic, route_decision_t decision);
int routing_remove_rule(route_rule_type_t rule_type, const void *rule_data);
route_entry_t* routing_find_rule(route_rule_type_t rule_type, const void *rule_data);
void routing_clear_table(void);
int routing_set_default_route(uint8_t nic_index, route_decision_t decision);

/* Packet routing decisions */
route_decision_t routing_decide(const packet_buffer_t *packet, uint8_t src_nic,
                               uint8_t *dest_nic);
route_decision_t routing_lookup_mac(const uint8_t *dest_mac, uint8_t src_nic,
                                   uint8_t *dest_nic);
route_decision_t routing_lookup_ethertype(uint16_t ethertype, uint8_t src_nic,
                                         uint8_t *dest_nic);

/* Bridge learning functions */
int bridge_table_init(bridge_table_t *table, uint16_t max_entries);
void bridge_table_cleanup(bridge_table_t *table);
int bridge_learn_mac(const uint8_t *mac, uint8_t nic_index);
bridge_entry_t* bridge_lookup_mac(const uint8_t *mac);
void bridge_age_entries(void);
void bridge_flush_table(void);
int bridge_remove_mac(const uint8_t *mac);

/* Packet processing */
int route_packet(packet_buffer_t *packet, uint8_t src_nic);
int forward_packet(packet_buffer_t *packet, uint8_t src_nic, uint8_t dest_nic);
int broadcast_packet(packet_buffer_t *packet, uint8_t src_nic);
int multicast_packet(packet_buffer_t *packet, uint8_t src_nic,
                    const uint8_t *dest_mac);

/* Special routing functions */
int route_handle_broadcast(packet_buffer_t *packet, uint8_t src_nic);
int route_handle_multicast(packet_buffer_t *packet, uint8_t src_nic);
int route_handle_unicast(packet_buffer_t *packet, uint8_t src_nic);
int route_handle_unknown_unicast(packet_buffer_t *packet, uint8_t src_nic);

/* Filtering and validation */
bool routing_should_forward(const packet_buffer_t *packet, uint8_t src_nic,
                           uint8_t dest_nic);
bool routing_is_loop(const packet_buffer_t *packet, uint8_t src_nic,
                    uint8_t dest_nic);
bool routing_validate_nic(uint8_t nic_index);

/* Statistics and monitoring */
void routing_stats_init(routing_stats_t *stats);
void routing_stats_update(routing_stats_t *stats, route_decision_t decision);
const routing_stats_t* routing_get_stats(void);
void routing_clear_stats(void);
void routing_print_stats(void);
void routing_print_table(void);
void routing_print_bridge_table(void);

/* Configuration */
int routing_set_learning_enabled(bool enable);
bool routing_get_learning_enabled(void);
int routing_set_aging_time(uint32_t aging_time_ms);
uint32_t routing_get_aging_time(void);
int routing_set_table_size(uint16_t max_entries);

/* MAC address utilities for routing */
bool routing_mac_equals(const uint8_t *mac1, const uint8_t *mac2);
bool routing_mac_match_mask(const uint8_t *mac, const uint8_t *pattern,
                           const uint8_t *mask);
void routing_mac_copy(uint8_t *dest, const uint8_t *src);
bool routing_is_local_mac(const uint8_t *mac);

/* Flow control and rate limiting */
int routing_set_rate_limit(uint8_t nic_index, uint32_t packets_per_sec);
int routing_check_rate_limit(uint8_t nic_index);
void routing_update_rate_counters(void);

/* VLAN support (if enabled) */
#ifdef ROUTING_VLAN_SUPPORT
typedef struct vlan_entry {
    uint16_t vlan_id;
    uint8_t nic_mask;                       /* Bitmask of NICs in this VLAN */
    struct vlan_entry *next;
} vlan_entry_t;

int routing_add_vlan(uint16_t vlan_id, uint8_t nic_mask);
int routing_remove_vlan(uint16_t vlan_id);
vlan_entry_t* routing_find_vlan(uint16_t vlan_id);
route_decision_t routing_decide_vlan(const packet_buffer_t *packet,
                                    uint8_t src_nic, uint8_t *dest_nic);
#endif

/* Debugging and diagnostics */
void routing_dump_table(void);
void routing_dump_bridge_table(void);
void routing_dump_packet_route(const packet_buffer_t *packet, uint8_t src_nic);
const char* routing_decision_to_string(route_decision_t decision);
const char* routing_rule_type_to_string(route_rule_type_t rule_type);

/* Test and validation functions */
int routing_self_test(void);
int routing_validate_configuration(void);
int routing_test_forwarding(uint8_t src_nic, uint8_t dest_nic);

#ifdef __cplusplus
}
#endif

#endif /* _ROUTING_H_ */
