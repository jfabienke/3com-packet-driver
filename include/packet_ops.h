/**
 * @file packet_ops.h
 * @brief Packet operation functions
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 */

#ifndef _PACKET_OPS_H_
#define _PACKET_OPS_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include "common.h"
#include "hardware.h"
#include "buffer_alloc.h"

/* Packet operation result codes */
#define PACKET_OP_SUCCESS           0
#define PACKET_OP_ERROR             -1
#define PACKET_OP_INVALID_PARAM     -2
#define PACKET_OP_NO_MEMORY         -3
#define PACKET_OP_TIMEOUT           -4
#define PACKET_OP_QUEUE_FULL        -5
#define PACKET_OP_QUEUE_EMPTY       -6
#define PACKET_OP_TOO_LARGE         -7
#define PACKET_OP_TOO_SMALL         -8
#define PACKET_OP_CHECKSUM_ERROR    -9
#define PACKET_OP_CRC_ERROR         -10

/* Packet flags */
#define PACKET_FLAG_BROADCAST       BIT(0)   /* Broadcast packet */
#define PACKET_FLAG_MULTICAST       BIT(1)   /* Multicast packet */
#define PACKET_FLAG_UNICAST         BIT(2)   /* Unicast packet */
#define PACKET_FLAG_ERROR           BIT(3)   /* Packet has error */
#define PACKET_FLAG_CRC_ERROR       BIT(4)   /* CRC error */
#define PACKET_FLAG_LENGTH_ERROR    BIT(5)   /* Length error */
#define PACKET_FLAG_FRAME_ERROR     BIT(6)   /* Framing error */
#define PACKET_FLAG_OVERRUN         BIT(7)   /* Buffer overrun */
#define PACKET_FLAG_UNDERRUN        BIT(8)   /* Buffer underrun */
#define PACKET_FLAG_COLLISION       BIT(9)   /* Collision detected */
#define PACKET_FLAG_LATE_COLLISION  BIT(10)  /* Late collision */
#define PACKET_FLAG_EXCESSIVE_COLL  BIT(11)  /* Excessive collisions */
#define PACKET_FLAG_JABBER          BIT(12)  /* Jabber error */

/* Packet priorities */
#define PACKET_PRIORITY_LOW         0
#define PACKET_PRIORITY_NORMAL      1
#define PACKET_PRIORITY_HIGH        2
#define PACKET_PRIORITY_URGENT      3

/* Loopback testing types and structures */
typedef enum {
    LOOPBACK_INTERNAL = 0,                  /* Internal/software loopback */
    LOOPBACK_EXTERNAL = 1                   /* External/hardware loopback */
} loopback_type_t;

/* Loopback test pattern structure */
typedef struct loopback_test_pattern {
    char name[32];                          /* Pattern name */
    uint8_t *data;                          /* Test data */
    uint16_t size;                          /* Data size */
    uint32_t timeout_ms;                    /* Test timeout */
} loopback_test_pattern_t;

/* Packet integrity mismatch detail */
typedef struct packet_mismatch_detail {
    uint16_t offset;                        /* Byte offset of mismatch */
    uint8_t expected;                       /* Expected value */
    uint8_t actual;                         /* Actual value */
} packet_mismatch_detail_t;

#define MAX_MISMATCH_DETAILS 16

/* Packet integrity result */
typedef struct packet_integrity_result {
    uint16_t bytes_compared;                /* Total bytes compared */
    uint16_t mismatch_count;                /* Number of mismatches */
    uint16_t error_rate_percent;            /* Error rate percentage */
    uint16_t single_bit_errors;             /* Single bit error count */
    uint16_t burst_errors;                  /* Burst error count */
    packet_mismatch_detail_t mismatch_details[MAX_MISMATCH_DETAILS];
    char error_pattern_description[64];     /* Error pattern description */
} packet_integrity_result_t;

/* Additional packet error codes for testing */
#define PACKET_ERR_LOOPBACK_FAILED      -20
#define PACKET_ERR_INTEGRITY_FAILED     -21

/* Packet buffer structure */
typedef struct packet_buffer {
    uint8_t *data;                          /* Packet data pointer */
    uint16_t length;                        /* Packet length */
    uint16_t capacity;                      /* Buffer capacity */
    uint16_t flags;                         /* Packet flags */
    uint8_t priority;                       /* Packet priority */
    uint16_t handle;                        /* Packet handle */
    uint32_t timestamp;                     /* Timestamp (if available) */
    struct packet_buffer *next;             /* Next buffer in queue */
    void *private_data;                     /* Private data pointer */
} packet_buffer_t;

/* Packet queue structure */
typedef struct packet_queue {
    packet_buffer_t *head;                  /* Queue head */
    packet_buffer_t *tail;                  /* Queue tail */
    uint16_t count;                         /* Number of packets in queue */
    uint16_t max_count;                     /* Maximum queue size */
    uint32_t total_bytes;                   /* Total bytes in queue */
    uint32_t max_bytes;                     /* Maximum bytes in queue */
    uint32_t dropped_packets;               /* Dropped packet count */
    uint32_t dropped_bytes;                 /* Dropped bytes count */
} packet_queue_t;

/* Ethernet header structure */
typedef struct eth_header {
    uint8_t dest_mac[ETH_ALEN];             /* Destination MAC address */
    uint8_t src_mac[ETH_ALEN];              /* Source MAC address */
    uint16_t ethertype;                     /* Ethernet type/length */
} PACKED eth_header_t;

/* Packet statistics structure */
typedef struct packet_stats {
    uint32_t tx_packets;                    /* Transmitted packets */
    uint32_t rx_packets;                    /* Received packets */
    uint32_t tx_bytes;                      /* Transmitted bytes */
    uint32_t rx_bytes;                      /* Received bytes */
    uint32_t tx_errors;                     /* Transmit errors */
    uint32_t rx_errors;                     /* Receive errors */
    uint32_t tx_dropped;                    /* Dropped TX packets */
    uint32_t rx_dropped;                    /* Dropped RX packets */
    uint32_t tx_buffer_full;                /* TX buffer full events */
    uint32_t rx_runt;                       /* Runt packets received */
    uint32_t rx_oversize;                   /* Oversized packets received */
    uint32_t routed_packets;                /* Packets routed */
    uint32_t collisions;                    /* Collision count */
    uint32_t crc_errors;                    /* CRC errors */
    uint32_t frame_errors;                  /* Frame errors */
    uint32_t overrun_errors;                /* Overrun errors */
    uint32_t underrun_errors;               /* Underrun errors */
} packet_stats_t;

/* Global packet queues */
extern packet_queue_t g_tx_queue;
extern packet_queue_t g_rx_queue;
extern packet_stats_t g_packet_stats;

/* Packet buffer management */
packet_buffer_t* packet_buffer_alloc(uint16_t size);
void packet_buffer_free(packet_buffer_t *buffer);
packet_buffer_t* packet_buffer_clone(const packet_buffer_t *source);
int packet_buffer_resize(packet_buffer_t *buffer, uint16_t new_size);
void packet_buffer_reset(packet_buffer_t *buffer);

/* Packet data manipulation */
int packet_set_data(packet_buffer_t *buffer, const uint8_t *data, uint16_t length);
int packet_append_data(packet_buffer_t *buffer, const uint8_t *data, uint16_t length);
int packet_prepend_data(packet_buffer_t *buffer, const uint8_t *data, uint16_t length);
int packet_remove_header(packet_buffer_t *buffer, uint16_t header_size);
int packet_add_padding(packet_buffer_t *buffer, uint16_t min_size);

/* Packet queue operations */
int packet_queue_init(packet_queue_t *queue, uint16_t max_packets, uint32_t max_bytes);
void packet_queue_cleanup(packet_queue_t *queue);
int packet_queue_enqueue(packet_queue_t *queue, packet_buffer_t *buffer);
packet_buffer_t* packet_queue_dequeue(packet_queue_t *queue);
packet_buffer_t* packet_queue_peek(const packet_queue_t *queue);
bool packet_queue_is_empty(const packet_queue_t *queue);
bool packet_queue_is_full(const packet_queue_t *queue);
void packet_queue_flush(packet_queue_t *queue);
uint16_t packet_queue_count(const packet_queue_t *queue);

/* Configuration includes */
#include "config.h"

/* Packet size constants */
#define PACKET_MIN_SIZE         64
#define PACKET_MAX_SIZE         1514

/* Ethernet frame constants from REFERENCES.md */
#define ETH_ALEN                6       /* Ethernet address length */
#define ETH_HEADER_LEN          14      /* Ethernet header length (6+6+2) */
#define ETH_MIN_DATA            46      /* Minimum data length */
#define ETH_MAX_DATA            1500    /* Maximum data length (MTU) */
#define ETH_MIN_FRAME           64      /* Minimum frame including CRC */
#define ETH_MAX_FRAME           1518    /* Maximum frame including CRC */

/* Common EtherType values */
#define ETH_P_IP                0x0800  /* Internet Protocol */
#define ETH_P_ARP               0x0806  /* Address Resolution Protocol */
#define ETH_P_IPV6              0x86DD  /* IPv6 */

/* Network byte order conversion helpers */
#ifndef htons
#define htons(x) ((((x) & 0x00FF) << 8) | (((x) & 0xFF00) >> 8))
#endif
#ifndef ntohs
#define ntohs(x) htons(x)
#endif

/* Error codes specific to our implementation */
#define PACKET_ERR_INVALID_PARAM    -1
#define PACKET_ERR_NOT_INITIALIZED  -2
#define PACKET_ERR_INVALID_SIZE     -3
#define PACKET_ERR_NO_ROUTE         -4
#define PACKET_ERR_INVALID_NIC      -5
#define PACKET_ERR_NO_PACKET        -6
#define PACKET_ERR_NO_BUFFERS       -7

/* Note: packet_stats_t is already defined above */

/* Performance metrics structures for enhanced monitoring */
typedef struct {
    uint8_t active;
    uint8_t link_up;
    uint16_t speed;           /* Speed in Mbps */
    uint8_t full_duplex;
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t tx_errors;
    uint32_t rx_errors;
} nic_performance_stats_t;

typedef struct {
    /* Global counters */
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t tx_errors;
    uint32_t rx_errors;
    uint32_t rx_dropped;
    
    /* Performance ratios (percentages) */
    uint32_t tx_error_rate;    /* TX errors / TX packets * 100 */
    uint32_t rx_error_rate;    /* RX errors / RX packets * 100 */
    uint32_t rx_drop_rate;     /* RX dropped / RX packets * 100 */
    
    /* Throughput estimates */
    uint32_t tx_throughput;    /* TX packets per second */
    uint32_t rx_throughput;    /* RX packets per second */
    
    /* Per-NIC statistics */
    uint8_t active_nics;
    nic_performance_stats_t nic_stats[MAX_NICS];
    
    /* Collection metadata */
    uint32_t collection_time;
} packet_performance_metrics_t;

/* Production queue management statistics */
typedef struct {
    uint16_t tx_queue_counts[4];        /* Current packet counts per priority */
    uint16_t tx_queue_max[4];           /* Maximum queue sizes per priority */
    uint32_t tx_queue_usage[4];         /* Queue usage percentages per priority */
    uint32_t tx_queue_dropped[4];       /* Dropped packets per priority queue */
    
    uint16_t rx_queue_count;            /* Current RX queue count */
    uint16_t rx_queue_max;              /* Maximum RX queue size */
    uint32_t rx_queue_usage;            /* RX queue usage percentage */
    uint32_t rx_queue_dropped;          /* RX queue dropped packets */
    
    uint32_t queue_full_events;         /* Total queue overflow events */
    uint32_t backpressure_events;       /* Flow control activation events */
    uint32_t priority_drops;            /* Priority-based packet drops */
    uint32_t adaptive_resizes;          /* Adaptive queue resize events */
    bool flow_control_active;           /* Current flow control status */
} packet_queue_management_stats_t;

/* High-level packet operations */
int packet_ops_init(const config_t *config);
int packet_send(const uint8_t *packet, size_t length, uint16_t handle);
int packet_send_enhanced(uint8_t interface_num, const uint8_t *packet_data, 
                        uint16_t length, const uint8_t *dest_addr, uint16_t handle);

/* Direct PIO optimization for 3c509B - Sprint 1.2 */
int packet_send_direct_pio_3c509b(uint8_t interface_num, const uint8_t *dest_addr,
                                 uint16_t ethertype, const void* payload, uint16_t payload_len);
int packet_receive(uint8_t *buffer, size_t max_length, size_t *actual_length, int nic_id);
int packet_receive_from_nic(int nic_index, uint8_t *buffer, size_t *length);
int packet_receive_process(uint8_t *raw_data, uint16_t length, uint8_t nic_index);
int packet_process_received(const uint8_t *packet, size_t length, int nic_id);
int packet_queue_tx(const uint8_t *packet, size_t length, int priority, uint16_t handle);
int packet_flush_tx_queue(void);
int packet_get_statistics(packet_stats_t *stats);
int packet_reset_statistics(void);
int packet_ops_is_initialized(void);
int packet_ops_cleanup(void);

/* GPT-5: Deferred processing functions */
void packet_process_deferred_work(void);  /* Process TX completions and other deferred work */
/* ISR -> bottom-half handoff */
int packet_isr_receive(uint8_t *packet_data, uint16_t packet_size, uint8_t nic_index);
int packet_bottom_half_init(bool enable_xms, uint32_t staging_count, uint32_t xms_count);

/* Loopback testing functions */
int packet_test_internal_loopback(int nic_index, const uint8_t *test_pattern, uint16_t pattern_size);
int packet_test_external_loopback(int nic_index, const loopback_test_pattern_t *test_patterns, int num_patterns);
int packet_test_cross_nic_loopback(int src_nic_index, int dest_nic_index, 
                                  const uint8_t *test_data, uint16_t data_size);
int packet_verify_loopback_integrity(const uint8_t *original_data, const uint8_t *received_data,
                                    uint16_t data_length, packet_integrity_result_t *integrity_result);

/* Multi-NIC testing functions */
int packet_route_multi_nic(const uint8_t *packet_data, uint16_t length, int src_nic_index);
int packet_send_multi_nic(const uint8_t *packet_data, uint16_t length,
                          const uint8_t *dest_addr, uint16_t handle);
int packet_handle_nic_failover(int failed_nic_index);
int packet_get_optimal_nic(const uint8_t *packet_data, uint16_t length);

/* Ethernet frame processing */
int packet_build_ethernet_frame(uint8_t *frame_buffer, uint16_t frame_size,
                               const uint8_t *dest_mac, const uint8_t *src_mac,
                               uint16_t ethertype, const uint8_t *payload,
                               uint16_t payload_len);
int packet_parse_ethernet_header(const uint8_t *frame_data, uint16_t frame_len,
                                eth_header_t *header);
bool packet_is_for_us(const uint8_t *frame_data, const uint8_t *our_mac);
bool packet_is_broadcast(const uint8_t *frame_data);
bool packet_is_multicast(const uint8_t *frame_data);
uint16_t packet_get_ethertype(const uint8_t *frame_data);

/* Multi-NIC coordination and routing */
int packet_route_multi_nic(const uint8_t *packet_data, uint16_t length, int src_nic_index);
int packet_send_multi_nic(const uint8_t *packet_data, uint16_t length,
                          const uint8_t *dest_addr, uint16_t handle);
int packet_handle_nic_failover(int failed_nic_index);
int packet_get_optimal_nic(const uint8_t *packet_data, uint16_t length);

/* Error handling and retry logic */
int packet_send_with_retry(const uint8_t *packet_data, uint16_t length,
                          const uint8_t *dest_addr, uint16_t handle,
                          int max_retries);
int packet_receive_with_recovery(uint8_t *buffer, size_t max_length, 
                                size_t *actual_length, int nic_id, 
                                uint32_t timeout_ms);

/* Performance monitoring and statistics */
void packet_update_detailed_stats(int nic_index, int packet_type, uint16_t length, int result);
int packet_get_performance_metrics(packet_performance_metrics_t *metrics);
int packet_monitor_health(void);
void packet_print_detailed_stats(void);

/* VDS (Virtual DMA Services) support */
void vds_process_deferred_unlocks(void);

/* Packet validation and processing */
int packet_validate(const packet_buffer_t *buffer);
int packet_validate_ethernet(const uint8_t *packet, uint16_t length);
bool packet_is_broadcast(const uint8_t *packet);
bool packet_is_multicast(const uint8_t *packet);
bool packet_is_for_us(const uint8_t *packet, const uint8_t *our_mac);
uint16_t packet_get_ethertype(const uint8_t *packet);

/* Packet filtering */
typedef bool (*packet_filter_func_t)(const packet_buffer_t *buffer, void *context);
int packet_add_filter(packet_filter_func_t filter, void *context);
int packet_remove_filter(packet_filter_func_t filter);
bool packet_should_accept(const packet_buffer_t *buffer);

/* Packet statistics */
void packet_stats_init(packet_stats_t *stats);
void packet_stats_update_tx(packet_stats_t *stats, uint16_t bytes, bool error);
void packet_stats_update_rx(packet_stats_t *stats, uint16_t bytes, bool error);
void packet_stats_update_collision(packet_stats_t *stats);
void packet_stats_update_error(packet_stats_t *stats, uint16_t error_type);
const packet_stats_t* packet_get_stats(void);
void packet_clear_stats(void);
void packet_print_stats(void);

/* Packet debugging and diagnostics */
void packet_dump_header(const uint8_t *packet, uint16_t length);
void packet_dump_data(const uint8_t *data, uint16_t length);
void packet_dump_buffer(const packet_buffer_t *buffer);
void packet_dump_queue(const packet_queue_t *queue);
const char* packet_error_to_string(int error_code);
const char* packet_flags_to_string(uint16_t flags);

/* Packet checksums and validation */
uint16_t packet_calculate_checksum(const uint8_t *data, uint16_t length);
bool packet_verify_checksum(const uint8_t *packet, uint16_t length);
uint32_t packet_calculate_crc32(const uint8_t *data, uint16_t length);
bool packet_verify_crc32(const uint8_t *packet, uint16_t length);

/* Production queue management functions */
int packet_queue_tx_enhanced(const uint8_t *packet, size_t length, int priority, uint16_t handle);
int packet_flush_tx_queue_enhanced(void);
int packet_get_queue_stats(packet_queue_management_stats_t *stats);

/* Ethernet frame operations */
int ethernet_build_header(uint8_t *buffer, const uint8_t *dest_mac,
                         const uint8_t *src_mac, uint16_t ethertype);
int ethernet_parse_header(const uint8_t *packet, eth_header_t *header);
int ethernet_set_destination(uint8_t *packet, const uint8_t *dest_mac);
int ethernet_set_source(uint8_t *packet, const uint8_t *src_mac);
int ethernet_set_ethertype(uint8_t *packet, uint16_t ethertype);

/* Broadcast and multicast operations */
int packet_send_broadcast(nic_info_t *nic, const uint8_t *data, uint16_t length);
int packet_send_multicast(nic_info_t *nic, const uint8_t *dest_mac,
                         const uint8_t *data, uint16_t length);
bool is_broadcast_mac(const uint8_t *mac);
bool is_multicast_mac(const uint8_t *mac);
bool is_zero_mac(const uint8_t *mac);

/* Packet timing and performance */
void packet_timestamp_enable(bool enable);
uint32_t packet_get_timestamp(void);
void packet_measure_latency_start(void);
uint32_t packet_measure_latency_end(void);

#ifdef __cplusplus
}
#endif

#endif /* _PACKET_OPS_H_ */
