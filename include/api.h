/**
 * @file api.h
 * @brief Packet Driver API implementation
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef _API_H_
#define _API_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include <stdint.h>
#include "config.h"

/* Packet Driver function numbers */
#define PD_FUNC_DRIVER_INFO     0x01FF
#define PD_FUNC_ACCESS_TYPE     0x0200
#define PD_FUNC_RELEASE_TYPE    0x0300
#define PD_FUNC_SEND_PKT        0x0400
#define PD_FUNC_TERMINATE       0x0500
#define PD_FUNC_GET_ADDRESS     0x0600
#define PD_FUNC_RESET_INTERFACE 0x0700
#define PD_FUNC_GET_PARAMETERS  0x0A00
#define PD_FUNC_SET_RCV_MODE    0x1400
#define PD_FUNC_GET_RCV_MODE    0x1500
#define PD_FUNC_GET_STATISTICS  0x1800
#define PD_FUNC_SET_ADDRESS     0x1900

/* Phase 3 Group 3B Extended API Functions */
#define PD_FUNC_SET_HANDLE_PRIORITY    0x2000
#define PD_FUNC_GET_ROUTING_INFO       0x2100
#define PD_FUNC_SET_LOAD_BALANCE       0x2200
#define PD_FUNC_GET_NIC_STATUS         0x2300
#define PD_FUNC_SET_QOS_PARAMS         0x2400
#define PD_FUNC_GET_FLOW_STATS         0x2500
#define PD_FUNC_SET_NIC_PREFERENCE     0x2600
#define PD_FUNC_GET_HANDLE_INFO        0x2700
#define PD_FUNC_SET_BANDWIDTH_LIMIT    0x2800
#define PD_FUNC_GET_ERROR_INFO         0x2900

/* Packet Driver classes */
#define PD_CLASS_ETHERNET   1
#define PD_CLASS_TOKEN_RING 2
#define PD_CLASS_ARCNET     3

/* Packet Driver types */
#define PD_TYPE_3COM        1
#define PD_TYPE_GENERIC     0

/* Error codes */
#define API_SUCCESS                  0
#define API_ERR_INVALID_PARAM       -1
#define API_ERR_NOT_INITIALIZED     -2
#define API_ERR_BAD_FUNCTION        -3
#define API_ERR_BAD_HANDLE          -4
#define API_ERR_NO_HANDLES          -5
#define API_ERR_NO_INTERFACE        -6
#define API_ERR_FUNCTION_NOT_SUPPORTED -7
#define API_ERR_NO_HANDLERS         -8
#define API_ERR_ROUTING_FAILED      -9
#define API_ERR_NIC_UNAVAILABLE     -10
#define API_ERR_BANDWIDTH_EXCEEDED  -11
#define API_ERR_PRIORITY_CONFLICT   -12
#define API_ERR_QOS_NOT_SUPPORTED   -13
#define API_ERR_LOAD_BALANCE_FAILED -14
#define API_ERR_TOPOLOGY_CHANGED    -15

/* Packet Driver structures */
typedef struct {
    uint16_t version;           /* Driver version */
    uint8_t class;              /* Driver class */
    uint8_t type;               /* Driver type */
    uint8_t number;             /* Interface number */
    uint8_t basic;              /* Basic functions supported */
    uint8_t extended;           /* Extended functions supported */
    uint8_t high_performance;   /* High performance mode */
    char name[16];              /* Driver name */
} pd_driver_info_t;

typedef struct {
    uint8_t class;              /* Packet class */
    uint16_t type;              /* Packet type */
    uint8_t number;             /* Interface number */
    uint8_t basic;              /* Basic/Extended mode */
    void far *receiver;         /* Receiver function */
} pd_access_params_t;

typedef struct {
    void far *buffer;           /* Packet buffer */
    uint16_t length;            /* Packet length */
} pd_send_params_t;

typedef struct {
    uint8_t address[16];        /* Hardware address */
    uint8_t length;             /* Address length */
} pd_address_params_t;

typedef struct {
    uint32_t packets_in;        /* Packets received */
    uint32_t packets_out;       /* Packets sent */
    uint32_t bytes_in;          /* Bytes received */
    uint32_t bytes_out;         /* Bytes sent */
    uint32_t errors_in;         /* Receive errors */
    uint32_t errors_out;        /* Transmit errors */
    uint32_t packets_lost;      /* Packets dropped */
} pd_statistics_t;

/* Phase 3 Extended Structures */

/* Extended packet handle structure for Phase 3 features */
typedef struct {
    /* Basic handle from Phase 2 */
    uint16_t handle_id;         /* Handle identifier */
    uint16_t packet_type;       /* Packet type filter */
    uint8_t interface_num;      /* Interface number */
    void far *receiver_func;    /* Receiver function pointer */
    
    /* Phase 3 extensions */
    uint8_t priority;           /* Application priority (0-255) */
    uint8_t preferred_nic;      /* Preferred NIC for this handle */
    uint32_t bandwidth_limit;   /* Bandwidth limit in bytes/sec */
    uint16_t flags;             /* Extended capability flags */
    uint32_t routing_preferences; /* Routing preference mask */
    
    /* Statistics extensions */
    uint32_t packets_routed;    /* Packets routed by this handle */
    uint32_t routing_failures;  /* Routing failure count */
    uint32_t qos_drops;         /* QoS-related drops */
    uint32_t bandwidth_drops;   /* Bandwidth limit drops */
    uint32_t nic_switches;      /* NIC switching events */
    
    /* QoS and timing */
    uint32_t last_packet_time;  /* Last packet timestamp */
    uint32_t bytes_this_second; /* Bytes sent this second */
    uint32_t time_window_start; /* Bandwidth window start */
} extended_packet_handle_t;

/* Handle capability flags */
#define HANDLE_FLAG_PRIORITY_ENABLED    0x0001
#define HANDLE_FLAG_QOS_ENABLED         0x0002
#define HANDLE_FLAG_LOAD_BALANCE        0x0004
#define HANDLE_FLAG_BANDWIDTH_LIMIT     0x0008
#define HANDLE_FLAG_NIC_PREFERENCE      0x0010
#define HANDLE_FLAG_ROUTING_AWARE       0x0020
#define HANDLE_FLAG_ERROR_RECOVERY      0x0040
#define HANDLE_FLAG_FLOW_CONTROL        0x0080

/* Load balancing configuration */
typedef struct {
    uint8_t mode;               /* Load balancing mode */
    uint8_t primary_nic;        /* Primary NIC */
    uint8_t secondary_nic;      /* Secondary NIC */
    uint16_t switch_threshold;  /* Switch threshold (ms) */
    uint32_t weight_primary;    /* Primary NIC weight */
    uint32_t weight_secondary;  /* Secondary NIC weight */
} pd_load_balance_params_t;

/* Load balancing modes */
#define LB_MODE_ROUND_ROBIN     0
#define LB_MODE_WEIGHTED        1
#define LB_MODE_PERFORMANCE     2
#define LB_MODE_APPLICATION     3
#define LB_MODE_FLOW_AWARE      4

/* NIC status information */
typedef struct {
    uint8_t nic_index;          /* NIC index */
    uint8_t status;             /* NIC status */
    uint16_t link_speed;        /* Link speed (Mbps) */
    uint32_t utilization;       /* Utilization percentage */
    uint32_t error_count;       /* Error count */
    uint32_t last_error_time;   /* Last error timestamp */
    char status_text[32];       /* Status description */
} pd_nic_status_t;

/* NIC status values */
#define NIC_STATUS_DOWN         0
#define NIC_STATUS_UP           1
#define NIC_STATUS_ERROR        2
#define NIC_STATUS_DEGRADED     3
#define NIC_STATUS_TESTING      4

/* QoS parameters */
typedef struct {
    uint8_t priority_class;     /* Priority class (0-7) */
    uint32_t min_bandwidth;     /* Minimum bandwidth guarantee */
    uint32_t max_bandwidth;     /* Maximum bandwidth limit */
    uint16_t max_latency;       /* Maximum latency (ms) */
    uint8_t drop_policy;        /* Drop policy */
    uint8_t reserved[3];        /* Reserved for future use */
} pd_qos_params_t;

/* QoS priority classes */
#define QOS_CLASS_BACKGROUND    0
#define QOS_CLASS_STANDARD      1
#define QOS_CLASS_EXCELLENT     2
#define QOS_CLASS_AUDIO_VIDEO   3
#define QOS_CLASS_VOICE         4
#define QOS_CLASS_INTERACTIVE   5
#define QOS_CLASS_CONTROL       6
#define QOS_CLASS_NETWORK       7

/* Flow statistics */
typedef struct {
    uint16_t handle;            /* Handle ID */
    uint32_t flow_id;           /* Flow identifier */
    uint32_t packets_sent;      /* Packets sent in flow */
    uint32_t bytes_sent;        /* Bytes sent in flow */
    uint32_t avg_latency;       /* Average latency (us) */
    uint32_t jitter;            /* Jitter measurement (us) */
    uint8_t active_nic;         /* Currently active NIC */
    uint8_t flow_state;         /* Flow state */
} pd_flow_stats_t;

/* Flow states */
#define FLOW_STATE_INACTIVE     0
#define FLOW_STATE_ACTIVE       1
#define FLOW_STATE_SUSPENDED    2
#define FLOW_STATE_ERROR        3

/* Routing information */
typedef struct {
    uint16_t route_count;       /* Number of routes */
    uint16_t arp_entries;       /* ARP table entries */
    uint32_t packets_routed;    /* Total routed packets */
    uint32_t routing_errors;    /* Routing errors */
    uint8_t default_nic;        /* Default output NIC */
    uint8_t routing_mode;       /* Routing mode */
    uint16_t reserved;          /* Reserved */
} pd_routing_info_t;

/* Enhanced error information */
typedef struct {
    uint16_t error_code;        /* Last error code */
    uint32_t error_time;        /* Error timestamp */
    uint8_t affected_nic;       /* Affected NIC */
    uint8_t error_severity;     /* Error severity */
    uint16_t recovery_action;   /* Recommended recovery */
    char error_description[64]; /* Error description */
} pd_error_info_t;

/* Error severity levels */
#define ERROR_SEVERITY_INFO     0
#define ERROR_SEVERITY_WARNING  1
#define ERROR_SEVERITY_ERROR    2
#define ERROR_SEVERITY_CRITICAL 3

/* Function prototypes */
int api_init(const config_t *config);
int api_install_hooks(const config_t *config);  /* Phase 10: Install hooks only */
int api_activate(const config_t *config);        /* Phase 13: Activate API */
int api_cleanup(void);
int pd_access_type(uint8_t function, uint16_t handle, void *params);
int pd_get_driver_info(void *info_ptr);
int pd_handle_access_type(void *params);
int pd_release_handle(uint16_t handle);
int pd_send_packet(uint16_t handle, void *params);
int pd_terminate(uint16_t handle);
int pd_get_address(uint16_t handle, void *params);
int pd_reset_interface(uint16_t handle);
int pd_get_parameters(uint16_t handle, void *params);
int pd_set_rcv_mode(uint16_t handle, void *params);
int pd_get_rcv_mode(uint16_t handle, void *params);
int pd_get_statistics(uint16_t handle, void *params);
int pd_set_address(uint16_t handle, void *params);
int pd_validate_handle(uint16_t handle);
int api_process_received_packet(const uint8_t *packet, size_t length, int nic_id);

/* Phase 3 Group 3B Extended API Functions */
int pd_set_handle_priority(uint16_t handle, void *params);
int pd_get_routing_info(uint16_t handle, void *params);
int pd_set_load_balance(uint16_t handle, void *params);
int pd_get_nic_status(uint16_t handle, void *params);
int pd_set_qos_params(uint16_t handle, void *params);
int pd_get_flow_stats(uint16_t handle, void *params);
int pd_set_nic_preference(uint16_t handle, void *params);
int pd_get_handle_info(uint16_t handle, void *params);
int pd_set_bandwidth_limit(uint16_t handle, void *params);
int pd_get_error_info(uint16_t handle, void *params);

/* Advanced multiplexing functions */
int api_init_extended_handles(void);
int api_cleanup_extended_handles(void);
int api_upgrade_handle(uint16_t handle);
int api_get_extended_handle(uint16_t handle, extended_packet_handle_t **ext_handle);
int api_set_handle_routing_preference(uint16_t handle, uint32_t preferences);
int api_check_bandwidth_limit(uint16_t handle, uint32_t packet_size);
int api_update_flow_stats(uint16_t handle, uint32_t packet_size, uint32_t latency);
int api_select_optimal_nic(uint16_t handle, const uint8_t *packet, uint8_t *selected_nic);
int api_handle_nic_failure(uint8_t failed_nic);
int api_recover_from_error(uint16_t handle, int error_code);

/* Load balancing and QoS functions */
int api_init_load_balancer(void);
int api_update_nic_weights(void);
int api_get_nic_load(uint8_t nic_index, uint32_t *load_percent);
int api_enforce_qos_policy(uint16_t handle, uint32_t packet_size);
int api_schedule_packet_transmission(uint16_t handle, const uint8_t *packet, uint16_t length);

/* Integration with Group 3A routing */
int api_integrate_with_routing(void);
int api_use_arp_for_resolution(const uint8_t *dest_mac, uint8_t *optimal_nic);
int api_use_static_routing(uint16_t packet_type, uint8_t *route_nic);
int api_coordinate_with_flow_tracking(uint16_t handle, const uint8_t *packet);

/* Virtual interrupt multiplexing */
int api_init_virtual_interrupts(void);
int api_register_virtual_handler(uint16_t handle, void far *handler);
int api_deliver_to_virtual_handler(uint16_t handle, const uint8_t *packet, uint16_t length);
int api_multiplex_packet_delivery(const uint8_t *packet, uint16_t length, uint8_t src_nic);

/* Enhanced error handling and recovery */
int api_register_error_handler(uint16_t handle, void far *error_handler);
int api_notify_topology_change(uint8_t event_type, uint8_t affected_nic);
int api_initiate_graceful_degradation(uint8_t failed_nic);
int api_coordinate_recovery_with_routing(uint8_t failed_nic);

#ifdef __cplusplus
}
#endif

#endif /* _API_H_ */
