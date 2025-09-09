/**
 * @file network_analysis.c
 * @brief Network analysis tools for packet inspection and flow monitoring
 * 
 * 3Com Packet Driver - Diagnostics Agent - Week 1
 * Implements comprehensive packet inspection, flow monitoring, and network analysis
 */

#include "../../include/diagnostics.h"
#include "../../include/common.h"
#include "../../include/hardware.h"
#include "../../include/packet_ops.h"
#include "../../docs/agents/shared/error-codes.h"
#include <string.h>
#include <stdio.h>

/* Network analysis configuration */
#define MAX_FLOW_ENTRIES            256
#define MAX_PACKET_SAMPLES          1000
#define FLOW_TIMEOUT_MS             300000  /* 5 minutes */
#define PACKET_INSPECTION_WINDOW    60000   /* 1 minute */
#define BOTTLENECK_DETECTION_THRESHOLD 80   /* 80% utilization */

/* Ethernet frame structure for packet inspection */
typedef struct ethernet_frame {
    uint8_t dest_mac[6];
    uint8_t src_mac[6];
    uint16_t ethertype;
} __attribute__((packed)) ethernet_frame_t;

/* IP header structure for flow analysis */
typedef struct ip_header {
    uint8_t version_ihl;
    uint8_t tos;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t header_checksum;
    uint32_t src_ip;
    uint32_t dest_ip;
} __attribute__((packed)) ip_header_t;

/* TCP/UDP header union for port extraction */
typedef union transport_header {
    struct {
        uint16_t src_port;
        uint16_t dest_port;
        uint32_t seq_num;
        uint32_t ack_num;
    } tcp;
    struct {
        uint16_t src_port;
        uint16_t dest_port;
        uint16_t length;
        uint16_t checksum;
    } udp;
} transport_header_t;

/* Packet sample for analysis */
typedef struct packet_sample {
    uint32_t timestamp;
    uint16_t size;
    uint8_t direction;      /* 0=RX, 1=TX */
    uint8_t nic_index;
    uint8_t protocol;       /* IP protocol number */
    uint32_t src_ip;
    uint32_t dest_ip;
    uint16_t src_port;
    uint16_t dest_port;
    uint8_t packet_type;    /* Categorized packet type */
    struct packet_sample *next;
} packet_sample_t;

/* Flow tracking entry */
typedef struct flow_tracking_entry {
    uint32_t flow_id;
    uint32_t src_ip;
    uint32_t dest_ip;
    uint16_t src_port;
    uint16_t dest_port;
    uint8_t protocol;
    uint8_t nic_index;
    
    /* Flow statistics */
    uint32_t packet_count;
    uint32_t byte_count;
    uint32_t first_seen;
    uint32_t last_seen;
    uint32_t avg_packet_size;
    
    /* Quality metrics */
    uint32_t retransmissions;
    uint32_t out_of_order;
    uint32_t lost_packets;
    double jitter_ms;
    
    /* Flow classification */
    uint8_t flow_type;      /* Interactive, Bulk, etc. */
    uint8_t priority;
    bool symmetric;         /* Bidirectional flow */
    
    struct flow_tracking_entry *next;
} flow_tracking_entry_t;

/* Network bottleneck detection */
typedef struct bottleneck_analysis {
    uint8_t bottleneck_type;
    uint8_t affected_nic;
    uint32_t utilization_percent;
    uint32_t queue_depth;
    uint32_t packet_drops;
    char description[128];
    uint32_t detected_time;
} bottleneck_analysis_t;

/* Network analysis system state */
typedef struct network_analyzer {
    bool initialized;
    bool packet_inspection_enabled;
    bool flow_monitoring_enabled;
    bool bottleneck_detection_enabled;
    
    /* Packet inspection */
    packet_sample_t *packet_samples_head;
    packet_sample_t *packet_samples_tail;
    uint16_t packet_sample_count;
    uint32_t inspection_window_ms;
    
    /* Flow monitoring */
    flow_tracking_entry_t *flow_entries[256]; /* Hash table */
    uint16_t active_flow_count;
    uint32_t flow_timeout_ms;
    uint32_t next_flow_id;
    
    /* Network statistics */
    uint32_t total_packets_inspected;
    uint32_t total_flows_tracked;
    uint32_t flows_aged_out;
    uint32_t packets_dropped;
    uint32_t bandwidth_utilization[MAX_NICS];
    
    /* Bottleneck detection */
    bottleneck_analysis_t recent_bottlenecks[10];
    uint8_t bottleneck_count;
    uint32_t bottleneck_threshold;
    
    /* Protocol distribution */
    uint32_t protocol_counts[256];
    uint32_t port_counts[65536]; /* Only track well-known ports */
    
} network_analyzer_t;

/* Packet type classifications */
#define PACKET_TYPE_UNKNOWN         0
#define PACKET_TYPE_ARP             1
#define PACKET_TYPE_IP              2
#define PACKET_TYPE_TCP             3
#define PACKET_TYPE_UDP             4
#define PACKET_TYPE_ICMP            5
#define PACKET_TYPE_BROADCAST       6
#define PACKET_TYPE_MULTICAST       7

/* Flow type classifications */
#define FLOW_TYPE_INTERACTIVE       1
#define FLOW_TYPE_BULK_TRANSFER     2
#define FLOW_TYPE_STREAMING         3
#define FLOW_TYPE_CONTROL           4

/* Bottleneck types */
#define BOTTLENECK_TYPE_BANDWIDTH   1
#define BOTTLENECK_TYPE_QUEUE       2
#define BOTTLENECK_TYPE_CPU         3
#define BOTTLENECK_TYPE_MEMORY      4

static network_analyzer_t g_network_analyzer = {0};

/* Helper functions */
static uint32_t hash_flow(uint32_t src_ip, uint32_t dest_ip, uint16_t src_port, uint16_t dest_port, uint8_t protocol) {
    return (src_ip ^ dest_ip ^ ((uint32_t)src_port << 16) ^ dest_port ^ protocol) % 256;
}

static uint8_t classify_packet_type(const void *packet_data, uint16_t packet_size) {
    if (packet_size < sizeof(ethernet_frame_t)) {
        return PACKET_TYPE_UNKNOWN;
    }
    
    const ethernet_frame_t *eth = (const ethernet_frame_t*)packet_data;
    uint16_t ethertype = ntohs(eth->ethertype);
    
    /* Check for broadcast */
    bool is_broadcast = (eth->dest_mac[0] == 0xFF && eth->dest_mac[1] == 0xFF && 
                        eth->dest_mac[2] == 0xFF && eth->dest_mac[3] == 0xFF &&
                        eth->dest_mac[4] == 0xFF && eth->dest_mac[5] == 0xFF);
    if (is_broadcast) {
        return PACKET_TYPE_BROADCAST;
    }
    
    /* Check for multicast */
    if (eth->dest_mac[0] & 0x01) {
        return PACKET_TYPE_MULTICAST;
    }
    
    switch (ethertype) {
        case 0x0806: return PACKET_TYPE_ARP;
        case 0x0800: return PACKET_TYPE_IP;
        default: return PACKET_TYPE_UNKNOWN;
    }
}

static uint8_t classify_flow_type(const flow_tracking_entry_t *flow) {
    if (!flow) return FLOW_TYPE_INTERACTIVE;
    
    /* Classify based on port numbers and traffic patterns */
    uint16_t min_port = (flow->src_port < flow->dest_port) ? flow->src_port : flow->dest_port;
    
    /* Well-known control ports */
    if (min_port == 21 || min_port == 22 || min_port == 23 || min_port == 53) {
        return FLOW_TYPE_CONTROL;
    }
    
    /* Streaming/media ports */
    if (min_port >= 1024 && flow->protocol == 17) { /* UDP */
        return FLOW_TYPE_STREAMING;
    }
    
    /* Bulk transfer detection based on packet size and frequency */
    if (flow->avg_packet_size > 1400 && flow->packet_count > 100) {
        return FLOW_TYPE_BULK_TRANSFER;
    }
    
    return FLOW_TYPE_INTERACTIVE;
}

/* Initialize network analysis system */
int network_analysis_init(void) {
    if (g_network_analyzer.initialized) {
        return SUCCESS;
    }
    
    /* Initialize configuration */
    g_network_analyzer.packet_inspection_enabled = true;
    g_network_analyzer.flow_monitoring_enabled = true;
    g_network_analyzer.bottleneck_detection_enabled = true;
    g_network_analyzer.inspection_window_ms = PACKET_INSPECTION_WINDOW;
    g_network_analyzer.flow_timeout_ms = FLOW_TIMEOUT_MS;
    g_network_analyzer.bottleneck_threshold = BOTTLENECK_DETECTION_THRESHOLD;
    
    /* Initialize packet sampling */
    g_network_analyzer.packet_samples_head = NULL;
    g_network_analyzer.packet_samples_tail = NULL;
    g_network_analyzer.packet_sample_count = 0;
    
    /* Initialize flow tracking hash table */
    memset(g_network_analyzer.flow_entries, 0, sizeof(g_network_analyzer.flow_entries));
    g_network_analyzer.active_flow_count = 0;
    g_network_analyzer.next_flow_id = 1;
    
    /* Initialize statistics */
    memset(&g_network_analyzer.protocol_counts, 0, sizeof(g_network_analyzer.protocol_counts));
    memset(&g_network_analyzer.port_counts, 0, sizeof(g_network_analyzer.port_counts));
    memset(&g_network_analyzer.bandwidth_utilization, 0, sizeof(g_network_analyzer.bandwidth_utilization));
    
    /* Initialize bottleneck tracking */
    memset(&g_network_analyzer.recent_bottlenecks, 0, sizeof(g_network_analyzer.recent_bottlenecks));
    g_network_analyzer.bottleneck_count = 0;
    
    g_network_analyzer.initialized = true;
    debug_log_info("Network analysis system initialized");
    return SUCCESS;
}

/* Inspect and analyze a packet */
int network_analysis_inspect_packet(const void *packet_data, uint16_t packet_size, 
                                    uint8_t direction, uint8_t nic_index) {
    if (!g_network_analyzer.initialized || !g_network_analyzer.packet_inspection_enabled || !packet_data) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Create packet sample */
    packet_sample_t *sample = (packet_sample_t*)malloc(sizeof(packet_sample_t));
    if (!sample) {
        return ERROR_OUT_OF_MEMORY;
    }
    
    memset(sample, 0, sizeof(packet_sample_t));
    sample->timestamp = diag_get_timestamp();
    sample->size = packet_size;
    sample->direction = direction;
    sample->nic_index = nic_index;
    sample->packet_type = classify_packet_type(packet_data, packet_size);
    
    /* Extract network layer information for IP packets */
    if (sample->packet_type == PACKET_TYPE_IP && packet_size >= sizeof(ethernet_frame_t) + sizeof(ip_header_t)) {
        const ip_header_t *ip = (const ip_header_t*)((uint8_t*)packet_data + sizeof(ethernet_frame_t));
        
        sample->protocol = ip->protocol;
        sample->src_ip = ntohl(ip->src_ip);
        sample->dest_ip = ntohl(ip->dest_ip);
        
        /* Extract transport layer ports if TCP/UDP */
        if ((ip->protocol == 6 || ip->protocol == 17) && 
            packet_size >= sizeof(ethernet_frame_t) + sizeof(ip_header_t) + sizeof(transport_header_t)) {
            const transport_header_t *transport = 
                (const transport_header_t*)((uint8_t*)ip + ((ip->version_ihl & 0x0F) * 4));
            
            sample->src_port = ntohs(transport->tcp.src_port);
            sample->dest_port = ntohs(transport->tcp.dest_port);
        }
    }
    
    /* Add to packet sample list */
    sample->next = NULL;
    if (!g_network_analyzer.packet_samples_head) {
        g_network_analyzer.packet_samples_head = sample;
        g_network_analyzer.packet_samples_tail = sample;
    } else {
        g_network_analyzer.packet_samples_tail->next = sample;
        g_network_analyzer.packet_samples_tail = sample;
    }
    
    g_network_analyzer.packet_sample_count++;
    g_network_analyzer.total_packets_inspected++;
    
    /* Update protocol statistics */
    if (sample->protocol < 256) {
        g_network_analyzer.protocol_counts[sample->protocol]++;
    }
    
    /* Update port statistics for well-known ports */
    if (sample->src_port < 1024) {
        g_network_analyzer.port_counts[sample->src_port]++;
    }
    if (sample->dest_port < 1024) {
        g_network_analyzer.port_counts[sample->dest_port]++;
    }
    
    /* Remove old samples */
    uint32_t cutoff_time = sample->timestamp - g_network_analyzer.inspection_window_ms;
    while (g_network_analyzer.packet_samples_head && 
           g_network_analyzer.packet_samples_head->timestamp < cutoff_time) {
        packet_sample_t *old_sample = g_network_analyzer.packet_samples_head;
        g_network_analyzer.packet_samples_head = old_sample->next;
        free(old_sample);
        g_network_analyzer.packet_sample_count--;
    }
    
    /* Update flow tracking if enabled */
    if (g_network_analyzer.flow_monitoring_enabled && 
        sample->packet_type == PACKET_TYPE_IP && sample->protocol > 0) {
        network_analysis_update_flow(sample);
    }
    
    return SUCCESS;
}

/* Update flow tracking with packet information */
int network_analysis_update_flow(const packet_sample_t *sample) {
    if (!g_network_analyzer.initialized || !sample) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Calculate hash for flow lookup */
    uint32_t hash = hash_flow(sample->src_ip, sample->dest_ip, sample->src_port, sample->dest_port, sample->protocol);
    
    /* Look for existing flow */
    flow_tracking_entry_t *flow = g_network_analyzer.flow_entries[hash];
    while (flow) {
        if (flow->src_ip == sample->src_ip && flow->dest_ip == sample->dest_ip &&
            flow->src_port == sample->src_port && flow->dest_port == sample->dest_port &&
            flow->protocol == sample->protocol) {
            break;
        }
        flow = flow->next;
    }
    
    /* Create new flow if not found */
    if (!flow) {
        flow = (flow_tracking_entry_t*)malloc(sizeof(flow_tracking_entry_t));
        if (!flow) {
            return ERROR_OUT_OF_MEMORY;
        }
        
        memset(flow, 0, sizeof(flow_tracking_entry_t));
        flow->flow_id = g_network_analyzer.next_flow_id++;
        flow->src_ip = sample->src_ip;
        flow->dest_ip = sample->dest_ip;
        flow->src_port = sample->src_port;
        flow->dest_port = sample->dest_port;
        flow->protocol = sample->protocol;
        flow->nic_index = sample->nic_index;
        flow->first_seen = sample->timestamp;
        
        /* Add to hash table */
        flow->next = g_network_analyzer.flow_entries[hash];
        g_network_analyzer.flow_entries[hash] = flow;
        g_network_analyzer.active_flow_count++;
        g_network_analyzer.total_flows_tracked++;
    }
    
    /* Update flow statistics */
    flow->packet_count++;
    flow->byte_count += sample->size;
    flow->last_seen = sample->timestamp;
    flow->avg_packet_size = flow->byte_count / flow->packet_count;
    flow->flow_type = classify_flow_type(flow);
    
    return SUCCESS;
}

/* Age out old flows */
int network_analysis_age_flows(void) {
    if (!g_network_analyzer.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    uint32_t current_time = diag_get_timestamp();
    uint32_t timeout_threshold = current_time - g_network_analyzer.flow_timeout_ms;
    
    for (int i = 0; i < 256; i++) {
        flow_tracking_entry_t **flow_ptr = &g_network_analyzer.flow_entries[i];
        
        while (*flow_ptr) {
            flow_tracking_entry_t *flow = *flow_ptr;
            
            if (flow->last_seen < timeout_threshold) {
                /* Remove aged flow */
                *flow_ptr = flow->next;
                free(flow);
                g_network_analyzer.active_flow_count--;
                g_network_analyzer.flows_aged_out++;
            } else {
                flow_ptr = &flow->next;
            }
        }
    }
    
    return SUCCESS;
}

/* Detect network bottlenecks */
int network_analysis_detect_bottlenecks(void) {
    if (!g_network_analyzer.initialized || !g_network_analyzer.bottleneck_detection_enabled) {
        return ERROR_INVALID_STATE;
    }
    
    uint32_t current_time = diag_get_timestamp();
    bool bottleneck_detected = false;
    
    /* Check bandwidth utilization */
    for (int nic = 0; nic < MAX_NICS; nic++) {
        if (g_network_analyzer.bandwidth_utilization[nic] > g_network_analyzer.bottleneck_threshold) {
            bottleneck_analysis_t *bottleneck = &g_network_analyzer.recent_bottlenecks[g_network_analyzer.bottleneck_count % 10];
            
            bottleneck->bottleneck_type = BOTTLENECK_TYPE_BANDWIDTH;
            bottleneck->affected_nic = nic;
            bottleneck->utilization_percent = g_network_analyzer.bandwidth_utilization[nic];
            bottleneck->detected_time = current_time;
            snprintf(bottleneck->description, sizeof(bottleneck->description),
                    "Bandwidth bottleneck on NIC %d: %lu%% utilization",
                    nic, bottleneck->utilization_percent);
            
            g_network_analyzer.bottleneck_count++;
            bottleneck_detected = true;
            
            debug_log_warning("Network bottleneck detected: %s", bottleneck->description);
        }
    }
    
    /* Check for flow concentration (too many flows on one NIC) */
    uint16_t nic_flow_counts[MAX_NICS] = {0};
    for (int i = 0; i < 256; i++) {
        flow_tracking_entry_t *flow = g_network_analyzer.flow_entries[i];
        while (flow) {
            if (flow->nic_index < MAX_NICS) {
                nic_flow_counts[flow->nic_index]++;
            }
            flow = flow->next;
        }
    }
    
    for (int nic = 0; nic < MAX_NICS; nic++) {
        if (nic_flow_counts[nic] > (g_network_analyzer.active_flow_count * 80 / 100)) {
            bottleneck_analysis_t *bottleneck = &g_network_analyzer.recent_bottlenecks[g_network_analyzer.bottleneck_count % 10];
            
            bottleneck->bottleneck_type = BOTTLENECK_TYPE_QUEUE;
            bottleneck->affected_nic = nic;
            bottleneck->queue_depth = nic_flow_counts[nic];
            bottleneck->detected_time = current_time;
            snprintf(bottleneck->description, sizeof(bottleneck->description),
                    "Flow concentration on NIC %d: %d flows (%d%% of total)",
                    nic, nic_flow_counts[nic], (nic_flow_counts[nic] * 100) / g_network_analyzer.active_flow_count);
            
            g_network_analyzer.bottleneck_count++;
            bottleneck_detected = true;
            
            debug_log_warning("Network bottleneck detected: %s", bottleneck->description);
        }
    }
    
    return bottleneck_detected ? 1 : 0;
}

/* Get network analysis statistics */
int network_analysis_get_statistics(uint32_t *packets_inspected, uint32_t *active_flows,
                                    uint32_t *bottlenecks_detected, uint32_t *flows_aged) {
    if (!g_network_analyzer.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    if (packets_inspected) *packets_inspected = g_network_analyzer.total_packets_inspected;
    if (active_flows) *active_flows = g_network_analyzer.active_flow_count;
    if (bottlenecks_detected) *bottlenecks_detected = g_network_analyzer.bottleneck_count;
    if (flows_aged) *flows_aged = g_network_analyzer.flows_aged_out;
    
    return SUCCESS;
}

/* Print network analysis dashboard */
int network_analysis_print_dashboard(void) {
    if (!g_network_analyzer.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    printf("\n=== NETWORK ANALYSIS DASHBOARD ===\n");
    printf("Packet Inspection: %s\n", g_network_analyzer.packet_inspection_enabled ? "Enabled" : "Disabled");
    printf("Flow Monitoring: %s\n", g_network_analyzer.flow_monitoring_enabled ? "Enabled" : "Disabled");
    printf("Bottleneck Detection: %s\n", g_network_analyzer.bottleneck_detection_enabled ? "Enabled" : "Disabled");
    
    printf("\nStatistics:\n");
    printf("  Packets Inspected: %lu\n", g_network_analyzer.total_packets_inspected);
    printf("  Active Flows: %d\n", g_network_analyzer.active_flow_count);
    printf("  Total Flows Tracked: %lu\n", g_network_analyzer.total_flows_tracked);
    printf("  Flows Aged Out: %lu\n", g_network_analyzer.flows_aged_out);
    printf("  Bottlenecks Detected: %d\n", g_network_analyzer.bottleneck_count);
    
    printf("\nTop Protocols:\n");
    for (int i = 0; i < 256; i++) {
        if (g_network_analyzer.protocol_counts[i] > 0) {
            const char *protocol_name = "Unknown";
            switch (i) {
                case 1: protocol_name = "ICMP"; break;
                case 6: protocol_name = "TCP"; break;
                case 17: protocol_name = "UDP"; break;
            }
            printf("  Protocol %d (%s): %lu packets\n", i, protocol_name, g_network_analyzer.protocol_counts[i]);
        }
    }
    
    printf("\nTop Well-Known Ports:\n");
    for (int i = 0; i < 1024; i++) {
        if (g_network_analyzer.port_counts[i] > 0) {
            const char *service_name = "Unknown";
            switch (i) {
                case 21: service_name = "FTP"; break;
                case 22: service_name = "SSH"; break;
                case 23: service_name = "Telnet"; break;
                case 25: service_name = "SMTP"; break;
                case 53: service_name = "DNS"; break;
                case 80: service_name = "HTTP"; break;
                case 443: service_name = "HTTPS"; break;
            }
            printf("  Port %d (%s): %lu packets\n", i, service_name, g_network_analyzer.port_counts[i]);
        }
    }
    
    if (g_network_analyzer.bottleneck_count > 0) {
        printf("\nRecent Bottlenecks:\n");
        int start = (g_network_analyzer.bottleneck_count > 10) ? g_network_analyzer.bottleneck_count - 10 : 0;
        for (int i = start; i < g_network_analyzer.bottleneck_count; i++) {
            bottleneck_analysis_t *bottleneck = &g_network_analyzer.recent_bottlenecks[i % 10];
            printf("  [%lu] %s\n", bottleneck->detected_time, bottleneck->description);
        }
    }
    
    return SUCCESS;
}

/* Export network analysis data */
int network_analysis_export_data(char *buffer, uint32_t buffer_size) {
    if (!g_network_analyzer.initialized || !buffer) {
        return ERROR_INVALID_PARAM;
    }
    
    uint32_t written = 0;
    written += snprintf(buffer + written, buffer_size - written,
                       "# Network Analysis Export\n");
    written += snprintf(buffer + written, buffer_size - written,
                       "# Timestamp: %lu\n", diag_get_timestamp());
    
    written += snprintf(buffer + written, buffer_size - written,
                       "\n[STATISTICS]\n");
    written += snprintf(buffer + written, buffer_size - written,
                       "packets_inspected=%lu\n", g_network_analyzer.total_packets_inspected);
    written += snprintf(buffer + written, buffer_size - written,
                       "active_flows=%d\n", g_network_analyzer.active_flow_count);
    written += snprintf(buffer + written, buffer_size - written,
                       "total_flows=%lu\n", g_network_analyzer.total_flows_tracked);
    written += snprintf(buffer + written, buffer_size - written,
                       "flows_aged=%lu\n", g_network_analyzer.flows_aged_out);
    
    written += snprintf(buffer + written, buffer_size - written,
                       "\n[PROTOCOL_DISTRIBUTION]\n");
    for (int i = 0; i < 256; i++) {
        if (g_network_analyzer.protocol_counts[i] > 0 && written < buffer_size - 100) {
            written += snprintf(buffer + written, buffer_size - written,
                               "protocol_%d=%lu\n", i, g_network_analyzer.protocol_counts[i]);
        }
    }
    
    return SUCCESS;
}

/* Week 1 specific: NE2000 emulation network monitoring */
int network_analysis_ne2000_emulation(const void *packet_data, uint16_t packet_size, bool tx_path) {
    if (!g_network_analyzer.initialized) {
        return ERROR_INVALID_STATE;
    }
    
    debug_log_trace("NE2000 emulation packet: size=%d, direction=%s", 
                    packet_size, tx_path ? "TX" : "RX");
    
    /* Inspect packet for NE2000 compatibility */
    int result = network_analysis_inspect_packet(packet_data, packet_size, tx_path ? 1 : 0, 0);
    if (result != SUCCESS) {
        debug_log_warning("NE2000 emulation packet inspection failed: 0x%04X", result);
        return result;
    }
    
    /* Check for NE2000-specific issues */
    if (packet_size > 1518) {
        debug_log_warning("NE2000 emulation: oversized packet detected (%d bytes)", packet_size);
        return ERROR_PACKET_TOO_LARGE;
    }
    
    if (packet_size < 64) {
        debug_log_warning("NE2000 emulation: undersized packet detected (%d bytes)", packet_size);
        return ERROR_PACKET_INVALID;
    }
    
    return SUCCESS;
}

/* Cleanup network analysis system */
void network_analysis_cleanup(void) {
    if (!g_network_analyzer.initialized) {
        return;
    }
    
    debug_log_info("Cleaning up network analysis system");
    
    /* Free packet samples */
    packet_sample_t *sample = g_network_analyzer.packet_samples_head;
    while (sample) {
        packet_sample_t *next = sample->next;
        free(sample);
        sample = next;
    }
    
    /* Free flow entries */
    for (int i = 0; i < 256; i++) {
        flow_tracking_entry_t *flow = g_network_analyzer.flow_entries[i];
        while (flow) {
            flow_tracking_entry_t *next = flow->next;
            free(flow);
            flow = next;
        }
    }
    
    memset(&g_network_analyzer, 0, sizeof(network_analyzer_t));
}