/**
 * @file promisc.h
 * @brief Promiscuous mode support with advanced packet capture and filtering
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef _PROMISC_H_
#define _PROMISC_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include "common.h"
#include "hardware.h"

/* Promiscuous mode constants */
#define PROMISC_BUFFER_COUNT       64     /* Number of capture buffers */
#define PROMISC_BUFFER_SIZE        1600   /* Size of each buffer */
#define PROMISC_MAX_FILTERS        16     /* Maximum number of filters */
#define PROMISC_FILTER_TIMEOUT_MS  1000   /* Filter timeout */
#define PROMISC_MAX_APPLICATIONS   8      /* Max applications using promiscuous mode */

/* Promiscuous mode levels */
typedef enum {
    PROMISC_LEVEL_OFF = 0,           /* Disabled */
    PROMISC_LEVEL_BASIC,             /* Basic promiscuous mode */
    PROMISC_LEVEL_FULL,              /* Full packet capture */
    PROMISC_LEVEL_SELECTIVE          /* Selective with filters */
} promisc_level_t;

/* Promiscuous mode filter types */
typedef enum {
    PROMISC_FILTER_ALL = 0,          /* Capture all packets */
    PROMISC_FILTER_PROTOCOL,         /* Filter by protocol */
    PROMISC_FILTER_MAC_SRC,          /* Filter by source MAC */
    PROMISC_FILTER_MAC_DST,          /* Filter by destination MAC */
    PROMISC_FILTER_LENGTH,           /* Filter by packet length */
    PROMISC_FILTER_CONTENT           /* Filter by packet content */
} promisc_filter_type_t;

/* Promiscuous mode filter definition */
typedef struct {
    promisc_filter_type_t type;      /* Filter type */
    bool enabled;                    /* Filter enabled flag */
    uint32_t match_value;            /* Match value */
    uint32_t mask;                   /* Mask for matching */
    uint8_t mac_addr[ETH_ALEN];      /* MAC address for MAC filters */
    uint8_t content_pattern[16];     /* Content pattern for content filters */
    uint8_t pattern_length;          /* Length of content pattern */
    uint32_t min_length;             /* Minimum packet length */
    uint32_t max_length;             /* Maximum packet length */
} promisc_filter_t;

/* Promiscuous mode statistics */
typedef struct {
    uint32_t total_packets;          /* All packets captured */
    uint32_t filtered_packets;       /* Packets matching filters */
    uint32_t dropped_packets;        /* Packets dropped due to buffer limits */
    uint32_t broadcast_packets;      /* Broadcast traffic */
    uint32_t multicast_packets;      /* Multicast traffic */
    uint32_t unicast_packets;        /* Unicast traffic */
    uint32_t error_packets;          /* Packets with errors */
    uint32_t oversized_packets;      /* Oversized packets */
    uint32_t undersized_packets;     /* Undersized packets */
    uint32_t buffer_overflows;       /* Buffer overflow count */
    uint32_t filter_matches;         /* Filter match count */
    uint32_t bytes_captured;         /* Total bytes captured */
} promiscuous_stats_t;

/* Promiscuous mode packet buffer */
typedef struct {
    uint32_t timestamp;              /* Packet timestamp */
    uint16_t length;                 /* Packet length */
    uint16_t status;                 /* Packet status flags */
    uint8_t nic_index;               /* Source NIC index */
    uint8_t filter_matched;          /* Which filter matched (0 = none) */
    uint8_t packet_type;             /* Packet type classification */
    uint8_t reserved;                /* Reserved for alignment */
    uint8_t data[PROMISC_BUFFER_SIZE]; /* Packet data */
} promisc_packet_buffer_t;

/* Promiscuous mode application handle */
typedef struct {
    uint16_t handle_id;              /* Application handle ID */
    uint32_t pid;                    /* Process ID (if available) */
    promisc_level_t level;           /* Promiscuous level for this app */
    uint32_t filter_mask;            /* Bitmask of active filters */
    void far *callback;              /* Callback function */
    uint32_t packets_delivered;      /* Packets delivered to this app */
    uint32_t packets_dropped;        /* Packets dropped for this app */
    bool active;                     /* Handle is active */
} promisc_app_handle_t;

/* Promiscuous mode configuration */
typedef struct {
    promisc_level_t level;           /* Current promiscuous level */
    bool enabled;                    /* Promiscuous mode enabled */
    uint8_t active_nic_mask;         /* Bitmask of NICs with promiscuous mode */
    uint32_t buffer_count;           /* Number of active buffers */
    uint32_t filter_count;           /* Number of active filters */
    uint32_t app_count;              /* Number of registered applications */
    uint32_t capture_timeout_ms;     /* Capture timeout */
    bool learning_mode;              /* Learning mode for automatic filtering */
    bool integration_mode;           /* Integration with routing/diagnostics */
} promisc_config_t;

/* Global promiscuous mode state */
extern promisc_config_t g_promisc_config;
extern promiscuous_stats_t g_promisc_stats;
extern promisc_packet_buffer_t g_promisc_buffers[PROMISC_BUFFER_COUNT];
extern promisc_filter_t g_promisc_filters[PROMISC_MAX_FILTERS];
extern promisc_app_handle_t g_promisc_apps[PROMISC_MAX_APPLICATIONS];
extern volatile uint32_t g_promisc_buffer_head;
extern volatile uint32_t g_promisc_buffer_tail;

/* Core promiscuous mode functions */
int promisc_init(void);
void promisc_cleanup(void);
int promisc_enable(nic_info_t *nic, promisc_level_t level);
int promisc_disable(nic_info_t *nic);
bool promisc_is_enabled(nic_info_t *nic);

/* Packet capture and processing */
int promisc_capture_packet(nic_info_t *nic, const uint8_t *packet, uint16_t length);
int promisc_get_packet(promisc_packet_buffer_t *buffer);
int promisc_peek_packet(promisc_packet_buffer_t *buffer);
void promisc_process_captured_packets(void);

/* Filter management */
int promisc_add_filter(const promisc_filter_t *filter);
int promisc_remove_filter(int filter_id);
int promisc_clear_filters(void);
bool promisc_packet_matches_filters(const uint8_t *packet, uint16_t length);
int promisc_get_filter_count(void);

/* Application management */
int promisc_register_application(uint32_t pid, promisc_level_t level, void far *callback);
int promisc_unregister_application(uint16_t handle);
int promisc_deliver_to_applications(const promisc_packet_buffer_t *packet);
int promisc_get_application_count(void);

/* Statistics and monitoring */
const promiscuous_stats_t* promisc_get_stats(void);
void promisc_clear_stats(void);
void promisc_update_stats(const uint8_t *packet, uint16_t length, bool filtered);
void promisc_print_stats(void);

/* Configuration management */
int promisc_set_config(const promisc_config_t *config);
const promisc_config_t* promisc_get_config(void);
int promisc_set_level(promisc_level_t level);
promisc_level_t promisc_get_level(void);

/* Integration with Groups 3A/3B/3C */
int promisc_integrate_routing(void);     /* Integration with Group 3A routing */
int promisc_integrate_api(void);         /* Integration with Group 3B API */
int promisc_integrate_diagnostics(void); /* Integration with Group 3C diagnostics */

/* Hardware-specific promiscuous mode */
int promisc_enable_3c509b(nic_info_t *nic, promisc_level_t level);
int promisc_disable_3c509b(nic_info_t *nic);
int promisc_enable_3c515(nic_info_t *nic, promisc_level_t level);
int promisc_disable_3c515(nic_info_t *nic);

/* Utility functions */
const char* promisc_level_to_string(promisc_level_t level);
const char* promisc_filter_type_to_string(promisc_filter_type_t type);
bool promisc_is_broadcast_packet(const uint8_t *packet);
bool promisc_is_multicast_packet(const uint8_t *packet);
uint16_t promisc_classify_packet(const uint8_t *packet, uint16_t length);

/* Low-level ASM support functions */
extern void promisc_asm_capture_packet(const uint8_t *packet, uint16_t length);
extern void promisc_asm_enable_capture(void);
extern void promisc_asm_disable_capture(void);
extern uint32_t promisc_asm_get_timestamp(void);

#ifdef __cplusplus
}
#endif

#endif /* _PROMISC_H_ */
