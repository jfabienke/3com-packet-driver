/**
 * @file helper_network_sim.h
 * @brief Network Topology Simulation Helpers
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This module provides comprehensive network topology simulation helpers
 * for testing ARP and routing functionality in realistic multi-NIC
 * network scenarios with topology changes, failover, and convergence.
 */

#ifndef HELPER_NETWORK_SIM_H
#define HELPER_NETWORK_SIM_H

#include "../../include/network_topology_sim.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Topology Initialization and Management ========== */

/**
 * Initialize network topology simulation
 * @param max_nodes Maximum number of nodes in topology
 * @param max_links Maximum number of links in topology
 * @return SUCCESS on success, error code on failure
 */
int network_topology_init(uint8_t max_nodes, uint8_t max_links);

/**
 * Cleanup network topology simulation
 */
void network_topology_cleanup(void);

/* ========== Node Management ========== */

/**
 * Add a node to the network topology
 * @param type Type of network node (host, switch, router)
 * @param nic_count Number of NICs for this node
 * @param mac_base Base MAC address for NICs (incremented for each NIC)
 * @return Node ID on success, negative error code on failure
 */
int network_add_node(network_node_type_t type, uint8_t nic_count, const uint8_t *mac_base);

/**
 * Remove a node from the network topology
 * @param node_id ID of node to remove
 * @return SUCCESS on success, error code on failure
 */
int network_remove_node(uint8_t node_id);

/**
 * Get node information
 * @param node_id ID of node to retrieve
 * @return Pointer to node structure, NULL if not found
 */
network_node_t* network_get_node(uint8_t node_id);

/* ========== Link Management ========== */

/**
 * Create a link between two nodes
 * @param node1_id First node ID
 * @param nic1_id NIC ID on first node
 * @param node2_id Second node ID
 * @param nic2_id NIC ID on second node
 * @param bandwidth Link bandwidth in Mbps
 * @param latency Link latency in microseconds
 * @param loss_rate Packet loss rate (0.0 - 1.0)
 * @return Link ID on success, negative error code on failure
 */
int network_create_link(uint8_t node1_id, uint8_t nic1_id, 
                       uint8_t node2_id, uint8_t nic2_id,
                       uint32_t bandwidth, uint32_t latency, float loss_rate);

/**
 * Disconnect a link
 * @param link_id ID of link to disconnect
 * @return SUCCESS on success, error code on failure
 */
int network_disconnect_link(uint8_t link_id);

/**
 * Set link state (up/down)
 * @param link_id ID of link to modify
 * @param up True for link up, false for link down
 * @return SUCCESS on success, error code on failure
 */
int network_set_link_state(uint8_t link_id, bool up);

/**
 * Get link information
 * @param link_id ID of link to retrieve
 * @return Pointer to link structure, NULL if not found
 */
network_link_t* network_get_link(uint8_t link_id);

/* ========== Packet Simulation ========== */

/**
 * Simulate packet flow between two nodes
 * @param src_node_id Source node ID
 * @param dest_node_id Destination node ID
 * @param packet Packet data
 * @param length Packet length
 * @return SUCCESS on success, error code on failure
 */
int network_simulate_packet_flow(uint8_t src_node_id, uint8_t dest_node_id,
                                const uint8_t *packet, uint16_t length);

/**
 * Flood packet to all nodes (broadcast/multicast simulation)
 * @param src_node_id Source node ID
 * @param packet Packet data
 * @param length Packet length
 * @return SUCCESS on success, error code on failure
 */
int network_flood_packet(uint8_t src_node_id, const uint8_t *packet, uint16_t length);

/* ========== Path Finding and Routing ========== */

/**
 * Find path between two nodes
 * @param src_node_id Source node ID
 * @param dest_node_id Destination node ID
 * @param path Array to store path (node IDs)
 * @param max_hops Maximum number of hops
 * @return Number of hops on success, negative error code on failure
 */
int network_find_path(uint8_t src_node_id, uint8_t dest_node_id, 
                     uint8_t *path, uint8_t max_hops);

/**
 * Calculate spanning tree from root node
 * @param root_node_id Root node for spanning tree
 * @return SUCCESS on success, error code on failure
 */
int network_calculate_spanning_tree(uint8_t root_node_id);

/* ========== Failure Simulation ========== */

/**
 * Simulate link failure for specified duration
 * @param link_id ID of link to fail
 * @param duration_ms Duration of failure in milliseconds
 * @return SUCCESS on success, error code on failure
 */
int network_simulate_link_failure(uint8_t link_id, uint32_t duration_ms);

/**
 * Simulate node failure for specified duration
 * @param node_id ID of node to fail
 * @param duration_ms Duration of failure in milliseconds
 * @return SUCCESS on success, error code on failure
 */
int network_simulate_node_failure(uint8_t node_id, uint32_t duration_ms);

/**
 * Trigger network convergence (update routing tables)
 * @return SUCCESS on success, error code on failure
 */
int network_trigger_convergence(void);

/**
 * Process recovery from failures
 * @return SUCCESS on success, error code on failure
 */
int network_process_recovery(void);

/* ========== Statistics and Monitoring ========== */

/**
 * Get topology statistics
 * @param stats Pointer to statistics structure to fill
 * @return SUCCESS on success, error code on failure
 */
int network_get_topology_stats(network_topology_stats_t *stats);

/**
 * Get path statistics between two nodes
 * @param src_node_id Source node ID
 * @param dest_node_id Destination node ID
 * @param stats Pointer to path statistics structure to fill
 * @return SUCCESS on success, error code on failure
 */
int network_get_path_stats(uint8_t src_node_id, uint8_t dest_node_id, 
                          network_path_stats_t *stats);

/* ========== Topology Creation Helpers ========== */

/**
 * Create a linear topology (node1 -- node2 -- node3 -- ...)
 * @param node_count Number of nodes in the linear topology
 * @param node_types Array of node types for each node
 * @return SUCCESS on success, error code on failure
 */
int network_create_linear_topology(uint8_t node_count, network_node_type_t *node_types);

/**
 * Create a star topology (hub in center, spokes around)
 * @param spoke_count Number of spoke nodes
 * @param hub_type Type of hub node (usually switch or router)
 * @param spoke_type Type of spoke nodes (usually hosts)
 * @return SUCCESS on success, error code on failure
 */
int network_create_star_topology(uint8_t spoke_count, network_node_type_t hub_type, 
                                network_node_type_t spoke_type);

/**
 * Create a ring topology (node1 -- node2 -- ... -- nodeN -- node1)
 * @param node_count Number of nodes in the ring
 * @param node_type Type of nodes in the ring
 * @return SUCCESS on success, error code on failure
 */
int network_create_ring_topology(uint8_t node_count, network_node_type_t node_type);

/**
 * Create a mesh topology (all nodes connected to all other nodes)
 * @param node_count Number of nodes in the mesh
 * @param node_type Type of nodes in the mesh
 * @param partial True for partial mesh, false for full mesh
 * @return SUCCESS on success, error code on failure
 */
int network_create_mesh_topology(uint8_t node_count, network_node_type_t node_type,
                                bool partial);

#ifdef __cplusplus
}
#endif

#endif /* HELPER_NETWORK_SIM_H */