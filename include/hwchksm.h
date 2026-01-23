/**
 * @file hw_checksum.h
 * @brief Hardware Checksum Offload System for 3Com Packet Driver
 *
 * This header provides a comprehensive hardware checksumming abstraction layer
 * with software fallback for NICs that don't support hardware checksum offload.
 * 
 * Sprint 2.1: Hardware Checksumming Research Implementation
 * 
 * Research findings:
 * - 3C515-TX: NO hardware checksumming support (ISA generation)
 * - 3C509B: NO hardware checksumming support (ISA generation)
 * - Hardware checksumming was introduced in later PCI generations (Cyclone/Tornado)
 *
 * This implementation provides:
 * - Software checksum calculation for TX path
 * - Software checksum validation for RX path
 * - Performance optimizations for DOS environment
 * - Integration with existing capability system
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef _HW_CHECKSUM_H_
#define _HW_CHECKSUM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "niccap.h"
#include "pktops.h"

/* Forward declarations */
struct nic_context;
struct packet_buffer;

/**
 * @brief Checksum protocol types
 */
typedef enum {
    CHECKSUM_PROTO_NONE = 0,        /* No checksum required */
    CHECKSUM_PROTO_IP = 1,          /* IPv4 header checksum */
    CHECKSUM_PROTO_TCP = 2,         /* TCP checksum */
    CHECKSUM_PROTO_UDP = 3,         /* UDP checksum */
    CHECKSUM_PROTO_ICMP = 4         /* ICMP checksum */
} checksum_protocol_t;

/**
 * @brief Checksum calculation modes
 */
typedef enum {
    CHECKSUM_MODE_NONE = 0,         /* No checksumming */
    CHECKSUM_MODE_SOFTWARE = 1,     /* Software-only checksumming */
    CHECKSUM_MODE_HARDWARE = 2,     /* Hardware-only checksumming */
    CHECKSUM_MODE_AUTO = 3          /* Auto-detect based on NIC capabilities */
} checksum_mode_t;

/**
 * @brief Checksum operation results
 */
typedef enum {
    CHECKSUM_RESULT_VALID = 0,      /* Checksum is valid */
    CHECKSUM_RESULT_INVALID = 1,    /* Checksum is invalid */
    CHECKSUM_RESULT_UNKNOWN = 2,    /* Cannot determine validity */
    CHECKSUM_RESULT_NOT_CHECKED = 3 /* Checksum not verified */
} checksum_result_t;

/**
 * @brief Checksum context for packet processing
 */
typedef struct checksum_context {
    checksum_mode_t mode;           /* Current checksumming mode */
    checksum_protocol_t protocol;   /* Protocol being processed */
    uint16_t header_offset;         /* Offset to protocol header */
    uint16_t checksum_offset;       /* Offset to checksum field */
    uint16_t data_length;           /* Length of data to checksum */
    uint32_t pseudo_header_sum;     /* Pseudo-header sum for TCP/UDP */
    bool hardware_capable;          /* Hardware supports this protocol */
    bool software_fallback;         /* Use software fallback */
} checksum_context_t;

/**
 * @brief Checksum statistics for performance monitoring
 */
typedef struct checksum_stats {
    /* Operation counters */
    uint32_t tx_checksums_calculated;  /* TX checksums calculated */
    uint32_t rx_checksums_validated;   /* RX checksums validated */
    uint32_t hardware_offloads;        /* Hardware offload operations */
    uint32_t software_fallbacks;       /* Software fallback operations */
    
    /* Error counters */
    uint32_t checksum_errors;          /* Invalid checksums detected */
    uint32_t calculation_errors;       /* Checksum calculation failures */
    uint32_t hardware_errors;          /* Hardware offload failures */
    
    /* Performance metrics */
    uint32_t avg_calc_time_us;         /* Average calculation time */
    uint32_t total_bytes_processed;    /* Total bytes checksummed */
    uint32_t cpu_cycles_saved;         /* Estimated CPU cycles saved */
    
    /* Protocol breakdown */
    uint32_t ip_checksums;             /* IP header checksums */
    uint32_t tcp_checksums;            /* TCP checksums */
    uint32_t udp_checksums;            /* UDP checksums */
    uint32_t icmp_checksums;           /* ICMP checksums */
} checksum_stats_t;

/* ========================================================================== */
/* CHECKSUM SYSTEM INITIALIZATION                                            */
/* ========================================================================== */

/**
 * @brief Initialize hardware checksum system
 * @param global_mode Global checksum mode preference
 * @return 0 on success, negative on error
 */
int hw_checksum_init(checksum_mode_t global_mode);

/**
 * @brief Cleanup hardware checksum system
 */
void hw_checksum_cleanup(void);

/**
 * @brief Configure checksum mode for specific NIC
 * @param ctx NIC context
 * @param mode Desired checksum mode
 * @return 0 on success, negative on error
 */
int hw_checksum_configure_nic(struct nic_context *ctx, checksum_mode_t mode);

/* ========================================================================== */
/* CAPABILITY DETECTION AND CONFIGURATION                                    */
/* ========================================================================== */

/**
 * @brief Detect hardware checksum capabilities for NIC
 * @param ctx NIC context
 * @return Bitmask of supported checksum protocols
 */
uint32_t hw_checksum_detect_capabilities(struct nic_context *ctx);

/**
 * @brief Check if NIC supports hardware checksumming for protocol
 * @param ctx NIC context
 * @param protocol Checksum protocol to check
 * @return true if supported, false otherwise
 */
bool hw_checksum_is_supported(struct nic_context *ctx, checksum_protocol_t protocol);

/**
 * @brief Get optimal checksum mode for NIC and protocol
 * @param ctx NIC context
 * @param protocol Checksum protocol
 * @return Recommended checksum mode
 */
checksum_mode_t hw_checksum_get_optimal_mode(struct nic_context *ctx, checksum_protocol_t protocol);

/* ========================================================================== */
/* TRANSMIT PATH CHECKSUM CALCULATION                                        */
/* ========================================================================== */

/**
 * @brief Calculate checksums for outgoing packet
 * @param ctx NIC context
 * @param packet Packet data
 * @param length Packet length
 * @param protocols Bitmask of protocols to checksum
 * @return 0 on success, negative on error
 */
int hw_checksum_tx_calculate(struct nic_context *ctx, uint8_t *packet, 
                            uint16_t length, uint32_t protocols);

/**
 * @brief Calculate IPv4 header checksum
 * @param ip_header Pointer to IPv4 header
 * @param header_length IPv4 header length in bytes
 * @return 0 on success, negative on error
 */
int hw_checksum_calculate_ip(uint8_t *ip_header, uint16_t header_length);

/**
 * @brief Calculate TCP checksum
 * @param ctx Checksum context with pseudo-header info
 * @param tcp_header Pointer to TCP header
 * @param tcp_length TCP header + data length
 * @return 0 on success, negative on error
 */
int hw_checksum_calculate_tcp(checksum_context_t *ctx, uint8_t *tcp_header, uint16_t tcp_length);

/**
 * @brief Calculate UDP checksum
 * @param ctx Checksum context with pseudo-header info
 * @param udp_header Pointer to UDP header
 * @param udp_length UDP header + data length
 * @return 0 on success, negative on error
 */
int hw_checksum_calculate_udp(checksum_context_t *ctx, uint8_t *udp_header, uint16_t udp_length);

/* ========================================================================== */
/* RECEIVE PATH CHECKSUM VALIDATION                                          */
/* ========================================================================== */

/**
 * @brief Validate checksums for incoming packet
 * @param ctx NIC context
 * @param packet Packet data
 * @param length Packet length
 * @param result_mask Bitmask of validation results per protocol
 * @return 0 on success, negative on error
 */
int hw_checksum_rx_validate(struct nic_context *ctx, const uint8_t *packet,
                           uint16_t length, uint32_t *result_mask);

/**
 * @brief Validate IPv4 header checksum
 * @param ip_header Pointer to IPv4 header
 * @param header_length IPv4 header length in bytes
 * @return Checksum validation result
 */
checksum_result_t hw_checksum_validate_ip(const uint8_t *ip_header, uint16_t header_length);

/**
 * @brief Validate TCP checksum
 * @param ip_header Pointer to IPv4 header (for pseudo-header)
 * @param tcp_header Pointer to TCP header
 * @param tcp_length TCP header + data length
 * @return Checksum validation result
 */
checksum_result_t hw_checksum_validate_tcp(const uint8_t *ip_header, 
                                          const uint8_t *tcp_header, uint16_t tcp_length);

/**
 * @brief Validate UDP checksum
 * @param ip_header Pointer to IPv4 header (for pseudo-header)
 * @param udp_header Pointer to UDP header
 * @param udp_length UDP header + data length
 * @return Checksum validation result
 */
checksum_result_t hw_checksum_validate_udp(const uint8_t *ip_header,
                                          const uint8_t *udp_header, uint16_t udp_length);

/* ========================================================================== */
/* SOFTWARE CHECKSUM IMPLEMENTATION                                          */
/* ========================================================================== */

/**
 * @brief Calculate Internet checksum (RFC 1071)
 * @param data Pointer to data
 * @param length Length of data in bytes
 * @param initial Initial checksum value (for continuation)
 * @return 16-bit Internet checksum
 */
uint16_t sw_checksum_internet(const uint8_t *data, uint16_t length, uint32_t initial);

/**
 * @brief Calculate Internet checksum with byte swap
 * @param data Pointer to data
 * @param length Length of data in bytes
 * @param initial Initial checksum value
 * @return 16-bit Internet checksum
 */
uint16_t sw_checksum_internet_ntohs(const uint8_t *data, uint16_t length, uint32_t initial);

/**
 * @brief Calculate TCP/UDP pseudo-header checksum
 * @param src_ip Source IP address
 * @param dst_ip Destination IP address
 * @param protocol Protocol (TCP=6, UDP=17)
 * @param length TCP/UDP length
 * @return Pseudo-header checksum
 */
uint32_t sw_checksum_pseudo_header(uint32_t src_ip, uint32_t dst_ip, 
                                  uint8_t protocol, uint16_t length);

/* ========================================================================== */
/* PERFORMANCE OPTIMIZATION                                                  */
/* ========================================================================== */

/**
 * @brief Optimized checksum calculation for DOS/16-bit environment
 * @param data Pointer to data (must be word-aligned)
 * @param length Length in bytes (should be even)
 * @param initial Initial checksum value
 * @return 16-bit checksum
 */
uint16_t sw_checksum_optimized_16bit(const uint8_t *data, uint16_t length, uint32_t initial);

/**
 * @brief Assembly-optimized checksum calculation
 * @param data Pointer to data
 * @param length Length in bytes
 * @param initial Initial checksum value
 * @return 16-bit checksum
 */
uint16_t sw_checksum_asm_optimized(const uint8_t *data, uint16_t length, uint32_t initial);

/* ========================================================================== */
/* STATISTICS AND MONITORING                                                 */
/* ========================================================================== */

/**
 * @brief Get checksum system statistics
 * @param stats Pointer to statistics structure
 * @return 0 on success, negative on error
 */
int hw_checksum_get_stats(checksum_stats_t *stats);

/**
 * @brief Clear checksum system statistics
 * @return 0 on success, negative on error
 */
int hw_checksum_clear_stats(void);

/**
 * @brief Update checksum statistics after operation
 * @param protocol Protocol that was checksummed
 * @param bytes_processed Number of bytes processed
 * @param time_us Time taken in microseconds
 * @param was_hardware true if hardware was used
 */
void hw_checksum_update_stats(checksum_protocol_t protocol, uint16_t bytes_processed,
                             uint16_t time_us, bool was_hardware);

/**
 * @brief Print detailed checksum statistics
 */
void hw_checksum_print_stats(void);

/* ========================================================================== */
/* DEBUGGING AND DIAGNOSTICS                                                 */
/* ========================================================================== */

/**
 * @brief Verify checksum calculation correctness
 * @param test_patterns Array of test patterns
 * @param num_patterns Number of test patterns
 * @return 0 if all tests pass, negative on failure
 */
int hw_checksum_self_test(void);

/**
 * @brief Dump packet checksum information for debugging
 * @param packet Packet data
 * @param length Packet length
 */
void hw_checksum_debug_packet(const uint8_t *packet, uint16_t length);

/**
 * @brief Convert checksum result to string
 * @param result Checksum result
 * @return String representation
 */
const char* hw_checksum_result_to_string(checksum_result_t result);

/**
 * @brief Convert checksum mode to string
 * @param mode Checksum mode
 * @return String representation
 */
const char* hw_checksum_mode_to_string(checksum_mode_t mode);

/* ========================================================================== */
/* PROTOCOL-SPECIFIC CONSTANTS                                               */
/* ========================================================================== */

/* IPv4 protocol numbers */
#define IP_PROTO_ICMP           1
#define IP_PROTO_TCP            6
#define IP_PROTO_UDP            17

/* Ethernet frame offsets */
#define ETH_HEADER_SIZE         14
#define IP_HEADER_MIN_SIZE      20
#define TCP_HEADER_MIN_SIZE     20
#define UDP_HEADER_SIZE         8
#define ICMP_HEADER_SIZE        8

/* IPv4 header offsets */
#define IP_OFFSET_VERSION_IHL   0
#define IP_OFFSET_TOS          1
#define IP_OFFSET_TOTAL_LEN    2
#define IP_OFFSET_ID           4
#define IP_OFFSET_FLAGS_FRAG   6
#define IP_OFFSET_TTL          8
#define IP_OFFSET_PROTOCOL     9
#define IP_OFFSET_CHECKSUM     10
#define IP_OFFSET_SRC_IP       12
#define IP_OFFSET_DST_IP       16

/* TCP header offsets */
#define TCP_OFFSET_SRC_PORT    0
#define TCP_OFFSET_DST_PORT    2
#define TCP_OFFSET_SEQ_NUM     4
#define TCP_OFFSET_ACK_NUM     8
#define TCP_OFFSET_DATA_OFF    12
#define TCP_OFFSET_FLAGS       13
#define TCP_OFFSET_WINDOW      14
#define TCP_OFFSET_CHECKSUM    16
#define TCP_OFFSET_URG_PTR     18

/* UDP header offsets */
#define UDP_OFFSET_SRC_PORT    0
#define UDP_OFFSET_DST_PORT    2
#define UDP_OFFSET_LENGTH      4
#define UDP_OFFSET_CHECKSUM    6

/* Error codes */
#define HW_CHECKSUM_SUCCESS             0
#define HW_CHECKSUM_ERROR              -1
#define HW_CHECKSUM_INVALID_PARAM      -2
#define HW_CHECKSUM_NOT_SUPPORTED      -3
#define HW_CHECKSUM_HARDWARE_ERROR     -4
#define HW_CHECKSUM_INVALID_PACKET     -5
#define HW_CHECKSUM_BUFFER_TOO_SMALL   -6

/* Performance optimization flags */
#define CHECKSUM_OPT_NONE              0x0000
#define CHECKSUM_OPT_ALIGN_16BIT       0x0001  /* Optimize for 16-bit alignment */
#define CHECKSUM_OPT_UNROLL_LOOPS      0x0002  /* Unroll checksum loops */
#define CHECKSUM_OPT_ASM_ACCELERATED   0x0004  /* Use assembly acceleration */
#define CHECKSUM_OPT_CPU_CACHE_AWARE   0x0008  /* Optimize for CPU cache */

#ifdef __cplusplus
}
#endif

#endif /* _HW_CHECKSUM_H_ */