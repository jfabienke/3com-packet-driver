/**
 * @file arp.h
 * @brief ARP Protocol Implementation (RFC 826)
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef _ARP_H_
#define _ARP_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include "common.h"
#include "static_routing.h"

/* ARP Constants from RFC 826 */
#define ARP_HW_TYPE_ETHERNET        1       /* Ethernet hardware type */
#define ARP_PROTO_TYPE_IP           0x0800  /* IPv4 protocol type */
#define ARP_HW_LEN_ETHERNET         6       /* Ethernet address length */
#define ARP_PROTO_LEN_IP            4       /* IPv4 address length */

/* ARP Operations */
#define ARP_OP_REQUEST              1       /* ARP request */
#define ARP_OP_REPLY                2       /* ARP reply */

/* ARP packet structure (28 bytes for Ethernet/IPv4) */
typedef struct arp_packet {
    uint16_t hw_type;                       /* Hardware type (network byte order) */
    uint16_t proto_type;                    /* Protocol type (network byte order) */
    uint8_t  hw_len;                        /* Hardware address length */
    uint8_t  proto_len;                     /* Protocol address length */
    uint16_t operation;                     /* Operation (network byte order) */
    uint8_t  sender_hw[ETH_ALEN];           /* Sender hardware address */
    uint8_t  sender_proto[4];               /* Sender protocol address */
    uint8_t  target_hw[ETH_ALEN];           /* Target hardware address */
    uint8_t  target_proto[4];               /* Target protocol address */
} PACKED arp_packet_t;

/* ARP table size and management */
#define ARP_TABLE_SIZE              256     /* Maximum ARP entries */
#define ARP_HASH_SIZE               64      /* Hash table size (power of 2) */
#define ARP_HASH_MASK               63      /* Hash mask (ARP_HASH_SIZE - 1) */
#define ARP_ENTRY_TIMEOUT           300000  /* 5 minutes in milliseconds */
#define ARP_REQUEST_TIMEOUT         3000    /* 3 seconds for ARP requests */
#define ARP_MAX_RETRIES             3       /* Maximum ARP request retries */

/* Enhanced ARP entry structure */
typedef struct arp_cache_entry {
    ip_addr_t ip;                           /* IP address */
    uint8_t mac[ETH_ALEN];                  /* MAC address */
    uint8_t nic_index;                      /* NIC where learned */
    uint32_t timestamp;                     /* Entry timestamp */
    uint16_t flags;                         /* Entry flags */
    uint16_t state;                         /* Entry state */
    uint16_t retry_count;                   /* Request retry count */
    uint32_t last_request_time;             /* Last request time */
    struct arp_cache_entry *next;           /* Next in linked list */
    struct arp_cache_entry *hash_next;      /* Next in hash bucket */
} arp_cache_entry_t;

/* ARP entry states */
#define ARP_STATE_FREE              0       /* Entry is free */
#define ARP_STATE_INCOMPLETE        1       /* Waiting for reply */
#define ARP_STATE_COMPLETE          2       /* Entry is complete */
#define ARP_STATE_EXPIRED           3       /* Entry has expired */
#define ARP_STATE_PERMANENT         4       /* Permanent entry */

/* ARP entry flags */
#define ARP_FLAG_STATIC             BIT(0)  /* Static entry */
#define ARP_FLAG_PUBLISHED          BIT(1)  /* Published (proxy ARP) */
#define ARP_FLAG_COMPLETE           BIT(2)  /* Complete entry */
#define ARP_FLAG_PERMANENT          BIT(3)  /* Permanent entry */
#define ARP_FLAG_PROXY              BIT(4)  /* Proxy ARP entry */
#define ARP_FLAG_LOCAL              BIT(5)  /* Local interface address */

/* ARP cache management structure */
typedef struct arp_cache {
    arp_cache_entry_t *entries;             /* Entry pool */
    arp_cache_entry_t *hash_table[ARP_HASH_SIZE]; /* Hash table */
    arp_cache_entry_t *free_list;           /* Free entry list */
    uint16_t entry_count;                   /* Number of entries */
    uint16_t max_entries;                   /* Maximum entries */
    uint32_t total_lookups;                 /* Total lookups */
    uint32_t successful_lookups;            /* Successful lookups */
    uint32_t cache_hits;                    /* Cache hits */
    uint32_t cache_misses;                  /* Cache misses */
    bool initialized;                       /* Cache initialized */
} arp_cache_t;

/* ARP statistics */
typedef struct arp_stats {
    uint32_t packets_received;              /* ARP packets received */
    uint32_t packets_sent;                  /* ARP packets sent */
    uint32_t requests_received;             /* ARP requests received */
    uint32_t requests_sent;                 /* ARP requests sent */
    uint32_t replies_received;              /* ARP replies received */
    uint32_t replies_sent;                  /* ARP replies sent */
    uint32_t cache_updates;                 /* Cache updates */
    uint32_t cache_timeouts;                /* Cache timeouts */
    uint32_t request_timeouts;              /* Request timeouts */
    uint32_t invalid_packets;               /* Invalid packets */
    uint32_t proxy_requests;                /* Proxy ARP requests */
    uint32_t gratuitous_arps;               /* Gratuitous ARPs */
} arp_stats_t;

/* Global ARP state */
extern arp_cache_t g_arp_cache;
extern arp_stats_t g_arp_stats;
extern bool g_arp_enabled;

/* ARP initialization and cleanup */
int arp_init(void);
void arp_cleanup(void);
int arp_enable(bool enable);
bool arp_is_enabled(void);

/* ARP cache management */
int arp_cache_init(arp_cache_t *cache, uint16_t max_entries);
void arp_cache_cleanup(arp_cache_t *cache);
arp_cache_entry_t* arp_cache_lookup(const ip_addr_t *ip);
int arp_cache_add(const ip_addr_t *ip, const uint8_t *mac, uint8_t nic_index, uint16_t flags);
int arp_cache_update(const ip_addr_t *ip, const uint8_t *mac, uint8_t nic_index);
int arp_cache_delete(const ip_addr_t *ip);
void arp_cache_flush(void);
void arp_cache_age_entries(void);

/* ARP packet processing */
int arp_process_packet(const uint8_t *packet, uint16_t length, uint8_t src_nic);
int arp_handle_request(const arp_packet_t *arp_pkt, uint8_t src_nic);
int arp_handle_reply(const arp_packet_t *arp_pkt, uint8_t src_nic);
bool arp_validate_packet(const arp_packet_t *arp_pkt, uint16_t length);

/* ARP packet generation */
int arp_send_request(const ip_addr_t *target_ip, uint8_t nic_index);
int arp_send_reply(const ip_addr_t *target_ip, const uint8_t *target_mac,
                   const ip_addr_t *sender_ip, uint8_t nic_index);
int arp_send_gratuitous(const ip_addr_t *ip, uint8_t nic_index);
int arp_build_packet(arp_packet_t *arp_pkt, uint16_t operation,
                     const uint8_t *sender_hw, const ip_addr_t *sender_ip,
                     const uint8_t *target_hw, const ip_addr_t *target_ip);

/* ARP resolution */
int arp_resolve(const ip_addr_t *ip, uint8_t *mac, uint8_t *nic_index);
int arp_resolve_async(const ip_addr_t *ip, uint8_t nic_index);
bool arp_is_resolved(const ip_addr_t *ip);
int arp_wait_for_resolution(const ip_addr_t *ip, uint32_t timeout_ms);

/* ARP utilities */
uint16_t arp_calculate_hash(const ip_addr_t *ip);
arp_cache_entry_t* arp_find_free_entry(void);
void arp_remove_from_hash(arp_cache_entry_t *entry);
void arp_add_to_hash(arp_cache_entry_t *entry);
bool arp_is_local_ip(const ip_addr_t *ip);
uint8_t arp_get_nic_for_ip(const ip_addr_t *ip);

/* ARP proxy support */
int arp_add_proxy_entry(const ip_addr_t *ip, uint8_t nic_index);
int arp_remove_proxy_entry(const ip_addr_t *ip);
bool arp_is_proxy_enabled(void);
int arp_set_proxy_enabled(bool enable);

/* ARP statistics and monitoring */
void arp_stats_init(arp_stats_t *stats);
const arp_stats_t* arp_get_stats(void);
void arp_clear_stats(void);
void arp_print_stats(void);
void arp_print_cache(void);

/* ARP debugging and diagnostics */
void arp_dump_packet(const arp_packet_t *arp_pkt);
void arp_dump_cache_entry(const arp_cache_entry_t *entry);
void arp_dump_cache(void);
const char* arp_operation_to_string(uint16_t operation);
const char* arp_state_to_string(uint16_t state);
const char* arp_flags_to_string(uint16_t flags);

/* ARP integration with packet pipeline */
int arp_register_with_pipeline(void);
int arp_process_received_packet(const uint8_t *packet, uint16_t length, uint8_t src_nic);
bool arp_is_arp_packet(const uint8_t *packet, uint16_t length);

/* ARP configuration */
int arp_set_timeout(uint32_t timeout_ms);
uint32_t arp_get_timeout(void);
int arp_set_max_retries(uint8_t max_retries);
uint8_t arp_get_max_retries(void);
int arp_set_request_timeout(uint32_t timeout_ms);
uint32_t arp_get_request_timeout(void);

#ifdef __cplusplus
}
#endif

#endif /* _ARP_H_ */