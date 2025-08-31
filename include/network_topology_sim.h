/**
 * @file network_topology_sim.h
 * @brief Network Topology Simulation Helpers
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This header provides comprehensive network topology simulation helpers
 * for testing ARP and routing functionality in realistic multi-NIC
 * network scenarios with topology changes, failover, and convergence.
 */

#ifndef _NETWORK_TOPOLOGY_SIM_H_
#define _NETWORK_TOPOLOGY_SIM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "hardware_mock.h"
#include <stdint.h>
#include <stdbool.h>

/* Topology simulation constants */
#define MAX_TOPOLOGY_NODES          32      /* Maximum nodes in topology */
#define MAX_TOPOLOGY_LINKS          64      /* Maximum links in topology */
#define MAX_NICS_PER_NODE           8       /* Maximum NICs per node */
#define MAX_TOPOLOGY_EVENTS         256     /* Maximum logged events */
#define INVALID_NODE_ID             0xFF    /* Invalid node identifier */
#define INVALID_LINK_ID             0xFF    /* Invalid link identifier */
#define INVALID_DEVICE_ID           0xFF    /* Invalid device identifier */

/* Node types */
typedef enum {
    NODE_TYPE_HOST = 0,                     /* End host */
    NODE_TYPE_SWITCH,                       /* Layer 2 switch */
    NODE_TYPE_ROUTER,                       /* Layer 3 router */
    NODE_TYPE_BRIDGE,                       /* Bridge device */
    NODE_TYPE_HUB                           /* Simple hub (deprecated) */
} network_node_type_t;

/* Link types */
typedef enum {
    LINK_TYPE_ETHERNET = 0,                 /* Standard Ethernet */
    LINK_TYPE_FAST_ETHERNET,                /* Fast Ethernet (100 Mbps) */
    LINK_TYPE_GIGABIT,                      /* Gigabit Ethernet */
    LINK_TYPE_SERIAL,                       /* Serial link */
    LINK_TYPE_WIRELESS                      /* Wireless link */
} network_link_type_t;

/* Duplex modes */
typedef enum {
    DUPLEX_HALF = 0,                        /* Half duplex */
    DUPLEX_FULL                             /* Full duplex */
} duplex_mode_t;

/* Spanning Tree Protocol states */
typedef enum {
    STP_STATE_DISABLED = 0,                 /* STP disabled */
    STP_STATE_BLOCKING,                     /* Blocking state */
    STP_STATE_LISTENING,                    /* Listening state */
    STP_STATE_LEARNING,                     /* Learning state */
    STP_STATE_FORWARDING                    /* Forwarding state */
} stp_state_t;

/* Topology event types */
typedef enum {
    TOPO_EVENT_NODE_ADDED = 0,              /* Node added to topology */
    TOPO_EVENT_NODE_REMOVED,                /* Node removed from topology */
    TOPO_EVENT_LINK_CREATED,                /* Link created */
    TOPO_EVENT_LINK_DISCONNECTED,           /* Link disconnected */
    TOPO_EVENT_LINK_UP,                     /* Link came up */
    TOPO_EVENT_LINK_DOWN,                   /* Link went down */
    TOPO_EVENT_CONVERGENCE_START,           /* Convergence started */
    TOPO_EVENT_CONVERGENCE_END              /* Convergence completed */
} topology_event_type_t;

/* Network Interface Card simulation */
typedef struct {
    uint8_t nic_id;                         /* NIC identifier within node */
    uint8_t mac_address[ETH_ALEN];          /* MAC address */
    bool enabled;                           /* NIC enabled */
    bool link_up;                           /* Link status */
    uint16_t speed_mbps;                    /* Link speed in Mbps */
    duplex_mode_t duplex;                   /* Duplex mode */
    uint8_t connected_link_id;              /* Connected link ID */
    uint32_t packets_sent;                  /* Packets sent statistics */
    uint32_t packets_received;              /* Packets received statistics */
    uint32_t bytes_sent;                    /* Bytes sent statistics */
    uint32_t bytes_received;                /* Bytes received statistics */
} network_nic_t;

/* Network node simulation */
typedef struct {
    uint8_t node_id;                        /* Unique node identifier */
    network_node_type_t type;               /* Node type */
    bool active;                            /* Node is active */
    bool can_forward;                       /* Can forward packets */
    bool can_learn;                         /* Can learn MAC addresses */
    bool is_router;                         /* Is a router (Layer 3) */
    
    /* Physical characteristics */
    uint8_t nic_count;                      /* Number of NICs */
    network_nic_t nics[MAX_NICS_PER_NODE];  /* NIC array */
    uint8_t mock_device_id;                 /* Associated mock device */
    
    /* Spanning Tree Protocol state */
    stp_state_t stp_state;                  /* STP state */
    uint8_t stp_root_id;                    /* STP root bridge ID */
    uint16_t stp_root_cost;                 /* Cost to root bridge */
    
    /* Failure simulation */
    bool failed_temporarily;                /* Temporarily failed */
    uint32_t failure_start_time;            /* Failure start time */
    uint32_t failure_duration_ms;           /* Failure duration */
    
    /* Statistics */
    uint32_t packets_forwarded;             /* Packets forwarded */
    uint32_t packets_dropped;               /* Packets dropped */
    uint32_t packets_received;              /* Total packets received */
    uint64_t bytes_received;                /* Total bytes received */
} network_node_t;

/* Network link simulation */
typedef struct {
    uint8_t link_id;                        /* Unique link identifier */
    network_link_type_t type;               /* Link type */
    bool active;                            /* Link is active */
    
    /* Endpoints */
    uint8_t node1_id;                       /* First endpoint node */
    uint8_t nic1_id;                        /* First endpoint NIC */
    uint8_t node2_id;                       /* Second endpoint node */
    uint8_t nic2_id;                        /* Second endpoint NIC */
    
    /* Link characteristics */
    uint16_t bandwidth_mbps;                /* Bandwidth in Mbps */
    uint16_t latency_ms;                    /* Propagation delay in ms */
    uint32_t loss_rate_ppm;                 /* Packet loss rate (parts per million) */
    duplex_mode_t duplex;                   /* Duplex mode */
    
    /* Failure simulation */
    bool failed_temporarily;                /* Temporarily failed */
    uint32_t failure_start_time;            /* Failure start time */
    uint32_t failure_duration_ms;           /* Failure duration */
    
    /* Statistics and state */
    uint32_t packets_sent;                  /* Packets transmitted */
    uint32_t packets_lost;                  /* Packets lost */
    uint64_t bytes_sent;                    /* Bytes transmitted */
    uint16_t utilization_percent;           /* Link utilization */
    uint32_t pending_packets;               /* Packets in transit */
    uint32_t total_propagation_delay;       /* Total propagation delay */
} network_link_t;

/* Topology event log entry */
typedef struct {
    topology_event_type_t event_type;       /* Type of event */
    uint32_t timestamp;                     /* Event timestamp */
    uint8_t node_id;                        /* Related node ID */
    uint8_t link_id;                        /* Related link ID */
    bool old_state;                         /* Previous state */
    bool new_state;                         /* New state */
    char description[64];                   /* Event description */
} network_topology_event_t;

/* Complete network topology */
typedef struct {
    /* Topology elements */
    network_node_t nodes[MAX_TOPOLOGY_NODES];
    network_link_t links[MAX_TOPOLOGY_LINKS];
    uint8_t node_count;                     /* Number of active nodes */
    uint8_t link_count;                     /* Number of active links */
    uint8_t max_nodes;                      /* Maximum nodes allowed */
    uint8_t max_links;                      /* Maximum links allowed */
    
    /* Topology state */
    uint32_t topology_version;              /* Topology version number */
    uint32_t convergence_time;              /* Last convergence time (ms) */
    
    /* Event log */
    network_topology_event_t events[MAX_TOPOLOGY_EVENTS];
    uint16_t event_count;                   /* Number of logged events */
} network_topology_t;

/* Topology statistics */
typedef struct {
    uint8_t active_nodes;                   /* Active node count */
    uint8_t failed_nodes;                   /* Failed node count */
    uint8_t active_links;                   /* Active link count */
    uint8_t failed_links;                   /* Failed link count */
    uint32_t total_bandwidth_mbps;          /* Total available bandwidth */
    uint64_t total_packets_sent;            /* Total packets sent */
    uint64_t total_packets_lost;            /* Total packets lost */
    uint64_t total_bytes_sent;              /* Total bytes sent */
    uint32_t topology_version;              /* Current topology version */
    uint32_t convergence_time_ms;           /* Last convergence time */
    uint16_t total_events;                  /* Total events logged */
} network_topology_stats_t;

/* Path statistics */
typedef struct {
    uint8_t hop_count;                      /* Number of hops in path */
    uint16_t total_latency_ms;              /* Total path latency */
    uint16_t min_bandwidth_mbps;            /* Minimum bandwidth along path */
    uint32_t total_loss_rate_ppm;           /* Combined loss rate */
} network_path_stats_t;

/* Function prototypes */

/* ========== Topology Management ========== */

/**
 * @brief Initialize network topology simulation
 * @param max_nodes Maximum number of nodes
 * @param max_links Maximum number of links
 * @return SUCCESS on success, error code on failure
 */
int network_topology_init(uint8_t max_nodes, uint8_t max_links);

/**
 * @brief Cleanup network topology simulation
 */
void network_topology_cleanup(void);

/* ========== Node Management ========== */

/**
 * @brief Add a node to the network topology
 * @param type Node type
 * @param nic_count Number of NICs on the node
 * @param mac_base Base MAC address (NULL for auto-generation)
 * @return Node ID on success, negative error code on failure
 */
int network_add_node(network_node_type_t type, uint8_t nic_count, const uint8_t *mac_base);

/**
 * @brief Remove a node from the network topology
 * @param node_id Node identifier
 * @return SUCCESS on success, error code on failure
 */
int network_remove_node(uint8_t node_id);

/**
 * @brief Get node information
 * @param node_id Node identifier
 * @return Node pointer or NULL if not found
 */
network_node_t* network_get_node(uint8_t node_id);

/* ========== Link Management ========== */

/**
 * @brief Create a link between two nodes
 * @param node1_id First node ID
 * @param nic1_id First node NIC ID
 * @param node2_id Second node ID
 * @param nic2_id Second node NIC ID
 * @param type Link type
 * @return Link ID on success, negative error code on failure
 */
int network_create_link(uint8_t node1_id, uint8_t nic1_id, 
                       uint8_t node2_id, uint8_t nic2_id,
                       network_link_type_t type);

/**
 * @brief Disconnect a link
 * @param link_id Link identifier
 * @return SUCCESS on success, error code on failure
 */
int network_disconnect_link(uint8_t link_id);

/**
 * @brief Set link state (up/down)
 * @param link_id Link identifier
 * @param up Link state (true = up, false = down)
 * @return SUCCESS on success, error code on failure
 */
int network_set_link_state(uint8_t link_id, bool up);

/**
 * @brief Get link information
 * @param link_id Link identifier
 * @return Link pointer or NULL if not found
 */
network_link_t* network_get_link(uint8_t link_id);

/* ========== Packet Simulation ========== */

/**
 * @brief Simulate packet flow between nodes
 * @param src_node_id Source node ID
 * @param dest_node_id Destination node ID
 * @param packet Packet data
 * @param length Packet length
 * @return SUCCESS on success, error code on failure
 */
int network_simulate_packet_flow(uint8_t src_node_id, uint8_t dest_node_id,
                                 const uint8_t *packet, uint16_t length);

/**
 * @brief Flood packet to all connected nodes
 * @param src_node_id Source node ID
 * @param packet Packet data
 * @param length Packet length
 * @return SUCCESS on success, error code on failure
 */
int network_flood_packet(uint8_t src_node_id, const uint8_t *packet, uint16_t length);

/* ========== Path Finding ========== */

/**
 * @brief Find shortest path between nodes
 * @param src_node_id Source node ID
 * @param dest_node_id Destination node ID
 * @param path Array to store path
 * @param path_length Pointer to store path length
 * @return SUCCESS on success, error code on failure
 */
int network_find_path(uint8_t src_node_id, uint8_t dest_node_id, 
                     uint8_t *path, uint8_t *path_length);

/**
 * @brief Calculate spanning tree for the topology
 * @param root_node_id Root node for spanning tree
 * @return SUCCESS on success, error code on failure
 */
int network_calculate_spanning_tree(uint8_t root_node_id);

/* ========== Topology Change Simulation ========== */

/**
 * @brief Simulate link failure
 * @param link_id Link identifier
 * @param duration_ms Failure duration in milliseconds
 * @return SUCCESS on success, error code on failure
 */
int network_simulate_link_failure(uint8_t link_id, uint32_t duration_ms);

/**
 * @brief Simulate node failure
 * @param node_id Node identifier
 * @param duration_ms Failure duration in milliseconds
 * @return SUCCESS on success, error code on failure
 */
int network_simulate_node_failure(uint8_t node_id, uint32_t duration_ms);

/**
 * @brief Trigger network convergence
 * @return SUCCESS on success, error code on failure
 */
int network_trigger_convergence(void);

/**
 * @brief Process recovery of failed elements
 * @return 1 if topology changed, 0 if no changes, negative on error
 */
int network_process_recovery(void);

/* ========== Statistics and Monitoring ========== */

/**
 * @brief Get topology statistics
 * @param stats Pointer to statistics structure
 * @return SUCCESS on success, error code on failure
 */
int network_get_topology_stats(network_topology_stats_t *stats);

/**
 * @brief Get path statistics between two nodes
 * @param src_node_id Source node ID
 * @param dest_node_id Destination node ID
 * @param stats Pointer to path statistics structure
 * @return SUCCESS on success, error code on failure
 */
int network_get_path_stats(uint8_t src_node_id, uint8_t dest_node_id, 
                          network_path_stats_t *stats);

/* ========== Pre-defined Topologies ========== */

/**
 * @brief Create linear topology (chain of nodes)
 * @param node_count Number of nodes
 * @param node_types Array of node types
 * @return SUCCESS on success, error code on failure
 */
int network_create_linear_topology(uint8_t node_count, network_node_type_t *node_types);

/**
 * @brief Create star topology (hub with spokes)
 * @param spoke_count Number of spoke nodes
 * @param hub_type Hub node type
 * @param spoke_type Spoke node type
 * @return Hub node ID on success, negative error code on failure
 */
int network_create_star_topology(uint8_t spoke_count, network_node_type_t hub_type, 
                                network_node_type_t spoke_type);

/**
 * @brief Create ring topology
 * @param node_count Number of nodes in ring
 * @param node_type Type for all nodes
 * @return SUCCESS on success, error code on failure
 */
int network_create_ring_topology(uint8_t node_count, network_node_type_t node_type);

/**
 * @brief Create mesh topology
 * @param node_count Number of nodes
 * @param node_type Type for all nodes
 * @param full_mesh True for full mesh, false for partial mesh
 * @return SUCCESS on success, error code on failure
 */
int network_create_mesh_topology(uint8_t node_count, network_node_type_t node_type, 
                                bool full_mesh);

/* ========== Helper Macros ========== */

#define NETWORK_NODE_IS_VALID(node_id) ((node_id) != INVALID_NODE_ID)
#define NETWORK_LINK_IS_VALID(link_id) ((link_id) != INVALID_LINK_ID)
#define NETWORK_DEVICE_IS_VALID(device_id) ((device_id) != INVALID_DEVICE_ID)

/* Convert bandwidth to different units */
#define MBPS_TO_KBPS(mbps) ((mbps) * 1000)
#define MBPS_TO_BPS(mbps) ((mbps) * 1000000)
#define BPS_TO_MBPS(bps) ((bps) / 1000000)

/* Calculate link utilization */
#define LINK_UTILIZATION(bytes_sent, bandwidth_mbps, time_ms) \
    (((bytes_sent) * 8 * 100) / ((bandwidth_mbps) * 1000 * (time_ms)))

#ifdef __cplusplus
}
#endif

#endif /* _NETWORK_TOPOLOGY_SIM_H_ */