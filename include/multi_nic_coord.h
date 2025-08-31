/**
 * @file multi_nic_coord.h
 * @brief Enhanced Multi-NIC Coordination Header
 * 
 * Phase 5 Enhancement: Advanced multi-NIC management with load balancing,
 * failover, and intelligent packet routing
 * 
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef MULTI_NIC_COORD_H
#define MULTI_NIC_COORD_H

#include <stdint.h>
/* DOS compatibility: avoid stdbool.h, use explicit int */
#include "common.h"

/* Maximum limits */
#define MAX_MULTI_NICS      8       /* Maximum NICs to coordinate */
#define MAX_NIC_GROUPS      4       /* Maximum NIC groups */
#define MAX_FLOWS           1024    /* Maximum tracked flows */

/* NIC States */
#define NIC_STATE_UNKNOWN   0x00
#define NIC_STATE_DOWN      0x01
#define NIC_STATE_UP        0x02
#define NIC_STATE_ERROR     0x03
#define NIC_STATE_TESTING   0x04

/* NIC Roles */
#define NIC_ROLE_PRIMARY    0x00
#define NIC_ROLE_STANDBY    0x01
#define NIC_ROLE_ACTIVE     0x02
#define NIC_ROLE_PASSIVE    0x03

/* Multi-NIC Modes */
#define MULTI_NIC_MODE_ACTIVE_STANDBY  0x00
#define MULTI_NIC_MODE_ACTIVE_ACTIVE   0x01
#define MULTI_NIC_MODE_LOAD_BALANCE    0x02
#define MULTI_NIC_MODE_LACP            0x03

/* Load Balancing Algorithms */
#define LB_ALGO_ROUND_ROBIN     0x00
#define LB_ALGO_WEIGHTED        0x01
#define LB_ALGO_LEAST_LOADED    0x02
#define LB_ALGO_HASH_BASED      0x03
#define LB_ALGO_ADAPTIVE        0x04
#define LB_ALGO_COUNT           5

/* Multi-NIC Flags */
#define MULTI_NIC_FLAG_ENABLED          0x01
#define MULTI_NIC_FLAG_AUTO_FAILBACK    0x02
#define MULTI_NIC_FLAG_HEALTH_CHECK     0x04
#define MULTI_NIC_FLAG_FLOW_TRACKING    0x08
#define MULTI_NIC_FLAG_STATS_ENABLED    0x10

/* Group Types */
#define GROUP_TYPE_FAILOVER     0x00
#define GROUP_TYPE_LOAD_BALANCE 0x01
#define GROUP_TYPE_AGGREGATE    0x02

/**
 * @brief NIC capabilities structure
 */
typedef struct {
    uint32_t speed;             /* Link speed in Mbps */
    uint16_t mtu;               /* Maximum transmission unit */
    uint16_t max_queue_size;    /* Maximum queue size */
    uint8_t duplex;             /* 0=half, 1=full */
    uint8_t features;           /* Feature flags */
    uint8_t offload_caps;       /* Offload capabilities */
    uint8_t reserved;
} nic_capabilities_t;

/**
 * @brief Per-NIC statistics
 */
typedef struct {
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t bytes_sent;
    uint32_t bytes_received;
    uint32_t errors;
    uint32_t drops;
    uint32_t packets_queued;
    uint32_t queue_overflows;
} nic_stats_t;

/**
 * @brief NIC entry in coordinator
 */
typedef struct {
    uint8_t nic_index;          /* NIC index */
    uint8_t state;              /* Current state */
    uint8_t role;               /* Current role */
    uint8_t priority;           /* Priority for selection */
    uint8_t weight;             /* Weight for load balancing */
    uint8_t consecutive_failures; /* Consecutive failure count */
    uint16_t reserved;
    uint32_t last_state_change; /* Last state change time */
    nic_capabilities_t capabilities;
    nic_stats_t stats;
} nic_entry_t;

/**
 * @brief Flow entry for connection tracking
 */
typedef struct {
    uint32_t flow_id;           /* Flow identifier */
    uint32_t flow_hash;         /* Flow hash for quick lookup */
    uint32_t src_ip;            /* Source IP */
    uint32_t dst_ip;            /* Destination IP */
    uint16_t src_port;          /* Source port */
    uint16_t dst_port;          /* Destination port */
    uint8_t protocol;           /* IP protocol */
    uint8_t nic_index;          /* Assigned NIC */
    uint32_t created;           /* Creation time */
    uint32_t last_activity;     /* Last activity time */
    uint32_t packet_count;      /* Packets in flow */
} flow_entry_t;

/**
 * @brief NIC group structure
 */
typedef struct {
    uint8_t group_id;           /* Group identifier */
    char name[16];              /* Group name */
    uint8_t type;               /* Group type */
    uint8_t member_count;       /* Number of members */
    uint8_t active_members;     /* Active member count */
    uint8_t *members;           /* Member NIC indices */
    uint32_t total_bandwidth;   /* Combined bandwidth */
} nic_group_t;

/**
 * @brief Multi-NIC configuration
 */
typedef struct {
    uint8_t mode;               /* Operating mode */
    uint8_t load_balance_algo;  /* Load balancing algorithm */
    uint8_t failover_threshold; /* Failures before failover */
    uint8_t failback_delay;     /* Delay before failback (seconds) */
    uint16_t health_check_interval; /* Health check interval (seconds) */
    uint16_t flow_timeout;      /* Flow timeout (seconds) */
    uint16_t max_flows;         /* Maximum tracked flows */
    uint8_t flags;              /* Configuration flags */
} multi_nic_config_t;

/**
 * @brief Multi-NIC statistics
 */
typedef struct {
    uint32_t packets_routed;    /* Total packets routed */
    uint32_t flow_hits;         /* Flow cache hits */
    uint32_t flow_misses;       /* Flow cache misses */
    uint32_t failovers;         /* Number of failovers */
    uint32_t failbacks;         /* Number of failbacks */
    uint32_t routing_failures;  /* Routing failures */
    uint32_t health_checks;     /* Health checks performed */
    uint32_t state_changes;     /* NIC state changes */
} multi_nic_stats_t;

/**
 * @brief Packet context for routing decisions
 */
typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t protocol;
    uint8_t priority;
    uint16_t packet_size;
    uint8_t selected_nic;       /* Output: selected NIC */
} packet_context_t;

/**
 * @brief Multi-NIC coordinator structure
 */
typedef struct {
    nic_entry_t nics[MAX_MULTI_NICS];
    nic_group_t groups[MAX_NIC_GROUPS];
    flow_entry_t *flow_table;
    multi_nic_config_t config;
    multi_nic_stats_t stats;
    uint8_t nic_count;
    uint8_t active_nic_count;
    uint8_t group_count;
    uint16_t flow_count;
    uint32_t last_health_check;
    uint32_t next_flow_id;
} multi_nic_coordinator_t;

/* Function pointer types */
typedef int (*load_balance_func_t)(packet_context_t *context, uint8_t *selected_nic);
typedef void (*failover_callback_t)(uint8_t old_nic, uint8_t new_nic);

/* Function Prototypes - Initialization */
int multi_nic_init(void);
int multi_nic_cleanup(void);
int multi_nic_configure(const multi_nic_config_t *config);

/* Function Prototypes - NIC Management */
int multi_nic_register(uint8_t nic_index, const nic_capabilities_t *caps);
int multi_nic_unregister(uint8_t nic_index);
int multi_nic_update_state(uint8_t nic_index, uint8_t new_state);
int multi_nic_set_priority(uint8_t nic_index, uint8_t priority);
int multi_nic_set_weight(uint8_t nic_index, uint8_t weight);

/* Function Prototypes - Packet Routing */
int multi_nic_select_tx(packet_context_t *context, uint8_t *selected_nic);
int multi_nic_route_packet(const uint8_t *packet, uint16_t length, uint8_t *selected_nic);

/* Function Prototypes - Failover Management */
int multi_nic_handle_failure(uint8_t failed_nic);
int multi_nic_initiate_failback(uint8_t primary_nic);
int multi_nic_register_failover_callback(failover_callback_t callback);

/* Function Prototypes - Health Monitoring */
int multi_nic_health_check(void);
int multi_nic_get_nic_health(uint8_t nic_index, bool *healthy);
int multi_nic_set_health_params(uint8_t threshold, uint16_t interval);

/* Function Prototypes - Group Management */
int multi_nic_create_group(uint8_t group_id, const char *name, uint8_t type);
int multi_nic_delete_group(uint8_t group_id);
int multi_nic_add_to_group(uint8_t group_id, uint8_t nic_index);
int multi_nic_remove_from_group(uint8_t group_id, uint8_t nic_index);

/* Function Prototypes - Flow Management */
int multi_nic_track_flow(packet_context_t *context);
int multi_nic_expire_flows(void);
int multi_nic_get_flow_stats(uint32_t flow_id, flow_entry_t *flow);

/* Function Prototypes - Statistics */
void multi_nic_get_stats(multi_nic_stats_t *stats);
void multi_nic_reset_stats(void);
void multi_nic_get_nic_stats(uint8_t nic_index, nic_stats_t *stats);
void multi_nic_dump_status(void);

/* Function Prototypes - Configuration */
int multi_nic_set_mode(uint8_t mode);
int multi_nic_set_load_balance_algo(uint8_t algo);
int multi_nic_enable_feature(uint8_t feature);
int multi_nic_disable_feature(uint8_t feature);

/* Internal helper functions */
static nic_entry_t* multi_nic_find_entry(uint8_t nic_index);
static flow_entry_t* multi_nic_find_flow(packet_context_t *context);
static int multi_nic_create_flow(packet_context_t *context, uint8_t nic_index);
static void multi_nic_migrate_flows(uint8_t from_nic, uint8_t to_nic);
static void multi_nic_cleanup_flows(void);
static bool multi_nic_check_nic_health(nic_entry_t *nic);
static int multi_nic_select_active_standby(uint8_t *selected_nic);
static int multi_nic_select_active_active(packet_context_t *context, uint8_t *selected_nic);
static int multi_nic_select_load_balance(packet_context_t *context, uint8_t *selected_nic);
static int multi_nic_select_lacp(packet_context_t *context, uint8_t *selected_nic);
static void multi_nic_schedule_failback(uint8_t nic_index);
static uint32_t multi_nic_hash_flow(packet_context_t *context);
static const char* multi_nic_state_name(uint8_t state);
static const char* multi_nic_role_name(uint8_t role);

/* External utility functions */
extern uint32_t get_system_time(void);
extern void log_set_level(uint8_t level);

#endif /* MULTI_NIC_COORD_H */