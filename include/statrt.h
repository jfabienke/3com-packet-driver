/**
 * @file static_routing.h
 * @brief Static subnet-based routing
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef _STATIC_ROUTING_H_
#define _STATIC_ROUTING_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include "common.h"
#include "routing.h"

/* Network address structure for IPv4 */
typedef struct ip_addr {
    union {
        uint8_t addr[4];                        /* IPv4 address bytes */
        uint8_t octets[4];                      /* Alias for compatibility */
    };
} ip_addr_t;

/* Subnet structure */
typedef struct subnet_info {
    ip_addr_t network;                          /* Network address */
    ip_addr_t netmask;                          /* Network mask */
    uint8_t prefix_len;                         /* CIDR prefix length */
    uint8_t nic_index;                          /* Associated NIC index */
    uint32_t flags;                             /* Subnet flags */
    struct subnet_info *next;                   /* Next subnet in list */
} subnet_info_t;

/* Static route entry structure */
typedef struct static_route {
    ip_addr_t dest_network;                     /* Destination network */
    ip_addr_t netmask;                          /* Network mask */
    ip_addr_t gateway;                          /* Gateway IP address */
    uint8_t dest_nic;                           /* Destination NIC index */
    uint8_t metric;                             /* Route metric (lower = better) */
    uint32_t flags;                             /* Route flags */
    uint32_t age;                               /* Route age (ticks) */
    struct static_route *next;                  /* Next route in table */
} static_route_t;

/* Static routing table structure */
typedef struct static_routing_table {
    static_route_t *routes;                     /* Route entries */
    subnet_info_t *subnets;                     /* Subnet information */
    uint16_t route_count;                       /* Number of routes */
    uint16_t max_routes;                        /* Maximum routes */
    uint16_t subnet_count;                      /* Number of subnets */
    uint16_t max_subnets;                       /* Maximum subnets */
    ip_addr_t default_gateway;                  /* Default gateway */
    uint8_t default_nic;                        /* Default NIC index */
    bool initialized;                           /* Table initialized */
} static_routing_table_t;

/* ARP entry structure for IP-to-MAC mapping */
typedef struct arp_entry {
    ip_addr_t ip;                               /* IP address */
    uint8_t mac[ETH_ALEN];                      /* MAC address */
    uint8_t nic_index;                          /* NIC where learned */
    uint32_t timestamp;                         /* Entry timestamp */
    uint32_t flags;                             /* ARP flags */
    struct arp_entry *next;                     /* Next entry */
} arp_entry_t;

/* ARP table structure */
typedef struct arp_table {
    arp_entry_t *entries;                       /* ARP entries */
    uint16_t entry_count;                       /* Number of entries */
    uint16_t max_entries;                       /* Maximum entries */
    uint32_t aging_time;                        /* Entry aging time (ms) */
} arp_table_t;

/* Route flags */
#define STATIC_ROUTE_FLAG_UP            BIT(0)  /* Route is up */
#define STATIC_ROUTE_FLAG_GATEWAY       BIT(1)  /* Route via gateway */
#define STATIC_ROUTE_FLAG_HOST          BIT(2)  /* Host route (not network) */
#define STATIC_ROUTE_FLAG_DYNAMIC       BIT(3)  /* Dynamically learned */
#define STATIC_ROUTE_FLAG_MODIFIED      BIT(4)  /* Route was modified */
#define STATIC_ROUTE_FLAG_CLONING       BIT(5)  /* Route cloning */
#define STATIC_ROUTE_FLAG_LLINFO        BIT(6)  /* Link layer info present */

/* Subnet flags */
#define SUBNET_FLAG_ACTIVE              BIT(0)  /* Subnet is active */
#define SUBNET_FLAG_PRIMARY             BIT(1)  /* Primary subnet on NIC */
#define SUBNET_FLAG_DHCP                BIT(2)  /* DHCP assigned */
#define SUBNET_FLAG_STATIC              BIT(3)  /* Statically configured */

/* ARP flags */
#define ARP_FLAG_COMPLETE               BIT(0)  /* ARP entry complete */
#define ARP_FLAG_PERMANENT              BIT(1)  /* Permanent entry */
#define ARP_FLAG_PUBLISHED              BIT(2)  /* Published entry */
#define ARP_FLAG_PROXY                  BIT(3)  /* Proxy ARP entry */

/* Global static routing state */
extern static_routing_table_t g_static_routing_table;
extern arp_table_t g_arp_table;
extern bool g_static_routing_enabled;

/* Static routing initialization and cleanup */
int static_routing_init(void);
void static_routing_cleanup(void);
int static_routing_enable(bool enable);
bool static_routing_is_enabled(void);

/* Static routing table management */
int static_routing_table_init(static_routing_table_t *table, uint16_t max_routes, uint16_t max_subnets);
void static_routing_table_cleanup(static_routing_table_t *table);

/* Route management */
int static_route_add(const ip_addr_t *dest_network, const ip_addr_t *netmask,
                    const ip_addr_t *gateway, uint8_t nic_index, uint8_t metric);
int static_route_delete(const ip_addr_t *dest_network, const ip_addr_t *netmask);
static_route_t* static_route_lookup(const ip_addr_t *dest_ip);
static_route_t* static_route_find_exact(const ip_addr_t *dest_network, const ip_addr_t *netmask);
void static_route_clear_all(void);

/* Subnet management */
int static_subnet_add(const ip_addr_t *network, const ip_addr_t *netmask, uint8_t nic_index);
int static_subnet_delete(const ip_addr_t *network, const ip_addr_t *netmask);
subnet_info_t* static_subnet_lookup(const ip_addr_t *ip);
subnet_info_t* static_subnet_find_by_nic(uint8_t nic_index);
bool static_subnet_contains_ip(const subnet_info_t *subnet, const ip_addr_t *ip);

/* ARP table management */
int arp_table_init(arp_table_t *table, uint16_t max_entries);
void arp_table_cleanup(arp_table_t *table);
int arp_entry_add(const ip_addr_t *ip, const uint8_t *mac, uint8_t nic_index);
int arp_entry_delete(const ip_addr_t *ip);
arp_entry_t* arp_entry_lookup(const ip_addr_t *ip);
void arp_table_age_entries(void);
void arp_table_flush(void);

/* Default gateway management */
int static_routing_set_default_gateway(const ip_addr_t *gateway, uint8_t nic_index);
int static_routing_get_default_gateway(ip_addr_t *gateway, uint8_t *nic_index);
int static_routing_delete_default_gateway(void);

/* Routing decisions for IP packets */
uint8_t static_routing_get_output_nic(const ip_addr_t *dest_ip);
int static_routing_get_next_hop(const ip_addr_t *dest_ip, ip_addr_t *next_hop, uint8_t *nic_index);
bool static_routing_is_local_ip(const ip_addr_t *ip);
bool static_routing_is_local_subnet(const ip_addr_t *ip);

/* IP address utilities */
void ip_addr_set(ip_addr_t *addr, uint8_t a, uint8_t b, uint8_t c, uint8_t d);
void ip_addr_copy(ip_addr_t *dest, const ip_addr_t *src);
bool ip_addr_equals(const ip_addr_t *addr1, const ip_addr_t *addr2);
bool ip_addr_is_zero(const ip_addr_t *addr);
bool ip_addr_is_broadcast(const ip_addr_t *addr);
bool ip_addr_is_multicast(const ip_addr_t *addr);
bool ip_addr_is_loopback(const ip_addr_t *addr);
uint32_t ip_addr_to_uint32(const ip_addr_t *addr);
void ip_addr_from_uint32(ip_addr_t *addr, uint32_t value);

/* Subnet utilities */
void subnet_apply_mask(ip_addr_t *result, const ip_addr_t *ip, const ip_addr_t *mask);
bool subnet_contains_ip(const ip_addr_t *network, const ip_addr_t *mask, const ip_addr_t *ip);
uint8_t subnet_mask_to_prefix_len(const ip_addr_t *mask);
void subnet_prefix_len_to_mask(ip_addr_t *mask, uint8_t prefix_len);
bool subnet_is_valid_mask(const ip_addr_t *mask);

/* Packet processing integration */
int static_routing_process_ip_packet(const uint8_t *packet, uint16_t length,
                                    uint8_t src_nic, uint8_t *dest_nic);
int static_routing_resolve_mac(const ip_addr_t *ip, uint8_t *mac, uint8_t *nic_index);

/* Statistics and monitoring */
typedef struct static_routing_stats {
    uint32_t routes_added;                      /* Routes added */
    uint32_t routes_deleted;                    /* Routes deleted */
    uint32_t route_lookups;                     /* Route lookup count */
    uint32_t route_hits;                        /* Successful lookups */
    uint32_t route_misses;                      /* Failed lookups */
    uint32_t arp_requests;                      /* ARP requests sent */
    uint32_t arp_replies;                       /* ARP replies received */
    uint32_t arp_timeouts;                      /* ARP timeouts */
    uint32_t packets_routed;                    /* Packets routed via static routes */
    uint32_t packets_to_gateway;                /* Packets sent to default gateway */
} static_routing_stats_t;

extern static_routing_stats_t g_static_routing_stats;

void static_routing_stats_init(static_routing_stats_t *stats);
const static_routing_stats_t* static_routing_get_stats(void);
void static_routing_clear_stats(void);

/* Configuration and debugging */
void static_routing_print_table(void);
void static_routing_print_subnets(void);
void static_routing_print_arp_table(void);
const char* static_route_flags_to_string(uint32_t flags);

/* IP packet parsing helpers */
typedef struct ip_header {
    uint8_t version_ihl;                        /* Version and header length */
    uint8_t tos;                                /* Type of service */
    uint16_t total_length;                      /* Total packet length */
    uint16_t identification;                    /* Identification */
    uint16_t flags_fragment;                    /* Flags and fragment offset */
    uint8_t ttl;                                /* Time to live */
    uint8_t protocol;                           /* Protocol */
    uint16_t checksum;                          /* Header checksum */
    ip_addr_t src_ip;                           /* Source IP address */
    ip_addr_t dest_ip;                          /* Destination IP address */
} PACKED ip_header_t;

/* IP header parsing */
bool static_routing_parse_ip_header(const uint8_t *packet, uint16_t length, ip_header_t *header);
int static_routing_validate_ip_header(const ip_header_t *header);
uint16_t static_routing_calculate_ip_checksum(const ip_header_t *header);

/* Integration with main routing system */
route_decision_t static_routing_decide(const packet_buffer_t *packet, uint8_t src_nic, uint8_t *dest_nic);

#ifdef __cplusplus
}
#endif

#endif /* _STATIC_ROUTING_H_ */
