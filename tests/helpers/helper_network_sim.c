/**
 * @file helper_network_sim.c
 * @brief Network Topology Simulation Helpers
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This module provides comprehensive network topology simulation helpers
 * for testing ARP and routing functionality in realistic multi-NIC
 * network scenarios with topology changes, failover, and convergence.
 */

#include "../../include/network_topology_sim.h"
#include "../../include/hardware_mock.h"
#include "../../include/arp.h"
#include "../../include/routing.h"
#include "../../include/static_routing.h"
#include "../../include/logging.h"
#include "../../include/memory.h"
#include <string.h>

/* Global topology simulation state */
static network_topology_t g_network_topology = {0};
static bool g_topology_initialized = false;
static uint32_t g_topology_event_counter = 0;

/* Forward declarations */
static int validate_node_id(uint8_t node_id);
static int validate_link_id(uint8_t link_id);
static network_node_t* find_node_by_id(uint8_t node_id);
static network_link_t* find_link_by_id(uint8_t link_id);
static void update_link_statistics(network_link_t *link, bool packet_sent, uint16_t packet_size);
static void simulate_propagation_delay(network_link_t *link);
static int inject_packet_to_node(uint8_t node_id, const uint8_t *packet, uint16_t length);

/* ========== Topology Initialization and Management ========== */

int network_topology_init(uint8_t max_nodes, uint8_t max_links) {
    if (g_topology_initialized) {
        return SUCCESS;
    }
    
    if (max_nodes > MAX_TOPOLOGY_NODES || max_links > MAX_TOPOLOGY_LINKS) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Initialize topology structure */
    memset(&g_network_topology, 0, sizeof(network_topology_t));
    g_network_topology.max_nodes = max_nodes;
    g_network_topology.max_links = max_links;
    g_network_topology.node_count = 0;
    g_network_topology.link_count = 0;
    g_network_topology.convergence_time = 0;
    g_network_topology.topology_version = 1;
    
    /* Initialize event log */
    g_network_topology.event_count = 0;
    
    g_topology_initialized = true;
    g_topology_event_counter = 0;
    
    log_info("Network topology simulation initialized: %d nodes, %d links", 
             max_nodes, max_links);
    
    return SUCCESS;
}

void network_topology_cleanup(void) {
    if (!g_topology_initialized) {
        return;
    }
    
    /* Clean up all nodes */
    for (int i = 0; i < g_network_topology.node_count; i++) {
        network_node_t *node = &g_network_topology.nodes[i];
        
        /* Destroy associated mock device */
        if (node->mock_device_id != INVALID_DEVICE_ID) {
            mock_device_destroy(node->mock_device_id);
        }
    }
    
    /* Clean up all links */
    for (int i = 0; i < g_network_topology.link_count; i++) {
        network_link_t *link = &g_network_topology.links[i];
        
        /* Clear any pending packets */
        link->pending_packets = 0;
    }
    
    memset(&g_network_topology, 0, sizeof(network_topology_t));
    g_topology_initialized = false;
    
    log_info("Network topology simulation cleaned up");
}

/* ========== Node Management ========== */

int network_add_node(network_node_type_t type, uint8_t nic_count, const uint8_t *mac_base) {
    if (!g_topology_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    if (g_network_topology.node_count >= g_network_topology.max_nodes) {
        return ERROR_NO_MEMORY;
    }
    
    if (nic_count == 0 || nic_count > MAX_NICS_PER_NODE) {
        return ERROR_INVALID_PARAM;
    }
    
    uint8_t node_id = g_network_topology.node_count;
    network_node_t *node = &g_network_topology.nodes[node_id];
    
    /* Initialize node */
    memset(node, 0, sizeof(network_node_t));
    node->node_id = node_id;
    node->type = type;
    node->nic_count = nic_count;
    node->active = true;
    node->stp_state = STP_STATE_FORWARDING;
    
    /* Set node capabilities based on type */
    switch (type) {
        case NODE_TYPE_HOST:
            node->can_forward = false;
            node->can_learn = true;
            node->is_router = false;
            break;
        case NODE_TYPE_SWITCH:
            node->can_forward = true;
            node->can_learn = true;
            node->is_router = false;
            break;
        case NODE_TYPE_ROUTER:
            node->can_forward = true;
            node->can_learn = true;
            node->is_router = true;
            break;
        case NODE_TYPE_BRIDGE:
            node->can_forward = true;
            node->can_learn = true;
            node->is_router = false;
            break;
        default:
            return ERROR_INVALID_PARAM;
    }
    
    /* Create mock device for this node */
    mock_device_type_t mock_type = (type == NODE_TYPE_HOST) ? MOCK_DEVICE_3C509B : MOCK_DEVICE_3C515;
    uint8_t mock_device_id = mock_device_create(mock_type, 0x300 + (node_id * 0x20), 5 + node_id);
    
    if (mock_device_id < 0) {
        return mock_device_id;
    }
    
    node->mock_device_id = mock_device_id;
    
    /* Configure NICs */
    for (int i = 0; i < nic_count; i++) {
        network_nic_t *nic = &node->nics[i];
        nic->nic_id = i;
        nic->enabled = true;
        nic->link_up = false;
        nic->speed_mbps = (mock_type == MOCK_DEVICE_3C509B) ? 10 : 100;
        nic->duplex = (mock_type == MOCK_DEVICE_3C515) ? DUPLEX_FULL : DUPLEX_HALF;
        nic->connected_link_id = INVALID_LINK_ID;
        
        /* Generate MAC address */
        if (mac_base) {
            memcpy(nic->mac_address, mac_base, ETH_ALEN);
            nic->mac_address[ETH_ALEN - 1] = (node_id << 4) | i;
        } else {
            nic->mac_address[0] = 0x00;
            nic->mac_address[1] = 0x10;
            nic->mac_address[2] = 0x4B;
            nic->mac_address[3] = 0xF0 + node_id;
            nic->mac_address[4] = 0x00;
            nic->mac_address[5] = i;
        }
    }
    
    /* Set primary MAC address for mock device */
    mock_device_set_mac_address(mock_device_id, node->nics[0].mac_address);
    mock_device_enable(mock_device_id, true);
    
    g_network_topology.node_count++;
    g_network_topology.topology_version++;
    
    /* Log topology event */
    network_topology_event_t *event = &g_network_topology.events[g_network_topology.event_count % MAX_TOPOLOGY_EVENTS];
    event->event_type = TOPO_EVENT_NODE_ADDED;
    event->timestamp = get_system_timestamp_ms();
    event->node_id = node_id;
    event->link_id = INVALID_LINK_ID;
    event->old_state = false;
    event->new_state = true;
    g_network_topology.event_count++;
    
    log_info("Added network node %d: type=%d, NICs=%d", node_id, type, nic_count);
    
    return node_id;
}

int network_remove_node(uint8_t node_id) {
    if (validate_node_id(node_id) != SUCCESS) {
        return ERROR_INVALID_PARAM;
    }
    
    network_node_t *node = find_node_by_id(node_id);
    if (!node) {
        return ERROR_NOT_FOUND;
    }
    
    /* Disconnect all links */
    for (int i = 0; i < node->nic_count; i++) {
        if (node->nics[i].connected_link_id != INVALID_LINK_ID) {
            network_disconnect_link(node->nics[i].connected_link_id);
        }
    }
    
    /* Destroy mock device */
    if (node->mock_device_id != INVALID_DEVICE_ID) {
        mock_device_destroy(node->mock_device_id);
    }
    
    /* Mark node as inactive rather than removing to preserve indices */
    node->active = false;
    
    /* Log topology event */
    network_topology_event_t *event = &g_network_topology.events[g_network_topology.event_count % MAX_TOPOLOGY_EVENTS];
    event->event_type = TOPO_EVENT_NODE_REMOVED;
    event->timestamp = get_system_timestamp_ms();
    event->node_id = node_id;
    event->link_id = INVALID_LINK_ID;
    event->old_state = true;
    event->new_state = false;
    g_network_topology.event_count++;
    
    g_network_topology.topology_version++;
    
    log_info("Removed network node %d", node_id);
    
    return SUCCESS;
}

network_node_t* network_get_node(uint8_t node_id) {
    if (validate_node_id(node_id) != SUCCESS) {
        return NULL;
    }
    
    return find_node_by_id(node_id);
}

/* ========== Link Management ========== */

int network_create_link(uint8_t node1_id, uint8_t nic1_id, 
                       uint8_t node2_id, uint8_t nic2_id,
                       network_link_type_t type) {
    if (!g_topology_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    if (g_network_topology.link_count >= g_network_topology.max_links) {
        return ERROR_NO_MEMORY;
    }
    
    if (validate_node_id(node1_id) != SUCCESS || validate_node_id(node2_id) != SUCCESS) {
        return ERROR_INVALID_PARAM;
    }
    
    network_node_t *node1 = find_node_by_id(node1_id);
    network_node_t *node2 = find_node_by_id(node2_id);
    
    if (!node1 || !node2 || !node1->active || !node2->active) {
        return ERROR_INVALID_PARAM;
    }
    
    if (nic1_id >= node1->nic_count || nic2_id >= node2->nic_count) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Check if NICs are already connected */
    if (node1->nics[nic1_id].connected_link_id != INVALID_LINK_ID ||
        node2->nics[nic2_id].connected_link_id != INVALID_LINK_ID) {
        return ERROR_BUSY;
    }
    
    uint8_t link_id = g_network_topology.link_count;
    network_link_t *link = &g_network_topology.links[link_id];
    
    /* Initialize link */
    memset(link, 0, sizeof(network_link_t));
    link->link_id = link_id;
    link->type = type;
    link->active = true;
    link->node1_id = node1_id;
    link->nic1_id = nic1_id;
    link->node2_id = node2_id;
    link->nic2_id = nic2_id;
    
    /* Set link characteristics based on type */
    switch (type) {
        case LINK_TYPE_ETHERNET:
            link->bandwidth_mbps = 100;
            link->latency_ms = 1;
            link->loss_rate_ppm = 0;
            link->duplex = DUPLEX_FULL;
            break;
        case LINK_TYPE_FAST_ETHERNET:
            link->bandwidth_mbps = 100;
            link->latency_ms = 1;
            link->loss_rate_ppm = 0;
            link->duplex = DUPLEX_FULL;
            break;
        case LINK_TYPE_GIGABIT:
            link->bandwidth_mbps = 1000;
            link->latency_ms = 1;
            link->loss_rate_ppm = 0;
            link->duplex = DUPLEX_FULL;
            break;
        case LINK_TYPE_SERIAL:
            link->bandwidth_mbps = 2;
            link->latency_ms = 10;
            link->loss_rate_ppm = 100;
            link->duplex = DUPLEX_FULL;
            break;
        case LINK_TYPE_WIRELESS:
            link->bandwidth_mbps = 54;
            link->latency_ms = 5;
            link->loss_rate_ppm = 1000;
            link->duplex = DUPLEX_HALF;
            break;
        default:
            return ERROR_INVALID_PARAM;
    }
    
    /* Connect NICs to link */
    node1->nics[nic1_id].connected_link_id = link_id;
    node2->nics[nic2_id].connected_link_id = link_id;
    
    /* Update NIC link status */
    node1->nics[nic1_id].link_up = true;
    node2->nics[nic2_id].link_up = true;
    
    /* Update mock device link status */
    mock_device_set_link_status(node1->mock_device_id, true, link->bandwidth_mbps);
    mock_device_set_link_status(node2->mock_device_id, true, link->bandwidth_mbps);
    
    g_network_topology.link_count++;
    g_network_topology.topology_version++;
    
    /* Log topology event */
    network_topology_event_t *event = &g_network_topology.events[g_network_topology.event_count % MAX_TOPOLOGY_EVENTS];
    event->event_type = TOPO_EVENT_LINK_CREATED;
    event->timestamp = get_system_timestamp_ms();
    event->node_id = node1_id;
    event->link_id = link_id;
    event->old_state = false;
    event->new_state = true;
    g_network_topology.event_count++;
    
    log_info("Created network link %d: Node %d(NIC %d) <-> Node %d(NIC %d)", 
             link_id, node1_id, nic1_id, node2_id, nic2_id);
    
    return link_id;
}

int network_disconnect_link(uint8_t link_id) {
    if (validate_link_id(link_id) != SUCCESS) {
        return ERROR_INVALID_PARAM;
    }
    
    network_link_t *link = find_link_by_id(link_id);
    if (!link || !link->active) {
        return ERROR_NOT_FOUND;
    }
    
    /* Update connected NICs */
    network_node_t *node1 = find_node_by_id(link->node1_id);
    network_node_t *node2 = find_node_by_id(link->node2_id);
    
    if (node1 && node1->active) {
        node1->nics[link->nic1_id].connected_link_id = INVALID_LINK_ID;
        node1->nics[link->nic1_id].link_up = false;
        mock_device_set_link_status(node1->mock_device_id, false, 0);
    }
    
    if (node2 && node2->active) {
        node2->nics[link->nic2_id].connected_link_id = INVALID_LINK_ID;
        node2->nics[link->nic2_id].link_up = false;
        mock_device_set_link_status(node2->mock_device_id, false, 0);
    }
    
    /* Deactivate link */
    link->active = false;
    
    /* Log topology event */
    network_topology_event_t *event = &g_network_topology.events[g_network_topology.event_count % MAX_TOPOLOGY_EVENTS];
    event->event_type = TOPO_EVENT_LINK_DISCONNECTED;
    event->timestamp = get_system_timestamp_ms();
    event->node_id = link->node1_id;
    event->link_id = link_id;
    event->old_state = true;
    event->new_state = false;
    g_network_topology.event_count++;
    
    g_network_topology.topology_version++;
    
    log_info("Disconnected network link %d", link_id);
    
    return SUCCESS;
}

int network_set_link_state(uint8_t link_id, bool up) {
    if (validate_link_id(link_id) != SUCCESS) {
        return ERROR_INVALID_PARAM;
    }
    
    network_link_t *link = find_link_by_id(link_id);
    if (!link) {
        return ERROR_NOT_FOUND;
    }
    
    bool old_state = link->active;
    link->active = up;
    
    /* Update connected NICs */
    network_node_t *node1 = find_node_by_id(link->node1_id);
    network_node_t *node2 = find_node_by_id(link->node2_id);
    
    if (node1 && node1->active) {
        node1->nics[link->nic1_id].link_up = up;
        mock_device_set_link_status(node1->mock_device_id, up, up ? link->bandwidth_mbps : 0);
    }
    
    if (node2 && node2->active) {
        node2->nics[link->nic2_id].link_up = up;
        mock_device_set_link_status(node2->mock_device_id, up, up ? link->bandwidth_mbps : 0);
    }
    
    /* Log topology event if state changed */
    if (old_state != up) {
        network_topology_event_t *event = &g_network_topology.events[g_network_topology.event_count % MAX_TOPOLOGY_EVENTS];
        event->event_type = up ? TOPO_EVENT_LINK_UP : TOPO_EVENT_LINK_DOWN;
        event->timestamp = get_system_timestamp_ms();
        event->node_id = link->node1_id;
        event->link_id = link_id;
        event->old_state = old_state;
        event->new_state = up;
        g_network_topology.event_count++;
        
        g_network_topology.topology_version++;
        
        log_info("Link %d state changed: %s", link_id, up ? "UP" : "DOWN");
    }
    
    return SUCCESS;
}

network_link_t* network_get_link(uint8_t link_id) {
    if (validate_link_id(link_id) != SUCCESS) {
        return NULL;
    }
    
    return find_link_by_id(link_id);
}

/* ========== Packet Simulation ========== */

int network_simulate_packet_flow(uint8_t src_node_id, uint8_t dest_node_id,
                                 const uint8_t *packet, uint16_t length) {
    if (!g_topology_initialized || !packet || length == 0) {
        return ERROR_INVALID_PARAM;
    }
    
    if (validate_node_id(src_node_id) != SUCCESS || validate_node_id(dest_node_id) != SUCCESS) {
        return ERROR_INVALID_PARAM;
    }
    
    network_node_t *src_node = find_node_by_id(src_node_id);
    network_node_t *dest_node = find_node_by_id(dest_node_id);
    
    if (!src_node || !dest_node || !src_node->active || !dest_node->active) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Extract destination MAC from packet */
    if (length < ETH_ALEN) {
        return ERROR_INVALID_PARAM;
    }
    
    const uint8_t *dest_mac = packet;
    
    /* Find path to destination */
    uint8_t path[MAX_TOPOLOGY_NODES];
    uint8_t path_length = 0;
    
    int result = network_find_path(src_node_id, dest_node_id, path, &path_length);
    if (result != SUCCESS) {
        /* Try to find path based on learned MAC addresses or flooding */
        return network_flood_packet(src_node_id, packet, length);
    }
    
    /* Simulate packet transmission along path */
    for (int i = 0; i < path_length - 1; i++) {
        uint8_t current_node = path[i];
        uint8_t next_node = path[i + 1];
        
        /* Find link between nodes */
        network_link_t *link = NULL;
        for (int j = 0; j < g_network_topology.link_count; j++) {
            network_link_t *candidate = &g_network_topology.links[j];
            if (candidate->active &&
                ((candidate->node1_id == current_node && candidate->node2_id == next_node) ||
                 (candidate->node1_id == next_node && candidate->node2_id == current_node))) {
                link = candidate;
                break;
            }
        }
        
        if (!link) {
            return ERROR_NOT_FOUND;
        }
        
        /* Simulate link transmission */
        simulate_propagation_delay(link);
        update_link_statistics(link, true, length);
        
        /* Check for packet loss */
        if (link->loss_rate_ppm > 0) {
            uint32_t random = g_topology_event_counter++ % 1000000;
            if (random < link->loss_rate_ppm) {
                update_link_statistics(link, false, length);
                return ERROR_IO; /* Packet lost */
            }
        }
    }
    
    /* Inject packet to destination node */
    return inject_packet_to_node(dest_node_id, packet, length);
}

int network_flood_packet(uint8_t src_node_id, const uint8_t *packet, uint16_t length) {
    if (!g_topology_initialized || !packet || length == 0) {
        return ERROR_INVALID_PARAM;
    }
    
    if (validate_node_id(src_node_id) != SUCCESS) {
        return ERROR_INVALID_PARAM;
    }
    
    network_node_t *src_node = find_node_by_id(src_node_id);
    if (!src_node || !src_node->active) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Flood packet to all connected nodes */
    int flooded_count = 0;
    
    for (int i = 0; i < src_node->nic_count; i++) {
        network_nic_t *nic = &src_node->nics[i];
        
        if (nic->connected_link_id == INVALID_LINK_ID || !nic->link_up) {
            continue;
        }
        
        network_link_t *link = find_link_by_id(nic->connected_link_id);
        if (!link || !link->active) {
            continue;
        }
        
        /* Find the other end of the link */
        uint8_t dest_node_id = (link->node1_id == src_node_id) ? link->node2_id : link->node1_id;
        network_node_t *dest_node = find_node_by_id(dest_node_id);
        
        if (!dest_node || !dest_node->active) {
            continue;
        }
        
        /* Only flood to nodes that can handle flooding */
        if (dest_node->type != NODE_TYPE_HOST) {
            simulate_propagation_delay(link);
            update_link_statistics(link, true, length);
            
            int result = inject_packet_to_node(dest_node_id, packet, length);
            if (result == SUCCESS) {
                flooded_count++;
            }
        }
    }
    
    return (flooded_count > 0) ? SUCCESS : ERROR_NOT_FOUND;
}

/* ========== Path Finding ========== */

int network_find_path(uint8_t src_node_id, uint8_t dest_node_id, 
                     uint8_t *path, uint8_t *path_length) {
    if (!path || !path_length || validate_node_id(src_node_id) != SUCCESS || 
        validate_node_id(dest_node_id) != SUCCESS) {
        return ERROR_INVALID_PARAM;
    }
    
    if (src_node_id == dest_node_id) {
        path[0] = src_node_id;
        *path_length = 1;
        return SUCCESS;
    }
    
    /* Simple breadth-first search for shortest path */
    bool visited[MAX_TOPOLOGY_NODES] = {false};
    uint8_t queue[MAX_TOPOLOGY_NODES];
    uint8_t parent[MAX_TOPOLOGY_NODES];
    uint8_t queue_start = 0, queue_end = 0;
    
    /* Initialize */
    for (int i = 0; i < MAX_TOPOLOGY_NODES; i++) {
        parent[i] = INVALID_NODE_ID;
    }
    
    /* Start BFS */
    queue[queue_end++] = src_node_id;
    visited[src_node_id] = true;
    
    while (queue_start < queue_end) {
        uint8_t current_node = queue[queue_start++];
        
        if (current_node == dest_node_id) {
            /* Found path - reconstruct it */
            uint8_t temp_path[MAX_TOPOLOGY_NODES];
            uint8_t temp_length = 0;
            
            uint8_t node = dest_node_id;
            while (node != INVALID_NODE_ID) {
                temp_path[temp_length++] = node;
                node = parent[node];
            }
            
            /* Reverse path */
            for (int i = 0; i < temp_length; i++) {
                path[i] = temp_path[temp_length - 1 - i];
            }
            *path_length = temp_length;
            
            return SUCCESS;
        }
        
        /* Explore neighbors */
        network_node_t *node = find_node_by_id(current_node);
        if (!node || !node->active) {
            continue;
        }
        
        for (int i = 0; i < node->nic_count; i++) {
            network_nic_t *nic = &node->nics[i];
            
            if (nic->connected_link_id == INVALID_LINK_ID || !nic->link_up) {
                continue;
            }
            
            network_link_t *link = find_link_by_id(nic->connected_link_id);
            if (!link || !link->active) {
                continue;
            }
            
            uint8_t neighbor_id = (link->node1_id == current_node) ? link->node2_id : link->node1_id;
            
            if (!visited[neighbor_id] && neighbor_id < g_network_topology.node_count) {
                visited[neighbor_id] = true;
                parent[neighbor_id] = current_node;
                queue[queue_end++] = neighbor_id;
            }
        }
    }
    
    return ERROR_NOT_FOUND; /* No path found */
}

int network_calculate_spanning_tree(uint8_t root_node_id) {
    if (!g_topology_initialized || validate_node_id(root_node_id) != SUCCESS) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Reset all nodes to default STP state */
    for (int i = 0; i < g_network_topology.node_count; i++) {
        network_node_t *node = &g_network_topology.nodes[i];
        if (node->active) {
            node->stp_state = STP_STATE_BLOCKING;
            node->stp_root_id = root_node_id;
            node->stp_root_cost = (i == root_node_id) ? 0 : UINT16_MAX;
        }
    }
    
    /* Set root node */
    network_node_t *root = find_node_by_id(root_node_id);
    if (!root || !root->active) {
        return ERROR_INVALID_PARAM;
    }
    
    root->stp_state = STP_STATE_FORWARDING;
    root->stp_root_cost = 0;
    
    /* Simple spanning tree calculation using Dijkstra-like approach */
    bool changed = true;
    int iterations = 0;
    
    while (changed && iterations < MAX_TOPOLOGY_NODES) {
        changed = false;
        iterations++;
        
        for (int i = 0; i < g_network_topology.link_count; i++) {
            network_link_t *link = &g_network_topology.links[i];
            if (!link->active) {
                continue;
            }
            
            network_node_t *node1 = find_node_by_id(link->node1_id);
            network_node_t *node2 = find_node_by_id(link->node2_id);
            
            if (!node1 || !node2 || !node1->active || !node2->active) {
                continue;
            }
            
            uint16_t link_cost = 10; /* Simple cost */
            
            /* Update node2 from node1 */
            if (node1->stp_root_cost != UINT16_MAX) {
                uint16_t new_cost = node1->stp_root_cost + link_cost;
                if (new_cost < node2->stp_root_cost) {
                    node2->stp_root_cost = new_cost;
                    node2->stp_state = STP_STATE_FORWARDING;
                    changed = true;
                }
            }
            
            /* Update node1 from node2 */
            if (node2->stp_root_cost != UINT16_MAX) {
                uint16_t new_cost = node2->stp_root_cost + link_cost;
                if (new_cost < node1->stp_root_cost) {
                    node1->stp_root_cost = new_cost;
                    node1->stp_state = STP_STATE_FORWARDING;
                    changed = true;
                }
            }
        }
    }
    
    log_info("Spanning tree calculated with root node %d", root_node_id);
    
    return SUCCESS;
}

/* ========== Topology Change Simulation ========== */

int network_simulate_link_failure(uint8_t link_id, uint32_t duration_ms) {
    if (validate_link_id(link_id) != SUCCESS) {
        return ERROR_INVALID_PARAM;
    }
    
    network_link_t *link = find_link_by_id(link_id);
    if (!link) {
        return ERROR_NOT_FOUND;
    }
    
    /* Store original state */
    bool original_state = link->active;
    
    /* Simulate failure */
    int result = network_set_link_state(link_id, false);
    if (result != SUCCESS) {
        return result;
    }
    
    /* Store failure information for potential recovery */
    link->failure_start_time = get_system_timestamp_ms();
    link->failure_duration_ms = duration_ms;
    link->failed_temporarily = true;
    
    log_info("Simulated failure of link %d for %d ms", link_id, duration_ms);
    
    return SUCCESS;
}

int network_simulate_node_failure(uint8_t node_id, uint32_t duration_ms) {
    if (validate_node_id(node_id) != SUCCESS) {
        return ERROR_INVALID_PARAM;
    }
    
    network_node_t *node = find_node_by_id(node_id);
    if (!node) {
        return ERROR_NOT_FOUND;
    }
    
    /* Store original state */
    bool original_state = node->active;
    
    /* Fail all connected links */
    for (int i = 0; i < node->nic_count; i++) {
        if (node->nics[i].connected_link_id != INVALID_LINK_ID) {
            network_set_link_state(node->nics[i].connected_link_id, false);
        }
    }
    
    /* Deactivate node */
    node->active = false;
    node->failure_start_time = get_system_timestamp_ms();
    node->failure_duration_ms = duration_ms;
    node->failed_temporarily = true;
    
    /* Update mock device */
    if (node->mock_device_id != INVALID_DEVICE_ID) {
        mock_device_enable(node->mock_device_id, false);
    }
    
    log_info("Simulated failure of node %d for %d ms", node_id, duration_ms);
    
    return SUCCESS;
}

int network_trigger_convergence(void) {
    if (!g_topology_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    uint32_t start_time = get_system_timestamp_ms();
    
    /* Clear all routing/bridging tables to force reconvergence */
    routing_clear_table();
    bridge_flush_table();
    arp_cache_flush();
    
    /* Recalculate spanning tree if there are switches/bridges */
    uint8_t root_node = INVALID_NODE_ID;
    for (int i = 0; i < g_network_topology.node_count; i++) {
        network_node_t *node = &g_network_topology.nodes[i];
        if (node->active && (node->type == NODE_TYPE_SWITCH || node->type == NODE_TYPE_BRIDGE)) {
            root_node = i;
            break;
        }
    }
    
    if (root_node != INVALID_NODE_ID) {
        network_calculate_spanning_tree(root_node);
    }
    
    /* Send gratuitous ARPs from all hosts */
    for (int i = 0; i < g_network_topology.node_count; i++) {
        network_node_t *node = &g_network_topology.nodes[i];
        if (node->active && node->type == NODE_TYPE_HOST) {
            /* Simulate gratuitous ARP for each NIC */
            for (int j = 0; j < node->nic_count; j++) {
                if (node->nics[j].link_up) {
                    /* Create simulated IP for this node */
                    ip_addr_t node_ip = {192, 168, (uint8_t)(i + 1), 1};
                    arp_send_gratuitous(&node_ip, j);
                }
            }
        }
    }
    
    uint32_t end_time = get_system_timestamp_ms();
    g_network_topology.convergence_time = end_time - start_time;
    g_network_topology.topology_version++;
    
    log_info("Network convergence triggered, completed in %d ms", 
             g_network_topology.convergence_time);
    
    return SUCCESS;
}

/* ========== Recovery Simulation ========== */

int network_process_recovery(void) {
    if (!g_topology_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    uint32_t current_time = get_system_timestamp_ms();
    bool topology_changed = false;
    
    /* Check for link recoveries */
    for (int i = 0; i < g_network_topology.link_count; i++) {
        network_link_t *link = &g_network_topology.links[i];
        
        if (link->failed_temporarily && !link->active) {
            uint32_t elapsed = current_time - link->failure_start_time;
            
            if (elapsed >= link->failure_duration_ms) {
                /* Recover link */
                network_set_link_state(i, true);
                link->failed_temporarily = false;
                topology_changed = true;
                
                log_info("Link %d recovered after %d ms", i, elapsed);
            }
        }
    }
    
    /* Check for node recoveries */
    for (int i = 0; i < g_network_topology.node_count; i++) {
        network_node_t *node = &g_network_topology.nodes[i];
        
        if (node->failed_temporarily && !node->active) {
            uint32_t elapsed = current_time - node->failure_start_time;
            
            if (elapsed >= node->failure_duration_ms) {
                /* Recover node */
                node->active = true;
                node->failed_temporarily = false;
                
                /* Restore connected links */
                for (int j = 0; j < node->nic_count; j++) {
                    if (node->nics[j].connected_link_id != INVALID_LINK_ID) {
                        network_set_link_state(node->nics[j].connected_link_id, true);
                    }
                }
                
                /* Update mock device */
                if (node->mock_device_id != INVALID_DEVICE_ID) {
                    mock_device_enable(node->mock_device_id, true);
                }
                
                topology_changed = true;
                
                log_info("Node %d recovered after %d ms", i, elapsed);
            }
        }
    }
    
    /* Trigger convergence if topology changed */
    if (topology_changed) {
        network_trigger_convergence();
    }
    
    return topology_changed ? 1 : 0;
}

/* ========== Statistics and Monitoring ========== */

int network_get_topology_stats(network_topology_stats_t *stats) {
    if (!stats || !g_topology_initialized) {
        return ERROR_INVALID_PARAM;
    }
    
    memset(stats, 0, sizeof(network_topology_stats_t));
    
    /* Count active elements */
    for (int i = 0; i < g_network_topology.node_count; i++) {
        if (g_network_topology.nodes[i].active) {
            stats->active_nodes++;
        } else {
            stats->failed_nodes++;
        }
    }
    
    for (int i = 0; i < g_network_topology.link_count; i++) {
        network_link_t *link = &g_network_topology.links[i];
        if (link->active) {
            stats->active_links++;
            stats->total_bandwidth_mbps += link->bandwidth_mbps;
            stats->total_packets_sent += link->packets_sent;
            stats->total_packets_lost += link->packets_lost;
            stats->total_bytes_sent += link->bytes_sent;
        } else {
            stats->failed_links++;
        }
    }
    
    stats->topology_version = g_network_topology.topology_version;
    stats->convergence_time_ms = g_network_topology.convergence_time;
    stats->total_events = g_network_topology.event_count;
    
    return SUCCESS;
}

int network_get_path_stats(uint8_t src_node_id, uint8_t dest_node_id, 
                          network_path_stats_t *stats) {
    if (!stats || validate_node_id(src_node_id) != SUCCESS || 
        validate_node_id(dest_node_id) != SUCCESS) {
        return ERROR_INVALID_PARAM;
    }
    
    uint8_t path[MAX_TOPOLOGY_NODES];
    uint8_t path_length = 0;
    
    int result = network_find_path(src_node_id, dest_node_id, path, &path_length);
    if (result != SUCCESS) {
        return result;
    }
    
    memset(stats, 0, sizeof(network_path_stats_t));
    stats->hop_count = path_length - 1;
    
    /* Calculate path statistics */
    for (int i = 0; i < path_length - 1; i++) {
        /* Find link between path[i] and path[i+1] */
        for (int j = 0; j < g_network_topology.link_count; j++) {
            network_link_t *link = &g_network_topology.links[j];
            if (link->active &&
                ((link->node1_id == path[i] && link->node2_id == path[i+1]) ||
                 (link->node1_id == path[i+1] && link->node2_id == path[i]))) {
                
                stats->total_latency_ms += link->latency_ms;
                stats->min_bandwidth_mbps = (stats->min_bandwidth_mbps == 0) ? 
                    link->bandwidth_mbps : 
                    (link->bandwidth_mbps < stats->min_bandwidth_mbps ? 
                     link->bandwidth_mbps : stats->min_bandwidth_mbps);
                stats->total_loss_rate_ppm += link->loss_rate_ppm;
                break;
            }
        }
    }
    
    return SUCCESS;
}

/* ========== Helper Functions ========== */

static int validate_node_id(uint8_t node_id) {
    if (!g_topology_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    if (node_id >= g_network_topology.node_count) {
        return ERROR_INVALID_PARAM;
    }
    
    return SUCCESS;
}

static int validate_link_id(uint8_t link_id) {
    if (!g_topology_initialized) {
        return ERROR_NOT_INITIALIZED;
    }
    
    if (link_id >= g_network_topology.link_count) {
        return ERROR_INVALID_PARAM;
    }
    
    return SUCCESS;
}

static network_node_t* find_node_by_id(uint8_t node_id) {
    if (validate_node_id(node_id) != SUCCESS) {
        return NULL;
    }
    
    return &g_network_topology.nodes[node_id];
}

static network_link_t* find_link_by_id(uint8_t link_id) {
    if (validate_link_id(link_id) != SUCCESS) {
        return NULL;
    }
    
    return &g_network_topology.links[link_id];
}

static void update_link_statistics(network_link_t *link, bool packet_sent, uint16_t packet_size) {
    if (!link) {
        return;
    }
    
    if (packet_sent) {
        link->packets_sent++;
        link->bytes_sent += packet_size;
    } else {
        link->packets_lost++;
    }
    
    link->utilization_percent = (link->bytes_sent * 8 * 100) / 
                               (link->bandwidth_mbps * 1000000); /* Simplified */
}

static void simulate_propagation_delay(network_link_t *link) {
    if (!link) {
        return;
    }
    
    /* In a real simulation, this would introduce actual delays */
    /* For testing, we just increment counters */
    link->total_propagation_delay += link->latency_ms;
}

static int inject_packet_to_node(uint8_t node_id, const uint8_t *packet, uint16_t length) {
    if (validate_node_id(node_id) != SUCCESS || !packet || length == 0) {
        return ERROR_INVALID_PARAM;
    }
    
    network_node_t *node = find_node_by_id(node_id);
    if (!node || !node->active) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Inject packet into mock device for processing */
    int result = mock_packet_inject_rx(node->mock_device_id, packet, length);
    
    /* Update node statistics */
    node->packets_received++;
    node->bytes_received += length;
    
    return result;
}

/* ========== Pre-defined Topologies ========== */

int network_create_linear_topology(uint8_t node_count, network_node_type_t *node_types) {
    if (!g_topology_initialized || node_count < 2 || node_count > MAX_TOPOLOGY_NODES || !node_types) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Create nodes */
    for (int i = 0; i < node_count; i++) {
        int result = network_add_node(node_types[i], 2, NULL);
        if (result < 0) {
            return result;
        }
    }
    
    /* Create links between adjacent nodes */
    for (int i = 0; i < node_count - 1; i++) {
        int result = network_create_link(i, 1, i + 1, 0, LINK_TYPE_FAST_ETHERNET);
        if (result < 0) {
            return result;
        }
    }
    
    log_info("Created linear topology with %d nodes", node_count);
    
    return SUCCESS;
}

int network_create_star_topology(uint8_t spoke_count, network_node_type_t hub_type, 
                                network_node_type_t spoke_type) {
    if (!g_topology_initialized || spoke_count == 0 || spoke_count > MAX_TOPOLOGY_NODES - 1) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Create hub node */
    int hub_id = network_add_node(hub_type, spoke_count, NULL);
    if (hub_id < 0) {
        return hub_id;
    }
    
    /* Create spoke nodes and connect to hub */
    for (int i = 0; i < spoke_count; i++) {
        int spoke_id = network_add_node(spoke_type, 1, NULL);
        if (spoke_id < 0) {
            return spoke_id;
        }
        
        int result = network_create_link(hub_id, i, spoke_id, 0, LINK_TYPE_FAST_ETHERNET);
        if (result < 0) {
            return result;
        }
    }
    
    log_info("Created star topology with hub node %d and %d spokes", hub_id, spoke_count);
    
    return hub_id;
}

int network_create_ring_topology(uint8_t node_count, network_node_type_t node_type) {
    if (!g_topology_initialized || node_count < 3 || node_count > MAX_TOPOLOGY_NODES) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Create nodes */
    for (int i = 0; i < node_count; i++) {
        int result = network_add_node(node_type, 2, NULL);
        if (result < 0) {
            return result;
        }
    }
    
    /* Create ring links */
    for (int i = 0; i < node_count; i++) {
        int next_node = (i + 1) % node_count;
        int link_port = (i == 0) ? 0 : 1;
        int next_port = (next_node == 0) ? 1 : 0;
        
        int result = network_create_link(i, link_port, next_node, next_port, LINK_TYPE_FAST_ETHERNET);
        if (result < 0) {
            return result;
        }
    }
    
    log_info("Created ring topology with %d nodes", node_count);
    
    return SUCCESS;
}

int network_create_mesh_topology(uint8_t node_count, network_node_type_t node_type, 
                                bool full_mesh) {
    if (!g_topology_initialized || node_count < 2 || node_count > MAX_TOPOLOGY_NODES) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Create nodes with enough NICs for mesh connectivity */
    uint8_t nics_per_node = full_mesh ? (node_count - 1) : 3;
    
    for (int i = 0; i < node_count; i++) {
        int result = network_add_node(node_type, nics_per_node, NULL);
        if (result < 0) {
            return result;
        }
    }
    
    /* Create mesh links */
    int link_count = 0;
    
    if (full_mesh) {
        /* Full mesh - connect every node to every other node */
        for (int i = 0; i < node_count; i++) {
            for (int j = i + 1; j < node_count; j++) {
                int result = network_create_link(i, j, j, i, LINK_TYPE_FAST_ETHERNET);
                if (result >= 0) {
                    link_count++;
                }
            }
        }
    } else {
        /* Partial mesh - create redundant paths but not full connectivity */
        for (int i = 0; i < node_count; i++) {
            for (int j = 0; j < 3 && j < node_count - 1; j++) {
                int target = (i + j + 1) % node_count;
                if (target != i) {
                    int result = network_create_link(i, j % nics_per_node, 
                                                   target, (i + j) % nics_per_node, 
                                                   LINK_TYPE_FAST_ETHERNET);
                    if (result >= 0) {
                        link_count++;
                    }
                }
            }
        }
    }
    
    log_info("Created %s mesh topology with %d nodes and %d links", 
             full_mesh ? "full" : "partial", node_count, link_count);
    
    return SUCCESS;
}